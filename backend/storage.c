/*
 * storage.c
 * ---------
 * File handling: load students.dat into the B-Tree on start-up and save the
 * B-Tree back to students.dat after every modification.
 */

#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_last_error[512] = "";

static void set_error(const char *msg)
{
    strncpy(g_last_error, msg, sizeof(g_last_error) - 1);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

const char *storage_last_error(void)
{
    return g_last_error;
}

/* Trim trailing CR / LF / spaces in place. */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            s[n - 1] = '\0';
            n--;
        } else {
            break;
        }
    }
}

int storage_load(BTree *t, const char *path, int *loaded, int *skipped)
{
    if (loaded != NULL) {
        *loaded = 0;
    }
    if (skipped != NULL) {
        *skipped = 0;
    }
    if (t == NULL || path == NULL) {
        set_error("invalid arguments passed to storage_load()");
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            set_error("students.dat does not exist yet - starting with an empty B-Tree");
            return 1;
        }
        snprintf(g_last_error, sizeof(g_last_error),
                 "cannot open %.200s for reading (%s)", path, strerror(errno));
        return -1;
    }

    char line[STUDENT_LINE_MAX];
    int lineNo = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        lineNo++;
        rtrim(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;                   /* blank line or comment */
        }

        /* regNo|name|department|cgpa */
        char *fields[4] = { NULL, NULL, NULL, NULL };
        int nf = 0;
        char *p = line;
        fields[nf++] = p;
        while (*p != '\0' && nf < 4) {
            if (*p == '|') {
                *p = '\0';
                fields[nf++] = p + 1;
            }
            p++;
        }

        if (nf != 4) {
            if (skipped != NULL) {
                (*skipped)++;
            }
            continue;
        }

        Student s;
        memset(&s, 0, sizeof(s));

        int regNo = 0;
        float cgpa = 0.0f;
        if (!student_parse_regno(fields[0], &regNo) ||
            !student_parse_text(fields[1], s.name, sizeof(s.name)) ||
            !student_parse_text(fields[2], s.department, sizeof(s.department)) ||
            !student_parse_cgpa(fields[3], &cgpa)) {
            if (skipped != NULL) {
                (*skipped)++;
            }
            continue;
        }

        s.regNo = regNo;
        s.cgpa = cgpa;

        int rc = btree_insert(t, regNo, &s);
        if (rc == 1) {
            /* duplicate register number in the file: first one wins */
            if (skipped != NULL) {
                (*skipped)++;
            }
        } else if (rc == 0) {
            if (loaded != NULL) {
                (*loaded)++;
            }
        } else {
            if (skipped != NULL) {
                (*skipped)++;
            }
        }
    }

    if (ferror(f)) {
        snprintf(g_last_error, sizeof(g_last_error),
                 "read error while parsing %.200s (%s)", path, strerror(errno));
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int storage_save(const BTree *t, const char *path)
{
    if (t == NULL || path == NULL) {
        set_error("invalid arguments passed to storage_save()");
        return -1;
    }

    /* Snapshot the records first: the write must not be able to tear the file
     * apart if it fails half way through. */
    int capacity = t->totalKeys > 0 ? t->totalKeys : 1;
    Student *all = (Student *)malloc(sizeof(Student) * (size_t)capacity);
    if (all == NULL) {
        set_error("out of memory while saving students.dat");
        return -1;
    }

    int count = btree_traverse((BTree *)t, all, capacity);

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        snprintf(g_last_error, sizeof(g_last_error),
                 "cannot open %.200s for writing (%s)", tmp, strerror(errno));
        free(all);
        return -1;
    }

    fputs("# Student Record Management System - persistent storage\n", f);
    fputs("# format: registerNumber|name|department|cgpa\n", f);

    for (int i = 0; i < count; i++) {
        char cgpa[32];
        student_cgpa_to_string(all[i].cgpa, cgpa, sizeof(cgpa));
        if (fprintf(f, "%d|%s|%s|%s\n", all[i].regNo, all[i].name,
                    all[i].department, cgpa) < 0) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "write error while saving %.200s (%s)", tmp, strerror(errno));
            fclose(f);
            remove(tmp);
            free(all);
            return -1;
        }
    }

    if (fclose(f) != 0) {
        snprintf(g_last_error, sizeof(g_last_error),
                 "cannot flush %.200s (%s)", tmp, strerror(errno));
        remove(tmp);
        free(all);
        return -1;
    }

    free(all);

    /* Atomically replace the real file (Windows cannot rename onto an
     * existing file, so remove it first). */
    remove(path);
    if (rename(tmp, path) != 0) {
        snprintf(g_last_error, sizeof(g_last_error),
                 "cannot rename %.200s to %.200s (%s)", tmp, path, strerror(errno));
        remove(tmp);
        return -1;
    }
    return 0;
}

void storage_seed_sample(BTree *t)
{
    static const struct {
        int regNo;
        const char *name;
        const char *dept;
        float cgpa;
    } sample[] = {
        { 101,  "Dinesh", "IT",    7.8f },
        { 104,  "Bala",   "CSE",   8.2f },
        { 106,  "Gokul",  "CSE",   9.8f },
        { 109,  "Kumar",  "ECE",   9.1f },
        { 1001, "Arun",   "AIDS",  8.5f },
        { 1002, "Elango", "AIDS",  8.9f }
    };

    if (t == NULL) {
        return;
    }
    for (size_t i = 0; i < sizeof(sample) / sizeof(sample[0]); i++) {
        Student s;
        memset(&s, 0, sizeof(s));
        s.regNo = sample[i].regNo;
        s.cgpa = sample[i].cgpa;
        strncpy(s.name, sample[i].name, sizeof(s.name) - 1);
        strncpy(s.department, sample[i].dept, sizeof(s.department) - 1);
        if (btree_insert(t, s.regNo, &s) == 0) {
            printf("  seeded %-5d %-10s %-6s %.2f\n", s.regNo, s.name,
                   s.department, s.cgpa);
        }
    }
}
