/*
 * btree.h
 * -------
 * Manual B-Tree implementation (minimum degree T = 3) used as the primary
 * index for the Student Record Management System.
 *
 *      T                    = 3
 *      max keys  per node   = 2T - 1 = 5
 *      max children per node= 2T     = 6
 *      min keys  per node   = T - 1 = 2   (root may hold fewer)
 *
 * The Register Number is the B-Tree key.  Every key carries its Student
 * record alongside it, so a single lookup returns the whole record.
 *
 * Insertion is "bottom up": the key is placed in the leaf and any node that
 * overflows past BTREE_MAX_KEYS is split immediately, promoting its middle
 * key to the parent (splitChild).  Deletion is the classic
 * borrow-from-sibling / merge algorithm so that every node keeps at least
 * T-1 keys.  Both keep the tree sorted, balanced and with all leaves at the
 * same depth.
 *
 * Nodes are allocated with one extra slot (BTREE_MAX_KEYS + 1 keys and
 * BTREE_MAX_CHILDREN + 1 children) so that a key can be written into an
 * already full leaf before the split happens.  That extra slot is transient:
 * once insert()/deleteKey() return, every node holds at most BTREE_MAX_KEYS
 * keys, so the "maximum 5 keys per node" invariant always holds at rest.
 *
 * This module knows nothing about HTTP, sockets or JSON APIs - it is pure
 * data-structure code (see server.c for the web layer).
 */

#ifndef BTREE_H
#define BTREE_H

#include <stdarg.h>
#include <stddef.h>

#include "student.h"

/* ------------------------------------------------------------------ */
/* B-Tree parameters                                                   */
/* ------------------------------------------------------------------ */
#define BTREE_T             3                                   /* minimum degree */
#define BTREE_MAX_KEYS      (2 * BTREE_T - 1)                   /* 5            */
#define BTREE_MAX_CHILDREN  (2 * BTREE_T)                       /* 6            */

/* Physical slot counts: one spare slot allows the transient overflow state
 * that the bottom-up split removes immediately. */
#define BTREE_KEY_SLOTS     (BTREE_MAX_KEYS + 1)                /* 6            */
#define BTREE_CHILD_SLOTS   (BTREE_MAX_CHILDREN + 1)            /* 7            */

/* ------------------------------------------------------------------ */
/* Growable text buffer (also used by the HTTP layer to build JSON)    */
/* ------------------------------------------------------------------ */
typedef struct JsonBuf {
    char  *data;
    size_t len;
    size_t cap;
} JsonBuf;

int  jsonbuf_init(JsonBuf *b);
int  jsonbuf_reserve(JsonBuf *b, size_t extra);
int  jsonbuf_append(JsonBuf *b, const char *s);
int  jsonbuf_appendf(JsonBuf *b, const char *fmt, ...);
int  jsonbuf_append_char(JsonBuf *b, char c);
int  jsonbuf_append_json_string(JsonBuf *b, const char *s);   /* escapes + quotes */
void jsonbuf_free(JsonBuf *b);

/* ------------------------------------------------------------------ */
/* Nodes and tree                                                      */
/* ------------------------------------------------------------------ */
typedef struct BTreeNode {
    int    n;                                   /* keys currently stored   */
    int    leaf;                                /* 1 = leaf, 0 = internal  */
    int    keys[BTREE_KEY_SLOTS];               /* register numbers, sorted */
    Student students[BTREE_KEY_SLOTS];          /* record parallel to keys  */
    struct BTreeNode *child[BTREE_CHILD_SLOTS]; /* n+1 children when internal */
} BTreeNode;

typedef struct BTree {
    BTreeNode *root;
    int nodeCount;      /* number of nodes currently in the tree   */
    int totalKeys;      /* number of records currently in the tree */
} BTree;

/* ------------------------------------------------------------------ */
/* Operation trace - used by the "Operation Visualization" feature.    */
/* Every insert / delete records human readable steps describing what   */
/* the B-Tree actually did.                                           */
/* ------------------------------------------------------------------ */
#define BTREE_TRACE_MAX_STEPS 48
#define BTREE_TRACE_STEP_LEN  192

typedef struct {
    int  count;
    char steps[BTREE_TRACE_MAX_STEPS][BTREE_TRACE_STEP_LEN];
} BTreeTrace;

/* ------------------------------------------------------------------ */
/* Core operations                                                     */
/* ------------------------------------------------------------------ */

/* Tree level */
BTree     *btree_create(void);
void       btree_destroy(BTree *t);
BTreeNode *createNode(int leaf);

/* Search: returns the node holding regNo and stores its index in *idx
 * (both may be NULL).  Returns NULL when the key is absent. */
BTreeNode *search(BTreeNode *x, int regNo, int *idx);
Student   *btree_search(BTree *t, int regNo);

/* Insertion.  Returns 0 = inserted, 1 = duplicate key, -1 = invalid record. */
int btree_insert(BTree *t, int regNo, const Student *rec);

/* Insertion helpers (bottom-up splitting). */
BTreeNode *splitChild(BTreeNode *x, int *upKey, Student *upRec);
int insertNonFull(BTreeNode *x, int regNo, const Student *rec,
                  int *upKey, Student *upRec, BTreeNode **upRight);

/* Deletion.  Returns 0 = deleted, 1 = key not found. */
int btree_delete(BTree *t, int regNo);

/* Update the record fields of an existing key (the key itself never moves). */
int btree_update(BTree *t, int regNo, const Student *rec);

/* Deletion helpers (CLRS style) */
void deleteKey(BTreeNode *x, int regNo);
void merge(BTreeNode *x, int idx);
void borrowFromPrevious(BTreeNode *x, int idx);
void borrowFromNext(BTreeNode *x, int idx);
void fill(BTreeNode *x, int idx);

/* Traversal / statistics */
int  traverse(BTreeNode *x, Student *out, int *count);   /* in-order, sorted */
int  btree_traverse(BTree *t, Student *out, int max);
void displayTree(BTreeNode *x, int level);               /* console dump */
int  btree_height(BTree *t);
int  btree_node_count(BTree *t);
int  btree_max_keys_in_node(BTree *t);
int  btree_levels(BTree *t);                             /* 1 + height */

/* JSON serialisation of the *actual* tree structure. */
void btree_to_json(BTreeNode *x, int level, JsonBuf *out);

/* ------------------------------------------------------------------ */
/* Operation trace                                                     */
/* ------------------------------------------------------------------ */
void        trace_reset(void);
void        trace_add(const char *fmt, ...);
int         trace_count(void);
const char *trace_step(int i);
void        trace_copy(BTreeTrace *dst);

/* Portable mutex so the threaded HTTP server can share one B-Tree. */
void btree_lock(void);
void btree_unlock(void);

#endif /* BTREE_H */
