// about.js — About tab: build info, server diagnostics, copy-agent-config.
//
// Anything that's read-only and informational lives here. The Settings
// tab is reserved for things the user can change. (See settings.js.)

import { httpJson, toast } from './api.js';

export async function renderAbout(panel) {
    let info = {}, health = {};
    try {
        [info, health] = await Promise.all([
            httpJson('/info').catch(() => ({})),
            httpJson('/health').catch(() => ({})),
        ]);
    } catch (e) { /* tolerate */ }

    const origin = window.location.origin;
    const mcpUrl = `${origin}/mcp`;
    const cfg = {
        mcpServers: {
            apollocore: {
                url: mcpUrl,
                transport: 'streamable-http',
            },
        },
    };
    const cfgStr = JSON.stringify(cfg, null, 2);

    // Capabilities counts come from /info; tools/resources/prompts are
    // integers. If the fetch failed, default to 0.
    const cap = `${info.tools ?? 0} tools, ${info.resources ?? 0} resources, ${info.prompts ?? 0} prompts`;

    panel.innerHTML = `
        <h2>About</h2>

        <div class="card">
            <h3 style="margin-top: 0;">Build</h3>
            <table class="kv">
                <tr><td class="key">Name</td>         <td>${escapeHtml(info.name ?? 'apollo_mcp_server')}</td></tr>
                <tr><td class="key">Title</td>        <td>${escapeHtml(info.title ?? 'ApolloCore MCP Server')}</td></tr>
                <tr><td class="key">Version</td>      <td>${escapeHtml(info.version ?? '—')}</td></tr>
                <tr><td class="key">MCP protocol</td> <td>${escapeHtml(info.protocol ?? '2025-11-25')}</td></tr>
                <tr><td class="key">Transport</td>    <td>${escapeHtml(info.transport ?? 'streamable-http')}</td></tr>
                <tr><td class="key">Vendor</td>       <td>${escapeHtml(info.vendor ?? 'ApolloCore')}</td></tr>
                <tr><td class="key">Capabilities</td> <td>${escapeHtml(cap)}</td></tr>
            </table>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Server (read-only — restart to change)</h3>
            <p class="muted">
                <strong>host</strong>, <strong>port</strong>, and <strong>data-dir</strong>
                are bind params and can't be changed at runtime. Edit the
                systemd unit or <code>./scripts/start.sh</code> and restart.
            </p>
            <table class="kv">
                <tr><td class="key">Hostname (browser sees)</td> <td>${escapeHtml(window.location.hostname)}</td></tr>
                <tr><td class="key">Port (browser sees)</td>     <td>${escapeHtml(window.location.port || '(default)')}</td></tr>
                <tr><td class="key">Live MCP sessions</td>       <td>${escapeHtml(String(health.sessions ?? '—'))}</td></tr>
                <tr><td class="key">Documents in KB</td>         <td>${escapeHtml(String(health.documents ?? '—'))}</td></tr>
                <tr><td class="key">Chunks in KB</td>            <td>${escapeHtml(String(health.chunks ?? '—'))}</td></tr>
                <tr><td class="key">Entities in KB</td>          <td>${escapeHtml(String(health.entities ?? '—'))}</td></tr>
            </table>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">Connect an agent</h3>
            <p>Paste this into <code>~/.config/Claude/claude_desktop_config.json</code>
            (or the equivalent in Cursor, Cline, Windsurf, Continue.dev):</p>
            <pre class="code" id="about-config">${escapeHtml(cfgStr)}</pre>
            <button id="about-copy" class="primary">Copy snippet</button>
            <a href="${escapeHtml(mcpUrl)}" target="_blank" rel="noopener">
                <button>Open raw MCP endpoint</button>
            </a>
        </div>

        <div class="card">
            <h3 style="margin-top: 0;">What is ApolloCore?</h3>
            <p>
                ApolloCore is a C++23 MCP (Model Context Protocol) server
                that exposes a local retrieval-augmented knowledge base to
                AI agents. It combines vector search, BM25 keyword search,
                an LLM-driven knowledge graph, and tree-sitter code
                extraction into one self-contained executable.
            </p>
            <p>
                The web UI you're looking at is just a thin client. Every
                number on this page was loaded by sending a real JSON-RPC
                request to <code>${escapeHtml(mcpUrl)}</code> — the same
                endpoint an agent uses.
            </p>
            <p class="muted">
                License: MIT • Source: this repository • UI version: 1.0.0
            </p>
        </div>
    `;

    document.getElementById('about-copy').addEventListener('click', () => {
        navigator.clipboard.writeText(cfgStr)
            .then(() => toast('Config copied'))
            .catch(e => toast(`Copy failed: ${e.message}`, 'err'));
    });
}

function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}