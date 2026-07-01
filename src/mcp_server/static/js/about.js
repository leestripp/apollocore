// about.js — About tab: build info, license, copy-agent-config snippet.

import { httpJson, toast } from './api.js';

export async function renderAbout(panel) {
    let info = {};
    try {
        info = await httpJson('/info');
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

    panel.innerHTML = `
        <h2>About</h2>

        <div class="card">
            <dl class="about-grid">
                <dt>Name</dt>     <dd>${escapeHtml(info.name ?? 'apollo_mcp_server')}</dd>
                <dt>Title</dt>    <dd>${escapeHtml(info.title ?? 'ApolloCore MCP Server')}</dd>
                <dt>Version</dt>  <dd>${escapeHtml(info.version ?? '—')}</dd>
                <dt>Protocol</dt> <dd>${escapeHtml(info.protocol ?? '2025-11-25')}</dd>
                <dt>Transport</dt><dd>${escapeHtml(info.transport ?? 'streamable-http')}</dd>
                <dt>Vendor</dt>   <dd>${escapeHtml(info.vendor ?? 'ApolloCore')}</dd>
            </dl>
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