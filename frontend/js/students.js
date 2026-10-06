/* ==================================================================
   students.js
   ------------------------------------------------------------------
   Dashboard page  : Add Student form + Search Student form
   Students page   : records table, edit modal, delete modal

   Every action calls the C backend; the frontend never keeps its own
   copy of the data and never implements its own B-Tree.
   ================================================================== */

(function () {
    'use strict';

    const App = window.App;
    const api = App.api;

    /* ------------------------------------------------------------ */
    /* Validation - mirrors the checks performed in C (student.c)    */
    /* ------------------------------------------------------------ */
    const RE_REGNO = /^\d{1,10}$/;
    const RE_CGPA = /^\d+(\.\d+)?$/;

    function setFieldError(fieldId, message) {
        const f = document.getElementById(fieldId);
        if (!f) { return; }
        f.classList.add('invalid');
        const err = f.querySelector('.err');
        if (err) { err.textContent = message; }
    }

    function clearFieldErrors(scope) {
        (scope || document).querySelectorAll('.field.invalid').forEach(function (f) {
            f.classList.remove('invalid');
            const err = f.querySelector('.err');
            if (err) { err.textContent = ''; }
        });
    }

    function validateRegNo(v) {
        if (!v) { return 'Register Number cannot be empty.'; }
        if (!RE_REGNO.test(v)) { return 'Register Number must be numeric.'; }
        return null;
    }
    function validateName(v) {
        if (!v || !v.trim()) { return 'Name cannot be empty.'; }
        if (v.trim().length > 60) { return 'Name is too long (max 60 characters).'; }
        return null;
    }
    function validateDept(v) {
        if (!v || !v.trim()) { return 'Department cannot be empty.'; }
        if (v.trim().length > 60) { return 'Department is too long (max 60 characters).'; }
        return null;
    }
    function validateCgpa(v) {
        const s = String(v === undefined || v === null ? '' : v).trim();
        if (s === '') { return 'CGPA cannot be empty.'; }
        if (!RE_CGPA.test(s)) { return 'CGPA must be a number between 0 and 10.'; }
        const n = Number(s);
        if (!isFinite(n) || n < 0 || n > 10) { return 'CGPA must be between 0 and 10.'; }
        return null;
    }

    /* ------------------------------------------------------------ */
    /* Modals                                                        */
    /* ------------------------------------------------------------ */
    function openModal(id) {
        const m = document.getElementById(id);
        if (m) { m.classList.add('open'); }
    }
    function closeModal(id) {
        const m = document.getElementById(id);
        if (m) { m.classList.remove('open'); }
    }
    function initModals() {
        document.querySelectorAll('.modal-backdrop').forEach(function (backdrop) {
            backdrop.addEventListener('click', function (e) {
                if (e.target === backdrop) { backdrop.classList.remove('open'); }
            });
        });
        document.querySelectorAll('[data-close]').forEach(function (btn) {
            btn.addEventListener('click', function () {
                closeModal(btn.getAttribute('data-close'));
            });
        });
        document.addEventListener('keydown', function (e) {
            if (e.key === 'Escape') {
                document.querySelectorAll('.modal-backdrop.open').forEach(function (m) {
                    m.classList.remove('open');
                });
            }
        });
    }

    /* ------------------------------------------------------------ */
    /* Shared refresh                                                */
    /* ------------------------------------------------------------ */
    async function refreshAll(options) {
        options = options || {};
        await App.refreshStats();
        await App.refreshTree({ highlightKey: options.highlightKey });
    }

    /* ============================================================ */
    /* A) ADD STUDENT  (index.html)                                  */
    /* ============================================================ */
    function initAddForm() {
        const form = document.getElementById('add-form');
        if (!form) { return; }

        const btn = document.getElementById('add-submit');
        const note = document.getElementById('add-note');

        form.addEventListener('submit', async function (e) {
            e.preventDefault();
            clearFieldErrors(form);

            const regNo = document.getElementById('add-reg').value.trim();
            const name = document.getElementById('add-name').value.trim();
            const dept = document.getElementById('add-dept').value.trim();
            const cgpa = document.getElementById('add-cgpa').value.trim();

            let bad = false;
            const eReg = validateRegNo(regNo);
            const eName = validateName(name);
            const eDept = validateDept(dept);
            const eCg = validateCgpa(cgpa);
            if (eReg) { setFieldError('f-reg', eReg); bad = true; }
            if (eName) { setFieldError('f-name', eName); bad = true; }
            if (eDept) { setFieldError('f-dept', eDept); bad = true; }
            if (eCg) { setFieldError('f-cgpa', eCg); bad = true; }

            if (bad) {
                App.toast('Please fix the highlighted fields', 'The student was not sent to the C backend.', 'error');
                return;
            }

            const before = App.state.tree ? JSON.parse(JSON.stringify(App.state.tree)) : null;
            const payload = {
                regNo: Number(regNo),
                name: name,
                department: dept,
                cgpa: Number(cgpa)
            };

            btn.disabled = true;
            const original = btn.innerHTML;
            btn.innerHTML = '<span class="spinner"></span> Inserting…';
            if (note) { note.textContent = 'Calling btree_insert() in C…'; }

            try {
                const data = await api('POST', 'api/student', payload);
                App.toast('Student ' + regNo + ' inserted successfully.',
                    'B-Tree insert completed in the C backend.', 'success');

                App.state.tree = data.tree || App.state.tree;
                App.recordOperation({
                    type: 'insert',
                    key: Number(regNo),
                    steps: data.steps || [],
                    before: before,
                    after: data.tree
                });

                form.reset();
                clearFieldErrors(form);
                await refreshAll({ highlightKey: Number(regNo) });
                if (note) { note.textContent = 'Saved to ' + (data.stats ? '' : '') + 'students.dat'; }
            } catch (err) {
                App.toast('Could not add student', err.message, 'error');
            } finally {
                btn.disabled = false;
                btn.innerHTML = original;
            }
        });

        form.addEventListener('reset', function () {
            clearFieldErrors(form);
            if (note) { note.textContent = ''; }
        });

        const sample = document.getElementById('add-sample');
        if (sample) {
            sample.addEventListener('click', async function () {
                sample.disabled = true;
                try {
                    const data = await api('POST', 'api/seed', {});
                    App.state.tree = data.tree || App.state.tree;
                    App.toast('Sample records loaded', 'The B-Tree was rebuilt with the sample data.', 'success');
                    await refreshAll();
                } catch (err) {
                    App.toast('Could not load sample data', err.message, 'error');
                } finally {
                    sample.disabled = false;
                }
            });
        }
    }

    /* ============================================================ */
    /* C) SEARCH STUDENT  (index.html)                               */
    /* ============================================================ */
    function initSearchForm() {
        const form = document.getElementById('search-form');
        if (!form) { return; }
        const input = document.getElementById('search-reg');
        const out = document.getElementById('search-result');

        form.addEventListener('submit', async function (e) {
            e.preventDefault();
            clearFieldErrors(form);

            const regNo = input.value.trim();
            const err = validateRegNo(regNo);
            if (err) {
                setFieldError('f-search', err);
                App.toast('Invalid Register Number', err, 'error');
                return;
            }

            out.innerHTML = '<div class="op-empty">Searching the B-Tree…</div>';

            try {
                const data = await api('GET', 'api/student?regNo=' + encodeURIComponent(regNo));
                const s = data.student;
                out.innerHTML = '';

                const card = App.el('div', 'result-card found');
                const h = App.el('h3');
                h.appendChild(App.el('span', 'found-tag', 'Student Found'));
                card.appendChild(h);

                const dl = App.el('dl', 'kv');
                const rows = [
                    ['Register Number', String(s.regNo)],
                    ['Name', s.name],
                    ['Department', s.department],
                    ['CGPA', App.fmtCgpa(s.cgpa)]
                ];
                rows.forEach(function (r) {
                    dl.appendChild(App.el('dt', null, r[0]));
                    dl.appendChild(App.el('dd', null, r[1]));
                });
                card.appendChild(dl);

                card.appendChild(App.el('div', 'footer-note',
                    'Found by the B-Tree search routine in the C backend (search()).'));
                out.appendChild(card);

                App.recordOperation({
                    type: 'search',
                    key: Number(regNo),
                    steps: [
                        'Search key ' + regNo,
                        'Compare with the keys of the current node',
                        'Descend into the correct child pointer',
                        'Key ' + regNo + ' found — record returned'
                    ],
                    before: App.state.tree,
                    after: App.state.tree
                });
            } catch (err) {
                out.innerHTML = '';
                const card = App.el('div', 'result-card missing');
                const h = App.el('h3');
                h.appendChild(App.el('span', 'missing-tag', 'Student not found'));
                card.appendChild(h);
                card.appendChild(App.el('p', null,
                    'No student with register number ' + regNo + ' exists in the B-Tree.'));
                card.appendChild(App.el('div', 'footer-note', err.message));
                out.appendChild(card);

                App.recordOperation({
                    type: 'search',
                    key: Number(regNo),
                    steps: [
                        'Search key ' + regNo,
                        'Compare with the keys of the current node',
                        'Descend into the correct child pointer',
                        'Key ' + regNo + ' is not present in the B-Tree'
                    ],
                    before: App.state.tree,
                    after: App.state.tree
                });
            }
        });

        const clear = document.getElementById('search-clear');
        if (clear) {
            clear.addEventListener('click', function () {
                input.value = '';
                clearFieldErrors(form);
                out.innerHTML = '<div class="op-empty">Search for a register number to see the record stored in the B-Tree.</div>';
                input.focus();
            });
        }
    }

    /* ============================================================ */
    /* D) STUDENT RECORDS TABLE  (students.html)                     */
    /* ============================================================ */
    const table = {
        students: [],
        editing: null,
        deleting: null
    };

    function renderTable(list) {
        const tbody = document.getElementById('student-tbody');
        const empty = document.getElementById('table-empty');
        const count = document.getElementById('record-count');
        if (!tbody) { return; }

        tbody.innerHTML = '';
        if (count) { count.textContent = String(list.length); }

        if (!list.length) {
            if (empty) {
                empty.style.display = 'block';
                if (!empty.dataset.defaultHtml) {
                    empty.dataset.defaultHtml = empty.innerHTML;
                }
                empty.innerHTML = empty.dataset.defaultHtml;
            }
            return;
        }
        if (empty) { empty.style.display = 'none'; }

        list.forEach(function (s) {
            const tr = document.createElement('tr');

            const tdReg = document.createElement('td');
            tdReg.className = 'num';
            tdReg.textContent = String(s.regNo);
            tr.appendChild(tdReg);

            const tdName = document.createElement('td');
            tdName.textContent = s.name;
            tr.appendChild(tdName);

            const tdDept = document.createElement('td');
            const tag = App.el('span', 'tag', s.department);
            tdDept.appendChild(tag);
            tr.appendChild(tdDept);

            const tdCgpa = document.createElement('td');
            const bar = App.el('div', 'cgpa-bar');
            const track = App.el('div', 'bar');
            const fill = App.el('span');
            fill.style.width = Math.max(0, Math.min(100, (Number(s.cgpa) / 10) * 100)) + '%';
            track.appendChild(fill);
            bar.appendChild(track);
            bar.appendChild(App.el('span', 'val', App.fmtCgpa(s.cgpa)));
            tdCgpa.appendChild(bar);
            tr.appendChild(tdCgpa);

            const tdAct = document.createElement('td');
            const actions = App.el('div', 'actions');

            const editBtn = App.el('button', 'btn btn-ghost btn-sm', 'Edit');
            editBtn.type = 'button';
            editBtn.addEventListener('click', function () { openEdit(s); });
            actions.appendChild(editBtn);

            const delBtn = App.el('button', 'btn btn-danger btn-sm', 'Delete');
            delBtn.type = 'button';
            delBtn.addEventListener('click', function () { openDelete(s); });
            actions.appendChild(delBtn);

            tdAct.appendChild(actions);
            tr.appendChild(tdAct);
            tbody.appendChild(tr);
        });
    }

    async function loadStudents() {
        const tbody = document.getElementById('student-tbody');
        if (!tbody) { return; }
        try {
            const data = await api('GET', 'api/students');
            table.students = data.students || [];
            renderTable(table.students);
        } catch (err) {
            App.toast('Could not load students', err.message, 'error');
            if (tbody) {
                tbody.innerHTML = '';
            }
        }
    }

    /* ---------------- edit ---------------- */
    function openEdit(student) {
        table.editing = student;
        document.getElementById('edit-name').value = student.name;
        document.getElementById('edit-dept').value = student.department;
        document.getElementById('edit-cgpa').value = App.fmtCgpa(student.cgpa);
        document.querySelector('#edit-key-note strong').textContent = String(student.regNo);
        clearFieldErrors(document.getElementById('edit-form'));
        openModal('edit-modal');
    }

    function initEditModal() {
        const save = document.getElementById('edit-save');
        if (!save) { return; }

        save.addEventListener('click', async function () {
            const form = document.getElementById('edit-form');
            clearFieldErrors(form);

            const student = table.editing;
            if (!student) { return; }

            const name = document.getElementById('edit-name').value.trim();
            const dept = document.getElementById('edit-dept').value.trim();
            const cgpa = document.getElementById('edit-cgpa').value.trim();

            let bad = false;
            const eName = validateName(name);
            const eDept = validateDept(dept);
            const eCg = validateCgpa(cgpa);
            if (eName) { setFieldError('ef-name', eName); bad = true; }
            if (eDept) { setFieldError('ef-dept', eDept); bad = true; }
            if (eCg) { setFieldError('ef-cgpa', eCg); bad = true; }
            if (bad) {
                App.toast('Please fix the highlighted fields', 'The update was not sent to the C backend.', 'error');
                return;
            }

            save.disabled = true;
            const original = save.innerHTML;
            save.innerHTML = '<span class="spinner"></span> Saving…';

            try {
                const data = await api('PUT', 'api/student?regNo=' + encodeURIComponent(student.regNo),
                    { name: name, department: dept, cgpa: Number(cgpa) });

                App.toast('Student ' + student.regNo + ' updated successfully.',
                    'Name, department and CGPA updated in the B-Tree.', 'success');
                closeModal('edit-modal');
                await loadStudents();
                await refreshAll({ highlightKey: student.regNo });

                App.recordOperation({
                    type: 'update',
                    key: student.regNo,
                    steps: data.steps || [],
                    before: App.state.tree,
                    after: App.state.tree
                });
            } catch (err) {
                App.toast('Could not update student', err.message, 'error');
            } finally {
                save.disabled = false;
                save.innerHTML = original;
            }
        });
    }

    /* ---------------- delete ---------------- */
    function openDelete(student) {
        table.deleting = student;
        document.getElementById('delete-reg').textContent = String(student.regNo);
        document.getElementById('delete-name').textContent = student.name;
        document.getElementById('delete-dept').textContent = student.department;
        document.getElementById('delete-cgpa').textContent = App.fmtCgpa(student.cgpa);
        document.getElementById('delete-question').innerHTML =
            'Are you sure you want to delete this student?';
        const strong = document.createElement('strong');
        strong.textContent = ' ' + student.name + ' (' + student.regNo + ')';
        document.getElementById('delete-question').appendChild(strong);
        openModal('delete-modal');
    }

    function initDeleteModal() {
        const confirm = document.getElementById('delete-confirm');
        if (!confirm) { return; }

        confirm.addEventListener('click', async function () {
            const student = table.deleting;
            if (!student) { return; }

            confirm.disabled = true;
            const original = confirm.innerHTML;
            confirm.innerHTML = '<span class="spinner"></span> Deleting…';

            try {
                const before = App.state.tree ? JSON.parse(JSON.stringify(App.state.tree)) : null;
                const data = await api('DELETE', 'api/student?regNo=' + encodeURIComponent(student.regNo));

                App.toast('Student ' + student.regNo + ' deleted successfully.',
                    'Removed from the B-Tree with the C delete algorithm.', 'success');
                closeModal('delete-modal');
                table.deleting = null;

                App.state.tree = data.tree || App.state.tree;
                App.recordOperation({
                    type: 'delete',
                    key: student.regNo,
                    steps: data.steps || [],
                    before: before,
                    after: data.tree
                });

                await loadStudents();
                await refreshAll({ highlightKey: student.regNo });
            } catch (err) {
                App.toast('Could not delete student', err.message, 'error');
            } finally {
                confirm.disabled = false;
                confirm.innerHTML = original;
            }
        });
    }

    /* ---------------- quick search ---------------- */
    function initQuickSearch() {
        const input = document.getElementById('quick-search');
        if (!input) { return; }
        let timer = null;

        input.addEventListener('input', function () {
            clearTimeout(timer);
            timer = setTimeout(async function () {
                const q = input.value.trim();
                if (!q) {
                    renderTable(table.students);
                    return;
                }
                if (!RE_REGNO.test(q)) {
                    renderTable([]);
                    const empty = document.getElementById('table-empty');
                    if (empty) {
                        empty.style.display = 'block';
                        empty.innerHTML = '<strong>Type a numeric Register Number</strong>Only digits are accepted.</div>';
                    }
                    return;
                }
                try {
                    const data = await api('GET', 'api/student?regNo=' + encodeURIComponent(q));
                    renderTable([data.student]);
                    const empty = document.getElementById('table-empty');
                    if (empty) { empty.style.display = 'none'; }
                } catch (err) {
                    renderTable([]);
                    const empty = document.getElementById('table-empty');
                    if (empty) {
                        empty.style.display = 'block';
                        empty.innerHTML = '<strong>No student with Register Number ' + App.esc(q) +
                            '</strong>' + App.esc(err.message);
                    }
                }
            }, 320);
        });
    }

    /* ============================================================ */
    /* Boot                                                          */
    /* ============================================================ */
    document.addEventListener('DOMContentLoaded', async function () {
        initModals();
        initAddForm();
        initSearchForm();
        initEditModal();
        initDeleteModal();
        initQuickSearch();

        const refreshBtn = document.getElementById('btn-refresh');
        if (refreshBtn) {
            refreshBtn.addEventListener('click', function () {
                loadStudents();
                refreshAll();
            });
        }
        const refreshAllBtn = document.getElementById('btn-refresh-all');
        if (refreshAllBtn) {
            refreshAllBtn.addEventListener('click', function () {
                refreshAll();
                App.toast('Refreshed', 'Statistics and the B-Tree were reloaded from the C backend.', 'info', 2200);
            });
        }

        await refreshAll();

        /* dashboard page: also show the last operation stored by the backend */
        if (document.getElementById('operation-flow') && !App.state.lastOperation) {
            try {
                const data = await api('GET', 'api/operation');
                if (data.steps && data.steps.length) {
                    App.recordOperation({
                        type: 'info',
                        key: null,
                        steps: data.steps,
                        before: App.state.tree,
                        after: App.state.tree
                    });
                }
            } catch (e) { /* ignore */ }
        }

        await loadStudents();
    });
})();
