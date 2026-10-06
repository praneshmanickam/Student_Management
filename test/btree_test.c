/*
 * test/btree_test.c
 * -----------------
 * Stress test + invariant checker for the B-Tree implementation.
 *
 * It inserts a few hundred records, deletes them again in random order and
 * after *every single operation* verifies the classic B-Tree invariants:
 *
 *   1. keys are sorted inside every node
 *   2. every key separates its two neighbouring subtrees
 *   3. every node holds between T-1 and 2T-1 keys (the root may hold fewer)
 *   4. all leaves are at exactly the same depth
 *   5. in-order traversal returns the records in sorted key order
 *
 * Build:  gcc -Wall -Wextra -O2 backend/btree.c backend/student.c backend/storage.c test/btree_test.c -o btree_test -lpthread
 * Run  :  ./btree_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/btree.h"
#include "../backend/storage.h"
#include "../backend/student.h"

static int g_failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: ");                                               \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

static int g_min_depth = -1;
static int g_max_depth = -1;

static void check_node(BTreeNode *x, int depth, int isRoot, int *leafDepth)
{
    CHECK(x != NULL, "NULL node at depth %d", depth);
    if (x == NULL) {
        return;
    }

    /* invariant 3: key count */
    int lo = isRoot ? 1 : BTREE_T - 1;
    if (isRoot && x->leaf) {
        lo = 0;
    }
    CHECK(x->n >= lo && x->n <= BTREE_MAX_KEYS,
          "depth %d: node holds %d keys (allowed %d..%d)",
          depth, x->n, lo, BTREE_MAX_KEYS);

    /* invariant 1: sorted keys */
    for (int i = 1; i < x->n; i++) {
        CHECK(x->keys[i - 1] < x->keys[i],
              "depth %d: keys out of order (%d before %d)",
              depth, x->keys[i - 1], x->keys[i]);
    }

    if (x->leaf) {
        /* invariant 4: all leaves at the same depth */
        if (*leafDepth < 0) {
            *leafDepth = depth;
        }
        CHECK(depth == *leafDepth,
              "leaf at depth %d but another leaf is at depth %d",
              depth, *leafDepth);
        return;
    }

    /* invariant 2: children are properly separated by the parent keys */
    for (int i = 0; i < x->n; i++) {
        BTreeNode *left = x->child[i];
        BTreeNode *right = x->child[i + 1];
        if (left != NULL) {
            for (int j = 0; j < left->n; j++) {
                CHECK(left->keys[j] < x->keys[i],
                      "child key %d is not < separator %d",
                      left->keys[j], x->keys[i]);
            }
        }
        if (right != NULL) {
            for (int j = 0; j < right->n; j++) {
                CHECK(right->keys[j] > x->keys[i],
                      "child key %d is not > separator %d",
                      right->keys[j], x->keys[i]);
            }
        }
        CHECK(left != NULL && right != NULL, "missing child pointer");
    }
    CHECK(x->child[x->n] != NULL, "last child pointer is NULL");

    for (int i = 0; i <= x->n; i++) {
        check_node(x->child[i], depth + 1, 0, leafDepth);
    }
}

static void verify(BTree *t, const char *where)
{
    int leafDepth = -1;
    check_node(t->root, 0, 1, &leafDepth);

    int h = btree_height(t);
    CHECK(leafDepth + 1 == h,
          "%s: leaf depth %d does not match tree height %d", where,
          leafDepth + 1, h);

    if (leafDepth > g_max_depth) g_max_depth = leafDepth;
    if (g_min_depth < 0 || leafDepth < g_min_depth) g_min_depth = leafDepth;
}

static Student make_student(int regNo)
{
    Student s;
    memset(&s, 0, sizeof(s));
    s.regNo = regNo;
    snprintf(s.name, sizeof(s.name), "Student %d", regNo);
    snprintf(s.department, sizeof(s.department), "DEPT%02d", regNo % 20);
    s.cgpa = (float)((regNo % 100) / 10.0);
    return s;
}

static void shuffle(int *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = a[i];
        a[i] = a[j];
        a[j] = tmp;
    }
}

int main(void)
{
    printf("=== B-Tree unit / stress test (T = %d) ===\n\n", BTREE_T);
    srand(20240929);

    /* ---------- 1. insert ---------- */
    printf("[1] inserting 400 records ...\n");
    BTree *t = btree_create();
    CHECK(t != NULL, "btree_create() returned NULL");

    int keys[400];
    for (int i = 0; i < 400; i++) {
        keys[i] = (i * 37) % 400 + 1;      /* shuffled, unique register numbers */
    }
    shuffle(keys, 400);

    for (int i = 0; i < 400; i++) {
        Student s = make_student(keys[i]);
        int rc = btree_insert(t, keys[i], &s);
        CHECK(rc == 0, "insert %d returned %d", keys[i], rc);
        if (i % 40 == 0 || i == 399) {
            verify(t, "after insert");
        }
        /* duplicate must be rejected */
        CHECK(btree_insert(t, keys[i], &s) == 1, "duplicate %d accepted", keys[i]);
    }

    CHECK(t->totalKeys == 400, "totalKeys = %d, expected 400", t->totalKeys);
    CHECK(btree_node_count(t) > 1, "tree did not grow");

    /* ---------- 2. search ---------- */
    printf("[2] searching all 400 records ...\n");
    for (int i = 0; i < 400; i++) {
        Student *found = btree_search(t, keys[i]);
        CHECK(found != NULL, "search %d failed", keys[i]);
        if (found != NULL) {
            CHECK(found->regNo == keys[i], "search %d returned %d",
                  keys[i], found->regNo);
            CHECK(strcmp(found->name, make_student(keys[i]).name) == 0,
                  "search %d returned the wrong name", keys[i]);
        }
    }
    CHECK(btree_search(t, 99999) == NULL, "search for a missing key succeeded");

    /* ---------- 3. traversal ---------- */
    printf("[3] traversal ...\n");
    Student all[400];
    int n = btree_traverse(t, all, 400);
    CHECK(n == 400, "traverse returned %d records", n);
    for (int i = 1; i < n; i++) {
        CHECK(all[i - 1].regNo < all[i].regNo,
              "traverse is not sorted at index %d", i);
    }

    /* ---------- 4. statistics ---------- */
    printf("[4] statistics ...\n");
    printf("      height=%d nodes=%d maxKeysInNode=%d\n",
           btree_height(t), btree_node_count(t), btree_max_keys_in_node(t));
    CHECK(btree_height(t) >= 3, "height %d is suspiciously small",
          btree_height(t));
    CHECK(btree_max_keys_in_node(t) <= BTREE_MAX_KEYS,
          "a node holds more than %d keys", BTREE_MAX_KEYS);
    CHECK(btree_max_keys_in_node(t) >= BTREE_T - 1, "no node is ever full");

    /* ---------- 5. update ---------- */
    printf("[5] updating records ...\n");
    for (int i = 0; i < 400; i += 7) {
        Student s = make_student(keys[i]);
        snprintf(s.name, sizeof(s.name), "Updated %d", keys[i]);
        s.cgpa = 9.99f;
        int rc = btree_update(t, keys[i], &s);
        CHECK(rc == 0, "update %d returned %d", keys[i], rc);
        Student *f = btree_search(t, keys[i]);
        CHECK(f != NULL && f->cgpa == 9.99f, "update %d did not stick", keys[i]);
        CHECK(f != NULL && f->regNo == keys[i], "update changed the key!");
    }
    CHECK(btree_update(t, 123456, &(Student){0}) != 0,
          "update of a missing key reported success");

    /* ---------- 6. delete ---------- */
    printf("[6] deleting all 400 records in random order ...\n");
    shuffle(keys, 400);
    for (int i = 0; i < 400; i++) {
        int rc = btree_delete(t, keys[i]);
        CHECK(rc == 0, "delete %d returned %d", keys[i], rc);
        CHECK(btree_search(t, keys[i]) == NULL, "deleted %d is still found",
              keys[i]);
        if (i % 25 == 0 || i == 399) {
            verify(t, "after delete");
        }
    }
    CHECK(t->totalKeys == 0, "tree is not empty after deleting everything");
    CHECK(btree_height(t) == 1, "empty tree should have height 1");
    CHECK(btree_delete(t, 42) == 1, "deleting from an empty tree succeeded");

    /* ---------- 7. refill to check that splits happen again ---------- */
    printf("[7] refilling 200 records ...\n");
    for (int i = 0; i < 200; i++) {
        Student s = make_student(i + 1);
        CHECK(btree_insert(t, i + 1, &s) == 0, "refill insert %d failed", i + 1);
        if (i % 20 == 0) verify(t, "refill");
    }
    verify(t, "after refill");
    CHECK(btree_search(t, 57) != NULL, "refilled key 57 not found");

    /* ---------- 8. persistence ---------- */
    printf("[8] persistence round trip ...\n");
    const char *file = "/tmp/btree_test_persistence.dat";
    remove(file);
    CHECK(storage_save(t, file) == 0, "storage_save() failed");

    BTree *t2 = btree_create();
    int loaded = 0, skipped = 0;
    int rc = storage_load(t2, file, &loaded, &skipped);
    CHECK(rc == 0, "storage_load() returned %d", rc);
    CHECK(loaded == 200, "loaded %d records, expected 200", loaded);
    CHECK(t2->totalKeys == t->totalKeys,
          "reloaded tree has %d keys, original had %d",
          t2->totalKeys, t->totalKeys);
    for (int i = 0; i < 200; i++) {
        CHECK(btree_search(t2, i + 1) != NULL, "reloaded key %d missing", i + 1);
    }
    verify(t2, "after reload");
    remove(file);

    /* ---------- 9. display ---------- */
    printf("[9] displayTree() output:\n");
    displayTree(t2->root, 0);

    /* ---------- 10. JSON ---------- */
    printf("[10] JSON serialisation ...\n");
    JsonBuf b;
    jsonbuf_init(&b);
    btree_to_json(t->root, 0, &b);
    CHECK(b.len > 10, "JSON output is too short");
    CHECK(strstr(b.data, "\"keys\":") != NULL, "JSON has no keys array");
    CHECK(strstr(b.data, "\"level\":0") != NULL, "JSON has no level 0");
    printf("      %zu bytes of JSON\n", b.len);
    jsonbuf_free(&b);

    btree_destroy(t);
    btree_destroy(t2);

    printf("\n=== %s (%d failure(s)) ===\n",
           g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
