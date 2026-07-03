# ApolloCore MCP Server — How To Connect

A guide for AI-agent developers. Shows how to connect, initialize a session, and call the
13 tools / read the 6 resources / render the 4 prompts that ApolloCore exposes.

---

## 1. Quick start

The server speaks two transports. Pick one.

### Option A — HTTP (default)

```bash
# Start the server:
./scripts/start.sh --detach

# Probe:
curl http://127.0.0.1:8765/health
# → {"status":"ok","tools":13,"prompts":4,"resources":6, ...}
```

Default bind is `127.0.0.1:8765` (loopback only — safe default per MCP spec §6.2).

### Option B — stdio

```bash
./build/src/mcp_server/apollo_mcp_server --transport stdio
```

stdin = newline-delimited JSON-RPC frames; stdout = responses; stderr = logs.
The two transports share one dispatch path (`mcp_dispatch.dispatch_one()`), so anything
that works on one works on the other with the same JSON-RPC envelope.

---

## 2. The mandatory headers

Every POST to `/mcp` **must** include these headers, or the server returns 4xx:

| Header | Value | Why |
|---|---|---|
| `Content-Type` | `application/json` | Otherwise body parsing fails |
| `Accept` | `application/json, text/event-stream` | Spec requirement; both transports |
| `Origin` | `http://localhost` (or matches your referrer) | CORS gate — missing = 403 |
| `MCP-Protocol-Version` | `2025-11-25` | Pin the protocol; mismatches → 400 |

`Authorization: Bearer <APOLLO_AUTH_TOKEN>` is required **only** if the server was
started with `APOLLO_AUTH_TOKEN` set in its environment.

---

## 3. Initialization handshake

```bash
curl -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Origin: http://localhost" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{
        "jsonrpc":"2.0","id":1,"method":"initialize",
        "params":{
          "protocolVersion":"2025-11-25",
          "capabilities":{},
          "clientInfo":{"name":"my-agent","version":"0.1.0"}
        }
      }'
```

Response headers include `Mcp-Session-Id: <uuid>`. **Save this** — every subsequent
request must echo it. Sessions idle out at 30 min (reaper runs every 5 min).

Response body includes:

```json
{
  "result": {
    "protocolVersion":"2025-11-25",
    "serverInfo":{"name":"apollo_mcp_server","title":"ApolloCore MCP Server","version":"1.0.0"},
    "capabilities":{"logging":{},"prompts":{},"resources":{"subscribe":false},"tools":{}},
    "instructions":"ApolloCore is a local-first RAG knowledge base. Use apollo_query_kb to search, apollo_add_document to ingest text, apollo_ingest_codebase for code directories, and apollo_search_symbols for code symbol lookup. Set the active project with apollo_set_project before reading."
  }
}
```

`ping` is allowed at any time (pre-init and post-init). Use it as a keep-alive.

---

## 4. Discovering capabilities

After initialize, list what's available:

```bash
curl -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Origin: http://localhost" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -H "Mcp-Session-Id: <sid-from-step-3>" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'

curl ... -d '{"jsonrpc":"2.0","id":3,"method":"resources/list"}'
curl ... -d '{"jsonrpc":"2.0","id":4,"method":"prompts/list"}'
```

Server returns 13 tools, 6 resources, 4 prompts at the time of writing — see §5 below.
The list grows over time, so always re-query rather than hard-coding tool names.

`tools/list`, `resources/list`, `prompts/list` all honor the **cursor** parameter:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"cursor":"<base64-offset>"}}
```

Page size is 50; the next cursor is in `result.nextCursor`. End-of-list = no
`nextCursor` field.

---

## 5. Tools

| Tool | Purpose | Read-only? |
|---|---|---|
| `apollo_query_kb` | Hybrid search — vector + BM25 + graph. Modes: `vector`, `hybrid`, `rerank`, `full`. **Default for any "look up in the KB" question.** | yes |
| `apollo_add_document` | Ingest a text document. Auto-chunks. | no |
| `apollo_extract_entities` | Run LLM-extracted entity discovery on a chunk of text. Needs the LLM model loaded. | no (mutates KB) |
| `apollo_ingest_codebase` | Walk a source directory; tree-sitter for code (FUNCTION/CLASS/namespaces), chunked embeddings for everything else. **Long-running** — supports cancellation via `notifications/cancelled`. | no |
| `apollo_search_symbols` | Find code symbols by name. Faster + more precise than `query_kb` for symbols. | yes |
| `apollo_get_neighbors` | Entity-graph traversal. `hops=1..3`. | yes |
| `apollo_create_kb` | Create a new KB at a given path. Errors if a KB exists. | no, destructive |
| `apollo_open_kb` | Replace the currently-open KB with one from disk. | no, destructive |
| `apollo_save_kb` | Persist in-memory KB to disk. | no |
| `apollo_get_stats` | Count documents / chunks / entities / relationships (filterable by project). | yes |
| `apollo_set_project` | Switch the active project — scopes all subsequent reads/writes. | no, destructive |
| `apollo_list_projects` | List all projects in the current KB. Result is `{"projects":[{"name","description","is_shared","owner","created_at","is_current"}]}`. | yes |
| `apollo_set_config` | Hot-reload embed/LLM model paths; persists to `<data-dir>/config.json`. | no, destructive |

Every tool has an `outputSchema` for validated `structuredContent`. **Always check
`result.isError`** on the response — boolean, in the JSON-RPC `result` envelope.

### Typical call

```bash
curl -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -H "Origin: http://localhost" -H "MCP-Protocol-Version: 2025-11-25" \
  -H "Mcp-Session-Id: <sid>" \
  -d '{
        "jsonrpc":"2.0","id":5,"method":"tools/call",
        "params":{"name":"apollo_query_kb",
                  "arguments":{"query":"how does the entity extractor work",
                               "mode":"hybrid","top_k":5}}
      }'
```

Response shape:

```json
{
  "result": {
    "content":[{"type":"text","text":"...human-readable answer..."}],
    "structuredContent":{...tool-specific JSON object...}
  }
}
```

The `text` part is what an LLM reads; `structuredContent` is what your agent logic
should parse.

---

## 6. Resources (read-only data sources)

| URI | Purpose |
|---|---|
| `apollo://projects` | All projects in the current KB |
| `apollo://config` | Model paths + load status (same shape as `GET /api/config`) |
| `apollo://stats` | Document/chunk/entity counts for current project |
| `apollo://entities` | All entities in current project (capped at 200) |
| `apollo://graph` | Subgraph (entities + relationships) for current project |
| `apollo://chunk/{id}` | Read one chunk by id |

Read with `resources/read`:

```bash
curl -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -H "Origin: http://localhost" -H "MCP-Protocol-Version: 2025-11-25" \
  -H "Mcp-Session-Id: <sid>" \
  -d '{
        "jsonrpc":"2.0","id":6,"method":"resources/read",
        "params":{"uri":"apollo://stats"}
      }'
```

The response `contents` field includes a text blob suitable for context, plus the
parsed JSON in `_meta.apollocore/structured` (server extension field).

---

## 7. Prompts (templated message starters)

| Name | Argument | What it generates |
|---|---|---|
| `summarize_kb` | — | Survey + overview of current KB |
| `answer_with_citations` | `question` (required) | Question answer with chunk-level citations |
| `code_review_summary` | `path` (required) | Walks a codebase structure from the KB side |
| `entity_research` | `entity` (required) | Everything known about one entity |

Get one with `prompts/get`:

```bash
curl -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -H "Origin: http://localhost" -H "MCP-Protocol-Version: 2025-11-25" \
  -H "Mcp-Session-Id: <sid>" \
  -d '{
        "jsonrpc":"2.0","id":7,"method":"prompts/get",
        "params":{"name":"answer_with_citations",
                  "arguments":{"question":"what does apollo_save_kb do?"}}
      }'
```

Returns a `description` + `messages[]` array you can stream straight into the model.

---

## 8. Server-Sent Events (SSE)

Open a long-lived subscription for async notifications (e.g. `notifications/tools/list_changed`,
progress frames for long-running tools):

```bash
curl -N -X GET http://127.0.0.1:8765/mcp \
  -H "Accept: text/event-stream" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -H "Mcp-Session-Id: <sid>"
```

The server sends `: keepalive\r\n` every 15s (matches MCP spec).
Events have `id: <numeric>` so clients can reconnect with `Last-Event-ID: <id>` to
resume — **though resume support is incomplete; re-subscribing after reconnect is the
safe path for now.**

---

## 9. Closing a session

Send `DELETE /mcp` with the `Mcp-Session-Id` header. A second DELETE returns 404
because the session is already gone.

```bash
curl -X DELETE http://127.0.0.1:8765/mcp \
  -H "Mcp-Session-Id: <sid>" -H "MCP-Protocol-Version: 2025-11-25"
```

Otherwise sessions idle out at 30 minutes (reaper evicts them).

---

## 10. Common errors and what they mean

| HTTP | JSON-RPC | Meaning | Fix |
|---|---|---|---|
| **403** | `-32600` | `Origin` missing or non-loopback | Add `Origin: http://localhost` |
| **400** | — | `MCP-Protocol-Version` missing or unsupported | Send `2025-11-25` |
| **400** | `-32600` | `Mcp-Session-Id` missing on non-init request | Initialize first, reuse the SID |
| **400** | `-32600` | `Mcp-Session-Id` references an unknown / expired session | Re-initialize to get a fresh SID |
| **400** | `PARSE_ERROR` | Body is not valid JSON | Fix the body |
| — | `-32601` | Unknown JSON-RPC `method` | Check name vs `tools/list` |
| — | `-32602` | `params` violated tool's `inputSchema` | Inspect `data` for the violation |
| — | `-32603` | Internal error (tool threw) | Inspect `data`; raise to maintainers |
| **401** | — | Wrong / missing `Authorization` header | Add `Bearer <APOLLO_AUTH_TOKEN>` |
| **404** | — | DELETE on unknown session | Session already gone; ignore |
| **405** | — | GET `/mcp` without `Accept: text/event-stream` | Add the header |

---

## 11. Using the official Python SDK

```python
from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

async with streamablehttp_client("http://127.0.0.1:8765/mcp") as (read, write, _):
    async with ClientSession(read, write) as session:
        await session.initialize()
        tools = await session.list_tools()
        for t in tools.tools:
            print(t.name, t.description)
        # call one
        result = await session.call_tool("apollo_list_projects", {})
        print(result.structuredContent)
```

Same headers get added automatically by the SDK on init. The `Mcp-Session-Id` is
managed for you across the whole `async with` block.

---

## 12. CLI flags for operator setup

The server binary accepts:

```
apollo_mcp_server
  --host HOST              default 127.0.0.1; pass 0.0.0.0 for LAN
  --port PORT              default 8765
  --transport TRANSPORT    'http' (default) or 'stdio'
  --data-dir DIR           ApolloCore KB directory (default data)
  --embed-model PATH       GGUF embedding model path
  --llm-model PATH         GGUF LLM model path
  --ui-static-dir DIR      web UI assets (default: bundled)
  --no-ui                  disable the web UI at /
  --admin                  enable /omninet admin UI
  -v, --verbose            verbose logging
```

The packaged launcher is `./scripts/start.sh` (defaults: `--port 8765 --host 0.0.0.0`,
uses `--data-dir ./data` and the bundled model paths). Use `./scripts/ensure-running.sh`
when you don't know whether the server is already up — it probes `/health` first and
does nothing if so.

---

## 13. What to do when something goes wrong

1. **Check `/health`** — fastest sanity check.
   `curl http://127.0.0.1:8765/health` → 200 + JSON or refuse.
2. **Look at the log** — `apollo_mcp_server.log` in the project root.
3. **Run a smoke test** — `./scripts/ensure-running.sh` is idempotent;
   the test scripts under `tests/` (conformance, SSE, stdio, pagination, auth) are
   the canonical "is it actually OK" probes.
4. **Re-initialize** if you get `-32600` about sessions — your old SID may have
   been reaped, or you started against a different server.

---

## 14. Spec compliance cheat-sheet

This server targets the **2025-11-25** version of the MCP spec. Notable behavior:

- Bearer token auth per **RFC 6750** — that is the entire auth model.
  No OAuth, no PKCE, no flow. (Those are for third-party registration
  with remote servers, not local-use MCPs.)
- Origin validation per **MCP §6.2** (loopback-friendly: evil non-loopback origins are
  rejected, but empty/missing origins are allowed for CLI clients).
- Cursor pagination per **MCP §8.5** (base64-encoded offsets, page size 50).
- Standard JSON-RPC error codes: `-32700` parse error, `-32600` invalid request,
  `-32601` method not found, `-32602` invalid params, `-32603` internal error.
- `notifications/{tools,resources,prompts}/list_changed` plumbing is wired, but
  no code path emits these today (registries are static) — safe to ignore for now.
