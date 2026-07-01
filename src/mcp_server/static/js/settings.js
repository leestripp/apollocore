// settings.js — Settings tab: list projects, switch active, show model paths, server config.

import { httpJson, readResource, callTool, toast } from './api.js';

export async function renderSettings(panel) {
    panel.innerHTML = `
        <h2>Settings</h2>

        <div class="card">
            <h3 style="margin-top: 0;">Projects</h3>
            <p class="muted">All knowledge bases accessible to this server.</p>
            <div id="settings-projects"></div>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Server configuration</h3>
            <table class="kv" id="settings-server"></table>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Models</h3>
            <table class="kv" id="settings-models"></table>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Build</h3>
            <table class="kv" id="settings-build"></table>
        </div>
    `;

    await load();

    async function load() {
        try {
            const [info, health, projectsRaw] = await Promise.all([
                httpJson('/info').catch(() => ({})),
                httpJson('/health').catch(() => ({})),
                readResource('apollo://projects').catch(() => null),
            ]);

            renderServer(info, health);
            renderModels(info, health);
            renderBuild(info);
            renderProjects(projectsRaw);
        } catch (e) {
            toast(`Settings load failed: ${e.message}`, 'err');
        }
    }

    function renderServer(info, health) {
        const rows = [
            ['Host', window.location.hostname],
            ['Port', window.location.port || '(default)'],
            ['Data dir (CLI/env)', '(see CLI /help on the server)'],
            ['Session id', '(none — server is stateless until first call)'],
            ['Protocol', info.protocol ?? '—'],
            ['Transport', info.transport ?? '—'],
            ['Live sessions', health.sessions ?? '—'],
        ];
        document.getElementById('settings-server').innerHTML =
            rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td>${escapeHtml(String(v))}</td></tr>`).join('');
    }

    function renderModels(info, health) {
        const rows = [
            ['Embedding model', info.embedding_model ?? info.embed_model ?? '(not configured)'],
            ['LLM model', info.llm_model ?? '(not configured)'],
            ['Server build', info.version ?? '—'],
        ];
        document.getElementById('settings-models').innerHTML =
            rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td class="mono">${escapeHtml(String(v))}</td></tr>`).join('');
    }

    function renderBuild(info) {
        const rows = [
            ['MCP protocol', info.protocol ?? '—'],
            ['Server version', info.version ?? '—'],
            ['Vendor', info.vendor ?? '—'],
            ['Capabilities', `${info.tools ?? 0} tools, ${info.resources ?? 0} resources, ${info.prompts ?? 0} prompts`],
        ];
        document.getElementById('settings-build').innerHTML =
            rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td>${escapeHtml(String(v))}</td></tr>`).join('');
    }

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
                    <button data-set-project="${escapeHtml(name)}">Open</button>
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