/*
 * main.c
 * ------
 * Entry point of the C backend.
 *
 * Responsibilities
 *   1. parse the command line
 *   2. load students.dat into the B-Tree
 *   3. install the /api/... route handlers (JSON in, JSON out)
 *   4. start the HTTP server
 *   5. save students.dat on shutdown
 *
 * Every API call is executed against the real B-Tree:
 *
 *   ADD    -> btree_insert()
 *   SEARCH -> search() / btree_search()
 *   UPDATE -> btree_update()
 *   DELETE -> btree_delete()
 *   LIST   -> btree_traverse()
 *   TREE   -> btree_to_json()
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btree.h"
#include "server.h"
#include "storage.h"
#include "student.h"

#if defined(_WIN32)
#  include <windows.h>
#endif

typedef struct {
    BTree      *tree;
    const char *dataFile;
    int         port;
    long        requests;
} AppContext;

/* ------------------------------------------------------------------ */
/* Small JSON helpers                                                  */
/* ------------------------------------------------------------------ */

static void append_student(JsonBuf *b, const Student *s)
{
    char cgpa[32];
    student_cgpa_to_string(s->cgpa, cgpa, sizeof(cgpa));

    jsonbuf_appendf(b, "{\"regNo\":%d,\"name\":", s->regNo);
    jsonbuf_append_json_string(b, s->name);
    jsonbuf_append(b, ",\"department\":");
    jsonbuf_append_json_string(b, s->department);
    jsonbuf_appendf(b, ",\"cgpa\":%s}", cgpa);
}

static void append_trace(JsonBuf *b, const char *field)
{
    BTreeTrace tr;
    trace_copy(&tr);

    jsonbuf_appendf(b, ",\"%s\":[", field);
    for (int i = 0; i < tr.count; i++) {
        if (i > 0) {
            jsonbuf_append_char(b, ',');
        }
        jsonbuf_append_json_string(b, tr.steps[i]);
    }
    jsonbuf_append_char(b, ']');
}

static void append_tree(JsonBuf *b, const char *field, const BTree *t)
{
    jsonbuf_appendf(b, ",\"%s\":", field);
    btree_to_json(t->root, 0, b);
}static void append_stats(JsonBuf *b, BTree *t, const char *field)
{
    int height = btree_height(t);
    int nodes = btree_node_count(t);
    int maxKeys = btree_max_keys_in_node(t);

    jsonbuf_appendf(b,
        ",\"%s\":{"
        "\"totalStudents\":%d,"
        "\"btreeHeight\":%d,"
        "\"totalNodes\":%d,"
        "\"maxKeysInNode\":%d,"
        "\"minDegree\":%d,"
        "\"maxKeys\":%d,"
        "\"maxChildren\":%d,"
        "\"rootKeys\":[",
        field, t->totalKeys, height, nodes, maxKeys,
        BTREE_T, BTREE_MAX_KEYS, BTREE_MAX_CHILDREN);

    for (int i = 0; i < t->root->n; i++) {
        if (i > 0) {
            jsonbuf_append_char(b, ',');
        }
        jsonbuf_appendf(b, "%d", t->root->keys[i]);
    }
    jsonbuf_append(b, "]}");
}

/* ================================================================== */
/* API handlers                                                        */
/* ================================================================== */

/* GET /api/stats -------------------------------------------------------- */
static void api_stats(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    btree_lock();
    JsonBuf b;
    if (!jsonbuf_init(&b)) {
        btree_unlock();
        res_error(res, 500, "Out of memory while building the response.");
        return;
    }
    jsonbuf_append(&b, "{\"success\":true");
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_appendf(&b, ",\"storageFile\":");
    jsonbuf_append_json_string(&b, ctx->dataFile);
    jsonbuf_appendf(&b, ",\"port\":%d}", ctx->port);
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* GET /api/students ----------------------------------------------------- */
static void api_students(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    btree_lock();

    int capacity = ctx->tree->totalKeys > 0 ? ctx->tree->totalKeys : 1;
    Student *all = (Student *)malloc(sizeof(Student) * (size_t)capacity);
    if (all == NULL) {
        btree_unlock();
        res_error(res, 500, "Out of memory while reading the B-Tree.");
        return;
    }

    int count = btree_traverse(ctx->tree, all, capacity);

    JsonBuf b;
    if (!jsonbuf_init(&b)) {
        free(all);
        btree_unlock();
        res_error(res, 500, "Out of memory while building the response.");
        return;
    }

    jsonbuf_appendf(&b, "{\"success\":true,\"count\":%d,\"students\":[", count);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            jsonbuf_append_char(&b, ',');
        }
        append_student(&b, &all[i]);
    }
    jsonbuf_append(&b, "]}");

    free(all);
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* GET /api/student?regNo=106 -------------------------------------------- */
static void api_get_student(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    char raw[128];
    if (!http_query_param(req, "regNo", raw, sizeof(raw)) || raw[0] == '\0') {
        res_error(res, 400, "Missing query parameter: regNo.");
        return;
    }

    int regNo = 0;
    if (!student_parse_regno(raw, &regNo)) {
        res_error(res, 400, "Invalid register number: it must be numeric.");
        return;
    }

    btree_lock();
    Student *found = btree_search(ctx->tree, regNo);
    if (found == NULL) {
        btree_unlock();
        JsonBuf b;
        jsonbuf_init(&b);
        jsonbuf_appendf(&b, "{\"success\":false,\"error\":\"Student not found.\","
                            "\"regNo\":%d,\"searchedBy\":\"btree search\"}", regNo);
        res_json(res, 404, b.data);
        jsonbuf_free(&b);
        return;
    }

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_append(&b, "{\"success\":true,\"student\":");
    append_student(&b, found);
    jsonbuf_append(&b, ",\"foundBy\":\"btree search\"}");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* POST /api/student ------------------------------------------------------ */
static void api_add_student(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    if (req->body == NULL || req->bodyLen == 0) {
        res_error(res, 400, "Empty request body. Send JSON student data.");
        return;
    }

    char rawReg[128] = "", rawName[STUDENT_NAME_MAX] = "";
    char rawDept[STUDENT_DEPT_MAX] = "", rawCgpa[64] = "";

    if (!http_json_get_raw(req->body, "regNo", rawReg, sizeof(rawReg)) ||
        !http_json_get_raw(req->body, "name", rawName, sizeof(rawName)) ||
        !http_json_get_raw(req->body, "department", rawDept, sizeof(rawDept)) ||
        !http_json_get_raw(req->body, "cgpa", rawCgpa, sizeof(rawCgpa))) {
        res_error(res, 400, "Invalid JSON body. Required fields: regNo, name, department, cgpa.");
        return;
    }

    Student s;
    memset(&s, 0, sizeof(s));

    if (!student_parse_regno(rawReg, &s.regNo)) {
        res_error(res, 400, "Register Number must be numeric.");
        return;
    }
    if (!student_parse_text(rawName, s.name, sizeof(s.name))) {
        res_error(res, 400, "Name cannot be empty.");
        return;
    }
    if (!student_parse_text(rawDept, s.department, sizeof(s.department))) {
        res_error(res, 400, "Department cannot be empty.");
        return;
    }
    if (!student_parse_cgpa(rawCgpa, &s.cgpa)) {
        res_error(res, 400, "CGPA must be a number between 0 and 10.");
        return;
    }

    btree_lock();
    trace_reset();
    int rc = btree_insert(ctx->tree, s.regNo, &s);
    if (rc == 1) {
        btree_unlock();
        JsonBuf b;
        jsonbuf_init(&b);
        jsonbuf_appendf(&b,
            "{\"success\":false,\"error\":\"Duplicate Register Number.\","
            "\"message\":\"Register Number %d already exists.\",\"regNo\":%d}",
            s.regNo, s.regNo);
        res_json(res, 409, b.data);
        jsonbuf_free(&b);
        return;
    }
    if (rc != 0) {
        btree_unlock();
        res_error(res, 500, "B-Tree insertion failed (memory allocation error).");
        return;
    }

    int saved = storage_save(ctx->tree, ctx->dataFile);

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_appendf(&b, "{\"success\":true,\"message\":\"Student %d inserted successfully.\","
                        "\"student\":", s.regNo);
    append_student(&b, &s);
    jsonbuf_append(&b, ",\"operation\":\"insert\"");
    append_trace(&b, "steps");
    append_tree(&b, "tree", ctx->tree);
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_appendf(&b, ",\"saved\":%s}", saved == 0 ? "true" : "false");
    btree_unlock();

    res_json(res, 201, b.data);
    jsonbuf_free(&b);
}

/* PUT /api/student?regNo=106 --------------------------------------------- */
static void api_update_student(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    char raw[128];
    if (!http_query_param(req, "regNo", raw, sizeof(raw)) || raw[0] == '\0') {
        res_error(res, 400, "Missing query parameter: regNo.");
        return;
    }

    int regNo = 0;
    if (!student_parse_regno(raw, &regNo)) {
        res_error(res, 400, "Invalid register number: it must be numeric.");
        return;
    }
    if (req->body == NULL || req->bodyLen == 0) {
        res_error(res, 400, "Empty request body. Send the fields to update.");
        return;
    }

    char rawName[STUDENT_NAME_MAX] = "", rawDept[STUDENT_DEPT_MAX] = "", rawCgpa[64] = "";
    int hasName = http_json_get_raw(req->body, "name", rawName, sizeof(rawName));
    int hasDept = http_json_get_raw(req->body, "department", rawDept, sizeof(rawDept));
    int hasCgpa = http_json_get_raw(req->body, "cgpa", rawCgpa, sizeof(rawCgpa));

    if (!hasName && !hasDept && !hasCgpa) {
        res_error(res, 400, "Nothing to update. Provide name, department and/or cgpa.");
        return;
    }

    btree_lock();

    Student *existing = btree_search(ctx->tree, regNo);
    if (existing == NULL) {
        btree_unlock();
        JsonBuf b;
        jsonbuf_init(&b);
        jsonbuf_appendf(&b,
            "{\"success\":false,\"error\":\"Student not found.\","
            "\"message\":\"No student with register number %d.\",\"regNo\":%d}",
            regNo, regNo);
        res_json(res, 404, b.data);
        jsonbuf_free(&b);
        return;
    }

    /* start from the current record so a partial update keeps the old values */
    Student updated = *existing;

    if (hasName && !student_parse_text(rawName, updated.name, sizeof(updated.name))) {
        btree_unlock();
        res_error(res, 400, "Name cannot be empty.");
        return;
    }
    if (hasDept && !student_parse_text(rawDept, updated.department, sizeof(updated.department))) {
        btree_unlock();
        res_error(res, 400, "Department cannot be empty.");
        return;
    }
    if (hasCgpa && !student_parse_cgpa(rawCgpa, &updated.cgpa)) {
        btree_unlock();
        res_error(res, 400, "CGPA must be a number between 0 and 10.");
        return;
    }

    trace_reset();
    int rc = btree_update(ctx->tree, regNo, &updated);
    if (rc != 0) {
        btree_unlock();
        res_error(res, 500, "B-Tree update failed.");
        return;
    }

    int saved = storage_save(ctx->tree, ctx->dataFile);

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_appendf(&b, "{\"success\":true,\"message\":\"Student %d updated successfully.\","
                        "\"student\":", regNo);
    append_student(&b, &updated);
    jsonbuf_append(&b, ",\"operation\":\"update\"");
    append_trace(&b, "steps");
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_appendf(&b, ",\"saved\":%s}", saved == 0 ? "true" : "false");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* DELETE /api/student?regNo=106 ------------------------------------------ */
static void api_delete_student(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    char raw[128];
    if (!http_query_param(req, "regNo", raw, sizeof(raw)) || raw[0] == '\0') {
        res_error(res, 400, "Missing query parameter: regNo.");
        return;
    }

    int regNo = 0;
    if (!student_parse_regno(raw, &regNo)) {
        res_error(res, 400, "Invalid register number: it must be numeric.");
        return;
    }

    btree_lock();
    trace_reset();
    int rc = btree_delete(ctx->tree, regNo);
    if (rc != 0) {
        btree_unlock();
        JsonBuf b;
        jsonbuf_init(&b);
        jsonbuf_appendf(&b,
            "{\"success\":false,\"error\":\"Student not found.\","
            "\"message\":\"No student with register number %d.\",\"regNo\":%d}",
            regNo, regNo);
        res_json(res, 404, b.data);
        jsonbuf_free(&b);
        return;
    }

    int saved = storage_save(ctx->tree, ctx->dataFile);

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_appendf(&b, "{\"success\":true,\"message\":\"Student %d deleted successfully.\","
                        "\"regNo\":%d,\"operation\":\"delete\"", regNo, regNo);
    append_trace(&b, "steps");
    append_tree(&b, "tree", ctx->tree);
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_appendf(&b, ",\"saved\":%s}", saved == 0 ? "true" : "false");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* GET /api/btree ---------------------------------------------------------- */
static void api_btree(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    btree_lock();
    JsonBuf b;
    if (!jsonbuf_init(&b)) {
        btree_unlock();
        res_error(res, 500, "Out of memory while serialising the B-Tree.");
        return;
    }
    jsonbuf_append(&b, "{\"success\":true,\"minDegree\":3,\"maxKeys\":5,\"tree\":");
    btree_to_json(ctx->tree->root, 0, &b);
    jsonbuf_append(&b, "}");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* GET /api/operation ------------------------------------------------------ */
static void api_operation(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    btree_lock();
    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_append(&b, "{\"success\":true");
    append_trace(&b, "steps");
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_append(&b, "}");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* POST /api/seed ---------------------------------------------------------- */
static void api_seed(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    btree_lock();
    trace_reset();
    trace_add("Seed the B-Tree with the sample records shipped with the project");
    storage_seed_sample(ctx->tree);
    int saved = storage_save(ctx->tree, ctx->dataFile);

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_append(&b, "{\"success\":true,\"message\":\"Sample student records loaded.\"");
    append_trace(&b, "steps");
    append_tree(&b, "tree", ctx->tree);
    append_stats(&b, ctx->tree, "stats");
    jsonbuf_appendf(&b, ",\"saved\":%s}", saved == 0 ? "true" : "false");
    btree_unlock();

    res_json(res, 200, b.data);
    jsonbuf_free(&b);
}

/* GET /api/health --------------------------------------------------------- */
static void api_health(const HttpRequest *req, HttpResponse *res, AppContext *ctx)
{
    (void)req;
    (void)ctx;
    res_json(res, 200, "{\"success\":true,\"status\":\"ok\","
                       "\"service\":\"Student Record Management System (C + B-Tree)\"}");
}

/* ================================================================== */
/* Router                                                              */
/* ================================================================== */

static void route(const HttpRequest *req, HttpResponse *res, void *userdata)
{
    AppContext *ctx = (AppContext *)userdata;

    ctx->requests++;

    if (res == NULL) {
        return;
    }
    res_init(res);

    const char *m = req->method;
    const char *p = req->path;

    /* strip a trailing slash so /api/students/ also works */
    char path[512];
    strncpy(path, p, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    size_t pl = strlen(path);
    while (pl > 1 && path[pl - 1] == '/') {
        path[--pl] = '\0';
    }

    if (strcmp(path, "/api/health") == 0 && strcmp(m, "GET") == 0) {
        api_health(req, res, ctx);
        return;
    }
    if (strcmp(path, "/api/stats") == 0 && strcmp(m, "GET") == 0) {
        api_stats(req, res, ctx);
        return;
    }
    if (strcmp(path, "/api/students") == 0 && strcmp(m, "GET") == 0) {
        api_students(req, res, ctx);
        return;
    }
    if (strcmp(path, "/api/student") == 0) {
        if (strcmp(m, "GET") == 0) {
            api_get_student(req, res, ctx);
            return;
        }
        if (strcmp(m, "POST") == 0) {
            api_add_student(req, res, ctx);
            return;
        }
        if (strcmp(m, "PUT") == 0) {
            api_update_student(req, res, ctx);
            return;
        }
        if (strcmp(m, "DELETE") == 0) {
            api_delete_student(req, res, ctx);
            return;
        }
        res_error(res, 405, "Method not allowed. Use GET, POST, PUT or DELETE on /api/student.");
        return;
    }
    if (strcmp(path, "/api/btree") == 0 && strcmp(m, "GET") == 0) {
        api_btree(req, res, ctx);
        return;
    }
    if (strcmp(path, "/api/operation") == 0 && strcmp(m, "GET") == 0) {
        api_operation(req, res, ctx);
        return;
    }
    if (strcmp(path, "/api/seed") == 0 && strcmp(m, "POST") == 0) {
        api_seed(req, res, ctx);
        return;
    }

    JsonBuf b;
    jsonbuf_init(&b);
    jsonbuf_appendf(&b, "{\"success\":false,\"status\":404,\"error\":");
    jsonbuf_append_json_string(&b, "Unknown API endpoint.");
    jsonbuf_appendf(&b, ",\"method\":");
    jsonbuf_append_json_string(&b, m);
    jsonbuf_appendf(&b, ",\"path\":");
    jsonbuf_append_json_string(&b, path);
    jsonbuf_append(&b, "}");
    res_json(res, 404, b.data);
    jsonbuf_free(&b);
}

/* ================================================================== */
/* main()                                                              */
/* ================================================================== */

static AppContext g_ctx;

#if defined(_WIN32)
static BOOL WINAPI on_console_ctrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        if (g_ctx.tree != NULL) {
            storage_save(g_ctx.tree, g_ctx.dataFile);
        }
        server_request_stop();
        return TRUE;
    }
    return FALSE;
}
#else
static void on_signal(int sig)
{
    (void)sig;
    if (g_ctx.tree != NULL) {
        storage_save(g_ctx.tree, g_ctx.dataFile);
    }
    server_request_stop();
}
#endif

static void usage(const char *exe)
{
    printf("Usage: %s [options]\n", exe);
    printf("  --port <n>        port to listen on (default %d)\n", SERVER_DEFAULT_PORT);
    printf("  --data <file>     student data file   (default %s)\n", STORAGE_DEFAULT_FILE);
    printf("  --frontend <dir>  frontend directory  (default: auto-detect)\n");
    printf("  --seed            load the sample records if the file is missing\n");
    printf("  --help            show this help\n");
}

/* Look for the frontend folder relative to the current directory and to the
 * backend folder, so the server can be started from anywhere. */
static int detect_frontend(char *out, size_t outsz)
{
    static const char *candidates[] = {
        "frontend",
        "../frontend",
        "../../frontend",
        ".."
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char probe[1024];
        snprintf(probe, sizeof(probe), "%s/index.html", candidates[i]);
        FILE *f = fopen(probe, "r");
        if (f != NULL) {
            fclose(f);
            strncpy(out, candidates[i], outsz - 1);
            out[outsz - 1] = '\0';
            return 1;
        }
    }
    strncpy(out, "frontend", outsz - 1);
    out[outsz - 1] = '\0';
    return 0;
}

int main(int argc, char **argv)
{
    int port = SERVER_DEFAULT_PORT;
    const char *dataFile = STORAGE_DEFAULT_FILE;
    char frontendDir[512];
    int seed = 0;

    detect_frontend(frontendDir, sizeof(frontendDir));

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            dataFile = argv[++i];
        } else if (strcmp(argv[i], "--frontend") == 0 && i + 1 < argc) {
            strncpy(frontendDir, argv[++i], sizeof(frontendDir) - 1);
            frontendDir[sizeof(frontendDir) - 1] = '\0';
        } else if (strcmp(argv[i], "--seed") == 0) {
            seed = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (port <= 0 || port > 65535) {
        printf("Invalid port %d\n", port);
        return 1;
    }

    printf("\n");
    printf("=========================================================\n");
    printf("  Student Record Management System Using B-Tree\n");
    printf("  Backend: C  |  Index: B-Tree (T = %d, max %d keys/node)\n",
           BTREE_T, BTREE_MAX_KEYS);
    printf("=========================================================\n\n");

    /* ---- 1. build the empty B-Tree ---------------------------------- */
    g_ctx.tree = btree_create();
    if (g_ctx.tree == NULL) {
        printf("FATAL: cannot allocate the B-Tree (out of memory).\n");
        return 1;
    }
    g_ctx.dataFile = dataFile;
    g_ctx.port = port;

    /* ---- 2. load students.dat into the B-Tree ------------------------ */
    printf("Loading records from %s ...\n", dataFile);
    int loaded = 0, skipped = 0;
    int rc = storage_load(g_ctx.tree, dataFile, &loaded, &skipped);

    if (rc == 1) {
        printf("  %s not found - starting with an empty B-Tree.\n", dataFile);
        if (seed) {
            printf("  --seed given, loading the sample records:\n");
            storage_seed_sample(g_ctx.tree);
            if (storage_save(g_ctx.tree, dataFile) == 0) {
                printf("  saved %d sample record(s) to %s\n",
                       g_ctx.tree->totalKeys, dataFile);
            }
        }
    } else if (rc == 0) {
        printf("  loaded %d record(s)", loaded);
        if (skipped > 0) {
            printf(" (%d line(s) skipped)", skipped);
        }
        printf("\n");
    } else {
        printf("  WARNING: %s\n", storage_last_error());
    }

    /* ---- 3. show the tree that was rebuilt from disk ------------------ */
    printf("\nB-Tree rebuilt from %s:\n", dataFile);
    displayTree(g_ctx.tree->root, 0);
    printf("  -> %d student(s), height %d, %d node(s)\n\n",
           g_ctx.tree->totalKeys, btree_height(g_ctx.tree),
           btree_node_count(g_ctx.tree));

    /* ---- 4. signals --------------------------------------------------- */
#if defined(_WIN32)
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#else
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
#endif

    /* ---- 5. run the HTTP server --------------------------------------- */
    int rc2 = server_run(port, frontendDir, route, &g_ctx);

    printf("\nShutting down - saving records to %s ...\n", dataFile);
    if (storage_save(g_ctx.tree, dataFile) == 0) {
        printf("Saved %d record(s). Bye.\n", g_ctx.tree->totalKeys);
    } else {
        printf("WARNING: could not save %s (%s)\n", dataFile,
               storage_last_error());
    }

    btree_destroy(g_ctx.tree);
    g_ctx.tree = NULL;

    return rc2 == 0 ? 0 : 1;
}
