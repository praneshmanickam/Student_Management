# Student Record Management System Using B-Tree

A complete full-stack web application that keeps student records in a **hand written B-Tree
implemented in C**, and drives a modern responsive dashboard through a small HTTP server that is
also written in C.

* The **B-Tree is the real index** — no database, no ORM, no B-Tree library.
* Every button in the browser ends up as a real B-Tree operation in C.
* Records are stored persistently in `students.dat` and are re-indexed into the B-Tree every
  time the server starts.

```
Browser (HTML + CSS + JavaScript)
        │  HTTP / JSON
        ▼
C HTTP server (server.c)  ──►  JSON API routes (main.c)
                                   │
                                   ▼
                          B-Tree in C (btree.c)     T = 3, max 5 keys / node
                                   │
                                   ▼
                     students.dat (storage.c)
```

---

## 1. Project introduction

This project demonstrates the practical application of the B-Tree data structure for efficient
student record management. The **Register Number** is used as the primary key, and the system
supports insertion, searching, updating, deletion, traversal and B-Tree visualization.

The dashboard shows live statistics (number of records, tree height, number of nodes, the largest
node, the minimum degree), draws the actual tree, and explains — step by step — which B-Tree
operations the C backend performed for the last insert or delete.

---

## 2. Features

| Area | Feature |
| --- | --- |
| Dashboard | Total students, B-Tree height, total nodes, max keys in a node, minimum degree |
| Add student | Validated form → `btree_insert()` in C, success / error messages |
| Search student | `search()` in C (no linear scan), "Student not found" when missing |
| Records table | Sorted by Register Number using B-Tree in-order traversal |
| Update student | `btree_update()` — name / department / CGPA, Register Number never changes |
| Delete student | Confirmation modal → `btree_delete()` with borrow / merge |
| Visualization | The real tree from `btree_to_json()`, drawn with SVG + HTML/CSS |
| Operation view | The exact steps the C B-Tree performed (split / borrow / merge / promote) |
| Persistence | `students.dat` is rewritten after every change and reloaded on start-up |
| Security | All input validated in C, JSON escaped, path traversal blocked, bounded buffers |
| Tests | C unit + stress test (400 inserts / 400 deletes), Node renderer test, HTTP integration test |

---

## 3. B-Tree explanation

A B-Tree of **minimum degree `T = 3`** is used:

| Property | Value |
| --- | --- |
| Minimum degree `T` | 3 |
| Maximum keys per node | `2T − 1 = 5` |
| Maximum children per node | `2T = 6` |
| Minimum keys per node (non-root) | `T − 1 = 2` |
| Key | Register Number |
| Height | Balanced — all leaves are at the same depth |

Insertion is **bottom-up**: the key is written into the correct leaf and any node that overflows
past 5 keys is split immediately, promoting its middle key to the parent (`splitChild()`).
Deletion is the classic algorithm: a node that would drop below `T − 1` keys is first topped up
with `fill()`, which either borrows a key from a sibling (`borrowFromPrevious()` /
`borrowFromNext()`) or merges two nodes (`merge()`).

Operations implemented in C (`backend/btree.c`):

```
createNode()          allocate a node
search()              find a register number
splitChild()          split an overflowing node
insertNonFull()       descend + insert, split on the way back up
insert()              public insert (duplicate check included)
deleteKey()           public delete
merge()               merge child + separator + sibling
borrowFromPrevious()  rotate a key from the left sibling
borrowFromNext()      rotate a key from the right sibling
fill()                top up a node before descending into it
traverse()            in-order walk → sorted record list
displayTree()         console dump of the tree
```

---

## 4. Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ frontend/  HTML5 + CSS3 + vanilla JavaScript (no framework)          │
│   index.html    dashboard + add student + search student             │
│   students.html records table + edit modal + delete modal            │
│   btree.html    B-Tree visualization + operation visualization       │
│   about.html    project documentation                                │
└───────────────────────────────┬──────────────────────────────────────┘
                                │  fetch() → JSON
┌───────────────────────────────▼──────────────────────────────────────┐
│ backend/server.c   minimal HTTP/1.1 server (sockets + threads)       │
│                    serves the static frontend files                  │
│                    forwards /api/* to the router                     │
└───────────────────────────────┬──────────────────────────────────────┘
                                │
┌───────────────────────────────▼──────────────────────────────────────┐
│ backend/main.c     JSON API routes, input validation, responses      │
└───────────────────────────────┬──────────────────────────────────────┘
                                │
┌───────────────────────────────▼──────────────────────────────────────┐
│ backend/btree.c    the B-Tree (T = 3)          ◄── the core of the   │
│ backend/student.c  student record + validation     project          │
│ backend/storage.c  students.dat load / save                          │
└──────────────────────────────────────────────────────────────────────┘
```

The B-Tree module knows nothing about HTTP, sockets or JSON: it is pure data-structure code.

---

## 5. Folder structure

```
Student_BTree_Web/
│
├── backend/
│   ├── main.c              # entry point + JSON API routes
│   ├── btree.c / btree.h   # the B-Tree data structure (T = 3)
│   ├── student.c / .h      # student record + validation helpers
│   ├── storage.c / .h      # students.dat load / save
│   ├── server.c / .h       # HTTP server (sockets + threads)
│   └── students.dat        # persistent student records
│
├── frontend/
│   ├── index.html          # dashboard, add student, search student
│   ├── students.html       # records table, edit, delete
│   ├── btree.html          # B-Tree visualization
│   ├── about.html          # about the project
│   ├── css/style.css
│   └── js/{app,students,btree}.js
│
├── test/
│   ├── btree_test.c        # C unit + invariant + stress test
│   ├── frontend_test.js    # renderer / layout test (Node)
│   └── integration_test.sh # end-to-end HTTP test
│
├── README.md
├── Makefile
└── run.bat                 # Windows one-click build + run
```

---

## 6. Requirements

* **C compiler** — GCC (Linux / macOS) or MinGW-w64 (Windows)
* **make** (optional, the raw gcc command is given below)
* A modern browser (Chrome, Edge, Firefox, Safari)
* Nothing else: no database, no Node.js, no external libraries.

---

## 7. Installation

```bash
# Linux / macOS
git clone <your-repo-url> Student_BTree_Web
cd Student_BTree_Web
```

On Windows: download / copy the `Student_BTree_Web` folder anywhere, e.g. `C:\Student_BTree_Web`.

---

## 8. Compilation

### Windows (MinGW-w64) — exact commands

Open **Command Prompt** (or PowerShell) inside the project folder:

```bat
cd C:\Student_BTree_Web

gcc -Wall -Wextra -O2 backend\*.c -o student_server.exe -lws2_32
```

Or simply double-click **`run.bat`**, which compiles (if needed) and starts the server.

### Linux / macOS

```bash
gcc -Wall -Wextra -O2 -std=c11 backend/*.c -o student_server -lpthread
```

### With make (Linux / macOS)

```bash
make            # builds ./student_server
make test       # builds and runs the C B-Tree test suite
make test-all   # C tests + frontend tests + HTTP integration test
```

The program compiles with **zero warnings** using `-Wall -Wextra`.

---

## 9. How to start the server

### Windows

```bat
student_server.exe --port 8080 --data backend\students.dat --frontend frontend
```

or just:

```bat
run.bat
```

### Linux / macOS

```bash
./student_server --port 8080 --data backend/students.dat --frontend frontend
```

Options (all optional):

| Option | Default | Meaning |
| --- | --- | --- |
| `--port <n>` | `8080` | TCP port to listen on |
| `--data <file>` | `students.dat` | student data file |
| `--frontend <dir>` | auto-detected | folder that holds the HTML/CSS/JS |
| `--seed` | off | load the sample records when the data file is missing |

The server prints the B-Tree it rebuilt from disk, then waits for requests:

```
=========================================================
  Student Record Management System Using B-Tree
  Backend: C  |  Index: B-Tree (T = 3, max 5 keys/node)
=========================================================

Loading records from backend/students.dat ...
  loaded 6 record(s)

B-Tree rebuilt from backend/students.dat:
Level 0: [109]
    |-- Level 1: [101 104 106]   (leaf)
    |-- Level 1: [1001 1002]   (leaf)
  -> 6 student(s), height 2, 3 node(s)

HTTP server listening on http://localhost:8080
Serving frontend from: frontend
```

Stop it with **Ctrl+C** — the records are saved automatically (they are also saved after every
single change).

---

## 10. How to open the website

Start the server, then open in your browser:

| Page | URL |
| --- | --- |
| Dashboard | <http://localhost:8080/> |
| Student records | <http://localhost:8080/students.html> |
| B-Tree visualization | <http://localhost:8080/btree.html> |
| About project | <http://localhost:8080/about.html> |

If you changed the port, use that port instead, e.g. <http://localhost:9090/>.

> The frontend talks to the API with **relative** URLs (`fetch('api/students')`), so the site also
> works when it is served behind a proxy or from a sub-path.

---

## 11. API documentation

All responses are JSON. `Content-Type: application/json`.

### `GET /api/students`

Every record, sorted by Register Number (B-Tree in-order traversal).

```json
{
  "success": true,
  "count": 6,
  "students": [
    { "regNo": 101,  "name": "Dinesh", "department": "IT",    "cgpa": 7.80 },
    { "regNo": 104,  "name": "Bala",   "department": "CSE",   "cgpa": 8.20 },
    { "regNo": 106,  "name": "Gokul",  "department": "CSE",   "cgpa": 9.80 }
  ]
}
```

### `GET /api/student?regNo=106`

```json
{
  "success": true,
  "student": { "regNo": 106, "name": "Gokul", "department": "CSE", "cgpa": 9.8 },
  "foundBy": "btree search"
}
```

Missing record → **HTTP 404**

```json
{ "success": false, "error": "Student not found.", "regNo": 106, "searchedBy": "btree search" }
```

### `POST /api/student`

Request body:

```json
{ "regNo": 106, "name": "Gokul", "department": "CSE", "cgpa": 9.8 }
```

Response **HTTP 201**:

```json
{
  "success": true,
  "message": "Student 106 inserted successfully.",
  "student": { "regNo": 106, "name": "Gokul", "department": "CSE", "cgpa": 9.80 },
  "operation": "insert",
  "steps": [
    "Insert key 106 (Gokul, CSE)",
    "Level 0: 106 >= 109 -> follow child 1",
    "Insert key 106 into leaf node at level 1",
    "Insert complete: 7 record(s), 4 node(s), height 2"
  ],
  "tree": { "level": 0, "leaf": false, "keys": [109], "children": [ ... ] },
  "stats": { "totalStudents": 7, "btreeHeight": 2, "totalNodes": 4 },
  "saved": true
}
```

Duplicate register number → **HTTP 409**

```json
{ "success": false, "error": "Duplicate Register Number.",
  "message": "Register Number 106 already exists.", "regNo": 106 }
```

### `PUT /api/student?regNo=106`

Updates `name`, `department` and/or `cgpa`. The Register Number (the B-Tree key) can never change.

```json
{ "name": "Gokul Raj", "department": "CSE", "cgpa": 9.9 }
```

```json
{ "success": true, "message": "Student 106 updated successfully.",
  "student": { "regNo": 106, "name": "Gokul Raj", "department": "CSE", "cgpa": 9.90 },
  "operation": "update",
  "steps": [ "Update record 106: name=Gokul Raj, department=CSE, cgpa=9.90",
             "Register number 106 is the B-Tree key and is never modified" ] }
```

### `DELETE /api/student?regNo=106`

```json
{
  "success": true,
  "message": "Student 106 deleted successfully.",
  "regNo": 106,
  "operation": "delete",
  "steps": [
    "Delete key 106 from the B-Tree",
    "Level 0: 106 not here, descend into child 0",
    "Level 1: key 106 found in this node",
    "Leaf node: remove key 106 (node now holds 2 keys)",
    "Delete complete: 6 record(s), 3 node(s), height 2"
  ],
  "tree": { ... },
  "saved": true
}
```

### `GET /api/btree`

The **actual** B-Tree, generated by `btree_to_json()`:

```json
{
  "success": true,
  "minDegree": 3,
  "maxKeys": 5,
  "tree": {
    "level": 0,
    "leaf": false,
    "keys": [109],
    "children": [
      { "level": 1, "leaf": true, "keys": [101, 104, 106] },
      { "level": 1, "leaf": true, "keys": [1001, 1002] }
    ]
  }
}
```

### `GET /api/stats`

```json
{
  "success": true,
  "stats": {
    "totalStudents": 6,
    "btreeHeight": 2,
    "totalNodes": 3,
    "maxKeysInNode": 3,
    "minDegree": 3,
    "maxKeys": 5,
    "maxChildren": 6,
    "rootKeys": [109]
  },
  "storageFile": "backend/students.dat",
  "port": 8080
}
```

### Other endpoints

| Endpoint | Purpose |
| --- | --- |
| `GET /api/health` | liveness check |
| `GET /api/operation` | steps of the last B-Tree operation |
| `POST /api/seed` | (re)load the sample records shipped with the project |

### Error responses

| HTTP | `error` | Cause |
| --- | --- | --- |
| 400 | `Register Number must be numeric.` | non numeric reg-no |
| 400 | `Name cannot be empty.` / `Department cannot be empty.` | empty text field |
| 400 | `CGPA must be a number between 0 and 10.` | out of range / not a number |
| 400 | `Invalid JSON body. …` | malformed or incomplete JSON |
| 400 | `Missing query parameter: regNo.` | missing `?regNo=` |
| 404 | `Student not found.` | no such register number |
| 404 | `Unknown API endpoint.` | wrong path |
| 405 | `Method not allowed. …` | wrong HTTP verb |
| 409 | `Duplicate Register Number.` | key already in the tree |
| 500 | `… memory …` | allocation failure |

---

## 12. Sample student data

`backend/students.dat` (format `registerNumber|name|department|cgpa`):

```
# Student Record Management System - persistent storage
# format: registerNumber|name|department|cgpa
101|Dinesh|IT|7.8
104|Bala|CSE|8.2
106|Gokul|CSE|9.8
109|Kumar|ECE|9.1
1001|Arun|AIDS|8.5
1002|Elango|AIDS|8.9
```

Inserting these six records in this order makes the B-Tree implementation produce exactly:

```
Level 0: [109]
Level 1: [101 104 106]
Level 1: [1001 1002]
```

Nothing about that shape is hard-coded: the tree is the natural result of splitting a node that
holds 6 keys (`[101 104 106 109 1001 1002]`) and promoting the middle key `109` into a new root.

Use **“Insert sample records”** on the dashboard, or `POST /api/seed`, to reload this data set at
any time.

---

## 13. Screenshots

*(add your own screenshots here — the pages listed below are what to capture)*

| # | Page | What it shows |
| --- | --- | --- |
| 1 | Dashboard | statistics cards, the current B-Tree, operation panel |
| 2 | Add Student | validated form + “Student 106 inserted successfully.” |
| 3 | Search Student | record found / “Student not found.” |
| 4 | Students | responsive sorted table with Edit / Delete |
| 5 | Update Student | modal, Register Number read-only |
| 6 | Delete Student | “Are you sure you want to delete this student?” |
| 7 | B-Tree Visualization | the real tree, levels, zoom |
| 8 | Operation Visualization | split / borrow / merge steps with before & after trees |

---

## 14. Future enhancements

* Range queries on Register Number using the ordered leaves of the B-Tree.
* Animated step-by-step playback of a split, borrow or merge.
* Secondary index by department and CGPA ranking reports.
* Binary serialisation of `students.dat` for very large data sets.
* Authentication and an audit log of every B-Tree operation.
* Pagination / lazy loading for large record sets.

---

## 15. Testing

```bash
make test            # C unit + invariant + stress test (400 inserts, 400 deletes)
make test-frontend   # renderer / layout test (needs Node.js)
make test-all        # everything, including the HTTP integration test
```

| Suite | What it verifies |
| --- | --- |
| `test/btree_test.c` | sorted keys, key-count bounds, child separation, all leaves at the same depth, duplicate rejection, update, delete in random order, persistence round trip, JSON output |
| `test/frontend_test.js` | tree layout has no overlapping nodes, parents are centred over their children, one box per node is rendered, level listing, operation panel, HTML escaping |
| `test/integration_test.sh` | the full stack over HTTP: add, search, update, delete, duplicate, invalid CGPA, missing student, malformed JSON, path traversal, restart persistence, all static files |

Current status: **all suites pass** (63 HTTP checks, C stress test, renderer test).

---

## 16. Notes for graders

* The B-Tree is written from scratch in `backend/btree.c` — no library, no database.
* `students.dat` is only the *storage medium*: on start-up every line is pushed through
  `btree_insert()`, so the B-Tree remains the index that answers every query.
* The frontend contains **no** data structure of its own: it renders whatever
  `GET /api/btree` returns.
* All validation is performed in C as well as in the browser (the browser copy only improves the
  user experience).
