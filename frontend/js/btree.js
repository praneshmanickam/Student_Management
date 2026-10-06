/* ==================================================================
   btree.js
   ------------------------------------------------------------------
   Dedicated B-Tree visualization page.
   The tree is fetched from the C backend (GET /api/btree) and drawn
   here with SVG edges + absolutely positioned HTML nodes.
   ================================================================== */

(function () {
    'use strict';

    const App = window.App;
    const api = App.api;

    let zoom = 1;
    let currentLayout = null;

    /* ------------------------------------------------------------ */
    /* Drawing                                                       */
    /* ------------------------------------------------------------ */
    function draw(highlightKey) {
        const stage = document.getElementById('tree-stage');
        if (!stage) { return; }

        const tree = App.state.tree;
        if (!tree) {
            stage.innerHTML = '<div class="empty-state">The C backend could not be reached.</div>';
            return;
        }

        const layout = App.buildLayout(tree);
        currentLayout = layout;

        const avail = Math.max(stage.clientWidth - 30, 240);
        const fit = Math.min(1, avail / layout.width);

        App.renderTree(stage, tree, {
            scale: zoom * fit,
            highlightKey: highlightKey
        });

        App.renderLevelList(document.getElementById('level-list'), tree);

        const zoomLabel = document.getElementById('zoom-label');
        if (zoomLabel) { zoomLabel.textContent = Math.round(zoom * fit * 100) + '%'; }

        const summary = document.getElementById('tree-summary');
        if (summary && App.state.stats) {
            const s = App.state.stats;
            summary.textContent = s.totalStudents + ' record(s) · height ' + s.btreeHeight +
                ' · ' + s.totalNodes + ' node(s) · largest node holds ' + s.maxKeysInNode + ' key(s)';
        }
    }

    async function load(options) {
        options = options || {};
        await App.refreshStats();
        updateProperties();
        const tree = await App.refreshTree({ render: false });
        if (!tree) {
            App.toast('Could not load the B-Tree',
                'Is the C server running? Start it with student_server.exe', 'error');
        }
        draw(options.highlightKey);
    }

    function updateProperties() {
        const s = App.state.stats;
        if (!s) { return; }
        const set = function (id, v) {
            const n = document.getElementById(id);
            if (n) { n.textContent = v; }
        };
        set('p-mindegree', s.minDegree);
        set('p-maxkeys', s.maxKeys);
        set('p-maxchildren', s.maxChildren);
        set('p-height', s.btreeHeight);
        set('p-nodes', s.totalNodes);
        set('p-total', s.totalStudents);
    }

    /* ------------------------------------------------------------ */
    /* Zoom controls                                                 */
    /* ------------------------------------------------------------ */
    function initZoom() {
        const inc = document.getElementById('zoom-in');
        const dec = document.getElementById('zoom-out');
        const fitBtn = document.getElementById('zoom-fit');

        if (inc) {
            inc.addEventListener('click', function () {
                zoom = Math.min(2.2, zoom + 0.15);
                draw();
            });
        }
        if (dec) {
            dec.addEventListener('click', function () {
                zoom = Math.max(0.4, zoom - 0.15);
                draw();
            });
        }
        if (fitBtn) {
            fitBtn.addEventListener('click', function () {
                zoom = 1;
                draw();
            });
        }

        let resizeTimer = null;
        window.addEventListener('resize', function () {
            clearTimeout(resizeTimer);
            resizeTimer = setTimeout(function () { draw(); }, 180);
        });
    }

    /* ------------------------------------------------------------ */
    /* Boot                                                          */
    /* ------------------------------------------------------------ */
    document.addEventListener('DOMContentLoaded', async function () {
        initZoom();

        const refresh = document.getElementById('btn-refresh-tree');
        if (refresh) {
            refresh.addEventListener('click', function () { load(); });
        }

        await load();

        /* show the last operation recorded by the C backend */
        try {
            const data = await api('GET', 'api/operation');
            if (data.steps && data.steps.length && !App.state.lastOperation) {
                App.recordOperation({
                    type: 'info',
                    key: null,
                    steps: data.steps,
                    before: App.state.tree,
                    after: App.state.tree
                });
            }
        } catch (e) { /* ignore */ }

        /* keep the visualization alive: poll the backend every 6 s so changes
           made in another tab / browser window show up automatically */
        setInterval(async function () {
            if (document.hidden) { return; }
            const tree = await App.refreshTree({ render: false });
            if (tree) {
                const before = JSON.stringify(App.state.tree);
                App.state.tree = tree;
                draw();
                if (before !== JSON.stringify(tree)) {
                    await App.refreshStats();
                    updateProperties();
                }
            }
        }, 6000);
    });
})();
