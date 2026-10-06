/*
 * server.c
 * --------
 * Minimal HTTP/1.1 server in plain C (no external libraries).
 *
 * Platform notes
 *   Windows : Winsock2 + _beginthreadex  -> link with -lws2_32
 *   Linux   : BSD sockets + pthreads     -> link with -lpthread
 *
 * The request pipeline is:
 *
 *   browser --TCP--> accept --> read request --> parse
 *                                          |-- /api/...  --> RouteHandler (main.c)
 *                                          `-- otherwise --> static file from frontend/
 */

/* Ask for the POSIX 2008 prototypes (localtime_r, ...) when compiling with a
 * strict -std=c11 on Linux / macOS. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "server.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  define _WIN32_WINNT 0x0600
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <process.h>
#  pragma comment(lib, "ws2_32.lib")
#  define SOCK_ERR      SOCKET
#  define SOCK_INVALID  INVALID_SOCKET
#  define sock_close    closesocket
#else
#  include <sys/time.h>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <pthread.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define SOCK_ERR      int
#  define SOCK_INVALID  (-1)
#  define sock_close    close
#endif

/* Bounded copy that always NUL terminates. */
static void copy_bounded(char *dst, size_t dstsz, const char *src)
{
    if (dst == NULL || dstsz == 0) {
        return;
    }
    size_t i = 0;
    while (i + 1 < dstsz && src != NULL && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Portable string duplicate (strdup is not part of standard C). */
static char *dupstr(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static const char *now_string(void)
{
    static char buf[32];
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Response helpers                                                    */
/* ------------------------------------------------------------------ */

void res_init(HttpResponse *res)
{
    if (res == NULL) {
        return;
    }
    res->status = 200;
    res->contentType = "application/json";
    res->body = NULL;
    res->bodyLen = 0;
}

static int res_set(HttpResponse *res, int status, const char *contentType,
                   const char *data, size_t len)
{
    if (res == NULL) {
        return 0;
    }
    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        res->status = 500;
        res->contentType = "application/json";
        free(res->body);
        res->body = dupstr("{\"success\":false,\"error\":\"out of memory\"}");
        res->bodyLen = res->body ? strlen(res->body) : 0;
        return 0;
    }
    if (len > 0 && data != NULL) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    free(res->body);
    res->body = copy;
    res->bodyLen = len;
    res->status = status;
    res->contentType = contentType;
    return 1;
}

void res_json(HttpResponse *res, int status, const char *json)
{
    res_set(res, status, "application/json; charset=utf-8", json,
            json ? strlen(json) : 0);
}

void res_text(HttpResponse *res, int status, const char *text)
{
    res_set(res, status, "text/plain; charset=utf-8", text,
            text ? strlen(text) : 0);
}

void res_error(HttpResponse *res, int status, const char *message)
{
    JsonBuf b;
    if (!jsonbuf_init(&b)) {
        res_text(res, 500, "internal server error");
        return;
    }
    jsonbuf_appendf(&b, "{\"success\":false,\"status\":%d,\"error\":", status);
    jsonbuf_append_json_string(&b, message ? message : "unknown error");
    jsonbuf_append(&b, "}");
    res_json(res, status, b.data);
    jsonbuf_free(&b);
}

void res_free(HttpResponse *res)
{
    if (res == NULL) {
        return;
    }
    free(res->body);
    res->body = NULL;
    res->bodyLen = 0;
}

/* ------------------------------------------------------------------ */
/* URL decoding + query string + tiny JSON reader                      */
/* ------------------------------------------------------------------ */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void http_url_decode(const char *src, char *dst, size_t dstsz)
{
    if (dst == NULL || dstsz == 0) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0' && o + 1 < dstsz; i++) {
        if (src[i] == '%' && hex_value(src[i + 1]) >= 0 &&
            hex_value(src[i + 2]) >= 0) {
            dst[o++] = (char)((hex_value(src[i + 1]) << 4) | hex_value(src[i + 2]));
            i += 2;
        } else if (src[i] == '+') {
            dst[o++] = ' ';
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

int http_query_param(const HttpRequest *req, const char *key, char *out,
                     size_t outsz)
{
    if (req == NULL || key == NULL || out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';

    const char *q = req->query;
    size_t keyLen = strlen(key);

    while (*q != '\0') {
        const char *amp = strchr(q, '&');
        size_t segLen = amp ? (size_t)(amp - q) : strlen(q);

        const char *eq = (const char *)memchr(q, '=', segLen);
        size_t nameLen = eq ? (size_t)(eq - q) : segLen;

        if (nameLen == keyLen && strncmp(q, key, keyLen) == 0) {
            char raw[1024];
            size_t valLen = eq ? segLen - nameLen - 1 : 0;
            if (valLen >= sizeof(raw)) {
                valLen = sizeof(raw) - 1;
            }
            if (eq != NULL) {
                memcpy(raw, eq + 1, valLen);
            }
            raw[valLen] = '\0';
            http_url_decode(raw, out, outsz);
            return 1;
        }
        if (amp == NULL) {
            break;
        }
        q = amp + 1;
    }
    return 0;
}

/* Very small tolerant JSON reader: looks for "key" then reads the value that
 * follows it.  Handles strings (with escapes) and raw numbers. */
static const char *find_key(const char *body, const char *key)
{
    if (body == NULL || key == NULL) {
        return NULL;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(body, pattern);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') {
        p++;
    }
    return p;
}

int http_json_get_raw(const char *body, const char *key, char *out, size_t outsz)
{
    if (out == NULL || outsz == 0) {
        return 0;
    }
    out[0] = '\0';
    const char *p = find_key(body, key);
    if (p == NULL) {
        return 0;                       /* key not present at all */
    }

    /* The key is present: report success even when the value is empty so the
     * caller can produce a precise "field cannot be empty" message. */
    size_t o = 0;
    if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"' && o + 1 < outsz) {
            if (*p == '\\' && p[1] != '\0') {
                p++;
                switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': out[o++] = '\r'; break;
                case 'b': out[o++] = '\b'; break;
                case 'f': out[o++] = '\f'; break;
                case 'u': {
                    /* keep it simple: skip \uXXXX and emit '?' */
                    if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) &&
                        isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                        p += 4;
                        out[o++] = '?';
                    } else {
                        out[o++] = '?';
                    }
                    break;
                }
                default: out[o++] = *p; break;
                }
                p++;
            } else {
                out[o++] = *p++;
            }
        }
        if (*p != '"') {
            return 0;                   /* unterminated string */
        }
    } else {
        while (*p != '\0' && *p != ',' && *p != '}' && *p != '\n' &&
               *p != '\r' && o + 1 < outsz) {
            out[o++] = *p++;
        }
        /* trim trailing spaces */
        while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
            o--;
        }
    }
    out[o] = '\0';
    return 1;
}

int http_json_get_string(const char *body, const char *key, char *out, size_t outsz)
{
    return http_json_get_raw(body, key, out, outsz);
}

int http_json_get_number(const char *body, const char *key, char *out, size_t outsz)
{
    return http_json_get_raw(body, key, out, outsz);
}

/* ------------------------------------------------------------------ */
/* Static file serving                                                 */
/* ------------------------------------------------------------------ */

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(dot, ".png") == 0)  return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0)  return "image/x-icon";
    if (strcmp(dot, ".txt") == 0)  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

/* Guard against directory traversal: only plain file names are allowed. */
static int safe_path(const char *in, char *out, size_t outsz)
{
    if (in == NULL || out == NULL || outsz == 0) {
        return 0;
    }
    if (in[0] == '\0' || in[0] != '/') {
        return 0;
    }
    if (strstr(in, "..") != NULL) {
        return 0;
    }
    if (strchr(in, '\\') != NULL || strchr(in, '\0') == NULL) {
        return 0;
    }
    for (const char *p = in; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
    }
    if (strlen(in) >= outsz) {
        return 0;
    }
    strcpy(out, in);
    return 1;
}

static int send_all(SOCK_ERR fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(fd, data + sent, (int)(len - sent), 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_response(SOCK_ERR fd, const HttpResponse *res)
{
    char header[512];
    int hl = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        res->status,
        (res->status == 200) ? "OK" :
        (res->status == 201) ? "Created" :
        (res->status == 204) ? "No Content" :
        (res->status == 400) ? "Bad Request" :
        (res->status == 404) ? "Not Found" :
        (res->status == 405) ? "Method Not Allowed" :
        (res->status == 409) ? "Conflict" :
        (res->status == 500) ? "Internal Server Error" : "OK",
        res->contentType ? res->contentType : "text/plain",
        res->bodyLen);

    if (hl <= 0) {
        return;
    }
    if (send_all(fd, header, (size_t)hl) != 0) {
        return;
    }
    if (res->bodyLen > 0 && res->body != NULL) {
        send_all(fd, res->body, res->bodyLen);
    }
}

static void serve_static(SOCK_ERR fd, const HttpRequest *req,
                         const char *frontendDir, HttpResponse *res)
{
    char clean[512];
    if (!safe_path(req->path, clean, sizeof(clean))) {
        res_error(res, 400, "Invalid request path.");
        send_response(fd, res);
        return;
    }

    char full[1024];
    if (strcmp(clean, "/") == 0) {
        snprintf(full, sizeof(full), "%s/index.html", frontendDir);
    } else {
        snprintf(full, sizeof(full), "%s%s", frontendDir, clean);
    }

    FILE *f = fopen(full, "rb");
    if (f == NULL) {
        res_error(res, 404, "File not found.");
        send_response(fd, res);
        return;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        res_error(res, 500, "Cannot read file.");
        send_response(fd, res);
        return;
    }
    long size = ftell(f);
    if (size < 0 || size > 8L * 1024L * 1024L) {
        fclose(f);
        res_error(res, 500, "Invalid file size.");
        send_response(fd, res);
        return;
    }
    rewind(f);

    char *data = (char *)malloc((size_t)size + 1);
    if (data == NULL) {
        fclose(f);
        res_error(res, 500, "Out of memory.");
        send_response(fd, res);
        return;
    }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);

    res_set(res, 200, mime_for(full), data, got);
    free(data);
    send_response(fd, res);
}

/* ------------------------------------------------------------------ */
/* Request reading                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    SOCK_ERR       fd;
    char           frontendDir[512];
    RouteHandler   route;
    void          *userdata;
} ConnCtx;

static int set_socket_timeouts(SOCK_ERR fd)
{
#if defined(_WIN32)
    DWORD ms = 15000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
}

static int recv_some(SOCK_ERR fd, char *buf, size_t len)
{
    for (;;) {
        int n = recv(fd, buf, (int)len, 0);
        if (n >= 0) {
            return n;
        }
#if defined(_WIN32)
        int err = WSAGetLastError();
        if (err == WSAEINTR) {
            continue;
        }
#else
        if (errno == EINTR) {
            continue;
        }
#endif
        return -1;
    }
}

/* Read the whole request (headers + body) into a growable buffer. */
static int read_request(SOCK_ERR fd, char **out, size_t *outLen)
{
    size_t cap = 8192;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) {
        return -1;
    }

    size_t headerEnd = 0;
    int haveHeader = 0;

    while (len < SERVER_MAX_HEADER + SERVER_MAX_BODY) {
        if (len == cap) {
            size_t ncap = cap * 2;
            if (ncap > SERVER_MAX_HEADER + SERVER_MAX_BODY) {
                ncap = SERVER_MAX_HEADER + SERVER_MAX_BODY;
            }
            char *nb = (char *)realloc(buf, ncap);
            if (nb == NULL) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }

        int n = recv_some(fd, buf + len, cap - len);
        if (n <= 0) {
            break;
        }
        len += (size_t)n;
        buf[len] = '\0';

        if (!haveHeader) {
            char *eoh = strstr(buf, "\r\n\r\n");
            if (eoh != NULL) {
                haveHeader = 1;
                headerEnd = (size_t)(eoh - buf) + 4;
            }
        }
        if (haveHeader) {
            /* find Content-Length inside the header block */
            size_t contentLen = 0;
            char save = buf[headerEnd];
            buf[headerEnd] = '\0';
            const char *cl = strstr(buf, "Content-Length:");
            if (cl == NULL) {
                cl = strstr(buf, "content-length:");
            }
            if (cl != NULL) {
                contentLen = (size_t)strtoul(cl + 15, NULL, 10);
                if (contentLen > SERVER_MAX_BODY) {
                    buf[headerEnd] = save;
                    free(buf);
                    return -2;           /* body too large */
                }
            }
            buf[headerEnd] = save;
            if (len >= headerEnd + contentLen) {
                break;                   /* complete request */
            }
        }
    }

    if (len == 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *outLen = len;
    return 0;
}

static void parse_request(const char *raw, size_t rawLen, HttpRequest *req)
{
    if (req == NULL) {
        return;
    }
    memset(req, 0, sizeof(*req));

    /* request line */
    size_t i = 0;
    while (i < rawLen && raw[i] != ' ' && i + 1 < sizeof(req->method)) {
        req->method[i] = raw[i];
        i++;
    }
    req->method[i] = '\0';
    while (i < rawLen && raw[i] == ' ') {
        i++;
    }

    char target[2048];
    size_t t = 0;
    while (i < rawLen && raw[i] != ' ' && raw[i] != '\r' && raw[i] != '\n' &&
           t + 1 < sizeof(target)) {
        target[t++] = raw[i++];
    }
    target[t] = '\0';

    char *q = strchr(target, '?');
    if (q != NULL) {
        *q = '\0';
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
    }
    copy_bounded(req->path, sizeof(req->path), target);

    /* body: everything after the blank line */
    const char *body = strstr(raw, "\r\n\r\n");
    if (body != NULL) {
        body += 4;
        size_t headerLen = (size_t)(body - raw);
        size_t bodyLen = rawLen > headerLen ? rawLen - headerLen : 0;
        if (bodyLen > 0) {
            req->body = (char *)malloc(bodyLen + 1);
            if (req->body != NULL) {
                memcpy(req->body, body, bodyLen);
                req->body[bodyLen] = '\0';
                req->bodyLen = bodyLen;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Connection worker                                                   */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)
static unsigned __stdcall client_thread(void *arg)
#else
static void *client_thread(void *arg)
#endif
{
    ConnCtx *ctx = (ConnCtx *)arg;
    SOCK_ERR fd = ctx->fd;

    set_socket_timeouts(fd);

    char *raw = NULL;
    size_t rawLen = 0;
    int rc = read_request(fd, &raw, &rawLen);

    if (rc == 0) {
        HttpRequest req;
        parse_request(raw, rawLen, &req);
        free(raw);
        raw = NULL;

        HttpResponse res;
        res_init(&res);

        const char *started = now_string();

        if (strcmp(req.method, "OPTIONS") == 0) {
            res.status = 204;
            res.contentType = "text/plain";
            send_response(fd, &res);
        } else if (strncmp(req.path, "/api", 4) == 0) {
            if (ctx->route != NULL) {
                ctx->route(&req, &res, ctx->userdata);
            } else {
                res_error(&res, 500, "No route handler installed.");
            }
            send_response(fd, &res);
        } else if (strcmp(req.method, "GET") == 0 || strcmp(req.method, "HEAD") == 0) {
            serve_static(fd, &req, ctx->frontendDir, &res);
        } else {
            res_error(&res, 405, "Method not allowed for this resource.");
            send_response(fd, &res);
        }

        log_msg("[%s] %s %s -> %d", started, req.method, req.path, res.status);
        res_free(&res);
        free(req.body);
    } else if (rc == -2) {
        HttpResponse res;
        res_init(&res);
        res_error(&res, 413, "Request body too large.");
        send_response(fd, &res);
        res_free(&res);
    }

    free(raw);
    sock_close(fd);
    free(ctx);

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* Server start-up                                                     */
/* ------------------------------------------------------------------ */

static int g_stop_flag = 0;

void server_request_stop(void)
{
    g_stop_flag = 1;
}

int server_run(int port, const char *frontendDir, RouteHandler route,
               void *userdata)
{
    if (frontendDir == NULL || route == NULL) {
        log_msg("server_run(): invalid arguments");
        return -1;
    }

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_msg("WSAStartup() failed");
        return -1;
    }
#endif

    SOCK_ERR srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_INVALID) {
        log_msg("socket() failed");
#if defined(_WIN32)
        WSACleanup();
#endif
        return -1;
    }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        log_msg("bind() failed on port %d (is the server already running?)", port);
        sock_close(srv);
#if defined(_WIN32)
        WSACleanup();
#endif
        return -1;
    }

    if (listen(srv, 64) != 0) {
        log_msg("listen() failed");
        sock_close(srv);
#if defined(_WIN32)
        WSACleanup();
#endif
        return -1;
    }

    log_msg("HTTP server listening on http://localhost:%d", port);
    log_msg("Serving frontend from: %s", frontendDir);

    while (!g_stop_flag) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        SOCK_ERR fd = accept(srv, (struct sockaddr *)&client, &clen);
        if (fd == SOCK_INVALID) {
#if defined(_WIN32)
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
#else
            if (errno == EINTR) {
                continue;
            }
#endif
            continue;
        }

        ConnCtx *ctx = (ConnCtx *)calloc(1, sizeof(ConnCtx));
        if (ctx == NULL) {
            sock_close(fd);
            continue;
        }
        ctx->fd = fd;
        strncpy(ctx->frontendDir, frontendDir, sizeof(ctx->frontendDir) - 1);
        ctx->route = route;
        ctx->userdata = userdata;

#if defined(_WIN32)
        uintptr_t th = _beginthreadex(NULL, 0, client_thread, ctx, 0, NULL);
        if (th == 0) {
            sock_close(fd);
            free(ctx);
        } else {
            CloseHandle((HANDLE)th);
        }
#else
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        if (pthread_create(&th, &attr, client_thread, ctx) != 0) {
            sock_close(fd);
            free(ctx);
        }
        pthread_attr_destroy(&attr);
#endif
    }

    sock_close(srv);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
