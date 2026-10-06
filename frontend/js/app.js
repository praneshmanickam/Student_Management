/* ==================================================================
   app.js - shared helpers for every page
   ------------------------------------------------------------------
   * tiny fetch() wrapper around the C backend REST API
   * toast notifications
   * sidebar navigation
   * B-Tree renderer (SVG edges + HTML nodes) - the tree data always
     comes from GET /api/btree, i.e. from the real C B-Tree
   * "Operation Visualization" panel (steps produced by the C backend)
   ================================================================== */

(function (global) {
    'use strict';

    /* ------------------------------------------------------------ */
    /* Shared state                                                   */
    /* ------------------------------------------------------------ */
    const state = {
        tree: null,          /* last known B-Tree structure (from the C backend) */
        stats: null,
        lastOperation: null, /* { type, key, steps, before, after } */
        online: false
    };

    /* ------------------------------------------------------------ */
    /* API helper                                                     */
    /* ------------------------------------------------------------ */
    async function api(method, path, body) {
        const opts = { method: method, headers: {} };
        if (body !== undefined && body !== null) {
            opts.headers['Content-Type'] = 'application/json';
            opts.body = JSON.stringify(body);
        }
        let res;
        try {
            res = await fetch(path, opts);
        } catch (e) {
            throw new Error('Cannot reach the C backend. Is student_server running?');
        }
        const text = await res.text();
        let data = {};
        try {
            data = text ? JSON.parse(text) : {};
        } catch (e) {
            data = { success: false, error: 'The server returned an invalid response.' };
        }
        if (!res.ok || data.success === false) {
            const err = new Error(data.error || data.message || ('Request failed (HTTP ' + res.status + ')'));
            err.status = res.status;
            err.data = data;
            throw err;
        }
        return data;
    }

    /* ------------------------------------------------------------ */
    /* Toast notifications                                            */
    /* ------------------------------------------------------------ */
    function toast(title, message, type, ms) {
        type = type || 'info';
        const wrap = document.querySelector('.toast-wrap');
        if (!wrap) { alert(title + (message ? '\n' + message : '')); return; }

        const el = document.createElement('div');
        el.className = 'toast ' + type;
        const icon = type === 'success' ? '✓' : (type === 'error' ? '!' : 'i');
        el.innerHTML =
            '<div class="t-icon">' + icon + '</div>' +
            '<div class="t-body"><div class="t-title"></div>' +
            (message ? '<div class="t-msg"></div>' : '') + '</div>';
        el.querySelector('.t-title').textContent = title;
        if (message) { el.querySelector('.t-msg').textContent = message; }
        wrap.appendChild(el);

        setTimeout(function () {
            el.classList.add('hide');
            setTimeout(function () { el.remove(); }, 260);
        }, ms || 4200);
    }

    /* ------------------------------------------------------------ */
    /* Small utilities                                                */
    /* ------------------------------------------------------------ */
    function el(tag, cls, text) {
        const n = document.createElement(tag);
        if (cls) { n.className = cls; }
        if (text !== undefined && text !== null) { n.textContent = text; }
        return n;
    }

    function esc(s) {
        return String(s === undefined || s === null ? '' : s)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    }

    function fmtCgpa(v) {
        const n = Number(v);
        return isFinite(n) ? n.toFixed(2) : '0.00';
    }

    /* ------------------------------------------------------------ */
    /* Sidebar / navigation                                           */
    /* ------------------------------------------------------------ */
    function initNav() {
        const sidebar = document.querySelector('.sidebar');
        const scrim = document.querySelector('.scrim');
        const burger = document.querySelector('.hamburger');
        if (burger && sidebar) {
            burger.addEventListener('click', function () {
                sidebar.classList.toggle('open');
                if (scrim) { scrim.classList.toggle('show'); }
            });
        }
        if (scrim) {
            scrim.addEventListener('click', function () {
                if (sidebar) { sidebar.classList.remove('open'); }
                scrim.classList.remove('show');
            });
        }

        /* mark the current page in the sidebar */
        const page = (location.pathname.split('/').pop() || 'index.html').toLowerCase();
        document.querySelectorAll('.nav a[data-page]').forEach(function (a) {
            const target = a.getAttribute('data-page').toLowerCase();
            if (target === page) { a.classList.add('active'); }
        });

        const year = document.querySelector('[data-year]');
        if (year) { year.textContent = String(new Date().getFullYear()); }
    }

    /* ------------------------------------------------------------ */
    /* B-Tree layout + rendering                                      */
    /* ------------------------------------------------------------ */
    const LAYOUT = { KEY_W: 46, NODE_H: 40, GAP: 26, LEVEL_GAP: 88 };

    function nodeWidth(node) {
        const keys = (node && node.keys) ? node.keys.length : 0;
        return Math.max(keys * LAYOUT.KEY_W + 8, LAYOUT.KEY_W + 8);
    }

    function isLeafNode(node) {
        return !node || !node.children || node.children.length === 0;
    }

    /* measure(): returns the horizontal space a subtree needs */
    function measure(node) {
        if (!node) { return 0; }
        const w = nodeWidth(node);
        if (isLeafNode(node)) {
            node._w = w;
            return w;
        }
        let total = 0;
        const widths = [];
        for (const c of node.children) {
            const cw = measure(c);
            widths.push(cw);
            total += cw;
        }
        total += LAYOUT.GAP * (widths.length - 1);
        node._w = Math.max(w, total);
        return node._w;
    }

    /* place(): assign an x centre to every node, children packed left to right */
    function place(node, left, level, out) {
        if (!node) { return; }
        node._level = level;
        out.push(node);

        if (isLeafNode(node)) {
            node._cx = left + node._w / 2;
            return;
        }

        /* children are laid out contiguously, centred under the node box */
        let total = 0;
        const widths = [];
        for (const c of node.children) {
            widths.push(c._w);
            total += c._w;
        }
        total += LAYOUT.GAP * (widths.length - 1);

        let x = left + Math.max(0, (node._w - total) / 2);
        const centres = [];
        for (let i = 0; i < node.children.length; i++) {
            place(node.children[i], x, level + 1, out);
            centres.push(node.children[i]._cx);
            x += widths[i] + LAYOUT.GAP;
        }

        /* the parent sits in the middle of its own children, which keeps the
           connectors symmetric no matter how wide the children are */
        node._cx = (centres[0] + centres[centres.length - 1]) / 2;
    }

    function buildLayout(root) {
        const nodes = [];
        const clone = JSON.parse(JSON.stringify(root || {}));
        measure(clone);
        place(clone, 0, 0, nodes);

        const edges = [];
        for (const n of nodes) {
            if (isLeafNode(n)) { continue; }
            const py = n._level * LAYOUT.LEVEL_GAP + LAYOUT.NODE_H;
            for (const c of n.children) {
                if (!c) { continue; }
                edges.push({
                    x1: n._cx, y1: py,
                    x2: c._cx, y2: c._level * LAYOUT.LEVEL_GAP
                });
            }
        }

        let maxLevel = 0;
        for (const n of nodes) { if (n._level > maxLevel) { maxLevel = n._level; } }

        return {
            nodes: nodes,
            edges: edges,
            width: Math.max(clone._w, 120),
            height: (maxLevel + 1) * LAYOUT.LEVEL_GAP + LAYOUT.NODE_H + 10,
            maxLevel: maxLevel
        };
    }

    /**
     * Render a B-Tree (JSON from /api/btree) into a container.
     * options: { scale, highlightKey, mini }
     */
    function renderTree(container, tree, options) {
        options = options || {};
        if (!container) { return null; }
        container.innerHTML = '';

        if (!tree || !Array.isArray(tree.keys)) {
            container.appendChild(el('div', 'empty-state',
                'The B-Tree is empty. Add a student to build the tree.'));
            return null;
        }

        const layout = buildLayout(tree);
        const pad = 24;
        const width = layout.width + pad * 2;
        const height = layout.height + 8;

        const fit = el('div');
        fit.style.width = width + 'px';
        fit.style.height = height + 'px';
        fit.style.position = 'relative';

        const canvas = el('div', 'tree-canvas');
        canvas.style.width = width + 'px';
        canvas.style.height = height + 'px';

        /* ---- edges ---- */
        const NS = 'http://www.w3.org/2000/svg';
        const svg = document.createElementNS(NS, 'svg');
        svg.setAttribute('class', 'tree-edges');
        svg.setAttribute('width', String(width));
        svg.setAttribute('height', String(height));
        svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
        for (const e of layout.edges) {
            const p = document.createElementNS(NS, 'path');
            const midY = (e.y1 + e.y2) / 2;
            p.setAttribute('d', 'M ' + (e.x1 + pad) + ' ' + e.y1 +
                ' C ' + (e.x1 + pad) + ' ' + midY + ', ' + (e.x2 + pad) + ' ' + midY +
                ', ' + (e.x2 + pad) + ' ' + e.y2);
            svg.appendChild(p);
        }
        canvas.appendChild(svg);

        /* ---- nodes ---- */
        for (const n of layout.nodes) {
            const w = nodeWidth(n);
            const box = el('div', 'bt-node' +
                (n._level === 0 ? ' is-root' : '') +
                (isLeafNode(n) ? ' is-leaf' : ''));
            box.style.left = (n._cx - w / 2 + pad) + 'px';
            box.style.top = (n._level * LAYOUT.LEVEL_GAP) + 'px';
            box.style.width = w + 'px';
            box.style.height = LAYOUT.NODE_H + 'px';
            box.title = 'Level ' + n._level + ' · ' + n.keys.length + ' key(s)' +
                (isLeafNode(n) ? ' · leaf' : ' · internal');

            for (let i = 0; i < n.keys.length; i++) {
                const k = el('div', 'key', String(n.keys[i]));
                k.style.width = LAYOUT.KEY_W + 'px';
                if (options.highlightKey !== undefined &&
                    Number(n.keys[i]) === Number(options.highlightKey)) {
                    k.style.background = 'linear-gradient(180deg,#fff7ed,#ffedd5)';
                    k.style.color = '#9a3412';
                }
                box.appendChild(k);
            }

            if (options.highlightKey !== undefined &&
                n.keys.some(function (k) { return Number(k) === Number(options.highlightKey); })) {
                box.classList.add('pulse');
            }
            canvas.appendChild(box);
        }

        fit.appendChild(canvas);
        container.appendChild(fit);

        if (options.scale && options.scale !== 1) {
            canvas.style.transform = 'scale(' + options.scale + ')';
            canvas.style.transformOrigin = 'top left';
            fit.style.width = (width * options.scale) + 'px';
            fit.style.height = (height * options.scale) + 'px';
        }
        return layout;
    }

    /** Render "Level 0: [109]" style listing of the real tree. */
    function renderLevelList(container, tree) {
        if (!container) { return; }
        container.innerHTML = '';
        if (!tree || !Array.isArray(tree.keys)) {
            container.appendChild(el('div', 'op-empty', 'No levels to show yet.'));
            return;
        }
        const rows = [];
        (function walk(node, level) {
            if (!node) { return; }
            rows.push({ level: level, keys: node.keys || [], leaf: isLeafNode(node), root: level === 0 });
            if (node.children) {
                for (const c of node.children) { walk(c, level + 1); }
            }
        })(tree, 0);

        for (const r of rows) {
            const row = el('div', 'level-row');
            row.appendChild(el('div', 'lvl', 'Level ' + r.level));
            const wrap = el('div', 'node-keys');
            for (const k of r.keys) {
                wrap.appendChild(el('span', 'k' + (r.root ? ' root' : (r.leaf ? ' leaf' : '')), String(k)));
            }
            row.appendChild(wrap);
            container.appendChild(row);
        }
    }

    /* ------------------------------------------------------------ */
    /* Operation visualization                                        */
    /* ------------------------------------------------------------ */
    function stepClass(step) {
        const s = String(step).toLowerCase();
        if (s.indexOf('complete') >= 0 || s.indexOf('insert key') >= 0 && s.indexOf('leaf') >= 0) {
            return 'result';
        }
        if (s.indexOf('split') >= 0) { return 'split'; }
        if (s.indexOf('merge') >= 0 || s.indexOf('borrow') >= 0) { return 'merge'; }
        return '';
    }

    function renderOperation(container, op) {
        if (!container) { return; }
        container.innerHTML = '';
        if (!op || !op.steps || !op.steps.length) {
            container.appendChild(el('div', 'op-empty',
                'Add or delete a student and the B-Tree operations performed by the C backend will appear here.'));
            return;
        }

        const head = el('div', 'op-title');
        const badge = el('span', 'badge-op ' + (op.type || 'insert'), String(op.type || 'insert').toUpperCase());
        head.appendChild(badge);
        head.appendChild(el('span', null,
            (op.type === 'delete' ? 'Delete Key ' : (op.type === 'update' ? 'Update Record ' : 'Insert Key ')) +
            (op.key !== undefined && op.key !== null ? op.key : '')));
        container.appendChild(head);

        const flow = el('div', 'op-flow');
        op.steps.forEach(function (step, i) {
            const s = el('div', 'op-step ' + stepClass(step), step);
            flow.appendChild(s);
            if (i < op.steps.length - 1) {
                flow.appendChild(el('div', 'op-arrow', '↓'));
            }
        });
        container.appendChild(flow);
    }

    function renderOperationCompare(container, op) {
        if (!container) { return; }
        container.innerHTML = '';
        const grid = el('div', 'op-compare');

        const before = el('div', 'op-box');
        before.appendChild(el('h4', null, 'Before'));
        const beforeStage = el('div', 'tree-stage');
        beforeStage.style.minHeight = '150px';
        beforeStage.style.padding = '10px';
        before.appendChild(beforeStage);
        grid.appendChild(before);

        const after = el('div', 'op-box');
        after.appendChild(el('h4', null, 'After'));
        const afterStage = el('div', 'tree-stage');
        afterStage.style.minHeight = '150px';
        afterStage.style.padding = '10px';
        after.appendChild(afterStage);
        grid.appendChild(after);

        container.appendChild(grid);

        const drawMini = function (stage, tree) {
            if (!tree) {
                stage.appendChild(el('div', 'op-empty', '—'));
                return;
            }
            const layout = buildLayout(tree);
            const avail = Math.max(stage.clientWidth - 20, 200);
            const scale = Math.min(0.72, avail / layout.width);
            renderTree(stage, tree, { scale: scale });
        };
        drawMini(beforeStage, op && op.before);
        drawMini(afterStage, op && op.after);
    }

    /** Record an operation performed by the C backend and show it. */
    function recordOperation(op) {
        state.lastOperation = op;
        const panel = document.getElementById('operation-flow');
        const compare = document.getElementById('operation-compare');
        if (panel) { renderOperation(panel, op); }
        if (compare) { renderOperationCompare(compare, op); }
    }

    /* ------------------------------------------------------------ */
    /* Stats                                                          */
    /* ------------------------------------------------------------ */
    function setText(id, value) {
        const n = document.getElementById(id);
        if (n) { n.textContent = value; }
    }

    async function refreshStats() {
        try {
            const data = await api('GET', 'api/stats');
            state.stats = data.stats || data;
            const s = state.stats;
            setText('stat-total', s.totalStudents);
            setText('stat-height', s.btreeHeight);
            setText('stat-nodes', s.totalNodes);
            setText('stat-maxkeys', s.maxKeysInNode);
            setText('stat-mindegree', s.minDegree);
            setText('stat-maxkeys-cap', s.maxKeys);
            setText('stat-storage', s.storageFile || 'students.dat');
            return state.stats;
        } catch (e) {
            return null;
        }
    }

    /* ------------------------------------------------------------ */
    /* Tree + backend status                                          */
    /* ------------------------------------------------------------ */
    async function refreshTree(options) {
        try {
            const data = await api('GET', 'api/btree');
            state.tree = data.tree;
            if (options && options.render !== false) {
                renderTree(document.getElementById('tree-stage'), state.tree,
                    { highlightKey: options.highlightKey });
                renderLevelList(document.getElementById('level-list'), state.tree);
            }
            return state.tree;
        } catch (e) {
            return null;
        }
    }

    function setBackendStatus(online) {
        state.online = !!online;
        document.querySelectorAll('.status-pill').forEach(function (p) {
            p.classList.toggle('offline', !online);
            const label = p.querySelector('[data-status-label]');
            if (label) { label.textContent = online ? 'C backend online' : 'C backend offline'; }
        });
    }

    async function pingBackend() {
        try {
            await api('GET', 'api/health');
            setBackendStatus(true);
        } catch (e) {
            setBackendStatus(false);
        }
    }

    /* ------------------------------------------------------------ */
    /* Export                                                         */
    /* ------------------------------------------------------------ */
    global.App = {
        state: state,
        api: api,
        toast: toast,
        el: el,
        esc: esc,
        fmtCgpa: fmtCgpa,
        initNav: initNav,
        buildLayout: buildLayout,
        renderTree: renderTree,
        renderLevelList: renderLevelList,
        renderOperation: renderOperation,
        renderOperationCompare: renderOperationCompare,
        recordOperation: recordOperation,
        refreshStats: refreshStats,
        refreshTree: refreshTree,
        pingBackend: pingBackend,
        setBackendStatus: setBackendStatus,
        isLeafNode: isLeafNode
    };

    document.addEventListener('DOMContentLoaded', function () {
        initNav();
        pingBackend();
    });
})(window);
