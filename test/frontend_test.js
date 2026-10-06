/*
 * test/frontend_test.js
 * ---------------------
 * Head-less test of the frontend B-Tree renderer (no browser needed).
 *
 * It loads frontend/js/app.js in a VM with a tiny DOM shim and then checks:
 *   1. the layout algorithm never overlaps two nodes on the same level
 *   2. every parent sits inside the span of its own children
 *   3. renderTree() really produces one box per node with the right keys
 *   4. the "Level n: [..]" listing matches the tree
 *   5. the operation panel renders the steps reported by the C backend
 *   6. escaping / formatting helpers
 *
 * Run:  node test/frontend_test.js
 */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');

/* ------------------------------------------------------------------ */
/* Minimal DOM shim                                                    */
/* ------------------------------------------------------------------ */
class FakeEl {
    constructor(tag) {
        this.tagName = String(tag).toUpperCase();
        this.children = [];
        this.style = {};
        this.dataset = {};
        this.className = '';
        this._text = '';
        this._html = '';
        this.attrs = {};
        const self = this;
        this.classList = {
            add: function (c) {
                if (self.className.split(' ').indexOf(c) < 0) {
                    self.className = (self.className + ' ' + c).trim();
                }
            },
            remove: function () {},
            toggle: function () {},
            contains: function () { return false; }
        };
    }
    appendChild(c) { this.children.push(c); return c; }
    setAttribute(k, v) { this.attrs[k] = String(v); }
    getAttribute(k) { return this.attrs[k]; }
    get textContent() { return this._text; }
    set textContent(v) { this._text = String(v); this.children = []; }
    get innerHTML() { return this._html; }
    set innerHTML(v) { this._html = String(v); this.children = []; }
    querySelector() { return null; }
    querySelectorAll() { return []; }
    addEventListener() {}
    removeEventListener() {}
    remove() {}
    get clientWidth() { return 900; }
}

const documentShim = {
    createElement: function (t) { return new FakeEl(t); },
    createElementNS: function (ns, t) { return new FakeEl(t); },
    addEventListener: function () {},
    querySelector: function () { return null; },
    querySelectorAll: function () { return []; },
    getElementById: function () { return null; },
    hidden: false
};

const sandbox = {
    window: {},
    document: documentShim,
    console: console,
    setTimeout: function () { return 0; },
    clearTimeout: function () {},
    setInterval: function () { return 0; },
    fetch: function () { return Promise.reject(new Error('no network in test')); },
    location: { pathname: '/index.html' },
    JSON: JSON,
    Math: Math,
    Number: Number,
    String: String,
    isFinite: isFinite
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(ROOT, 'frontend/js/app.js'), 'utf8'), sandbox,
    { filename: 'app.js' });

const App = sandbox.window.App;

/* ------------------------------------------------------------------ */
/* Test helpers                                                        */
/* ------------------------------------------------------------------ */
let failures = 0;
function check(cond, msg) {
    if (!cond) {
        console.log('  FAIL: ' + msg);
        failures++;
    }
}

/* Rebuild the same B-Tree the C backend builds for a key list, so the
   renderer can be fed a realistic (and large) structure. */
function buildTreeJson(keys) {
    const T = 3, MAX = 2 * T - 1;
    let root = { keys: [], children: null };

    function split(node) {
        const mid = Math.floor(node.keys.length / 2);
        const upKey = node.keys[mid];
        const right = { keys: node.keys.slice(mid + 1), children: null };
        if (node.children) {
            right.children = node.children.slice(mid + 1);
            node.children = node.children.slice(0, mid + 1);
        }
        node.keys = node.keys.slice(0, mid);
        return { upKey: upKey, right: right };
    }

    function insert(node, key) {
        if (!node.children) {
            node.keys.push(key);
            node.keys.sort(function (a, b) { return a - b; });
            if (node.keys.length > MAX) { return split(node); }
            return null;
        }
        let i = 0;
        while (i < node.keys.length && key > node.keys[i]) { i++; }
        const res = insert(node.children[i], key);
        if (res) {
            node.keys.splice(i, 0, res.upKey);
            node.children.splice(i + 1, 0, res.right);
            if (node.keys.length > MAX) { return split(node); }
        }
        return null;
    }

    for (const k of keys) {
        const res = insert(root, k);
        if (res) {
            root = { keys: [res.upKey], children: [root, res.right] };
        }
    }
    return root;
}

function toJson(node, level) {
    const out = { level: level, keys: node.keys.slice() };
    if (node.children) {
        out.children = node.children.map(function (c) { return toJson(c, level + 1); });
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* 1. layout of the sample tree                                        */
/* ------------------------------------------------------------------ */
console.log('[1] layout of the sample B-Tree');

const sample = {
    level: 0, keys: [109],
    children: [
        { level: 1, keys: [101, 104, 106] },
        { level: 1, keys: [1001, 1002] }
    ]
};

const layout = App.buildLayout(sample);
check(layout.nodes.length === 3, 'expected 3 nodes, got ' + layout.nodes.length);
check(layout.edges.length === 2, 'expected 2 edges, got ' + layout.edges.length);
check(layout.maxLevel === 1, 'expected max level 1, got ' + layout.maxLevel);
check(layout.nodes[0]._level === 0 && layout.nodes[0].keys[0] === 109,
    'the root must be key 109 at level 0');
check(layout.width > 0 && layout.height > 0, 'the layout must have a size');

const root = layout.nodes.find(function (n) { return n._level === 0; });
const kids = layout.nodes.filter(function (n) { return n._level === 1; });
const mid = (kids[0]._cx + kids[1]._cx) / 2;
check(Math.abs(root._cx - mid) < 0.001,
    'the root is not centred over its children (' + root._cx + ' vs ' + mid + ')');
check(kids[0]._cx < kids[1]._cx, 'the left child must be left of the right child');

/* ------------------------------------------------------------------ */
/* 2. no overlaps on any level, for a large tree                       */
/* ------------------------------------------------------------------ */
console.log('[2] no overlapping nodes on a 200 key tree');

const big = toJson(buildTreeJson(Array.from({ length: 200 }, function (_, i) { return i * 3 + 1; })), 0);
const bigLayout = App.buildLayout(big);

const byLevel = {};
for (const n of bigLayout.nodes) {
    (byLevel[n._level] = byLevel[n._level] || []).push(n);
}
let levelsChecked = 0;
for (const lvl of Object.keys(byLevel)) {
    const list = byLevel[lvl].slice().sort(function (a, b) { return a._cx - b._cx; });
    for (let i = 1; i < list.length; i++) {
        const prev = list[i - 1];
        const cur = list[i];
        const prevRight = prev._cx + (prev.keys.length * 46 + 8) / 2;
        const curLeft = cur._cx - (cur.keys.length * 46 + 8) / 2;
        check(curLeft >= prevRight - 0.001,
            'level ' + lvl + ': nodes overlap (' + prevRight + ' > ' + curLeft + ')');
    }
    levelsChecked++;
}
check(levelsChecked >= 3, 'expected at least 3 levels, got ' + levelsChecked);

for (const n of bigLayout.nodes) {
    if (!n.children || n.children.length === 0) { continue; }
    const centres = n.children.map(function (c) { return c._cx; });
    const lo = Math.min.apply(null, centres);
    const hi = Math.max.apply(null, centres);
    check(n._cx >= lo - 0.001 && n._cx <= hi + 0.001,
        'a parent is not inside the span of its children');
}

/* ------------------------------------------------------------------ */
/* 3. renderTree() produces one box per node                           */
/* ------------------------------------------------------------------ */
console.log('[3] renderTree() output');

const stage = new FakeEl('div');
App.renderTree(stage, sample, {});
const nodeBoxes = [];
(function walk(el) {
    for (const c of el.children) {
        if (c.className && c.className.indexOf('bt-node') >= 0) { nodeBoxes.push(c); }
        walk(c);
    }
})(stage);

check(nodeBoxes.length === 3, 'expected 3 rendered node boxes, got ' + nodeBoxes.length);
const allKeys = [];
for (const box of nodeBoxes) {
    for (const k of box.children) {
        if (k.className === 'key') { allKeys.push(Number(k.textContent)); }
    }
}
check(allKeys.length === 6, 'expected 6 rendered keys, got ' + allKeys.length);
check(allKeys.indexOf(109) >= 0 && allKeys.indexOf(106) >= 0,
    'the rendered keys do not contain 109 and 106');

check(nodeBoxes.some(function (b) { return b.className.indexOf('is-root') >= 0; }),
    'no node was marked as the root');
check(nodeBoxes.some(function (b) { return b.className.indexOf('is-leaf') >= 0; }),
    'no node was marked as a leaf');

const emptyStage = new FakeEl('div');
App.renderTree(emptyStage, null, {});
check(emptyStage.children.length === 1 && /empty/.test(emptyStage.children[0].className),
    'an empty tree must render the empty state');

/* ------------------------------------------------------------------ */
/* 4. level listing                                                    */
/* ------------------------------------------------------------------ */
console.log('[4] level listing');

const levelBox = new FakeEl('div');
App.renderLevelList(levelBox, sample);
check(levelBox.children.length === 3, 'expected 3 level rows, got ' + levelBox.children.length);
const levelText = levelBox.children.map(function (r) { return r.children[0].textContent; }).join(',');
check(/Level 0/.test(levelText) && /Level 1/.test(levelText), 'level rows are wrong: ' + levelText);
const firstRowKeys = levelBox.children[0].children[1].children.map(function (k) { return k.textContent; }).join(' ');
check(firstRowKeys === '109', 'level 0 should list [109], got [' + firstRowKeys + ']');

/* ------------------------------------------------------------------ */
/* 5. operation panel                                                  */
/* ------------------------------------------------------------------ */
console.log('[5] operation visualization');

const opBox = new FakeEl('div');
App.renderOperation(opBox, {
    type: 'insert',
    key: 105,
    steps: [
        'Insert key 105',
        'Node full (6 keys) -> split node, promote middle key 109',
        'Insert key 105 into leaf node at level 1'
    ]
});
check(opBox.children.length === 2, 'the operation panel should have a title and a flow');
const steps = opBox.children[1].children.filter(function (c) {
    return c.className.indexOf('op-step') >= 0;
});
check(steps.length === 3, 'expected 3 steps, got ' + steps.length);
check(steps[1].className.indexOf('split') >= 0, 'the split step should be highlighted as a split');
check(opBox.children[1].children[1].textContent.indexOf('↓') >= 0,
    'steps must be joined by arrows');

App.renderOperation(new FakeEl('div'), null);

/* ------------------------------------------------------------------ */
/* 6. helpers                                                          */
/* ------------------------------------------------------------------ */
console.log('[6] escaping / formatting');
check(App.esc('<script>') === '&lt;script&gt;', 'esc() must escape HTML');
check(App.fmtCgpa(9.8) === '9.80', 'fmtCgpa(9.8) must be 9.80');

/* ------------------------------------------------------------------ */
console.log('');
if (failures === 0) {
    console.log('=== FRONTEND TESTS PASSED ===');
    process.exit(0);
}
console.log('=== ' + failures + ' FRONTEND TEST(S) FAILED ===');
process.exit(1);
