// health.js — Health tab: server stats + global KB inventory.
//
// Per the user's model (see docs/prompt.txt and prompt.txt):
//   * There is exactly ONE knowledge base, located at the path set by the
//     operator at startup. The AI agent cannot move or recreate it.
//   * "Projects" inside that KB are TAGS — they identify the origin of a
//     chunk and make bulk deletion easy. They are NOT isolation units.
//     Every chunk is reachable from every search; every entity is part of
//     the same graph; nothing is private to a project.
//   * Therefore the Health tab shows GLOBAL totals as the primary numbers
//     (no "select a project" gate), and the per-project list below is
//     just an inventory so the operator can see which "deletion unit"
//     holds what.
//
// This file uses a few small ES-module helpers from ./api.js —
// `httpJson()`, `callTool()`, `readResource()`, and `toast()`. No other
// dependencies. No bundler. Hand-written.

import { httpJson, callTool, readResource, toast } from './api.js';

let refreshTimer = null;

export async function renderHealth(panel) {
    panel.innerHTML = `
        <div class="row" style="margin-bottom: 16px;">
            <h2 style="margin: 0;">Health</h2>
            <span class="spacer"></span>
            <span id="health-refresh-status" class="muted"></span>
            <button id="health-refresh-now">Refresh</button>
        </div>

        <div class="cards-2">
            <div class="card">
                <h3 style="margin-top: 0;">Server</h3>
                <table class="kv" id="health-server"></table>
            </div>
            <div class="card">
                <h3 style="margin-top: 0;">Surface</h3>
                <table class="kv" id="health-surface"></table>
            </div>
            <div class="card">
                <h3 style="margin-top: 0;">Knowledge base</h3>
                <p class="muted" style="margin: 4px 0 8px;">
                    One global KB shared by every project below.
                    Counts include <strong>every project's data</strong>.
                </p>
                <table class="kv" id="health-kb"></table>
            </div>
            <div class="card">
                <h3 style="margin-top: 0;">Probe</h3>
                <p class="muted">
                    Round-trip time of a single
                    <code>tools/call apollo_get_stats</code>.
                </p>
                <div class="row">
                    <span class="badge" id="health-probe-rtt">—</span>
                    <span class="muted" id="health-probe-result"></span>
                    <button id="health-probe-btn">Run probe</button>
                </div>
            </div>
        </div>

        <div class="card" style="margin-top: 16px;">
            <h3 style="margin-top: 0;">Projects (deletion units)</h3>
            <p class="muted" style="margin: 4px 0 8px;">
                Projects are <strong>tags for provenance and bulk deletion</strong>.
                They do not isolate data &mdash; searches, entities and graph
                edges all see across every project. Use them to remember where
                ingested data came from, and to wipe one project's data at a
                time when you no longer need it.
            </p>
            <div id="health-projects">…</div>
        </div>
    `;

    document.getElementById('health-refresh-now').addEventListener('click', refresh);
    document.getElementById('health-probe-btn').addEventListener('click', runProbe);

    await refresh();
    refreshTimer = setInterval(refresh, 5000);

    return () => clearInterval(refreshTimer);
}

async function refresh() {
    const status = document.getElementById('health-refresh-status');
    if (status) status.innerHTML = '<span class="spinner"></span> refreshing…';

    try {
        const [health, info, rawStats] = await Promise.all([
            httpJson('/health').catch(() => null),
            httpJson('/info').catch(() => null),
            callTool('apollo_get_stats', {}).catch(() => null),
        ]);
        // apollo_get_stats: callTool already unwraps the JSON-in-content-text into
// a plain object via unwrapToolResult, so rawStats IS the stats object
// (with documents, chunks, entities, projects[]). The MCP `structuredContent`
// field is also exposed by the backend but we don't need to parse it ourselves.
        const stats = rawStats && typeof rawStats === 'object' && 'documents' in rawStats
            ? rawStats
            : null;

        renderServer(info, health);
        renderSurface(info, health);
        renderKB(stats);
        renderProjects(stats);

        if (status) status.innerHTML =
            `<span class="muted">Updated ${new Date().toLocaleTimeString()}</span>`;
    } catch (e) {
        console.error(e);
        toast(`Health refresh failed: ${e.message}`, 'err');
        if (status) status.textContent = 'refresh failed';
    }
}

async function runProbe() {
    const rtt = document.getElementById('health-probe-rtt');
    const result = document.getElementById('health-probe-result');
    rtt.textContent = '…';
    result.textContent = '';
    const t0 = performance.now();
    try {
        await callTool('apollo_get_stats', {});
        const ms = (performance.now() - t0).toFixed(1);
        rtt.textContent = `${ms} ms`;
        rtt.className = 'badge badge-ok';
        result.textContent = 'OK';
    } catch (e) {
        const ms = (performance.now() - t0).toFixed(1);
        rtt.textContent = `${ms} ms`;
        rtt.className = 'badge badge-err';
        result.textContent = e.message;
    }
}

// ── Card renderers ──────────────────────────────────────────────────────
// Every card is just a key/value table. The data shape is whatever
// /health, /info, and apollo://stats return — see the C++ side for the
// source of truth. We tolerate missing fields defensively because the
// server may briefly return a partial payload during model load.

function renderServer(info, health) {
    const rows = [];
    if (info) {
        rows.push(['Name', info.name ?? '—']);
        rows.push(['Version', info.version ?? '—']);
        rows.push(['Protocol', info.protocol ?? '—']);
        rows.push(['Transport', info.transport ?? '—']);
        rows.push(['Endpoint', info.endpoint ?? '—']);
    }
    if (health) {
        rows.push(['Status', badge(health.status === 'ok' ? 'ok' : 'err', health.status)]);
    }
    paintRows('health-server', rows);
}

function renderSurface(info, health) {
    const rows = [];
    if (health) {
        rows.push(['Tools',     health.tools ?? 0]);
        rows.push(['Resources', health.resources ?? 0]);
        rows.push(['Prompts',   health.prompts ?? 0]);
        rows.push(['Sessions',  health.sessions ?? 0]);
    }
    if (info) {
        rows.push(['Vendor', info.vendor ?? '—']);
        rows.push(['Title',  info.title ?? '—']);
    }
    paintRows('health-surface', rows);
}

// Single global KB. "Active project" is not a thing the UI should ask
// about — every search, every entity, every chunk is in the same pool.
function renderKB(stats) {
    const rows = [];
    if (stats && typeof stats === 'object') {
        rows.push(['Documents',     fmtNum(stats.documents)]);
        rows.push(['Chunks',        fmtNum(stats.chunks)]);
        rows.push(['Entities',      fmtNum(stats.entities)]);
        rows.push(['Relationships', fmtNum(stats.relationships)]);
        rows.push(['Projects',      fmtNum((stats.projects || []).length)]);
    } else {
        rows.push(['Status', 'KB not yet loaded. Models may still be initialising.']);
    }
    paintRows('health-kb', rows);
}

// Per-project inventory. These are the "deletion units" — knowing how
// many chunks/entities live under each tag lets the operator audit
// provenance and bulk-wipe data they no longer want to keep.
//
// No toolbar here: the actual delete action lives on the Settings tab
// (or will, once a dedicated `apollo_delete_project_data` tool is
// added — see CodeMax.md § Active Plan for that next step).
function renderProjects(stats) {
    const host = document.getElementById('health-projects');
    if (!host) return;

    let projects = [];
    if (stats && Array.isArray(stats.projects)) {
        projects = stats.projects;
    } else if (stats && typeof stats === 'object') {
        // Defensive: server might wrap projects under a different key.
        projects = stats.project_breakdown || stats.by_project || [];
    }

    if (projects.length === 0) {
        host.innerHTML = `
            <div class="empty">
                <p>No projects yet. New ingestions auto-create a project tag
                   (default = <code>"default"</code>) so the user can find
                   the data later. Tools like
                   <code>apollo_ingest_codebase(path, project=...)</code>
                   stamp the project on every chunk they create.</p>
            </div>`;
        return;
    }

    // Stable sort: largest first, then alphabetical.
    projects.sort((a, b) =>
        (b.chunks ?? 0) - (a.chunks ?? 0)
        || String(a.name).localeCompare(String(b.name)));

    const totalChunks = projects.reduce((s, p) => s + (p.chunks ?? 0), 0);

    host.innerHTML = `
        <table class="kv">
            <thead>
                <tr>
                    <th style="text-align: left;">Project</th>
                    <th class="num">Docs</th>
                    <th class="num">Chunks</th>
                    <th class="num">Entities</th>
                    <th class="num">Rels</th>
                    <th class="num">Share</th>
                </tr>
            </thead>
            <tbody>
                ${projects.map(p => {
                    const share = totalChunks > 0
                        ? ((p.chunks ?? 0) * 100 / totalChunks).toFixed(1) + '%'
                        : '—';
                    return `
                        <tr>
                            <td><strong>${escapeHtml(p.name ?? '—')}</strong></td>
                            <td class="num">${fmtNum(p.documents)}</td>
                            <td class="num">${fmtNum(p.chunks)}</td>
                            <td class="num">${fmtNum(p.entities)}</td>
                            <td class="num">${fmtNum(p.relationships)}</td>
                            <td class="num muted">${share}</td>
                        </tr>
                    `;
                }).join('')}
            </tbody>
            <tfoot>
                <tr>
                    <td><strong>Total</strong></td>
                    <td class="num">${fmtNum(projects.reduce((s, p) => s + (p.documents ?? 0), 0))}</td>
                    <td class="num"><strong>${fmtNum(totalChunks)}</strong></td>
                    <td class="num">${fmtNum(projects.reduce((s, p) => s + (p.entities ?? 0), 0))}</td>
                    <td class="num">${fmtNum(projects.reduce((s, p) => s + (p.relationships ?? 0), 0))}</td>
                    <td class="num">100.0%</td>
                </tr>
            </tfoot>
        </table>
    `;
}

// ── Tiny DOM helpers (kept local so health.js stays self-contained) ─────

function paintRows(elementId, rows) {
    const table = document.getElementById(elementId);
    if (!table) return;
    if (!rows || rows.length === 0) {
        table.innerHTML = '<tr><td colspan="2" class="muted">—</td></tr>';
        return;
    }
    table.innerHTML = rows.map(([k, v]) =>
        `<tr><td class="key">${escapeHtml(k)}</td>`
        + `<td>${typeof v === 'string' ? escapeHtml(v) : v}</td></tr>`
    ).join('');
}

function fmtNum(n) {
    if (n == null || Number.isNaN(Number(n))) return '—';
    return Number(n).toLocaleString();
}

function badge(kind, text) {
    return `<span class="badge badge-${kind}">${escapeHtml(String(text))}</span>`;
}

function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
