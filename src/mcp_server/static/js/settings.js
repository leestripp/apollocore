// settings.js — Settings tab: change the things you can change.
//
// What's here (every configurable knob the user might want to touch):
//   • Models — pick embed/llm from a filterable list of every *.gguf
//     found in any models_dir. Add or remove models folders; the list
//     updates immediately. Freeform path entry is one click away for
//     power users.
//   • Storage — data_dir is editable. Changing it requires a restart
//     (the SQLite KB is bound at startup), but the new value is
//     persisted to config.json so the next start picks it up.
//   • Diagnostics — verbose logging + default search mode. Both
//     hot-reloadable; no restart needed.
//   • Projects — view project tags (read-only).
//
// What's NOT here:
//   • Read-only diagnostics (host, port, sessions, build info) live on
//     the About tab. Settings = things you can change.
//
// Why expose all of this in the UI (not just via MCP tools)?
//   The user has no idea what's going on if we hide it from them.
//   Even if an agent can change a setting via a tool, the user needs
//   to see and be able to change every configurable knob here.

import { httpJson, httpPost, readResource, callTool, toast } from './api.js';

export async function renderSettings(panel) {
    panel.innerHTML = `
        <h2>Settings</h2>

        <div class="card">
            <h3 style="margin-top: 0;">Models</h3>
            <p class="muted">
                ApolloCore scans every directory in <strong>Models folders</strong>
                for <code>.gguf</code> files. Pick one from the dropdown —
                no paths to type, no Browse dialogs.
            </p>

            <h4 style="margin-top: 14px;">Models folders</h4>
            <div id="settings-models-dirs"></div>

            <h4 style="margin-top: 18px;">Embedding model</h4>
            <div id="cfg-embed"></div>

            <h4 style="margin-top: 18px;">LLM model</h4>
            <div id="cfg-llm"></div>

            <p class="muted" id="settings-config-path" style="margin-top: 12px;"></p>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Storage</h3>
            <p class="muted">
                The SQLite knowledge base lives at <strong>Data directory</strong>.
                Changing it requires a restart because the data dir is bound at
                startup — it controls where chunks, embeddings, and the entity
                graph are persisted.
            </p>
            <table class="kv" id="settings-storage"></table>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Diagnostics</h3>
            <p class="muted">
                Verbose logging and the default search mode used by the Search
                tab when the user doesn't override per-query. Both are
                <strong>hot-reloadable</strong> — no restart required.
            </p>
            <div id="settings-diagnostics"></div>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Projects (deletion units)</h3>
            <p class="muted">
                Every knowledge base shares one global pool of chunks.
                Projects here are <strong>tags for provenance and bulk deletion</strong>
                &mdash; they do not isolate data, they just remember where
                ingested data came from and let you wipe one project at a
                time when you no longer need it. Projects are created
                automatically when you ingest data with a new project name.
            </p>
            <div id="settings-projects"></div>
        </div>
    `;

    await load();

    async function load() {
        try {
            const [config, modelsResp, projectsRaw] = await Promise.all([
                httpJson('/api/config').catch((e) => ({ error: e.message })),
                httpJson('/api/models').catch(() => ({ models: [], dirs: [] })),
                readResource('apollo://projects').catch(() => null),
            ]);
            renderStorage(config);
            renderDiagnostics(config);
            renderModelsDirs(config, modelsResp);
            renderModelPicker('cfg-embed', 'embed_model', config, modelsResp);
            renderModelPicker('cfg-llm',   'llm_model',   config, modelsResp);
            renderProjects(projectsRaw);
        } catch (e) {
            toast(`Settings load failed: ${e.message}`, 'err');
        }
    }

    // ── Storage (data_dir) ───────────────────────────────────────
    // data_dir is editable. Saving persists the new path to config.json
    // and the server replies with data_dir_restart_required=true so
    // the UI can show the right hint.
    function renderStorage(config) {
        const host = document.getElementById('settings-storage');
        const dd = config.data_dir || '';
        host.innerHTML = `
            <tr><td class="key">Data directory</td>
                <td><input id="cfg-data-dir" type="text" class="mono"
                           value="${escapeHtml(dd)}"
                           placeholder="/path/to/data (or ~/.apollocore/data)"
                           style="width: 100%;"></td></tr>
            <tr><td class="key">config.json</td>
                <td class="mono muted">${escapeHtml(config.config_path || '(not yet written)')}</td></tr>
            <tr><td colspan="2">
                <button id="cfg-data-dir-apply" class="primary" style="margin-top: 6px;">Apply (restart required)</button>
                <span class="muted" style="margin-left: 8px;">
                    Holds the SQLite KB and embedding files. Run
                    <code>./scripts/ensure-running.sh</code> after applying.
                </span>
            </td></tr>
        `;
        document.getElementById('cfg-data-dir-apply').addEventListener('click', async () => {
            const newDir = document.getElementById('cfg-data-dir').value.trim();
            if (!newDir) { toast('data_dir cannot be empty.', 'err'); return; }
            const r = await applyConfig({ data_dir: newDir });
            if (r && r.data_dir_restart_required) {
                toast('Saved. Restart the server to take effect.');
            }
        });
        document.getElementById('settings-config-path').textContent =
            config.config_path ? `config.json: ${config.config_path}` : '';
    }

    // ── Diagnostics (verbose logging, default search mode) ───────
    // Both hot-reloadable. Toggles write to /api/config and the
    // server applies them immediately.
    function renderDiagnostics(config) {
        const host = document.getElementById('settings-diagnostics');
        const verbose = !!config.verbose;
        const mode = config.default_search_mode || 'vector';
        host.innerHTML = `
            <table class="kv">
                <tr><td class="key">Verbose logging</td>
                    <td><label><input id="cfg-verbose" type="checkbox" ${verbose ? 'checked' : ''}> Enable debug-level log output</label></td></tr>
                <tr><td class="key">Default search mode</td>
                    <td>
                        <select id="cfg-default-mode">
                            <option value="vector"  ${mode==='vector'  ? 'selected' : ''}>vector — fast embedding only</option>
                            <option value="hybrid"  ${mode==='hybrid'  ? 'selected' : ''}>hybrid — vector + BM25 + RRF</option>
                            <option value="rerank"  ${mode==='rerank'  ? 'selected' : ''}>rerank — hybrid + cross-encoder</option>
                            <option value="full"    ${mode==='full'    ? 'selected' : ''}>full — all of the above + graph</option>
                        </select>
                    </td></tr>
            </table>
            <p class="muted" style="margin-top: 6px;">
                Default search mode is used by the Search tab when the user
                doesn't override per-query. Both settings are saved to
                <code>config.json</code> and survive restarts.
            </p>
        `;
        document.getElementById('cfg-verbose').addEventListener('change', async (e) => {
            await applyConfig({ verbose: e.target.checked });
        });
        document.getElementById('cfg-default-mode').addEventListener('change', async (e) => {
            await applyConfig({ default_search_mode: e.target.value });
        });
    }

    // ── Models folders ────────────────────────────────────────────
    // Each models_dir is one line with a Remove button. An "Add
    // folder…" input below accepts a path; pressing Enter or Add
    // updates the server-side models_dirs and rescans.
    function renderModelsDirs(config, modelsResp) {
        const host = document.getElementById('settings-models-dirs');
        const dirs = (modelsResp.dirs && modelsResp.dirs.length)
                   ? modelsResp.dirs
                   : (config.models_dir || []);
        const count = (modelsResp.models || []).length;
        host.innerHTML = `
            <div id="models-dirs-list">
                ${dirs.map((d, i) => `
                    <div class="row" style="padding: 4px 0;">
                        <span class="mono" style="flex: 1;">${escapeHtml(d)}</span>
                        <button data-rm-dir="${i}" type="button">Remove</button>
                    </div>
                `).join('')}
            </div>
            <div class="row" style="margin-top: 6px;">
                <input id="add-dir-input" type="text" class="mono"
                       placeholder="/path/to/models (press Enter to add)"
                       style="flex: 1;">
                <button id="add-dir-btn" type="button">Add</button>
            </div>
            <p class="muted" style="margin-top: 6px;">
                ${count} ${count === 1 ? 'model' : 'models'} found across these folders.
            </p>
        `;
        host.querySelectorAll('[data-rm-dir]').forEach(btn => {
            btn.addEventListener('click', async () => {
                const idx = parseInt(btn.dataset.rmDir, 10);
                const next = dirs.filter((_, i) => i !== idx);
                await applyConfig({ models_dir: next });
            });
        });
        const addBtn = document.getElementById('add-dir-btn');
        const addInput = document.getElementById('add-dir-input');
        const doAdd = async () => {
            const v = addInput.value.trim();
            if (!v) return;
            if (dirs.includes(v)) {
                toast('Already in the list.', 'warn');
                return;
            }
            await applyConfig({ models_dir: [...dirs, v] });
        };
        addBtn.addEventListener('click', doAdd);
        addInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') { e.preventDefault(); doAdd(); }
        });
    }

    // ── Model picker (the dropdown) ──────────────────────────────
    // A filterable list of every *.gguf in any models_dir. The
    // currently-loaded model is highlighted. Click → Apply. A
    // "Use custom path…" link drops back to a freeform input.
    function renderModelPicker(hostId, field, config, modelsResp) {
        const host = document.getElementById(hostId);
        const models = modelsResp.models || [];
        const currentPath = config[field] || '';
        // The model is `embed_model` or `llm_model`. The corresponding
        // loaded/error keys in the response drop the trailing `_model`.
        const baseKey = field.replace(/_model$/, '');

        // Two modes: dropdown (default) or freeform.
        const freeformOpen = host.dataset.freeformOpen === '1';

        if (freeformOpen) {
            host.innerHTML = `
                <input type="text" class="mono"
                       value="${escapeHtml(currentPath)}"
                       placeholder="/full/path/to/model.gguf"
                       style="width: 100%;">
                <div class="row" style="margin-top: 6px;">
                    <button class="primary" data-apply-custom>Apply</button>
                    <button data-cancel-custom>Back to list</button>
                    <span class="spacer"></span>
                    <span id="${hostId}-status" class="muted"></span>
                </div>
            `;
            const input = host.querySelector('input');
            const statusEl = host.querySelector(`#${hostId}-status`);
            setStatus(statusEl, config[`${baseKey}_loaded`], config[`${baseKey}_error`], !!currentPath);

            host.querySelector('[data-apply-custom]').addEventListener('click', async () => {
                await applyConfig({ [field]: input.value.trim() });
            });
            host.querySelector('[data-cancel-custom]').addEventListener('click', () => {
                host.dataset.freeformOpen = '0';
                renderModelPicker(hostId, field, config, modelsResp);
            });
            input.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') {
                    e.preventDefault();
                    host.querySelector('[data-apply-custom]').click();
                }
            });
            return;
        }

        // Default: filterable list
        host.innerHTML = `
            <input type="search" class="filter" placeholder="Filter ${models.length} models…"
                   style="width: 100%; margin-bottom: 6px;">
            <select size="6" class="mono"
                    style="width: 100%; font-family: var(--mono);"
                    data-model-list>
                ${models.map(m => {
                    const sel = (m.path === currentPath) ? 'selected' : '';
                    const sizeMB = (m.size_bytes / 1048576).toFixed(1);
                    return `<option value="${escapeHtml(m.path)}" ${sel}>${escapeHtml(m.name)} — ${sizeMB} MB</option>`;
                }).join('')}
                ${models.length === 0
                    ? '<option disabled>(no models found — add a Models folder above)</option>'
                    : ''}
            </select>
            <div class="row" style="margin-top: 6px;">
                <button class="primary" data-apply-pick>Apply selection</button>
                <button data-use-custom>Use custom path…</button>
                <span class="spacer"></span>
                <span id="${hostId}-status" class="muted"></span>
            </div>
        `;
        const list = host.querySelector('[data-model-list]');
        const filter = host.querySelector('input.filter');
        const statusEl = host.querySelector(`#${hostId}-status`);
        setStatus(statusEl, config[`${baseKey}_loaded`], config[`${baseKey}_error`], !!currentPath);

        filter.addEventListener('input', () => {
            const q = filter.value.trim().toLowerCase();
            for (const opt of list.options) {
                if (opt.disabled) continue;
                opt.hidden = q && !opt.text.toLowerCase().includes(q);
            }
        });
        list.addEventListener('dblclick', () => {
            host.querySelector('[data-apply-pick]').click();
        });
        host.querySelector('[data-apply-pick]').addEventListener('click', async () => {
            const sel = list.value;
            if (!sel) {
                toast('Pick a model from the list first.', 'warn');
                return;
            }
            await applyConfig({ [field]: sel });
        });
        host.querySelector('[data-use-custom]').addEventListener('click', () => {
            host.dataset.freeformOpen = '1';
            renderModelPicker(hostId, field, config, modelsResp);
        });
    }

    function setStatus(el, loaded, err, hasPath) {
        if (!el) return;
        if (!hasPath) { el.textContent = '— none —'; el.className = 'muted'; return; }
        if (loaded) { el.textContent = '✓ loaded'; el.className = 'badge badge-ok'; return; }
        el.textContent = `✗ ${err || 'failed'}`;
        el.className = 'badge badge-err';
    }

    async function applyConfig(patch) {
        try {
            const res = await httpPost('/api/config', patch);
            if (res?.ok === false) {
                const detail = res.embed_error || res.llm_error || res.error || 'unknown';
                toast(detail, 'err');
            }
            await load();
            return res;
        } catch (e) {
            toast(`Update failed: ${e.message}`, 'err');
            await load();
            return null;
        }
    }

    // ── Projects (read-only list) ──────────────────────────────────────
    function renderProjects(raw) {
        const host = document.getElementById('settings-projects');
        let list = [];
        if (Array.isArray(raw)) {
            list = raw;
        } else if (raw && typeof raw === 'object') {
            list = raw.projects ?? raw.items ?? [];
        }

        if (list.length === 0) {
            host.innerHTML = `
                <div class="empty">
                    <h3>No projects yet</h3>
                    <p>Projects are created automatically when you ingest data with a
                    new project name (use the <code>project</code> arg on
                    <code>apollo_add_document</code> or
                    <code>apollo_ingest_codebase</code>).</p>
                </div>`;
            return;
        }

        host.innerHTML = `<table class="kv" style="width:100%;">
            <thead><tr>
                <th>Project</th>
                <th>Documents</th>
                <th>Chunks</th>
                <th>Entities</th>
                <th>Relations</th>
            </tr></thead>
            <tbody>
            ${list.map(p => `
                <tr>
                    <td><strong>${escapeHtml(p.name ?? p.id ?? '(unnamed)')}</strong></td>
                    <td>${p.documents ?? 0}</td>
                    <td>${p.chunks ?? 0}</td>
                    <td>${p.entities ?? 0}</td>
                    <td>${p.relationships ?? 0}</td>
                </tr>`).join('')}
            </tbody>
        </table>`;
    }
}

function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}