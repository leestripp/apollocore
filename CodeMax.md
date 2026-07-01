# CodeMax.md — ApolloCore Project Context

> Machine-readable project context file. Read this first before any task.

---

## System Information

| Field | Value |
|-------|-------|
| Hostname | `dev-pc` |
| OS | Ubuntu 26.04 LTS (Resolute Raccoon) |
| Kernel | Linux 7.0.3-6.stable |
| Architecture | x86_64 |
| Container | Podman (`docker.io/library/ubuntu:26.04`, ID `30ba44506a6d003153c80023c4474e67d3487e9178df254bde55b19209cc8683`) |
| Display | Wayland, GNOME |
| Session | local, xterm-256color |
| User / UID | `lee` / 1000 |
| Date / Time | 2026-06-30 17:19:45 AEST (UTC+10) |
| Shell | `/usr/bin/bash` |
| Compiler | g++ 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| CMake | 4.2.3 |
| CPU cores | 12 |

---

# 🔨 CURRENT TASK — ApolloCore (MCP Server)

**Source of truth:** `./prompt.txt`
**Started:** 2026-06-30 (this session)
**Owner:** user `lee`

## Task Summary

ApolloCore **is** an MCP (Model Context Protocol) server. Period.

The deliverable is a single executable, `apollo_mcp_server`, that AI agents
connect to over HTTP. Internally the RAG / knowledge-graph code is still
organized in a static library target (`apollo_core`) — that's a CMake
implementation detail, not a public-facing product. There is no opt-in
flag, no alternative mode, no library for downstream consumers to link.

This refactor:

1. Exposes the RAG/KG capabilities over MCP via **OmniNet** (HTTP + WebSocket).
2. Implements **MCP 2025-11-25** (Streamable HTTP transport), with a forward path
   to the 2026-07-28 RC breaking changes (stateless core, `Mcp-Method` /
   `Mcp-Name` headers, removable session).
3. Applies **2026 MCP best practices** (outcome-oriented tool design, flat
   arguments, service-prefixed names, paginated, curated).

## Why MCP and Why Now

- Anthropic's open-standard protocol since 2024; ~1 year later there is a critical mass
  of MCP clients (Claude Desktop, Cursor, Windsurf, Cline, Zed, Continue.dev, etc.).
- ApolloCore's whole point is **giving an agent a knowledge base**. Refusing to speak
  MCP means consumers must write glue code — exactly the friction the project was
  refactored to remove (`commit 230a42e` removed MCP/OmniNet once to make it a static lib).
- **OmniNet** (`https://gitlab.com/leestripp/omninet`, by `leestripp`) provides
  production-grade HTTP+TLS+WebSocket+SQLite+session+admin-dashboard in C++23 — a
  perfect match for MCP's Streamable HTTP transport.

## Why the Right Spec to Target Is 2025-11-25 (Not 2026-07-28 RC)

The 2026-07-28 RC is a **breaking** change (stateless, no session, `Mcp-Method` headers,
Extensions, Tasks moved out of core). It is a **future** spec — final ships **2026-07-28**.
No client SDK is stable yet for it.

**Decision:** Implement **2025-11-25** now (Streamable HTTP, initialize handshake,
`Mcp-Session-Id` optional). Architect code so we can move to 2026-07-28 cleanly when
clients catch up (Phase 4 below). Track this in the implementation with a `MCP_VERSION`
define.

---

## Research Findings (carried in from web search)

### MCP Spec Versions
- **2025-03-26** — first stable; STDIO + Streamable HTTP (post-SSE).
- **2025-06-18** — minor; tools/resources/prompts refinements.
- **2025-11-25** — latest stable; Resources additions, schema updates, OAuth.
- **2026-07-28** — Release Candidate; **stateless**, `Mcp-Method`/`Mcp-Name` headers,
  Extensions framework (MCP Apps, Tasks), full JSON-Schema 2020-12.

### Transport (2025-11-25) — `modelcontextprotocol.io/specification/2025-11-25/basic/transports`
- **STDIO** — JSON-RPC 2.0 over `\n`-delimited stdin/stdout. Single-process. Best for local agents.
- **Streamable HTTP** — JSON-RPC 2.0 over a single `POST /mcp` endpoint.
  - Optional SSE response (`text/event-stream`) for streaming server-to-client pushes.
  - Optional session via `Mcp-Session-Id` header.
  - **Requirements satisfied by OmniNet's HTTP server** + a small SSE handler.

### Core JSON-RPC Methods (server side)
- `initialize` / `initialized` — handshake, exchange `protocolVersion` + capabilities
- `ping`
- `tools/list` — return registered tools (name + description + JSON-Schema for args)
- `tools/call` — invoke a tool, return `CallToolResult`
- `resources/list`, `resources/templates/list`, `resources/read`
- `prompts/list`, `prompts/get`
- `notifications/cancelled`, `notifications/progress`
- `logging/setLevel`

### Server→Client Notifications
- `notifications/tools/list_changed`, `notifications/resources/list_changed`,
  `notifications/prompts/list_changed` (the server must support these capabilities
  IF it lists the relevant feature)

### Best Practices (2026, distilled from multiple sources)
1. **Outcome-oriented tools, not 1:1 REST mapping** — `query_kb(q)` not
   `vector_search()` + `bm25_search()` + `graph_expand()`.
2. **Flat, typed arguments** — use `Literal` enums and top-level primitives, no
   nested config objects.
3. **Prompts and docstrings ARE context** — write them so the agent knows when to
   invoke the tool and what to expect back.
4. **Curate ruthlessly** — 5-15 tools per server; one server, one job.
5. **Service-prefixed names** — `apollo_query_kb`, `apollo_add_document` —
   so multiple MCP servers co-existing in one client don't collide.
6. **Errors inside the result, not exceptions** — return
   `CallToolResult { isError: true, content: [...] }` with a helpful message,
   so the LLM can self-correct.
7. **Paginate large results** — never return hundreds of records.
8. **Annotations** — mark tools `readOnly`, `destructive`, `idempotent`, `openWorld`.

### OmniNet (from project README — `https://gitlab.com/leestripp/omninet`)
- C++23 HTTP+WebSocket+TLS server, MIT.
- Public target: **`OmniNet`** (capitalized, case-sensitive in `target_link_libraries`).
- Brings its own: OpenSSL 3, SQLite3, optional zlib, optional sqlite-vec,
  `nlohmann/json v3.11.3` via FetchContent.
- API surface we will use:
  - `OmniNet::init()` / `OmniNet::cleanup()` — bootstrap
  - `OmniNet::Logger::instance()` — logging
  - `OmniNet::HTTP::Server` — HTTP server with `Config { address, port, workerCount, requestTimeout, corsEnabled }`
  - `server.post("/mcp", handler)`, `server.get("/mcp", handler)`, `server.get("/health", ...)`
  - `server.adminHandler().mount(server)` — built-in admin UI
  - `OmniNet::HTTP::StaticHandler` — static files (admin assets)
  - `OmniNet::HTTP::Request::bodyAsString()`, `header()`, `path()`, `method()`
  - `OmniNet::HTTP::Response::setStatus()`, `setContentType()`, `setHeader()`, `setBody()`
- **Important gotcha:** OmniNet's static lib is built on first configure via
  FetchContent → **first build takes ~5+ min** (OpenSSL, sqlite-vec, etc.).

---

## Architecture — ApolloCore (the only deliverable)

```
┌─────────────────────────────────────────────────────────────────────┐
│                      AI Agent (Claude, Cursor, ...)                 │
│                   speaks MCP JSON-RPC over HTTP                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  POST /mcp   (Streamable HTTP transport)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│           apollo_mcp_server  ←  THE ONLY DELIVERABLE                │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                 OmniNet HTTP+WS server                        │  │
│  │  Route table:                                                 │  │
│  │    POST /mcp        → JSON-RPC dispatcher                     │  │
│  │    GET  /mcp        → SSE stream (optional, 2025-11-25)       │  │
│  │    GET  /health     → JSON {status:"ok",version,tools}        │  │
│  │    GET  /info       → Server card-style info                   │  │
│  │    /*               → admin dashboard (OmniNet built-in)      │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  McpDispatcher (src/mcp_server/dispatcher.cpp)                 │  │
│  │    routes JSON-RPC methods → Tool/Resource/Prompt handlers    │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  ServerState (src/mcp_server/server_state.cpp)                │  │
│  │    one ApolloCore instance, session_id → project_id map       │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  Tools      (src/mcp_server/tools.cpp)        — 12 tools      │  │
│  │  Resources  (src/mcp_server/resources.cpp)    —  5 resources  │  │
│  │  Prompts     (src/mcp_server/prompts.cpp)      —  4 prompts    │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────────────┘
                          │  internal link (CMake target)
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│           apollo_core  (INTERNAL static library target)            │
│            apollo_persistence.hpp / ApolloCore class               │
│            RAG, hybrid search, KG, entity extraction                │
└─────────────────────────────────────────────────────────────────────┘
                          │  SQLite + FBIN + llama.cpp
                          ▼
                   existing ApolloCore storage
```

## MCP Surface Design — Outcome-Oriented Tools

### **TOOLS** (agent-callable functions, 12 total — curated)

| Tool name | Args (flat) | Maps to | Mutates? |
|-----------|-------------|---------|----------|
| `apollo_query_kb` | `query: str, mode: "vector"\|"hybrid"\|"rerank"\|"full", top_k: int = 5` | `query()`/`query_hybrid()`/`query_with_rerank()`/`query_full()` | no |
| `apollo_add_document` | `text: str, doc_id?: str, chunk_size: int = 512` | `add_document()` | yes |
| `apollo_extract_entities` | `text: str, chunk_id?: str` | `extract_entities()` | yes |
| `apollo_ingest_codebase` | `path: str, project: str` | `apollo_codebase_ingest()` + tree-sitter | yes |
| `apollo_search_symbols` | `name: str, kind: "FUNCTION"\|"CLASS"\|"NAMESPACE"\|"VARIABLE"\|"ALL" = "ALL", limit: int = 20` | vector search on graph | no |
| `apollo_get_neighbors` | `entity: str, hops: int = 1` | `get_neighbors()` | no |
| `apollo_create_kb` | `path: str, name: str = "Untitled"` | `create()` | yes |
| `apollo_open_kb` | `path: str` | `open()` | yes |
| `apollo_save_kb` | (no args) | `save()` | yes |
| `apollo_get_stats` | `project?: str` | `get_stats()` | no |
| `apollo_set_project` | `project: str` | `set_current_project()` | yes |
| `apollo_list_projects` | (no args) | `list_projects()` as JSON | no |

**Annotations** (per MCP `ToolAnnotations`):
- read-only: `apollo_query_kb`, `apollo_search_symbols`, `apollo_get_neighbors`,
  `apollo_get_stats`, `apollo_list_projects`
- destructive: `apollo_create_kb`, `apollo_open_kb` (replaces current KB), `apollo_set_project`
- idempotent: most reads
- openWorld: none

### **RESOURCES** (read-only data, 5)

| URI | Description |
|-----|-------------|
| `apollo://projects` | JSON list of all projects |
| `apollo://entities?project=&limit=` | All entities in current project (paginated) |
| `apollo://graph?project=` | Full knowledge-graph subgraph as JSON |
| `apollo://stats?project=` | Per-project statistics |
| `apollo://chunk/{chunk_id}` | Raw chunk content + metadata |

### **PROMPTS** (pre-built workflows, 4)

| Name | Args | Description |
|------|------|-------------|
| `summarize_kb` | `project?: str` | "Summarize what is in this KB" |
| `answer_with_citations` | `question: str` | "Answer the question using only KB content, cite chunks" |
| `code_review_summary` | `path: str` | "Walk me through this codebase's structure" |
| `entity_research` | `entity: str` | "Tell me everything we know about X" |

### JSON-RPC Error Handling

Per MCP: `isError: true` on the **result** (not a protocol-level error) lets the LLM
self-correct. We always return informative strings:

```jsonc
// success
{"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"..."}], "isError":false}}

// not found / wrong args → result with isError:true
{"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"No open knowledge base. Call apollo_open_kb first."}], "isError":true}}

// protocol violations → JSON-RPC error object (Parse error, Method not found, etc.)
{"jsonrpc":"2.0","id":3,"error":{"code":-32601,"message":"Method not found: tools/cal"}}
```

---

## Implementation Phases

### **Phase 0 — Repo prep** ✅ DONE
- `CodeMax.md` updated with the full plan
- Branch `main` (no branch switch — per project conventions)
- System info captured

### **Phase 1 — CMake glue + OmniNet FetchContent** ✅ DONE
- `CMakeLists.txt` updated: OmniNet declared via FetchContent (tag `main`),
  new `apollo_mcp_server` target wired into `src/mcp_server/` directly
  (no opt-in flag — the MCP server IS ApolloCore).
- `OMNINET_SKIP_FETCH` escape hatch added for power users.
- **Verified:** Configure succeeded; OmniNet 0.7.7, OpenSSL 3.5.5, sqlite-vec,
  tree-sitter all detected.

### **Phase 2 — MCP dispatcher skeleton** ✅ DONE
- `jsonrpc.{hpp,cpp}` — JSON-RPC 2.0 envelope parser/serializer
- `dispatcher.{hpp,cpp}` — method router for `initialize` / `ping` /
  `tools/list` / `tools/call` / `resources/list` / `resources/read` /
  `prompts/list` / `prompts/get` / `notifications/initialized`
- `server_state.{hpp,cpp}` — owns ApolloCore, manages sessions

### **Phase 3 — ApolloCore wiring + Tools/Resources/Prompts implementation** ✅ DONE
- `tools.{hpp,cpp}` — 12 service-prefixed tools
  - `apollo_query_kb` (vector/hybrid/rerank/full modes)
  - `apollo_add_document`
  - `apollo_extract_entities`
  - `apollo_ingest_codebase` (with inline `ingest_directory_local` helper since
    the upstream `apollocore_v2::ingest_directory` is declared but unimplemented)
  - `apollo_search_symbols` (vector-search approximation)
  - `apollo_get_neighbors` (graph-traversal approximation)
  - `apollo_create_kb` / `apollo_open_kb` / `apollo_save_kb`
  - `apollo_get_stats` / `apollo_set_project` / `apollo_list_projects`
- `resources.{hpp,cpp}` — 5 URI-addressed resources
- `prompts.{hpp,cpp}` — 4 pre-built workflows
- JSON Schema 2020-12 for every tool input

### **Phase 4 — HTTP server (OmniNet) + Streamable HTTP transport** ✅ DONE
- `http_server.{hpp,cpp}` — OmniNet configuration, route registration,
  POST /mcp JSON-RPC dispatcher, GET /mcp SSE stub, GET /health, GET /info
- `main.cpp` — CLI args, environment variables, OmniNet bootstrap, model loading
- **Verified end-to-end:**
  - `POST /mcp initialize` returns protocolVersion=2025-11-25 with capabilities
  - `POST /mcp tools/list` returns 12 tools with full schemas + annotations
  - `POST /mcp tools/call apollo_create_kb` creates a real KB on disk
  - `POST /mcp tools/call apollo_add_document` chunks and embeds the doc
  - `POST /mcp tools/call apollo_query_kb` returns hybrid-ranked results
  - `POST /mcp resources/read apollo://stats` returns 2 chunks / 1 document
  - `POST /mcp ping` and `POST /mcp prompts/list` work
  - `GET /health` and `GET /info` return diagnostic JSON
- 56 MB executable (`build/src/mcp_server/apollo_mcp_server`)

### **Phase 5 — Tests, docs, polish** ✅ DONE
- **Tests added:** `tests/test_mcp_dispatcher.cpp` (10 dispatcher-level tests
  covering initialize, tools/list, resources/list, prompts/list, ping,
  unknown method → -32601, end-to-end create_kb + stats, unknown tool,
  missing required arg, notification suppression). `tests/test_mcp_tools.cpp`
  (6 tool/resource/prompt integration tests using real ApolloCore KB
  persistence in tmpdir).
- Both MCP tests registered with CTest via `add_mcp_test()` helper in
  `tests/CMakeLists.txt`. They compile the MCP sources directly into the test
  executable (no HTTP needed).
- **Full test suite:** **16/16 CTest targets pass** (2 new MCP tests +
  14 pre-existing tests, all green).
- **README updated:** New top-level "🤖 MCP Server Mode (NEW)" section
  covering build, run, tools/resources/prompts catalog, sample curl probes,
  Claude Desktop / Cursor / Cline config snippets, spec-versioning note.
- **`docs/mcp-quickstart.md` added:** 10-step worked session from
  `cmake --build` to Claude Desktop config, with copy-paste curl commands
  and example responses for every major endpoint.

### **Phase 6 (future) — 2026-07-28 migration prep** 🔲 FUTURE
- Drop `initialize` handshake → add `_meta` per-request
- Drop `Mcp-Session-Id` → use explicit `session_id` argument in tools
- Add `Mcp-Method` and `Mcp-Name` HTTP headers
- Adopt `Mcp-Protocol-Version` header
- Move long-running ops (ingest) to Tasks extension

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| OmniNet `0.5.2` tag doesn't exist or stale | medium | medium | Use `main` branch initially; pin later. Track in README. |
| OmniNet firewall tool fails to build | certain (per skill) | none | Per skill: ignore it; build `--target apollo_mcp_server`. |
| First OmniNet build is 5+ min | certain | minor | Document in README. |
| llama.cpp + OmniNet both pull heavy deps | likely | long build | Keep them parallel; ninja+ccache recommended. |
| Need to update 13 existing test suites | low | none | We're a NEW target — `apollo_core` static lib is unchanged. |
| MCP spec changes before 2026-07-28 | low | low | Version-gate the dispatcher: `#define MCP_VERSION "2025-11-25"`. |

## Open Decisions

- [x] MCP spec version to target first → **2025-11-25**
- [ ] Default port → **8765** (proposed; `8765` doesn't collide with common web ports)
- [ ] Auth: OAuth? API key? Anonymous for v1? → **Anonymous for v1; localhost-only default**; admin via OmniNet's built-in auth.
- [ ] Tool subset: 12 tools listed above; anything missing? → **review after Phase 3 wiring**
- [ ] SSE: implement or skip for v1? → **skip for v1; defer to Phase 6**

