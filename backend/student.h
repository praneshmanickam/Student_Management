/*
 * student.h
 * ---------
 * Student record definition and validation helpers.
 *
 * A student record is the *payload* stored inside the B-Tree.  The B-Tree key
 * is the Register Number (regNo); the remaining fields are the record data.
 *
 * Validation lives here (and therefore in the C backend) so that the server
 * never trusts the JavaScript running in the browser.
 */

#ifndef STUDENT_H
#define STUDENT_H

#include <stddef.h>

/* Field size limits - keep them generous but bounded so that a malicious or
 * broken client can never make the server overflow a buffer. */
#define STUDENT_NAME_MAX        64
#define STUDENT_DEPT_MAX        64
#define STUDENT_LINE_MAX        512

/* CGPA is stored on a 0.0 - 10.0 scale. */
#define STUDENT_CGPA_MIN        0.0
#define STUDENT_CGPA_MAX        10.0

typedef struct Student {
    int  regNo;                              /* B-Tree key, unique            */
    char name[STUDENT_NAME_MAX];             /* student name                   */
    char department[STUDENT_DEPT_MAX];       /* department / branch            */
    float cgpa;                              /* cumulative grade point average */
} Student;

/* ---- validation / sanitising (implemented in student.c) ------------------ */

/* Parse and validate a register number coming from the network.
 * Accepts only digits (an optional leading '+' is tolerated), no sign, no
 * decimals, no whitespace.  Returns 1 on success, 0 on failure. */
int  student_parse_regno(const char *raw, int *out);

/* Validate + sanitise a text field (name / department).
 * Trims surrounding whitespace, collapses control characters, enforces the
 * maximum length.  Returns 1 on success, 0 on failure. */
int  student_parse_text(const char *raw, char *out, size_t outsz);

/* Validate + parse a CGPA value ("9.8").  Range checked. */
int  student_parse_cgpa(const char *raw, float *out);

/* In-place sanitiser used on data read back from students.dat. */
void student_sanitize(char *s);

/* Formatting helpers. */
void student_cgpa_to_string(float cgpa, char *out, size_t outsz);

/* Returns 1 when the record is internally consistent. */
int  student_is_valid(const Student *s);

#endif /* STUDENT_H */
