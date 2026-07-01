// settings.js — Settings tab: change the things you can change.
//
// What's here:
//   • Projects — switch the active project (apollo_set_project)
//   • Models   — edit & hot-reload embedding / LLM model paths
//                (persists to <data-dir>/config.json)
//
// What's NOT here:
//   • Read-only diagnostics (host, port, sessions, build info) live on
//     the About tab. The Settings page only contains things you can
//     change — anything else is theatre.
//
// UX:
//   • Browse button opens the system-native file picker (filtered to
//     *.gguf). Picking a file auto-applies.
//   • Typing + Enter or Tab/blur also auto-applies. There is no separate
//     "Apply" button — the moment the path is committed, the change is
//     hot-reloaded and persisted.

import { httpJson, httpPost, readResource, callTool, toast } from './api.js';

export async function renderSettings(panel) {
    panel.innerHTML = `
        <h2>Settings</h2>

        <div class="card">
            <h3 style="margin-top: 0;">Models</h3>
            <p class="muted">
                Pick a <code>.gguf</code> file with <strong>Browse</strong>, or paste a path.
                Changes auto-apply on file-pick / Enter / blur — no Apply button needed.
                They're hot-reloaded and persisted to <code>&lt;data-dir&gt;/config.json</code>,
                so they survive restarts.
            </p>
            <table class="kv" style="table-layout: fixed;">
                <colgroup>
                    <col style="width: 140px;">
                    <col>
                    <col style="width: 220px;">
                    <col style="width: 110px;">
                </colgroup>
                <tbody id="settings-models-body"></tbody>
            </table>
            <p class="muted" id="settings-config-path" style="margin-top: 10px;"></p>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Projects</h3>
            <p class="muted">All knowledge bases accessible to this server.</p>
            <div id="settings-projects"></div>
        </div>
    `;

    await load();

    async function load() {
        try {
            const [config, projectsRaw] = await Promise.all([
                httpJson('/api/config').catch((e) => ({ error: e.message })),
                readResource('apollo://projects').catch(() => null),
            ]);
            renderModels(config);
            renderProjects(projectsRaw);
        } catch (e) {
            toast(`Settings load failed: ${e.message}`, 'err');
        }
    }

    // ── Models ────────────────────────────────────────────────────
    // Two rows (embedding, LLM). Each row has an input, a Browse button,
    // and a status pill that updates immediately after each save.
    function renderModels(cfg) {
        const body = document.getElementById('settings-models-body');
        if (cfg && cfg.error) {
            body.innerHTML = `<tr><td colspan="4" class="muted">config unavailable: ${escapeHtml(cfg.error)}</td></tr>`;
            return;
        }
        const embedPath = cfg?.embed_model ?? '';
        const llmPath   = cfg?.llm_model ?? '';

        body.innerHTML = `
            <tr>
                <td class="key">Embedding model</td>
                <td><input id="cfg-embed-input" type="text" class="mono"
                           value="${escapeHtml(embedPath)}"
                           placeholder="/path/to/nomic-embed-text-v1.5.Q4_K_M.gguf"
                           style="width: 100%;"></td>
                <td><button id="cfg-embed-browse" type="button">Browse…</button></td>
                <td id="cfg-embed-status" class="muted" style="text-align: right;"></td>
            </tr>
            <tr>
                <td class="key">LLM model</td>
                <td><input id="cfg-llm-input" type="text" class="mono"
                           value="${escapeHtml(llmPath)}"
                           placeholder="/path/to/qwen2.5-0.5b-instruct-q4_k_m.gguf"
                           style="width: 100%;"></td>
                <td><button id="cfg-llm-browse" type="button">Browse…</button></td>
                <td id="cfg-llm-status" class="muted" style="text-align: right;"></td>
            </tr>
        `;
        document.getElementById('settings-config-path').textContent =
            cfg?.config_path ? `config.json: ${cfg.config_path}` : 'config.json: (not yet written)';

        setStatus('cfg-embed-status', cfg?.embed_loaded, cfg?.embed_error, !!embedPath);
        setStatus('cfg-llm-status',   cfg?.llm_loaded,   cfg?.llm_error,   !!llmPath);

        wireAutoApply('cfg-embed-input', 'cfg-embed-browse', 'embed_model');
        wireAutoApply('cfg-llm-input',   'cfg-llm-browse',   'llm_model');
    }

    function setStatus(id, loaded, err, hasPath) {
        const el = document.getElementById(id);
        if (!el) return;
        if (!hasPath) { el.textContent = '— empty —'; el.className = 'muted'; return; }
        if (loaded) { el.textContent = '✓ loaded'; el.className = 'badge badge-ok'; return; }
        el.textContent = `✗ ${err || 'failed'}`;
        el.className = 'badge badge-err';
    }

    // Wire a row: input + Browse button + auto-apply on pick/blur/Enter.
    function wireAutoApply(inputId, browseId, fieldName) {
        const input = document.getElementById(inputId);
        const browse = document.getElementById(browseId);

        // Hidden <input type="file"> for the system picker. We make it
        // accept .gguf only. The "system native file browser" here is
        // GTK file chooser on Linux, NSOpenPanel on macOS, the Win32
        // common dialog on Windows — same as any other app.
        const fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.accept = '.gguf';
        fileInput.style.display = 'none';
        fileInput.addEventListener('change', () => {
            const f = fileInput.files?.[0];
            if (!f) return;
            // showOpenFilePicker returns a File object with .path in
            // Chromium and Firefox alike. The standard File API gives us
            // name but not always full path; we use .path where available.
            const path = f.path || f.name;
            input.value = path;
            applyConfig({ [fieldName]: path });
        });
        document.body.appendChild(fileInput);

        browse.addEventListener('click', () => fileInput.click());
        input.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                applyConfig({ [fieldName]: input.value.trim() });
            }
        });
        input.addEventListener('blur', () => {
            const cur = input.value.trim();
            // Only fire if the input differs from the last-known value.
            // Without this, every tab-switch on the page would re-POST.
            if (cur === input.dataset.lastApplied) return;
            if (cur === '') return;  // empty is a no-op; user can clear by typing valid path then clearing
            applyConfig({ [fieldName]: cur });
        });
    }

    async function applyConfig(patch) {
        // Optimistic UI: mark the input as last-applied immediately so
        // we don't double-fire on blur. The server response confirms.
        const fieldName = Object.keys(patch)[0];
        const inputId = fieldName === 'embed_model' ? 'cfg-embed-input' : 'cfg-llm-input';
        const statusId = fieldName === 'embed_model' ? 'cfg-embed-status' : 'cfg-llm-status';
        const input = document.getElementById(inputId);
        if (input) input.dataset.lastApplied = input.value.trim();

        try {
            let res;
            try {
                res = await httpPost('/api/config', patch);
            } catch (e) {
                // HTTP route failed — try the MCP tool as a fallback.
                res = await callTool('apollo_set_config', patch);
            }
            if (res?.ok === false || res?.is_error) {
                const detail = res?.embed_error || res?.llm_error || res?.error || 'unknown';
                toast(`${fieldName}: ${detail}`, 'err');
            } else if (patch.embed_model !== undefined) {
                toast(patch.embed_model ? 'Embedding model applied.' : 'Embedding unchanged.');
            } else if (patch.llm_model !== undefined) {
                toast(patch.llm_model ? 'LLM model applied.' : 'LLM unchanged.');
            }
            // Refresh from server to show the authoritative load status.
            await load();
        } catch (e) {
            toast(`Update failed: ${e.message}`, 'err');
            await load();
        }
    }

    // ── Projects ──────────────────────────────────────────────────
    function renderProjects(raw) {
        const host = document.getElementById('settings-projects');
        let list = [];
        let active = null;
        if (Array.isArray(raw)) {
            list = raw;
        } else if (raw && typeof raw === 'object') {
            list = raw.projects ?? raw.items ?? [];
            active = raw.active ?? raw.current ?? null;
        }

        if (list.length === 0) {
            host.innerHTML = `
                <div class="empty">
                    <h3>No projects yet</h3>
                    <p>Create one with the <code>apollo_create_kb</code> tool or via the CLI.</p>
                </div>`;
            return;
        }

        host.innerHTML = list.map(p => {
            const name = p.name ?? p.id ?? '(unnamed)';
            const isActive = (active && active === name) || p.is_active;
            return `
                <div class="row" style="padding: 8px 0; border-bottom: 1px solid var(--border);">
                    <span class="badge ${isActive ? 'badge-ok' : ''}">${isActive ? 'active' : 'idle'}</span>
                    <strong>${escapeHtml(name)}</strong>
                    <span class="muted">${escapeHtml(p.description ?? '')}</span>
                    <span class="spacer"></span>
                    <button data-set-project="${escapeHtml(name)}">${isActive ? 'Open (active)' : 'Open'}</button>
                </div>
            `;
        }).join('');

        host.querySelectorAll('[data-set-project]').forEach(btn => {
            btn.addEventListener('click', async () => {
                const name = btn.dataset.setProject;
                btn.disabled = true;
                try {
                    await callTool('apollo_set_project', { project_name: name });
                    toast(`Switched to ${name}`);
                    await load();
                } catch (e) {
                    toast(`Switch failed: ${e.message}`, 'err');
                } finally {
                    btn.disabled = false;
                }
            });
        });
    }
}

function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}