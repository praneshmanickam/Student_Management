#!/usr/bin/env bash
# ======================================================================
#  test/integration_test.sh
#  End-to-end test of the whole stack through the HTTP API:
#  add, search, update, delete, duplicate reg-no, invalid CGPA,
#  missing student, persistence across a server restart.
#
#  Usage: bash test/integration_test.sh [port]
# ======================================================================
set -u

PORT="${1:-8177}"
BASE="http://localhost:${PORT}"
DATA="/tmp/srms_integration_$$.dat"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/student_server"
PASS=0
FAIL=0

if [ ! -x "$BIN" ]; then
    echo "building the server ..."
    make -C "$ROOT" >/dev/null || { echo "build failed"; exit 1; }
fi

start_server() {
    ( cd "$ROOT" && setsid ./student_server --port "$PORT" --data "$DATA" --frontend frontend \
        > "/tmp/srms_server_$$.log" 2>&1 < /dev/null & echo $! > "/tmp/srms_pid_$$" )
    for _ in $(seq 1 40); do
        curl -s -o /dev/null "$BASE/api/health" && return 0
        sleep 0.2
    done
    echo "server did not start"; cat "/tmp/srms_server_$$.log"; exit 1
}

stop_server() {
    # kill every server that is listening on our test port
    for pid in $(pgrep -f "student_server --port $PORT" 2>/dev/null); do
        kill "$pid" 2>/dev/null
    done
    [ -f "/tmp/srms_pid_$$" ] && kill "$(cat "/tmp/srms_pid_$$")" 2>/dev/null
    sleep 0.5
}

ok()   { PASS=$((PASS+1)); printf "  \033[32mPASS\033[0m %s\n" "$1"; }
bad()  { FAIL=$((FAIL+1)); printf "  \033[31mFAIL\033[0m %s\n" "$1"; }

# expect_json <description> <json> <jq-like python check>
check_json() {
    local desc="$1" json="$2" py="$3"
    if printf '%s' "$json" | python3 -c "
import json,sys
d=json.load(sys.stdin)
$py
" 2>/dev/null; then ok "$desc"; else bad "$desc  ->  $json"; fi
}

code_of() { printf '%s' "$1" | tail -c 20 | grep -o '[0-9]\{3\}' | tail -1; }

echo "============================================================"
echo " Student Record Management System - integration test"
echo "============================================================"
rm -f "$DATA"

# ---------------------------------------------------------------- start
start_server
echo
echo "[1] server start-up"
check_json "health endpoint answers" "$(curl -s $BASE/api/health)" "sys.exit(0 if d['success'] else 1)"
check_json "empty tree on first start" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['totalStudents']==0 else 1)"

# ---------------------------------------------------------------- add
echo
echo "[2] add students (the sample data set)"
add() {
    curl -s -X POST "$BASE/api/student" -H "Content-Type: application/json" \
        -d "{\"regNo\":$1,\"name\":\"$2\",\"department\":\"$3\",\"cgpa\":$4}"
}
for rec in "101|Dinesh|IT|7.8" "104|Bala|CSE|8.2" "106|Gokul|CSE|9.8" "109|Kumar|ECE|9.1" "1001|Arun|AIDS|8.5" "1002|Elango|AIDS|8.9"; do
    IFS='|' read -r r n d c <<< "$rec"
    out="$(add "$r" "$n" "$d" "$c")"
    check_json "insert $r ($n)" "$out" "sys.exit(0 if d['success'] and d['student']['regNo']==$r and d['student']['name']=='$n' else 1)"
done

echo
echo "[3] the B-Tree built from the sample data"
check_json "6 students indexed" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['totalStudents']==6 else 1)"
check_json "height is 2" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['btreeHeight']==2 else 1)"
check_json "3 nodes" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['totalNodes']==3 else 1)"
check_json "min degree 3 / max 5 keys" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['minDegree']==3 and d['stats']['maxKeys']==5 else 1)"
check_json "root key is 109" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['rootKeys']==[109] else 1)"
check_json "level 1 = [101,104,106] and [1001,1002]" "$(curl -s $BASE/api/btree)" \
    "sys.exit(0 if d['tree']['keys']==[109] and d['tree']['children'][0]['keys']==[101,104,106] and d['tree']['children'][1]['keys']==[1001,1002] else 1)"

# filling the right leaf to 6 keys must split it and promote 1005 to the root
for r in 1003 1004 1005; do add "$r" "S$r" AIDS 7.5 >/dev/null; done
check_json "inserting the 6th key splits the leaf and promotes a middle key" "$(add 1006 S1006 AIDS 7.5)" \
    "sys.exit(0 if any('split' in s for s in d.get('steps',[])) and len(d['tree']['keys'])==2 and d['stats']['totalNodes']==4 else 1)"
check_json "the tree grew to 10 students" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['totalStudents']==10 else 1)"

# ---------------------------------------------------------------- search
echo
echo "[4] search"
check_json "search 106 returns Gokul / CSE / 9.8" "$(curl -s "$BASE/api/student?regNo=106")" \
    "sys.exit(0 if d['student']['name']=='Gokul' and d['student']['department']=='CSE' and abs(d['student']['cgpa']-9.8)<0.001 else 1)"
missing="$(curl -s -o /tmp/m.json -w '%{http_code}' "$BASE/api/student?regNo=9999")"
check_json "search of a missing student returns 404 + 'Student not found'" "$(cat /tmp/m.json)" "sys.exit(0 if d['success']==False and 'not found' in d['error'] else 1)"
[ "$missing" = "404" ] && ok "missing student HTTP status is 404" || bad "missing student HTTP status is $missing"
check_json "non numeric regNo is rejected" "$(curl -s "$BASE/api/student?regNo=abc")" "sys.exit(0 if d['success']==False else 1)"

# ---------------------------------------------------------------- update
echo
echo "[5] update"
check_json "update 106 name/department/cgpa" \
    "$(curl -s -X PUT "$BASE/api/student?regNo=106" -H "Content-Type: application/json" -d '{"name":"Gokul Raj","department":"CSE","cgpa":9.9}')" \
    "sys.exit(0 if d['student']['name']=='Gokul Raj' and abs(d['student']['cgpa']-9.9)<0.001 and d['student']['regNo']==106 else 1)"
check_json "register number cannot be changed" \
    "$(curl -s -X PUT "$BASE/api/student?regNo=106" -H "Content-Type: application/json" -d '{"regNo":999,"name":"X"}')" \
    "sys.exit(0 if d['student']['regNo']==106 else 1)"
check_json "update of a missing student returns 404" \
    "$(curl -s -o /dev/null -w '%{http_code}' -X PUT "$BASE/api/student?regNo=555" -H 'Content-Type: application/json' -d '{"name":"X"}')" \
    "sys.exit(0 if d==404 else 1)"
check_json "invalid CGPA on update is rejected" \
    "$(curl -s -X PUT "$BASE/api/student?regNo=106" -H 'Content-Type: application/json' -d '{"cgpa":12}')" \
    "sys.exit(0 if d['success']==False and 'CGPA' in d['error'] else 1)"

# ---------------------------------------------------------------- delete
echo
echo "[6] delete (borrow / merge paths)"
del() { curl -s -X DELETE "$BASE/api/student?regNo=$1"; }
check_json "delete 106 reports the B-Tree steps" "$(del 106)" "sys.exit(0 if d['success'] and len(d.get('steps',[]))>1 else 1)"
check_json "106 is gone from the tree" "$(curl -s $BASE/api/student?regNo=106)" "sys.exit(0 if d['success']==False else 1)"
check_json "delete of a missing student returns 404" "$(del 106)" "sys.exit(0 if d['success']==False else 1)"
check_json "delete 1001 keeps the B-Tree valid" "$(del 1001)" "sys.exit(0 if d['success'] and d['stats']['totalStudents']==8 else 1)"

# a bigger tree, so deleting keys has to borrow from / merge with siblings
for r in $(seq 1 40); do
    curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' \
        -d "{\"regNo\":$((300+r)),\"name\":\"Student $r\",\"department\":\"CSE\",\"cgpa\":7.5}" >/dev/null
done
BORROWED=0
MERGED=0
for r in $(seq 1 40); do
    steps="$(del $((300+r)) | python3 -c "import json,sys; print('|'.join(json.load(sys.stdin).get('steps',[])))" 2>/dev/null)"
    case "$steps" in *Borrow*) BORROWED=1 ;; esac
    case "$steps" in *Merged*) MERGED=1 ;; esac
done
[ "$BORROWED" = "1" ] && ok "deleting keys borrowed from a sibling (borrowFromPrevious / borrowFromNext)" \
    || bad "no delete reported a borrow"
[ "$MERGED" = "1" ] && ok "deleting keys merged two nodes (merge)" || bad "no delete reported a merge"

for r in 101 104 109 1002 1003 1004 1005 1006; do
    out="$(del "$r")"
    check_json "delete $r" "$out" "sys.exit(0 if d['success'] else 1)"
done
check_json "tree is empty" "$(curl -s $BASE/api/stats)" "sys.exit(0 if d['stats']['totalStudents']==0 and d['stats']['btreeHeight']==1 else 1)"
check_json "empty tree JSON is still valid" "$(curl -s $BASE/api/btree)" "sys.exit(0 if d['tree']['keys']==[] else 1)"

# ---------------------------------------------------------------- validation
echo
echo "[7] validation and error handling"
check_json "duplicate register number -> 409" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":500,"name":"A","department":"CSE","cgpa":8}' >/dev/null; curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":500,"name":"B","department":"CSE","cgpa":8}')" \
    "sys.exit(0 if d['success']==False and 'Duplicate' in d['error'] else 1)"
check_json "CGPA above 10 rejected" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":501,"name":"A","department":"CSE","cgpa":11}')" \
    "sys.exit(0 if d['success']==False and 'CGPA' in d['error'] else 1)"
check_json "CGPA below 0 rejected" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":502,"name":"A","department":"CSE","cgpa":-1}')" \
    "sys.exit(0 if d['success']==False and 'CGPA' in d['error'] else 1)"
check_json "empty name rejected" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":503,"name":"","department":"CSE","cgpa":8}')" \
    "sys.exit(0 if d['success']==False and 'Name' in d['error'] else 1)"
check_json "empty department rejected" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":504,"name":"A","department":"","cgpa":8}')" \
    "sys.exit(0 if d['success']==False and 'Department' in d['error'] else 1)"
check_json "non numeric register number rejected" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{"regNo":"12ab","name":"A","department":"CSE","cgpa":8}')" \
    "sys.exit(0 if d['success']==False and 'numeric' in d['error'] else 1)"
check_json "malformed JSON does not crash the server" \
    "$(curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' -d '{regNo: 5')" \
    "sys.exit(0 if d['success']==False else 1)"
check_json "empty body rejected" "$(curl -s -X POST "$BASE/api/student")" "sys.exit(0 if d['success']==False else 1)"
check_json "unknown endpoint returns 404 JSON" "$(curl -s $BASE/api/does-not-exist)" "sys.exit(0 if d['success']==False else 1)"
check_json "path traversal is blocked" "$(curl -s --path-as-is "$BASE/../backend/main.c")" "sys.exit(0 if d['success']==False else 1)"
check_json "server still alive after all the bad requests" "$(curl -s $BASE/api/health)" "sys.exit(0 if d['success'] else 1)"

# ---------------------------------------------------------------- persistence
echo
echo "[8] persistence across a restart"
curl -s -X POST "$BASE/api/student" -H 'Content-Type: application/json' \
    -d '{"regNo":777,"name":"Persist Test","department":"CSE","cgpa":8.8}' >/dev/null
stop_server
start_server
check_json "record 777 survived the restart" "$(curl -s "$BASE/api/student?regNo=777")" \
    "sys.exit(0 if d['student']['name']=='Persist Test' else 1)"
check_json "students.dat file exists and is non empty" "$(wc -c < "$DATA" | tr -d ' ')" "sys.exit(0 if int(d)>0 else 1)"
check_json "traversal is still sorted" "$(curl -s $BASE/api/students)" \
    "sys.exit(0 if [s['regNo'] for s in d['students']]==sorted(s['regNo'] for s in d['students']) else 1)"

# ---------------------------------------------------------------- frontend
echo
echo "[9] frontend files"
for p in / /index.html /students.html /btree.html /about.html /css/style.css /js/app.js /js/students.js /js/btree.js; do
    c="$(curl -s -o /dev/null -w '%{http_code}' "$BASE$p")"
    [ "$c" = "200" ] && ok "GET $p -> 200" || bad "GET $p -> $c"
done

stop_server
rm -f "$DATA" "/tmp/srms_pid_$$" "/tmp/srms_server_$$.log"

echo
echo "============================================================"
printf " result: \033[32m%d passed\033[0m, \033[31m%d failed\033[0m\n" "$PASS" "$FAIL"
echo "============================================================"
[ "$FAIL" -eq 0 ]
