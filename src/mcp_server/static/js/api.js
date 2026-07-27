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
const PROTOCOL_VERSION = '2025-11-25';

let _id = 1;
function nextId() { return _id++; }

// Module-level session ID. Captured from the initialize response headers
// and attached to every subsequent request — per MCP 2025-11-25 the
// server rejects post-initialize requests without it (HTTP 400, -32600).
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
export async function rpc(method, params = {}, sessionId = null, _retried = false) {
    const sid = sessionId ?? _sessionId;
    const headers = {
        'Content-Type': 'application/json',
        'Accept': 'application/json',
    };
    if (sid) {
        headers['Mcp-Session-Id'] = sid;
        // Spec §Transports#Protocol-Version-Header: clients SHOULD send
        // the negotiated version on every post-initialize request.
        headers['MCP-Protocol-Version'] = PROTOCOL_VERSION;
    }

    const resp = await fetch(MCP_ENDPOINT, {
        method: 'POST',
        headers,
        body: JSON.stringify({ jsonrpc: '2.0', id: nextId(), method, params }),
    });
    const respSid = resp.headers.get('Mcp-Session-Id');
    let body = {};
    try { body = await resp.json(); } catch { /* non-JSON error page */ }

    // Session went stale (server restarted, session reaped): the server
    // 400s with "Unknown MCP-Session-Id". Re-initialize once and retry
    // transparently so an open tab survives a server restart.
    if (!resp.ok && sid && !_retried && method !== 'initialize'
        && (resp.status === 400 || resp.status === 404)) {
        _sessionId = null;
        try {
            await initialize();
            return rpc(method, params, null, true);
        } catch { /* fall through to the original error */ }
    }

    if (body.error) return { error: body.error, sessionId: respSid };
    if (!resp.ok) {
        return { error: { code: resp.status, message: `HTTP ${resp.status} from ${MCP_ENDPOINT}` },
                 sessionId: respSid };
    }
    return { result: body.result, sessionId: respSid };
}

/** Send a true JSON-RPC notification (no id — spec: server MUST NOT reply). */
async function notify(method, params = {}) {
    const headers = {
        'Content-Type': 'application/json',
        'Accept': 'application/json',
    };
    if (_sessionId) {
        headers['Mcp-Session-Id'] = _sessionId;
        headers['MCP-Protocol-Version'] = PROTOCOL_VERSION;
    }
    await fetch(MCP_ENDPOINT, {
        method: 'POST',
        headers,
        body: JSON.stringify({ jsonrpc: '2.0', method, params }),
    });
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
        protocolVersion: PROTOCOL_VERSION,
        capabilities: { roots: { listChanged: false } },
        clientInfo: { name: 'apollocore-web-ui', version: '1.0.0' },
    });
    if (r.error) throw new Error(`${r.error.code}: ${r.error.message}`);
    if (r.sessionId) _sessionId = r.sessionId;
    // Spec §Lifecycle: confirm the handshake with an initialized
    // notification. Best-effort — the server tolerates its absence.
    try { await notify('notifications/initialized'); } catch { /* ignore */ }
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

/** Escape a string for safe interpolation into innerHTML. */
export function escapeHtml(s) {
    return String(s ?? '')
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
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
