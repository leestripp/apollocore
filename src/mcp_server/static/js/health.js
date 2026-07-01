// health.js — Health tab: server stats, project info, refresh loop.

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
                <h3 style="margin-top: 0;">Active project &amp; KB stats</h3>
                <table class="kv" id="health-kb"></table>
            </div>
            <div class="card">
                <h3 style="margin-top: 0;">Probe</h3>
                <p class="muted">Round-trip time of a single <code>tools/call apollo_get_stats</code> call.</p>
                <div class="row">
                    <span class="badge" id="health-probe-rtt">—</span>
                    <span class="muted" id="health-probe-result"></span>
                    <button id="health-probe-btn">Run probe</button>
                </div>
            </div>
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
        const [health, info, stats] = await Promise.all([
            httpJson('/health').catch(() => null),
            httpJson('/info').catch(() => null),
            readResource('apollo://stats').catch(() => null),
        ]);

        renderServer(info, health);
        renderSurface(info, health);
        renderKB(stats);

        if (status) status.innerHTML = `<span class="muted">Updated ${new Date().toLocaleTimeString()}</span>`;
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
    const table = document.getElementById('health-server');
    table.innerHTML = rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td>${typeof v === 'string' ? escapeHtml(v) : v}</td></tr>`).join('');
}

function renderSurface(info, health) {
    const rows = [];
    if (health) {
        rows.push(['Tools', health.tools ?? 0]);
        rows.push(['Resources', health.resources ?? 0]);
        rows.push(['Prompts', health.prompts ?? 0]);
        rows.push(['Sessions', health.sessions ?? 0]);
    }
    if (info) {
        rows.push(['Vendor', info.vendor ?? '—']);
        rows.push(['Title', info.title ?? '—']);
    }
    const table = document.getElementById('health-surface');
    table.innerHTML = rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td>${typeof v === 'string' ? escapeHtml(v) : v}</td></tr>`).join('');
}

function renderKB(stats) {
    const rows = [];
    if (stats && typeof stats === 'object') {
        rows.push(['Project', stats.project ?? stats.name ?? '—']);
        rows.push(['Documents', stats.documents ?? stats.document_count ?? '—']);
        rows.push(['Chunks', stats.chunks ?? stats.chunk_count ?? '—']);
        rows.push(['Entities', stats.entities ?? stats.entity_count ?? '—']);
        rows.push(['Relationships', stats.relationships ?? stats.relationship_count ?? '—']);
        rows.push(['KB path', stats.path ?? '—']);
    } else {
        rows.push(['Status', 'No KB open. Use Settings → Open a KB.']);
    }
    const table = document.getElementById('health-kb');
    table.innerHTML = rows.map(([k, v]) => `<tr><td class="key">${escapeHtml(k)}</td><td>${typeof v === 'string' ? escapeHtml(v) : v}</td></tr>`).join('');
}

function badge(kind, text) {
    return `<span class="badge badge-${kind}">${escapeHtml(text)}</span>`;
}

function escapeHtml(s) {
    return String(s)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}