/*
 * storage.h
 * ---------
 * Persistent file storage for the student records.
 *
 * students.dat is a simple pipe separated text file:
 *
 *      # register|name|department|cgpa
 *      101|Dinesh|IT|7.8
 *      104|Bala|CSE|8.2
 *
 * The file is only the *storage medium*.  The B-Tree stays the single index:
 * on start-up every line is read and pushed through btree_insert(), and after
 * every add / update / delete the whole tree is written back out again.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include "btree.h"

#define STORAGE_DEFAULT_FILE "students.dat"

/* Load the file into the B-Tree.
 * Returns  0 = file read, 1 = file does not exist yet (empty tree),
 *         -1 = file could not be read.  *loaded / *skpped report how many
 * records were inserted and how many lines were rejected. */
int storage_load(BTree *t, const char *path, int *loaded, int *skipped);

/* Write the whole B-Tree (in sorted order) back to the file.
 * Returns 0 on success, -1 on a write error. */
int storage_save(const BTree *t, const char *path);

/* Insert the demo records shipped with the project (used with --seed). */
void storage_seed_sample(BTree *t);

/* Human readable description of the last storage error. */
const char *storage_last_error(void);

#endif /* STORAGE_H */
