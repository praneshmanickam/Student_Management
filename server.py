#!/usr/bin/env python3
"""
Student Record Management System Using B-Tree (Python Backend Server)
Provides HTTP REST API matching the C backend, serves static frontend,
and persists student records to students.dat.
"""

import sys
import os
import json
import urllib.parse
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler

BTREE_T = 3
BTREE_MAX_KEYS = 2 * BTREE_T - 1      # 5
BTREE_MAX_CHILDREN = 2 * BTREE_T      # 6
BTREE_MIN_KEYS = BTREE_T - 1          # 2

class Student:
    def __init__(self, reg_no, name, department, cgpa):
        self.reg_no = int(reg_no)
        self.name = str(name).strip()
        self.department = str(department).strip()
        self.cgpa = round(float(cgpa), 2)

    def to_dict(self):
        return {
            "regNo": self.reg_no,
            "name": self.name,
            "department": self.department,
            "cgpa": f"{self.cgpa:.2f}"
        }

class BTreeNode:
    def __init__(self, leaf=True):
        self.leaf = leaf
        self.keys = []
        self.students = []
        self.children = []

class BTreeTrace:
    def __init__(self):
        self.steps = []

    def reset(self):
        self.steps = []

    def add(self, fmt, *args):
        if args:
            step = fmt % args
        else:
            step = fmt
        self.steps.append(step)

trace = BTreeTrace()

class BTree:
    def __init__(self):
        self.root = BTreeNode(leaf=True)
        self.total_keys = 0

    def search_node(self, node, reg_no):
        if not node:
            return None, -1
        i = 0
        while i < len(node.keys) and reg_no > node.keys[i]:
            i += 1
        if i < len(node.keys) and reg_no == node.keys[i]:
            return node, i
        if node.leaf:
            return None, -1
        return self.search_node(node.children[i], reg_no)

    def search(self, reg_no):
        node, idx = self.search_node(self.root, reg_no)
        if node is not None and idx >= 0:
            return node.students[idx]
        return None

    def height(self):
        cur = self.root
        h = 0
        while cur:
            h += 1
            if cur.leaf:
                break
            cur = cur.children[0] if cur.children else None
        return h

    def node_count(self):
        def _count(node):
            if not node:
                return 0
            n = 1
            if not node.leaf:
                for c in node.children:
                    n += _count(c)
            return n
        return _count(self.root)

    def max_keys_in_node(self):
        def _max(node):
            if not node:
                return 0
            m = len(node.keys)
            if not node.leaf:
                for c in node.children:
                    m = max(m, _max(c))
            return m
        return _max(self.root)

    def traverse(self):
        result = []
        def _trav(node):
            if not node:
                return
            for i in range(len(node.keys)):
                if not node.leaf and i < len(node.children):
                    _trav(node.children[i])
                result.append(node.students[i])
            if not node.leaf and len(node.children) > len(node.keys):
                _trav(node.children[-1])
        _trav(self.root)
        return result

    def split_child(self, x):
        mid = len(x.keys) // 2
        up_key = x.keys[mid]
        up_rec = x.students[mid]

        right = BTreeNode(leaf=x.leaf)
        right.keys = x.keys[mid + 1:]
        right.students = x.students[mid + 1:]
        if not x.leaf:
            right.children = x.children[mid + 1:]
            x.children = x.children[:mid + 1]

        x.keys = x.keys[:mid]
        x.students = x.students[:mid]

        trace.add("Node full (%d keys) -> split node, promote middle key %d", BTREE_MAX_KEYS + 1, up_key)
        return right, up_key, up_rec

    def insert(self, student):
        reg_no = student.reg_no
        if self.search(reg_no) is not None:
            return 1  # Duplicate

        trace.add("Insert key %d (%s, %s)", reg_no, student.name, student.department)

        def _insert_non_full(x, depth=0):
            if x.leaf:
                i = 0
                while i < len(x.keys) and reg_no > x.keys[i]:
                    i += 1
                x.keys.insert(i, reg_no)
                x.students.insert(i, student)

                trace.add("Insert key %d into leaf node at level %d", reg_no, depth)

                if len(x.keys) > BTREE_MAX_KEYS:
                    right, up_key, up_rec = self.split_child(x)
                    return True, up_key, up_rec, right
                return False, None, None, None

            c = 0
            while c < len(x.keys) and reg_no > x.keys[c]:
                c += 1

            cmp_symbol = "<" if (c < len(x.keys) and reg_no < x.keys[c]) else ">="
            key_val = x.keys[c] if c < len(x.keys) else x.keys[-1]
            trace.add("Level %d: %d %s %d -> follow child %d", depth, reg_no, cmp_symbol, key_val, c)

            split_occurred, up_key, up_rec, right = _insert_non_full(x.children[c], depth + 1)
            if split_occurred:
                x.keys.insert(c, up_key)
                x.students.insert(c, up_rec)
                x.children.insert(c + 1, right)

                if len(x.keys) > BTREE_MAX_KEYS:
                    r2, u_k, u_r = self.split_child(x)
                    return True, u_k, u_r, r2
            return False, None, None, None

        split_occurred, up_key, up_rec, up_right = _insert_non_full(self.root, 0)
        if split_occurred:
            new_root = BTreeNode(leaf=False)
            new_root.keys = [up_key]
            new_root.students = [up_rec]
            new_root.children = [self.root, up_right]
            self.root = new_root
            trace.add("Root split -> new root key %d, tree height is now %d", up_key, self.height())

        self.total_keys += 1
        trace.add("Insert complete: %d record(s), %d node(s), height %d", self.total_keys, self.node_count(), self.height())
        return 0

    def update(self, reg_no, name=None, department=None, cgpa=None):
        node, idx = self.search_node(self.root, reg_no)
        if not node or idx < 0:
            return 1
        s = node.students[idx]
        if name is not None:
            s.name = name
        if department is not None:
            s.department = department
        if cgpa is not None:
            s.cgpa = round(float(cgpa), 2)
        trace.add("Update record %d: name=%s, department=%s, cgpa=%.2f", reg_no, s.name, s.department, s.cgpa)
        trace.add("Register number %d is the B-Tree key and is never modified", reg_no)
        return 0

    def delete(self, reg_no):
        if self.search(reg_no) is None:
            return 1
        trace.add("Delete key %d from the B-Tree", reg_no)

        def _remove_from_leaf(x, idx):
            x.keys.pop(idx)
            x.students.pop(idx)

        def _borrow_from_prev(x, idx):
            child = x.children[idx]
            sibling = x.children[idx - 1]

            child.keys.insert(0, x.keys[idx - 1])
            child.students.insert(0, x.students[idx - 1])
            if not child.leaf:
                child.children.insert(0, sibling.children.pop())

            x.keys[idx - 1] = sibling.keys.pop()
            x.students[idx - 1] = sibling.students.pop()
            trace.add("Borrow key %d from previous sibling (rotate right through parent)", child.keys[0])

        def _borrow_from_next(x, idx):
            child = x.children[idx]
            sibling = x.children[idx + 1]

            child.keys.append(x.keys[idx])
            child.students.append(x.students[idx])
            if not child.leaf:
                child.children.append(sibling.children.pop(0))

            x.keys[idx] = sibling.keys.pop(0)
            x.students[idx] = sibling.students.pop(0)
            trace.add("Borrow key %d from next sibling (rotate left through parent)", x.keys[idx])

        def _merge(x, idx):
            child = x.children[idx]
            sibling = x.children.pop(idx + 1)

            child.keys.append(x.keys.pop(idx))
            child.students.append(x.students.pop(idx))

            child.keys.extend(sibling.keys)
            child.students.extend(sibling.students)
            if not child.leaf:
                child.children.extend(sibling.children)

            trace.add("Merged two nodes into one node with %d keys", len(child.keys))

        def _fill(x, idx):
            if idx != 0 and len(x.children[idx - 1].keys) >= BTREE_T:
                _borrow_from_prev(x, idx)
            elif idx != len(x.children) - 1 and len(x.children[idx + 1].keys) >= BTREE_T:
                _borrow_from_next(x, idx)
            else:
                if idx != len(x.children) - 1:
                    _merge(x, idx)
                else:
                    _merge(x, idx - 1)

        def _delete_key_depth(x, reg_no, depth=0):
            if not x:
                return
            idx = 0
            while idx < len(x.keys) and reg_no > x.keys[idx]:
                idx += 1

            if idx < len(x.keys) and x.keys[idx] == reg_no:
                trace.add("Level %d: key %d found in this node", depth, reg_no)
                if x.leaf:
                    _remove_from_leaf(x, idx)
                    trace.add("Leaf node: remove key %d (node now holds %d keys)", reg_no, len(x.keys))
                else:
                    k = x.keys[idx]
                    if len(x.children[idx].keys) >= BTREE_T:
                        cur = x.children[idx]
                        while not cur.leaf:
                            cur = cur.children[-1]
                        pred_key = cur.keys[-1]
                        pred_rec = cur.students[-1]
                        x.keys[idx] = pred_key
                        x.students[idx] = pred_rec
                        trace.add("Internal node: replace key %d with predecessor %d", k, pred_key)
                        _delete_key_depth(x.children[idx], pred_key, depth + 1)
                    elif len(x.children[idx + 1].keys) >= BTREE_T:
                        cur = x.children[idx + 1]
                        while not cur.leaf:
                            cur = cur.children[0]
                        succ_key = cur.keys[0]
                        succ_rec = cur.students[0]
                        x.keys[idx] = succ_key
                        x.students[idx] = succ_rec
                        trace.add("Internal node: replace key %d with successor %d", k, succ_key)
                        _delete_key_depth(x.children[idx + 1], succ_key, depth + 1)
                    else:
                        trace.add("Both children have %d keys (< T = %d) -> merge children", len(x.children[idx].keys), BTREE_T)
                        _merge(x, idx)
                        _delete_key_depth(x.children[idx], k, depth + 1)
                return

            if x.leaf:
                trace.add("Level %d: key %d not present in this leaf", depth, reg_no)
                return

            last_child = (idx == len(x.keys))
            trace.add("Level %d: %d not here, descend into child %d", depth, reg_no, idx)

            if len(x.children[idx].keys) < BTREE_T:
                trace.add("Child %d holds only %d key(s) (< T = %d) -> fill it first", idx, len(x.children[idx].keys), BTREE_T)
                _fill(x, idx)

            if last_child and idx > len(x.keys):
                _delete_key_depth(x.children[idx - 1], reg_no, depth + 1)
            else:
                _delete_key_depth(x.children[idx], reg_no, depth + 1)

        _delete_key_depth(self.root, reg_no, 0)

        if len(self.root.keys) == 0:
            if not self.root.leaf:
                self.root = self.root.children[0]
                trace.add("Root became empty -> height reduced to %d", self.height())

        self.total_keys -= 1
        trace.add("Delete complete: %d record(s), %d node(s), height %d", self.total_keys, self.node_count(), self.height())
        return 0

    def to_json_node(self, node, level=0):
        if not node:
            return None
        res = {
            "level": level,
            "leaf": node.leaf,
            "keys": node.keys
        }
        if not node.leaf:
            res["children"] = [self.to_json_node(c, level + 1) for c in node.children]
        return res

def get_stats_dict(tree, data_file, port):
    root_keys = tree.root.keys if tree and tree.root else []
    return {
        "totalStudents": tree.total_keys,
        "btreeHeight": tree.height(),
        "totalNodes": tree.node_count(),
        "maxKeysInNode": tree.max_keys_in_node(),
        "minDegree": BTREE_T,
        "maxKeys": BTREE_MAX_KEYS,
        "maxChildren": BTREE_MAX_CHILDREN,
        "rootKeys": root_keys
    }

def storage_load(tree, data_path):
    if not os.path.exists(data_path):
        return False
    loaded = 0
    with open(data_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) == 4:
                try:
                    reg_no = int(parts[0].strip())
                    name = parts[1].strip()
                    dept = parts[2].strip()
                    cgpa = float(parts[3].strip())
                    s = Student(reg_no, name, dept, cgpa)
                    if tree.insert(s) == 0:
                        loaded += 1
                except ValueError:
                    continue
    return True

def storage_save(tree, data_path):
    students = tree.traverse()
    tmp_path = data_path + ".tmp"
    os.makedirs(os.path.dirname(os.path.abspath(data_path)), exist_ok=True)
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write("# Student Record Management System - persistent storage\n")
        f.write("# format: registerNumber|name|department|cgpa\n")
        for s in students:
            f.write(f"{s.reg_no}|{s.name}|{s.department}|{s.cgpa:.2f}\n")
    if os.path.exists(data_path):
        os.remove(data_path)
    os.rename(tmp_path, data_path)
    return True

def storage_seed_sample(tree):
    samples = [
        (101,  "Dinesh", "IT",    7.8),
        (104,  "Bala",   "CSE",   8.2),
        (106,  "Gokul",  "CSE",   9.8),
        (109,  "Kumar",  "ECE",   9.1),
        (1001, "Arun",   "AIDS",  8.5),
        (1002, "Elango", "AIDS",  8.9)
    ]
    for reg, name, dept, cgpa in samples:
        tree.insert(Student(reg, name, dept, cgpa))

class RequestHandler(BaseHTTPRequestHandler):
    tree = None
    data_file = "backend/students.dat"
    port = 8080
    frontend_dir = "frontend"

    def log_message(self, format, *args):
        # Suppress standard HTTP request logging to keep console output clean
        pass

    def send_json(self, status_code, data):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status_code, message):
        self.send_json(status_code, {"success": False, "status": status_code, "error": message})

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip('/')
        if not path:
            path = '/'
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/api/health":
            self.send_json(200, {
                "success": True,
                "status": "ok",
                "service": "Student Record Management System (C + B-Tree)"
            })
            return

        if path == "/api/stats":
            self.send_json(200, {
                "success": True,
                "stats": get_stats_dict(self.tree, self.data_file, self.port),
                "storageFile": self.data_file,
                "port": self.port
            })
            return

        if path == "/api/students":
            all_students = [s.to_dict() for s in self.tree.traverse()]
            self.send_json(200, {
                "success": True,
                "count": len(all_students),
                "students": all_students
            })
            return

        if path == "/api/student":
            raw_reg = query.get("regNo", [None])[0]
            if not raw_reg:
                self.send_error_json(400, "Missing query parameter: regNo.")
                return
            try:
                reg_no = int(raw_reg)
            except ValueError:
                self.send_error_json(400, "Invalid register number: it must be numeric.")
                return

            found = self.tree.search(reg_no)
            if not found:
                self.send_json(404, {
                    "success": False,
                    "error": "Student not found.",
                    "regNo": reg_no,
                    "searchedBy": "btree search"
                })
                return
            self.send_json(200, {
                "success": True,
                "student": found.to_dict(),
                "foundBy": "btree search"
            })
            return

        if path == "/api/btree":
            self.send_json(200, {
                "success": True,
                "minDegree": BTREE_T,
                "maxKeys": BTREE_MAX_KEYS,
                "tree": self.tree.to_json_node(self.tree.root)
            })
            return

        if path == "/api/operation":
            self.send_json(200, {
                "success": True,
                "steps": trace.steps,
                "stats": get_stats_dict(self.tree, self.data_file, self.port)
            })
            return

        if path.startswith("/api/"):
            self.send_json(404, {
                "success": False,
                "status": 404,
                "error": "Unknown API endpoint.",
                "method": "GET",
                "path": path
            })
            return

        # Serve static files from frontend
        return self.serve_static(parsed.path)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip('/')

        content_length = int(self.headers.get('Content-Length', 0))
        body_data = self.rfile.read(content_length) if content_length > 0 else b""

        if path == "/api/seed":
            trace.reset()
            trace.add("Seed the B-Tree with the sample records shipped with the project")
            storage_seed_sample(self.tree)
            saved = storage_save(self.tree, self.data_file)
            self.send_json(200, {
                "success": True,
                "message": "Sample student records loaded.",
                "steps": trace.steps,
                "tree": self.tree.to_json_node(self.tree.root),
                "stats": get_stats_dict(self.tree, self.data_file, self.port),
                "saved": saved
            })
            return

        if path == "/api/student":
            try:
                data = json.loads(body_data.decode("utf-8"))
            except Exception:
                self.send_error_json(400, "Invalid JSON body. Required fields: regNo, name, department, cgpa.")
                return

            if "regNo" not in data or "name" not in data or "department" not in data or "cgpa" not in data:
                self.send_error_json(400, "Invalid JSON body. Required fields: regNo, name, department, cgpa.")
                return

            try:
                reg_no = int(data["regNo"])
            except Exception:
                self.send_error_json(400, "Register Number must be numeric.")
                return

            name = str(data["name"]).strip()
            dept = str(data["department"]).strip()
            if not name:
                self.send_error_json(400, "Name cannot be empty.")
                return
            if not dept:
                self.send_error_json(400, "Department cannot be empty.")
                return

            try:
                cgpa = float(data["cgpa"])
                if cgpa < 0.0 or cgpa > 10.0:
                    raise ValueError()
            except Exception:
                self.send_error_json(400, "CGPA must be a number between 0 and 10.")
                return

            student = Student(reg_no, name, dept, cgpa)
            trace.reset()
            rc = self.tree.insert(student)
            if rc == 1:
                self.send_json(409, {
                    "success": False,
                    "error": "Duplicate Register Number.",
                    "message": f"Register Number {reg_no} already exists.",
                    "regNo": reg_no
                })
                return

            saved = storage_save(self.tree, self.data_file)
            self.send_json(201, {
                "success": True,
                "message": f"Student {reg_no} inserted successfully.",
                "student": student.to_dict(),
                "operation": "insert",
                "steps": trace.steps,
                "tree": self.tree.to_json_node(self.tree.root),
                "stats": get_stats_dict(self.tree, self.data_file, self.port),
                "saved": saved
            })
            return

        self.send_json(404, {"success": False, "error": "Unknown API endpoint."})

    def do_PUT(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip('/')
        query = urllib.parse.parse_qs(parsed.query)

        content_length = int(self.headers.get('Content-Length', 0))
        body_data = self.rfile.read(content_length) if content_length > 0 else b""

        if path == "/api/student":
            raw_reg = query.get("regNo", [None])[0]
            if not raw_reg:
                self.send_error_json(400, "Missing query parameter: regNo.")
                return
            try:
                reg_no = int(raw_reg)
            except ValueError:
                self.send_error_json(400, "Invalid register number: it must be numeric.")
                return

            try:
                data = json.loads(body_data.decode("utf-8")) if body_data else {}
            except Exception:
                self.send_error_json(400, "Invalid JSON body.")
                return

            has_name = "name" in data
            has_dept = "department" in data
            has_cgpa = "cgpa" in data

            if not has_name and not has_dept and not has_cgpa:
                self.send_error_json(400, "Nothing to update. Provide name, department and/or cgpa.")
                return

            existing = self.tree.search(reg_no)
            if not existing:
                self.send_json(404, {
                    "success": False,
                    "error": "Student not found.",
                    "message": f"No student with register number {reg_no}.",
                    "regNo": reg_no
                })
                return

            name = str(data["name"]).strip() if has_name else None
            dept = str(data["department"]).strip() if has_dept else None
            cgpa = None
            if has_cgpa:
                try:
                    cgpa = float(data["cgpa"])
                    if cgpa < 0.0 or cgpa > 10.0:
                        raise ValueError()
                except Exception:
                    self.send_error_json(400, "CGPA must be a number between 0 and 10.")
                    return

            trace.reset()
            self.tree.update(reg_no, name=name, department=dept, cgpa=cgpa)
            saved = storage_save(self.tree, self.data_file)
            updated = self.tree.search(reg_no)

            self.send_json(200, {
                "success": True,
                "message": f"Student {reg_no} updated successfully.",
                "student": updated.to_dict(),
                "operation": "update",
                "steps": trace.steps,
                "stats": get_stats_dict(self.tree, self.data_file, self.port),
                "saved": saved
            })
            return

        self.send_json(404, {"success": False, "error": "Unknown API endpoint."})

    def do_DELETE(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip('/')
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/api/student":
            raw_reg = query.get("regNo", [None])[0]
            if not raw_reg:
                self.send_error_json(400, "Missing query parameter: regNo.")
                return
            try:
                reg_no = int(raw_reg)
            except ValueError:
                self.send_error_json(400, "Invalid register number: it must be numeric.")
                return

            trace.reset()
            rc = self.tree.delete(reg_no)
            if rc != 0:
                self.send_json(404, {
                    "success": False,
                    "error": "Student not found.",
                    "message": f"No student with register number {reg_no}.",
                    "regNo": reg_no
                })
                return

            saved = storage_save(self.tree, self.data_file)
            self.send_json(200, {
                "success": True,
                "message": f"Student {reg_no} deleted successfully.",
                "regNo": reg_no,
                "operation": "delete",
                "steps": trace.steps,
                "tree": self.tree.to_json_node(self.tree.root),
                "stats": get_stats_dict(self.tree, self.data_file, self.port),
                "saved": saved
            })
            return

        self.send_json(404, {"success": False, "error": "Unknown API endpoint."})

    def serve_static(self, rel_path):
        rel_path = rel_path.lstrip('/')
        if not rel_path:
            rel_path = "index.html"
        full_path = os.path.join(self.frontend_dir, rel_path)

        if os.path.isdir(full_path):
            full_path = os.path.join(full_path, "index.html")

        if not os.path.isfile(full_path):
            self.send_error_json(404, f"File not found: {rel_path}")
            return

        ext = os.path.splitext(full_path)[1].lower()
        content_types = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "application/javascript; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".png": "image/png",
            ".jpg": "image/jpeg",
            ".svg": "image/svg+xml",
            ".ico": "image/x-icon"
        }
        ct = content_types.get(ext, "application/octet-stream")

        try:
            with open(full_path, "rb") as f:
                content = f.read()
            self.send_response(200)
            self.send_header("Content-Type", ct)
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_error_json(500, f"Error reading file: {str(e)}")

def main():
    parser = argparse.ArgumentParser(description="Student Record Management System B-Tree Server")
    parser.add_argument("--port", type=int, default=8080, help="Port to listen on")
    parser.add_argument("--data", type=str, default="backend/students.dat", help="Path to data file")
    parser.add_argument("--frontend", type=str, default="frontend", help="Path to frontend directory")
    parser.add_argument("--seed", action="store_true", help="Seed with sample data if file missing")
    args = parser.parse_args()

    tree = BTree()
    if not storage_load(tree, args.data):
        print(f"Data file {args.data} not found or empty.")
        if args.seed or True:  # Default to seeding if empty
            print("Seeding sample records...")
            storage_seed_sample(tree)
            storage_save(tree, args.data)

    RequestHandler.tree = tree
    RequestHandler.data_file = args.data
    RequestHandler.port = args.port
    RequestHandler.frontend_dir = args.frontend

    server = HTTPServer(('0.0.0.0', args.port), RequestHandler)
    print("\n=========================================================")
    print("  Student Record Management System Using B-Tree")
    print(f"  Backend: Python | Index: B-Tree (T = {BTREE_T}, max {BTREE_MAX_KEYS} keys/node)")
    print("=========================================================\n")
    print(f"    Website : http://localhost:{args.port}")
    print(f"    API     : http://localhost:{args.port}/api/students")
    print(f"\n    Server running... Records saved to {args.data}\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        storage_save(tree, args.data)
        print("Data saved. Goodbye!")

if __name__ == "__main__":
    main()
