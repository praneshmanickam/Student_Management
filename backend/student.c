/*
 * student.c
 * ---------
 * Student record validation and sanitising.
 *
 * Everything that arrives from the browser is validated here, in C, before it
 * is allowed anywhere near the B-Tree.  The frontend performs the same checks
 * for user friendliness, but the backend never relies on them.
 */

#include "student.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Remove control characters and trim leading/trailing whitespace in place. */
void student_sanitize(char *s)
{
    if (s == NULL) {
        return;
    }

    char *dst = s;
    char *src = s;
    int pending_space = 0;

    while (*src != '\0') {
        unsigned char c = (unsigned char)*src;

        if (iscntrl(c)) {
            /* drop control characters entirely */
            src++;
            continue;
        }
        if (isspace(c)) {
            /* collapse runs of whitespace into a single space */
            if (dst != s) {
                pending_space = 1;
            }
            src++;
            continue;
        }
        if (pending_space) {
            *dst++ = ' ';
            pending_space = 0;
        }
        *dst++ = (char)c;
        src++;
    }
    *dst = '\0';
}

int student_parse_text(const char *raw, char *out, size_t outsz)
{
    if (raw == NULL || out == NULL || outsz == 0) {
        return 0;
    }

    /* Work on a bounded copy first: never read past the client buffer. */
    char tmp[STUDENT_LINE_MAX];
    size_t i = 0;
    while (raw[i] != '\0' && i + 1 < sizeof(tmp)) {
        tmp[i] = raw[i];
        i++;
    }
    tmp[i] = '\0';

    student_sanitize(tmp);

    if (tmp[0] == '\0') {
        return 0;                       /* empty field is not allowed */
    }
    if (strlen(tmp) >= outsz) {
        return 0;                       /* too long for the destination */
    }

    /* Only printable characters are allowed inside a name / department. */
    for (i = 0; tmp[i] != '\0'; i++) {
        unsigned char c = (unsigned char)tmp[i];
        if (c < 0x20 || c > 0x7e) {
            return 0;
        }
    }

    strcpy(out, tmp);
    return 1;
}

int student_parse_regno(const char *raw, int *out)
{
    if (raw == NULL || out == NULL) {
        return 0;
    }

    const char *p = raw;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p == '+') {                    /* tolerate a leading plus sign */
        p++;
    }
    if (*p < '0' || *p > '9') {         /* must start with a digit */
        return 0;
    }

    long value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        if (value > 2147483647L) {      /* keep inside 32 bit int range */
            return 0;
        }
        digits++;
        p++;
    }

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '\0') {                   /* trailing garbage */
        return 0;
    }
    if (digits == 0 || digits > 10) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

int student_parse_cgpa(const char *raw, float *out)
{
    if (raw == NULL || out == NULL) {
        return 0;
    }

    char tmp[64];
    size_t i = 0;
    while (raw[i] != '\0' && i + 1 < sizeof(tmp)) {
        tmp[i] = raw[i];
        i++;
    }
    tmp[i] = '\0';

    /* trim */
    student_sanitize(tmp);
    if (tmp[0] == '\0') {
        return 0;
    }

    char *end = NULL;
    float value = (float)strtod(tmp, &end);
    if (end == tmp) {
        return 0;                       /* not a number at all */
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\0') {
        return 0;                       /* trailing garbage */
    }
    if (value < STUDENT_CGPA_MIN || value > STUDENT_CGPA_MAX) {
        return 0;                       /* out of the 0..10 range */
    }

    *out = value;
    return 1;
}

void student_cgpa_to_string(float cgpa, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return;
    }
    snprintf(out, outsz, "%.2f", cgpa);
}

int student_is_valid(const Student *s)
{
    if (s == NULL) {
        return 0;
    }
    if (s->regNo <= 0) {
        return 0;
    }
    if (s->name[0] == '\0' || s->department[0] == '\0') {
        return 0;
    }
    if (s->cgpa < STUDENT_CGPA_MIN || s->cgpa > STUDENT_CGPA_MAX) {
        return 0;
    }
    return 1;
}
