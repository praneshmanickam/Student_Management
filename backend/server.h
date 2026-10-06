/*
 * server.h
 * --------
 * A very small, dependency free HTTP/1.1 server written in C.
 *
 *   * works on Windows (Winsock2) and on Linux / macOS (BSD sockets)
 *   * one worker thread per connection (pthreads / _beginthreadex)
 *   * serves the static frontend files (HTML / CSS / JS) from a directory
 *   * forwards every /api/... request to a route handler supplied by main.c
 *
 * The server knows nothing about B-Trees or students - it only moves bytes.
 */

#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>

#include "btree.h"

#define SERVER_DEFAULT_PORT 8080
#define SERVER_MAX_BODY     (1024 * 1024)     /* 1 MB request body  */
#define SERVER_MAX_HEADER   (64 * 1024)       /* 64 KB header block */

typedef struct HttpRequest {
    char   method[12];
    char   path[512];
    char   query[2048];
    char  *body;
    size_t bodyLen;
} HttpRequest;

typedef struct HttpResponse {
    int         status;
    const char *contentType;
    char       *body;          /* malloc'ed, NUL terminated */
    size_t      bodyLen;
} HttpResponse;

/* Implemented in main.c: turns an /api/... request into a JSON response. */
typedef void (*RouteHandler)(const HttpRequest *req, HttpResponse *res,
                             void *userdata);

/* Blocking server loop (returns 0 on clean shutdown, -1 on error). */
int server_run(int port, const char *frontendDir, RouteHandler route,
               void *userdata);

/* Ask the accept loop to return (called from a signal handler). */
void server_request_stop(void);

/* ---- response helpers --------------------------------------------- */
void res_init(HttpResponse *res);
void res_json(HttpResponse *res, int status, const char *json);
void res_error(HttpResponse *res, int status, const char *message);
void res_text(HttpResponse *res, int status, const char *text);
void res_free(HttpResponse *res);

/* ---- request parsing helpers -------------------------------------- */
int  http_query_param(const HttpRequest *req, const char *key, char *out,
                      size_t outsz);
void http_url_decode(const char *src, char *dst, size_t dstsz);
int  http_json_get_string(const char *body, const char *key, char *out,
                          size_t outsz);
int  http_json_get_number(const char *body, const char *key, char *out,
                          size_t outsz);
int  http_json_get_raw(const char *body, const char *key, char *out,
                       size_t outsz);

#endif /* SERVER_H */
