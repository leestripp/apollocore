// api.js — tiny JSON-RPC 2.0 wrapper over fetch('/mcp').
//
// Usage:
//   const r = await rpc('tools/call', { name: 'apollo_get_stats', arguments: {} });
//   if (r.error) { ... }
//   const data = unwrapToolResult(r.result);  // returns the parsed object (or {isError: true})
//
// The UI is a regular MCP client. Every method documented at
// modelcontextprotocol.io/specification/2025-11-25 is reachable from here.

const MCP_ENDPOINT = '/mcp';

let _id = 1;
function nextId() { return _id++; }

// Module-level session ID. Set by `initialize()` from the response headers.
// Subsequent rpc() calls automatically include it. Per the MCP 2025-11-25 spec,
// post-initialize requests MUST carry the negotiated Mcp-Session-Id or the
// server rejects them with code -32600.
let _sessionId = null;
export function getSessionId() { return _sessionId; }

/**
 * Send a JSON-RPC 2.0 request to /mcp.
 *
 * @param {string} method  e.g. "tools/call"
 * @param {object} params  method-specific params
 * @param {string|null} sessionId  optional override for Mcp-Session-Id header
 * @returns {Promise<{result?: object, error?: {code: number, message: string, data?: any}, sessionId?: string}>}
 */
export async function rpc(method, params = {}, sessionId = null) {
    const headers = {
        'Content-Type': 'application/json',
        'Accept': 'application/json',
    };
    if (sessionId) headers['Mcp-Session-Id'] = sessionId;

    const resp = await fetch(MCP_ENDPOINT, {
        method: 'POST',
        headers,
        body: JSON.stringify({ jsonrpc: '2.0', id: nextId(), method, params }),
    });
    const sid = resp.headers.get('Mcp-Session-Id');
    const body = await resp.json();

    if (body.error) return { error: body.error, sessionId: sid };
    return { result: body.result, sessionId: sid };
}

/** Raw HTTP fetch helper for non-JSON-RPC endpoints like /health and /info. */
export async function httpJson(path) {
    const resp = await fetch(path, { headers: { 'Accept': 'application/json' } });
    if (!resp.ok) throw new Error(`HTTP ${resp.status} for ${path}`);
    return resp.json();
}

/** POST JSON to a non-RPC endpoint. Throws on non-2xx with the body included. */
export async function httpPost(path, body) {
    const resp = await fetch(path, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Accept': 'application/json',
        },
        body: JSON.stringify(body ?? {}),
    });
    const text = await resp.text();
    let json = {};
    try { json = text ? JSON.parse(text) : {}; } catch { /* keep {} */ }
    if (!resp.ok) {
        const detail = json.error ?? text ?? `HTTP ${resp.status}`;
        const err = new Error(detail);
        err.body = json;
        err.status = resp.status;
        throw err;
    }
    return json;
}

/** Unwrap the `result` of a tools/call response.
 *  If isError is true, throws a domain error so the caller can show a toast.
 */
export function unwrapToolResult(result) {
    if (!result) throw new Error('Empty tool result');
    if (result.isError) {
        const text = Array.isArray(result.content)
            ? result.content.find(c => c.type === 'text')?.text ?? 'Unknown tool error'
            : 'Unknown tool error';
        throw new Error(text);
    }
    // A tool returns `content: [{type: "text", text: "<JSON-string>"}]` — parse it.
    const item = Array.isArray(result.content)
        ? result.content.find(c => c.type === 'text')
        : null;
    if (!item) return result;  // some tools return rich content directly
    try {
        return JSON.parse(item.text);
    } catch {
        return item.text;  // not JSON; raw text
    }
}

/** Send a tools/call request and unwrap the result. */
export async function callTool(name, args = {}) {
    const r = await rpc('tools/call', { name, arguments: args });
    if (r.error) throw new Error(`${r.error.code}: ${r.error.message}`);
    return unwrapToolResult(r.result);
}

/** Send a resources/read and return its text content (parsed if JSON). */
export async function readResource(uri) {
    const r = await rpc('resources/read', { uri });
    if (r.error) throw new Error(`${r.error.code}: ${r.error.message}`);
    const item = r.result?.contents?.[0];
    if (!item) throw new Error(`No contents for ${uri}`);
    if (item.mimeType === 'application/json') return JSON.parse(item.text ?? item.blob ?? '{}');
    return item.text ?? item.blob ?? '';
}

/** List tools. */
export async function listTools() {
    const r = await rpc('tools/list');
    if (r.error) throw new Error(`${r.error.code}: ${r.error.message}`);
    return r.result?.tools ?? [];
}

/** Initialize the MCP session. Safe to call repeatedly; returns serverInfo. */
export async function initialize() {
    const r = await rpc('initialize', {
        protocolVersion: '2025-11-25',
        capabilities: { roots: { listChanged: false }, sampling: {} },
        clientInfo: { name: 'apollocore-web-ui', version: '1.0.0' },
    });
    if (r.error) throw new Error(`${r.error.code}: ${r.error.message}`);
    // Send initialized notification (no id → notification; rpc() still assigns one,
    // but JSON-RPC 2.0 allows either. We don't strictly need it for the server to work.)
    return r.result;
}

/** Show a toast notification. kind: 'info' | 'warn' | 'err'. */
export function toast(message, kind = 'info') {
    const host = document.getElementById('toast-host');
    if (!host) return;
    const el = document.createElement('div');
    el.className = 'toast' + (kind === 'err' ? ' err' : kind === 'warn' ? ' warn' : '');
    el.textContent = message;
    host.appendChild(el);
    setTimeout(() => {
        el.style.opacity = '0';
        setTimeout(() => el.remove(), 200);
    }, 4000);
}

/** Format a Date or ISO string as relative time ("3 min ago"). */
export function relTime(ts) {
    if (!ts) return 'never';
    const t = typeof ts === 'string' ? new Date(ts).getTime() : ts;
    const s = Math.floor((Date.now() - t) / 1000);
    if (s < 60) return `${s}s ago`;
    if (s < 3600) return `${Math.floor(s / 60)}m ago`;
    if (s < 86400) return `${Math.floor(s / 3600)}h ago`;
    return `${Math.floor(s / 86400)}d ago`;
}