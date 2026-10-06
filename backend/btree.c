/*
 * btree.c
 * --------
 * Manual B-Tree implementation, minimum degree T = 3.
 *
 * This file contains the data structure only: no sockets, no HTTP, no file
 * handling.  It is deliberately written so that the whole algorithm can be
 * read and graded on its own.
 *
 * Insert : insertNonFull() -> splitChild()  (bottom up splitting)
 * Delete : deleteKey() -> fill() -> borrowFromPrevious() / borrowFromNext()
 *                       -> merge()
 */

#include "btree.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* ================================================================== */
/* Portable mutex                                                      */
/* ================================================================== */

#if defined(_WIN32)
static CRITICAL_SECTION g_tree_lock;
static int              g_tree_lock_ready = 0;
#else
static pthread_mutex_t  g_tree_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void lock_init_once(void)
{
#if defined(_WIN32)
    if (!g_tree_lock_ready) {
        InitializeCriticalSection(&g_tree_lock);
        g_tree_lock_ready = 1;
    }
#endif
}

void btree_lock(void)
{
    lock_init_once();
#if defined(_WIN32)
    EnterCriticalSection(&g_tree_lock);
#else
    pthread_mutex_lock(&g_tree_lock);
#endif
}

void btree_unlock(void)
{
#if defined(_WIN32)
    LeaveCriticalSection(&g_tree_lock);
#else
    pthread_mutex_unlock(&g_tree_lock);
#endif
}

/* ================================================================== */
/* Growable JSON / text buffer                                         */
/* ================================================================== */

int jsonbuf_init(JsonBuf *b)
{
    if (b == NULL) {
        return 0;
    }
    b->cap = 256;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (b->data == NULL) {
        b->cap = 0;
        return 0;
    }
    b->data[0] = '\0';
    return 1;
}

int jsonbuf_reserve(JsonBuf *b, size_t extra)
{
    if (b == NULL) {
        return 0;
    }
    if (b->data == NULL && !jsonbuf_init(b)) {
        return 0;
    }
    if (b->len + extra + 1 <= b->cap) {
        return 1;
    }
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) {
        cap *= 2;
    }
    char *p = (char *)realloc(b->data, cap);
    if (p == NULL) {
        return 0;
    }
    b->data = p;
    b->cap = cap;
    return 1;
}

int jsonbuf_append(JsonBuf *b, const char *s)
{
    if (b == NULL || s == NULL) {
        return 0;
    }
    size_t n = strlen(s);
    if (!jsonbuf_reserve(b, n)) {
        return 0;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

int jsonbuf_append_char(JsonBuf *b, char c)
{
    if (!jsonbuf_reserve(b, 1)) {
        return 0;
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 1;
}

int jsonbuf_appendf(JsonBuf *b, const char *fmt, ...)
{
    if (b == NULL || fmt == NULL) {
        return 0;
    }
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return 0;
    }
    if ((size_t)n < sizeof(tmp)) {
        return jsonbuf_append(b, tmp);
    }
    /* rare: formatted text longer than the scratch buffer */
    char *big = (char *)malloc((size_t)n + 1);
    if (big == NULL) {
        return 0;
    }
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    int ok = jsonbuf_append(b, big);
    free(big);
    return ok;
}

/* Append a JSON string literal (with surrounding quotes and escaping). */
int jsonbuf_append_json_string(JsonBuf *b, const char *s)
{
    if (!jsonbuf_reserve(b, 8) || !jsonbuf_append_char(b, '"')) {
        return 0;
    }
    if (s != NULL) {
        for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
            unsigned char c = *p;
            switch (c) {
            case '"':  if (!jsonbuf_append(b, "\\\"")) return 0; break;
            case '\\': if (!jsonbuf_append(b, "\\\\")) return 0; break;
            case '\b': if (!jsonbuf_append(b, "\\b"))  return 0; break;
            case '\f': if (!jsonbuf_append(b, "\\f"))  return 0; break;
            case '\n': if (!jsonbuf_append(b, "\\n"))  return 0; break;
            case '\r': if (!jsonbuf_append(b, "\\r"))  return 0; break;
            case '\t': if (!jsonbuf_append(b, "\\t"))  return 0; break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    if (!jsonbuf_append(b, esc)) return 0;
                } else if (!jsonbuf_append_char(b, (char)c)) {
                    return 0;
                }
                break;
            }
        }
    }
    return jsonbuf_append_char(b, '"');
}

void jsonbuf_free(JsonBuf *b)
{
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* ================================================================== */
/* Operation trace                                                     */
/* ================================================================== */

static BTreeTrace g_trace;
#if defined(_WIN32)
static CRITICAL_SECTION g_trace_lock;
static int              g_trace_lock_ready = 0;
#else
static pthread_mutex_t  g_trace_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Bounded, always NUL terminating string copy (no strncpy truncation
 * warnings and no chance of leaving the destination unterminated). */
static void copy_field(char *dst, size_t dstsz, const char *src)
{
    if (dst == NULL || dstsz == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dstsz && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void trace_lock_fn(void)
{
#if defined(_WIN32)
    if (!g_trace_lock_ready) {
        InitializeCriticalSection(&g_trace_lock);
        g_trace_lock_ready = 1;
    }
    EnterCriticalSection(&g_trace_lock);
#else
    pthread_mutex_lock(&g_trace_lock);
#endif
}

static void trace_unlock_fn(void)
{
#if defined(_WIN32)
    LeaveCriticalSection(&g_trace_lock);
#else
    pthread_mutex_unlock(&g_trace_lock);
#endif
}

void trace_reset(void)
{
    trace_lock_fn();
    g_trace.count = 0;
    trace_unlock_fn();
}

void trace_add(const char *fmt, ...)
{
    if (fmt == NULL) {
        return;
    }
    char tmp[BTREE_TRACE_STEP_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    trace_lock_fn();
    if (g_trace.count < BTREE_TRACE_MAX_STEPS) {
        copy_field(g_trace.steps[g_trace.count], BTREE_TRACE_STEP_LEN, tmp);
        g_trace.count++;
    }
    trace_unlock_fn();
}

int trace_count(void)
{
    trace_lock_fn();
    int n = g_trace.count;
    trace_unlock_fn();
    return n;
}

const char *trace_step(int i)
{
    trace_lock_fn();
    const char *s = (i >= 0 && i < g_trace.count) ? g_trace.steps[i] : "";
    trace_unlock_fn();
    return s;
}

void trace_copy(BTreeTrace *dst)
{
    if (dst == NULL) {
        return;
    }
    trace_lock_fn();
    dst->count = g_trace.count;
    for (int i = 0; i < g_trace.count; i++) {
        copy_field(dst->steps[i], BTREE_TRACE_STEP_LEN, g_trace.steps[i]);
    }
    trace_unlock_fn();
}

/* ================================================================== */
/* Node creation / destruction                                         */
/* ================================================================== */

BTreeNode *createNode(int leaf)
{
    BTreeNode *node = (BTreeNode *)calloc(1, sizeof(BTreeNode));
    if (node == NULL) {
        return NULL;                    /* caller reports the allocation error */
    }
    node->n = 0;
    node->leaf = leaf ? 1 : 0;
    return node;
}

static void freeNode(BTreeNode *x)
{
    free(x);
}

BTree *btree_create(void)
{
    BTree *t = (BTree *)calloc(1, sizeof(BTree));
    if (t == NULL) {
        return NULL;
    }
    t->root = createNode(1);
    if (t->root == NULL) {
        free(t);
        return NULL;
    }
    t->nodeCount = 1;
    t->totalKeys = 0;
    return t;
}

static void destroyRec(BTreeNode *x)
{
    if (x == NULL) {
        return;
    }
    if (!x->leaf) {
        for (int i = 0; i <= x->n; i++) {
            destroyRec(x->child[i]);
        }
    }
    freeNode(x);
}

/* forward declaration: the node counter is recomputed after every mutation so
 * that it can never drift out of sync with the real tree */
static int countNodesRec(BTreeNode *x);

void btree_destroy(BTree *t)
{
    if (t == NULL) {
        return;
    }
    destroyRec(t->root);
    free(t);
}

/* ================================================================== */
/* SEARCH                                                              */
/* ================================================================== */

BTreeNode *search(BTreeNode *x, int regNo, int *idx)
{
    if (x == NULL) {
        return NULL;
    }
    int i = 0;
    while (i < x->n && regNo > x->keys[i]) {
        i++;
    }
    if (i < x->n && regNo == x->keys[i]) {
        if (idx != NULL) {
            *idx = i;
        }
        return x;
    }
    if (x->leaf) {
        return NULL;
    }
    return search(x->child[i], regNo, idx);
}

Student *btree_search(BTree *t, int regNo)
{
    if (t == NULL) {
        return NULL;
    }
    int idx = -1;
    BTreeNode *node = search(t->root, regNo, &idx);
    if (node == NULL || idx < 0) {
        return NULL;
    }
    return &node->students[idx];
}

/* ================================================================== */
/* INSERT                                                              */
/* ================================================================== */

/*
 * splitChild()
 * ------------
 * Split an *overflowing* node x (it currently holds BTREE_MAX_KEYS + 1 keys)
 * into x (left half) and a brand new right sibling.  The middle key is
 * promoted to the parent through *upKey / *upRec and the new right node is
 * returned so the caller can attach it as child[i+1].
 *
 * With T = 3 the node holds 6 keys [k0 k1 k2 k3 k4 k5]:
 *      left  = [k0 k1 k2]   (3 keys, T keys)
 *      promote k3
 *      right = [k4 k5]      (2 keys, T-1 keys)
 */
BTreeNode *splitChild(BTreeNode *x, int *upKey, Student *upRec)
{
    if (x == NULL || upKey == NULL || upRec == NULL ||
        x->n != BTREE_MAX_KEYS + 1) {
        return NULL;
    }

    int n = x->n;                       /* 6 */
    int mid = n / 2;                    /* 3 -> promoted key index */

    BTreeNode *right = createNode(x->leaf);
    if (right == NULL) {
        return NULL;                    /* allocation failure */
    }

    *upKey = x->keys[mid];
    *upRec = x->students[mid];

    int rightKeys = n - mid - 1;        /* 2 */
    for (int j = 0; j < rightKeys; j++) {
        right->keys[j]     = x->keys[mid + 1 + j];
        right->students[j] = x->students[mid + 1 + j];
    }
    if (!x->leaf) {
        for (int j = 0; j <= rightKeys; j++) {
            right->child[j] = x->child[mid + 1 + j];
            x->child[mid + 1 + j] = NULL;
        }
    }
    right->n = rightKeys;

    x->n = mid;                         /* left keeps [k0 k1 k2] */
    x->child[mid + 1] = NULL;

    trace_add("Node full (%d keys) -> split node, promote middle key %d",
              BTREE_MAX_KEYS + 1, *upKey);
    return right;
}

/*
 * insertNonFull()
 * ---------------
 * Descend to the correct leaf, insert the key in sorted position and split on
 * the way back up whenever a node overflows.  Returns 1 when x itself was
 * split (the promoted key / new right node are handed back to the caller).
 */
static int insertNonFullDepth(BTreeNode *x, int regNo, const Student *rec,
                              int depth, int *upKey, Student *upRec,
                              BTreeNode **upRight)
{
    if (x == NULL) {
        return -1;
    }

    if (x->leaf) {
        int i = x->n - 1;
        while (i >= 0 && regNo < x->keys[i]) {
            x->keys[i + 1]     = x->keys[i];
            x->students[i + 1] = x->students[i];
            i--;
        }
        x->keys[i + 1]     = regNo;
        x->students[i + 1] = *rec;
        x->n++;

        trace_add("Insert key %d into leaf node at level %d", regNo, depth);

        if (x->n > BTREE_MAX_KEYS) {
            BTreeNode *right = splitChild(x, upKey, upRec);
            if (right == NULL) {
                return -1;              /* allocation failure */
            }
            *upRight = right;
            return 1;
        }
        return 0;
    }

    /* internal node: pick the child that must hold the key */
    int c = 0;
    while (c < x->n && regNo > x->keys[c]) {
        c++;
    }
    trace_add("Level %d: %d %s %d -> follow child %d",
              depth, regNo,
              (c < x->n && regNo < x->keys[c]) ? "<" : ">=",
              (c < x->n) ? x->keys[c] : x->keys[x->n - 1], c);

    int k = 0;
    Student r;
    BTreeNode *right = NULL;
    int rc = insertNonFullDepth(x->child[c], regNo, rec, depth + 1, &k, &r, &right);
    if (rc < 0) {
        return -1;
    }
    if (rc == 1) {
        /* a child split: make room for the promoted key and the new sibling */
        for (int i = x->n; i > c; i--) {
            x->keys[i]     = x->keys[i - 1];
            x->students[i] = x->students[i - 1];
        }
        for (int i = x->n + 1; i > c + 1; i--) {
            x->child[i] = x->child[i - 1];
        }
        x->keys[c]     = k;
        x->students[c] = r;
        x->child[c + 1] = right;
        x->n++;

        if (x->n > BTREE_MAX_KEYS) {
            BTreeNode *r2 = splitChild(x, upKey, upRec);
            if (r2 == NULL) {
                return -1;
            }
            *upRight = r2;
            return 1;
        }
    }
    return 0;
}

int insertNonFull(BTreeNode *x, int regNo, const Student *rec,
                  int *upKey, Student *upRec, BTreeNode **upRight)
{
    return insertNonFullDepth(x, regNo, rec, 0, upKey, upRec, upRight);
}

int btree_insert(BTree *t, int regNo, const Student *rec)
{
    if (t == NULL || rec == NULL || !student_is_valid(rec) || rec->regNo != regNo) {
        return -1;
    }
    if (btree_search(t, regNo) != NULL) {
        return 1;                       /* duplicate register number */
    }

    trace_add("Insert key %d (%s, %s)", regNo, rec->name, rec->department);

    int upKey = 0;
    Student upRec;
    BTreeNode *upRight = NULL;

    int rc = insertNonFullDepth(t->root, regNo, rec, 0, &upKey, &upRec, &upRight);
    if (rc < 0) {
        return -1;                      /* memory allocation failure */
    }

    if (rc == 1) {
        /* the root itself split: the tree grows by one level */
        BTreeNode *newRoot = createNode(0);
        if (newRoot == NULL) {
            return -1;
        }
        newRoot->keys[0]     = upKey;
        newRoot->students[0] = upRec;
        newRoot->child[0]    = t->root;
        newRoot->child[1]    = upRight;
        newRoot->n = 1;
        t->root = newRoot;
        t->nodeCount++;
        trace_add("Root split -> new root key %d, tree height is now %d",
                  upKey, btree_height(t));
    }

    t->totalKeys++;
    t->nodeCount = countNodesRec(t->root);
    trace_add("Insert complete: %d record(s), %d node(s), height %d",
              t->totalKeys, t->nodeCount, btree_height(t));
    return 0;
}

/* ================================================================== */
/* DELETE                                                              */
/* ================================================================== */

static void removeFromLeaf(BTreeNode *x, int idx)
{
    for (int i = idx + 1; i < x->n; i++) {
        x->keys[i - 1]     = x->keys[i];
        x->students[i - 1] = x->students[i];
    }
    x->n--;
}

static void removeFromNonLeaf(BTreeNode *x, int idx)
{
    int k = x->keys[idx];

    if (x->child[idx]->n >= BTREE_T) {
        /* replace with the in-order predecessor and delete it from the left */
        BTreeNode *cur = x->child[idx];
        while (!cur->leaf) {
            cur = cur->child[cur->n];
        }
        int predKey = cur->keys[cur->n - 1];
        x->keys[idx]     = predKey;
        x->students[idx] = cur->students[cur->n - 1];
        trace_add("Internal node: replace key %d with predecessor %d", k, predKey);
        deleteKey(x->child[idx], predKey);
    } else if (x->child[idx + 1]->n >= BTREE_T) {
        /* replace with the in-order successor and delete it from the right */
        BTreeNode *cur = x->child[idx + 1];
        while (!cur->leaf) {
            cur = cur->child[0];
        }
        int succKey = cur->keys[0];
        x->keys[idx]     = succKey;
        x->students[idx] = cur->students[0];
        trace_add("Internal node: replace key %d with successor %d", k, succKey);
        deleteKey(x->child[idx + 1], succKey);
    } else {
        /* both children are minimal: merge them and recurse */
        trace_add("Both children have %d keys (< T = %d) -> merge children",
                  x->child[idx]->n, BTREE_T);
        merge(x, idx);
        deleteKey(x->child[idx], k);
    }
}

/*
 * borrowFromPrevious()
 * --------------------
 * child[idx] is short of keys, so take one from the left sibling child[idx-1]
 * through the parent key.
 */
void borrowFromPrevious(BTreeNode *x, int idx)
{
    BTreeNode *child   = x->child[idx];
    BTreeNode *sibling = x->child[idx - 1];

    for (int i = child->n - 1; i >= 0; i--) {
        child->keys[i + 1]     = child->keys[i];
        child->students[i + 1] = child->students[i];
    }
    if (!child->leaf) {
        for (int i = child->n; i >= 0; i--) {
            child->child[i + 1] = child->child[i];
        }
    }

    child->keys[0]     = x->keys[idx - 1];
    child->students[0] = x->students[idx - 1];
    if (!child->leaf) {
        child->child[0] = sibling->child[sibling->n];
        sibling->child[sibling->n] = NULL;
    }
    child->n++;

    x->keys[idx - 1]     = sibling->keys[sibling->n - 1];
    x->students[idx - 1] = sibling->students[sibling->n - 1];
    sibling->n--;

    trace_add("Borrow key %d from previous sibling (rotate right through parent)",
              child->keys[0]);
}

/*
 * borrowFromNext()
 * ----------------
 * child[idx] is short of keys, so take one from the right sibling child[idx+1]
 * through the parent key.
 */
void borrowFromNext(BTreeNode *x, int idx)
{
    BTreeNode *child   = x->child[idx];
    BTreeNode *sibling = x->child[idx + 1];

    child->keys[child->n]     = x->keys[idx];
    child->students[child->n] = x->students[idx];
    if (!child->leaf) {
        child->child[child->n + 1] = sibling->child[0];
    }
    child->n++;

    x->keys[idx]     = sibling->keys[0];
    x->students[idx] = sibling->students[0];

    for (int i = 1; i < sibling->n; i++) {
        sibling->keys[i - 1]     = sibling->keys[i];
        sibling->students[i - 1] = sibling->students[i];
    }
    if (!sibling->leaf) {
        for (int i = 1; i <= sibling->n; i++) {
            sibling->child[i - 1] = sibling->child[i];
        }
    }
    sibling->n--;

    trace_add("Borrow key %d from next sibling (rotate left through parent)",
              x->keys[idx]);
}

/*
 * merge()
 * -------
 * Merge child[idx], the parent key idx and child[idx+1] into a single node.
 * child[idx] keeps (T-1) + 1 + (T-1) = 2T-1 = 5 keys, which is exactly the
 * maximum, so the merged node never overflows.
 */
void merge(BTreeNode *x, int idx)
{
    BTreeNode *child   = x->child[idx];
    BTreeNode *sibling = x->child[idx + 1];

    child->keys[child->n]     = x->keys[idx];
    child->students[child->n] = x->students[idx];
    child->n++;

    for (int i = 0; i < sibling->n; i++) {
        child->keys[child->n + i]     = sibling->keys[i];
        child->students[child->n + i] = sibling->students[i];
    }
    if (!child->leaf) {
        for (int i = 0; i <= sibling->n; i++) {
            child->child[child->n + i] = sibling->child[i];
        }
    }
    child->n += sibling->n;

    /* pull the parent key and the sibling pointer out of the parent */
    for (int i = idx + 1; i < x->n; i++) {
        x->keys[i - 1]     = x->keys[i];
        x->students[i - 1] = x->students[i];
    }
    for (int i = idx + 2; i <= x->n; i++) {
        x->child[i - 1] = x->child[i];
    }
    x->child[x->n] = NULL;
    x->n--;

    freeNode(sibling);
    trace_add("Merged two nodes into one node with %d keys", child->n);
}

/*
 * fill()
 * ------
 * Make sure child[idx] has at least T keys before we descend into it.
 */
void fill(BTreeNode *x, int idx)
{
    if (idx != 0 && x->child[idx - 1]->n >= BTREE_T) {
        borrowFromPrevious(x, idx);
    } else if (idx != x->n && x->child[idx + 1]->n >= BTREE_T) {
        borrowFromNext(x, idx);
    } else {
        if (idx != x->n) {
            merge(x, idx);
        } else {
            merge(x, idx - 1);
        }
    }
}

static void deleteKeyDepth(BTreeNode *x, int regNo, int depth)
{
    if (x == NULL) {
        return;
    }

    int idx = 0;
    while (idx < x->n && regNo > x->keys[idx]) {
        idx++;
    }

    if (idx < x->n && x->keys[idx] == regNo) {
        trace_add("Level %d: key %d found in this node", depth, regNo);
        if (x->leaf) {
            removeFromLeaf(x, idx);
            trace_add("Leaf node: remove key %d (node now holds %d keys)",
                      regNo, x->n);
        } else {
            removeFromNonLeaf(x, idx);
        }
        return;
    }

    if (x->leaf) {
        trace_add("Level %d: key %d not present in this leaf", depth, regNo);
        return;                         /* key is not in the tree */
    }

    int lastChild = (idx == x->n);
    trace_add("Level %d: %d not here, descend into child %d", depth, regNo, idx);

    if (x->child[idx]->n < BTREE_T) {
        trace_add("Child %d holds only %d key(s) (< T = %d) -> fill it first",
                  idx, x->child[idx]->n, BTREE_T);
        fill(x, idx);
    }

    /* after a merge the useful child may have shifted one position left */
    if (lastChild && idx > x->n) {
        deleteKeyDepth(x->child[idx - 1], regNo, depth + 1);
    } else {
        deleteKeyDepth(x->child[idx], regNo, depth + 1);
    }
}

void deleteKey(BTreeNode *x, int regNo)
{
    deleteKeyDepth(x, regNo, 0);
}

int btree_delete(BTree *t, int regNo)
{
    if (t == NULL || t->root == NULL) {
        return 1;
    }
    if (btree_search(t, regNo) == NULL) {
        return 1;                       /* not found */
    }

    trace_add("Delete key %d from the B-Tree", regNo);
    deleteKeyDepth(t->root, regNo, 0);

    /* the root may have become empty: the tree shrinks by one level */
    if (t->root->n == 0) {
        BTreeNode *old = t->root;
        if (old->leaf) {
            /* tree is now empty - keep a single empty leaf as the root */
        } else {
            t->root = old->child[0];
            freeNode(old);
            t->nodeCount--;
            trace_add("Root became empty -> height reduced to %d", btree_height(t));
        }
    }

    t->totalKeys--;
    t->nodeCount = countNodesRec(t->root);
    trace_add("Delete complete: %d record(s), %d node(s), height %d",
              t->totalKeys, t->nodeCount, btree_height(t));
    return 0;
}

/* ================================================================== */
/* UPDATE                                                              */
/* ================================================================== */

int btree_update(BTree *t, int regNo, const Student *rec)
{
    if (t == NULL || rec == NULL || !student_is_valid(rec) || rec->regNo != regNo) {
        return -1;
    }
    Student *found = btree_search(t, regNo);
    if (found == NULL) {
        return 1;                       /* not found */
    }
    /* the register number (the B-Tree key) is deliberately left untouched */
    found->regNo = regNo;
    copy_field(found->name, sizeof(found->name), rec->name);
    copy_field(found->department, sizeof(found->department), rec->department);
    found->cgpa = rec->cgpa;

    trace_add("Update record %d: name=%s, department=%s, cgpa=%.2f",
              regNo, found->name, found->department, found->cgpa);
    trace_add("Register number %d is the B-Tree key and is never modified",
              regNo);
    return 0;
}

/* ================================================================== */
/* TRAVERSAL / STATISTICS                                              */
/* ================================================================== */

int traverse(BTreeNode *x, Student *out, int *count)
{
    if (x == NULL) {
        return 0;
    }
    for (int i = 0; i < x->n; i++) {
        if (!x->leaf) {
            traverse(x->child[i], out, count);
        }
        if (out != NULL && count != NULL && *count >= 0) {
            out[*count] = x->students[i];
        }
        if (count != NULL) {
            (*count)++;
        }
    }
    if (!x->leaf) {
        traverse(x->child[x->n], out, count);
    }
    return 0;
}

int btree_traverse(BTree *t, Student *out, int max)
{
    if (t == NULL) {
        return 0;
    }
    int count = 0;
    traverse(t->root, out, &count);
    if (count > max) {
        count = max;                    /* defensive: never exceed the buffer */
    }
    return count;
}

void displayTree(BTreeNode *x, int level)
{
    if (x == NULL) {
        return;
    }
    printf("Level %d: [", level);
    for (int i = 0; i < x->n; i++) {
        printf("%d", x->keys[i]);
        if (i + 1 < x->n) {
            printf(" ");
        }
    }
    printf("]%s\n", x->leaf ? "   (leaf)" : "");

    if (!x->leaf) {
        for (int i = 0; i <= x->n; i++) {
            for (int s = 0; s <= level; s++) {
                printf("    ");
            }
            printf("|-- ");
            displayTree(x->child[i], level + 1);
        }
    }
}

static int heightRec(BTreeNode *x)
{
    if (x == NULL) {
        return 0;
    }
    if (x->leaf) {
        return 1;
    }
    return 1 + heightRec(x->child[0]);
}

int btree_height(BTree *t)
{
    if (t == NULL || t->root == NULL) {
        return 0;
    }
    return heightRec(t->root);
}

int btree_levels(BTree *t)
{
    return btree_height(t);
}

static int countNodesRec(BTreeNode *x)
{
    if (x == NULL) {
        return 0;
    }
    int n = 1;
    if (!x->leaf) {
        for (int i = 0; i <= x->n; i++) {
            n += countNodesRec(x->child[i]);
        }
    }
    return n;
}

int btree_node_count(BTree *t)
{
    if (t == NULL) {
        return 0;
    }
    return countNodesRec(t->root);
}

static int maxKeysRec(BTreeNode *x)
{
    if (x == NULL) {
        return 0;
    }
    int best = x->n;
    if (!x->leaf) {
        for (int i = 0; i <= x->n; i++) {
            int c = maxKeysRec(x->child[i]);
            if (c > best) {
                best = c;
            }
        }
    }
    return best;
}

int btree_max_keys_in_node(BTree *t)
{
    if (t == NULL) {
        return 0;
    }
    return maxKeysRec(t->root);
}

/* ================================================================== */
/* JSON serialisation of the real tree                                 */
/* ================================================================== */

void btree_to_json(BTreeNode *x, int level, JsonBuf *out)
{
    if (x == NULL || out == NULL) {
        jsonbuf_append(out, "null");
        return;
    }

    jsonbuf_appendf(out, "{\"level\":%d,\"leaf\":%s,\"keys\":[", level,
                    x->leaf ? "true" : "false");
    for (int i = 0; i < x->n; i++) {
        if (i > 0) {
            jsonbuf_append_char(out, ',');
        }
        jsonbuf_appendf(out, "%d", x->keys[i]);
    }
    jsonbuf_append_char(out, ']');

    if (!x->leaf) {
        jsonbuf_append(out, ",\"children\":[");
        for (int i = 0; i <= x->n; i++) {
            if (i > 0) {
                jsonbuf_append_char(out, ',');
            }
            btree_to_json(x->child[i], level + 1, out);
        }
        jsonbuf_append_char(out, ']');
    }

    jsonbuf_append_char(out, '}');
}
