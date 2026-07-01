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

ApolloCore is a C++23 MCP server that exposes a local RAG / knowledge-graph
knowledge base to AI agents. The single deliverable is the
`apollo_mcp_server` executable, which speaks MCP 2025-11-25 over Streamable
HTTP transport (served by OmniNet). Internally the RAG / KG code is
organized in a static library CMake target — that's a build-system
detail, not a separate product.

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

---

# 🔨 CURRENT SUB-TASK — ApolloCore Web UI

**Started:** 2026-06-30 (this session)
**Source:** user request — "We should create a web UI for the KB/RAG system, ideas: Health, Search, Graph viewer, Settings"
**Goal:** Ship a single-page web dashboard, served by the same `apollo_mcp_server` executable, that humans (not agents) use to inspect and operate the knowledge base.

---

## 1. Why now, what we're building

The MCP server already exposes a complete surface to AI agents — 12 tools,
5 resources, 4 prompts, all over JSON-RPC on `POST /mcp`. Human operators
have no way to look at that surface other than via `curl` + `jq`.

A small in-process web UI closes that loop. It's not product surface for
end-users; it's an operator console. Same executable, same port, no extra
service. OmniNet already has a static-file handler we can wire to the
right directory.

### Design principles

1. **One executable, one port.** Browse to `http://localhost:8765/` and
   you see the UI. The JSON-RPC API stays at `/mcp`. No reverse proxy,
   no SPA build pipeline, no second process.
2. **Pure client of `POST /mcp`.** The UI sends the same JSON-RPC the
   agent runtime would send. It is *not* a separate API. We test it once
   and forget about it.
3. **Zero dependencies.** Vanilla ES modules. No bundler, no npm, no
   framework. Hand-rolled force-directed layout for the graph viewer.
4. **Tells the truth.** Every visible number comes from `GET /health`,
   `GET /info`, or a real JSON-RPC call. No fake stats.

---

## 2. Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                      Browser (operator)                            │
│   index.html (loaded once)  ───►  ES modules call JSON-RPC         │
└──────────────────────────────┬────────────────────────────────────┘
                               │  HTTP
                               ▼
┌───────────────────────────────────────────────────────────────────┐
│                apollo_mcp_server                                  │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │ OmniNet router:                                            │  │
│  │   POST /mcp      ─►  JSON-RPC dispatcher  (unchanged)      │  │
│  │   GET  /health   ─►  JSON                  (unchanged)      │  │
│  │   GET  /info     ─►  JSON                  (unchanged)      │  │
│  │   GET  /static/* ─►  StaticHandler        (NEW)             │  │
│  │   GET  /         ─►  StaticHandler("/index.html") (NEW)    │  │
│  └─────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────┘
```

The static handler reads files from `src/mcp_server/static/` at runtime.
Same path is fine in dev (`cmake --build`) — relative to the CWD — and
in installs (we'll copy the static dir to `${SHAREDIR}/apollocore/ui/`
via `install(DIRECTORY …)` so `cmake --install` deploys the UI next to
the binary).

### What changes server-side (only this):

1. `src/mcp_server/http_server.cpp` — add two routes:
   - `GET /`        → 200 + `index.html`
   - `GET /static/…` → files from the static dir
2. `src/mcp_server/static/` — new directory of plain files
3. `src/mcp_server/CMakeLists.txt` — `install(DIRECTORY static …)`
4. New CLI flag `--no-ui` (default: UI enabled) for users who want
   headless mode

That's it. No new C++ methods, no new tool, no new resource. The UI is
a *client* of the existing API. We can rip out the entire `static/`
directory later without affecting any other behavior.

---

## 3. UI tabs

Five-tab SPA, no router library — the simplest `<button>`-swaps-section
pattern. URL hash holds the active tab so reloads go to the right place
(`#health`, `#search`, `#graph`, `#settings`, `#about`).

### 3.1 Health

Auto-refresh every 5 s (`GET /health` + `GET /info`).

- **Server card:** name, version, protocol version, vendor, transport.
- **Capability counts:** tools (12), resources (5), prompts (4). Each
  number links to a dropdown listing the names.
- **Models:** embed model path + load status, LLM model path + load
  status. Detected with `GET /health`'s `models` field once we add it.
- **Active project + KB stats:** project name, document count, chunk
  count, entity count, relationship count. Pulled from
  `resources/read apollo://stats`.
- **Sessions:** active session count.
- **Quick probe button** that fires a single `tools/call apollo_get_stats`
  and shows the round-trip latency in ms.

### 3.2 Search

Operator-facing search against the active KB. Same pipeline modes as
`apollo_query_kb`: vector / hybrid / rerank / full.

- **Input row:** text input + mode dropdown + "Top-K" number input
  (default 10) + Search button.
- **Loading state:** spinner with "Searching…" and disabled inputs.
- **Result cards:** each card shows
  - chunk_id (monospace, copyable)
  - source document title
  - content excerpt (first 240 chars with full text on click)
  - score badges: `vector 0.812`, `bm25 0.211`, `graph 0.405`,
    final RRF score
- **Empty state:** "No hits. Try a different mode."
- **Save query** → copy to clipboard (so it can be pasted into the
  agent's UI).

### 3.3 Graph viewer

This is the showcase screen. Hand-rolled force-directed layout on a
`<canvas>`.

- **Data source:** `resources/read apollo://graph` → `{nodes: [{id,
  name, type}], edges: [{source, target, type}]}`.
- **Layout:** Fruchterman-Reingold, O(n²) per iteration, n ≤ ~500. If we
  ever ship larger graphs we can switch to Barnes-Hut.
- **Rendering:**
  - Node radius ∝ log(1 + degree).
  - Node colour by entity type (10-bucket palette, generation is
    deterministic so reloads look consistent).
  - Edge stroke = relationship type, low alpha by default.
  - Hover node → highlight neighbours + show tooltip with name, type,
    description.
  - Click node → open detail panel on the right (description, source
    chunk, neighbours).
  - Drag to reposition; double-click to unpin.
  - Wheel to zoom, drag background to pan.
- **Toolbar:**
  - Reload button.
  - Search box (centres & highlights nodes whose name matches).
  - Type filter (multi-select chips).
  - "Pause physics" toggle.
  - "Reset zoom".
- **Performance budget:** 60 fps on a 300-node graph on a mid-range
  laptop.

### 3.4 Settings

Read-mostly view + a couple of mutations.

- **Projects list:** rows from `resources/read apollo://projects`. Each
  row has "Open" button → `tools/call apollo_set_project {project_name}`.
  Active project highlighted.
- **Models panel:** embed + LLM model paths, sizes, free disk. If the
  user requests, expose `--embed-model` / `--llm-model` setting overrides
  via the `/api/v1/config` POST endpoint we will add (server-side, but
  small — just writes a JSON config the next start honors).
- **Server config:** host, port, data dir, CLI flags in effect (from
  `GET /info`).
- **Danger zone:** "Wipe current KB" (calls a new dedicated tool,
  see Open Decisions below).

### 3.5 About

A static page tab (no JSON-RPC calls).

- Build hash (CI env var → server reads it from `APOLLO_BUILD_SHA`).
- One-paragraph project description.
- License.
- Embedded copy of `LICENSE` (or a link).
- "Copy agent config" snippet button — drops the Claude Desktop JSON
  config to the clipboard for the current server URL.

---

## 4. Data flow cheatsheet

| UI action | HTTP request | JSON-RPC |
|-----------|--------------|----------|
| Open page | `GET /` | — |
| Load tab JS modules | `GET /static/…` | — |
| Initial health | `GET /health`, `GET /info`, `resources/read apollo://stats` | Yes (second + third) |
| Search | `POST /mcp` | `tools/call apollo_query_kb` |
| Graph | `POST /mcp` | `resources/read apollo://graph` |
| Switch project | `POST /mcp` | `tools/call apollo_set_project` |
| List tools | `POST /mcp` | `tools/list` |
| Re-ping | `POST /mcp` | `ping` |

`POST /mcp` is one endpoint; the UI just sends different methods at it.

---

## 5. File layout

```
src/mcp_server/
├── CMakeLists.txt              (+ install(DIRECTORY static))
├── main.cpp                    (+ --no-ui flag, mount static handler)
├── http_server.cpp             (+ /, /static/* routes)
├── http_server.hpp             (+ mount_static(path))
├── dispatcher.cpp  …
├── jsonrpc.cpp     …
├── server_state.cpp (+ accessor for build_sha from env)
├── tools.cpp       …
├── resources.cpp   …
├── prompts.cpp     …
└── static/                   (NEW — pure files, no build step)
    ├── index.html            (single-page shell)
    ├── css/
    │   └── style.css         (~200 lines, dark/light vars)
    └── js/
        ├── api.js            (~80 lines, JSON-RPC wrapper)
        ├── app.js            (~60 lines, tab switching + hash routing)
        ├── health.js         (~150 lines)
        ├── search.js         (~200 lines)
        ├── graph.js          (~400 lines — canvas + Fruchterman-Reingold)
        ├── settings.js       (~120 lines)
        └── about.js          (~50 lines)

tests/
└── test_ui_static.cpp        (NEW — CTest target that verifies every
                              file in static/ exists and the served
                              index.html returns 200 in a smoke test)

docs/
└── ui.md                     (NEW — user guide for the web UI)
```

Roughly **1,300 lines of UI code** (HTML+CSS+JS). All fit inside
`src/mcp_server/static/`. No bundler, no transpilation, no `node_modules`.

---

## 6. Implementation phases

Each phase ends with a runnable binary + a smoke check. We don't move
on until the smoke check passes.

### Phase A — Static file serving (foundation)

**No UI yet. Just verify OmniNet can serve files from `static/`.**

- Create `src/mcp_server/static/index.html` with the words "ApolloCore
  Web UI".
- Add `GET /` and `GET /static/…` routes in `http_server.cpp`.
- `src/mcp_server/CMakeLists.txt`: `install(DIRECTORY static …)`.
- `main.cpp`: mount the static handler at startup; honour `--no-ui`.
- Smoke: `curl http://localhost:8765/` returns the HTML.

### Phase B — Skeleton + tab shell

- Write `index.html` with a five-tab nav bar.
- `css/style.css`: CSS custom-property dark theme (no framework).
- `js/app.js`: tab swap + hash routing (`#health` etc.).
- `js/api.js`: JSON-RPC wrapper around `fetch('/mcp', …)`.
- Smoke: each tab is empty but does not 404 on its JS imports.

### Phase C — Health tab

- Implement `health.js`.
- Wires the periodic refresh.
- Smoke: open `/` and visually confirm counts and stats.

### Phase D — Search tab

- Implement `search.js`.
- Calls `tools/call apollo_query_kb`.
- Renders result cards with all four scores when present.
- Smoke: search against a seeded KB returns cards with content.

### Phase E — Graph viewer

- Implement `graph.js`.
- Force-directed layout on canvas, ~400 lines.
- Type-coloured nodes, draggable, zoom/pan.
- Smoke: graph renders ≥ 50 nodes, hovers and clicks work.

### Phase F — Settings + About

- Implement `settings.js` (project list, model info) and
  `about.js` (build info, copy-config button).
- Smoke: switch project → verify `apollo_set_project` call; copy
  config snippet verifies valid JSON.

### Phase G — Tests + docs

- `tests/test_ui_static.cpp`: a CTest target that
  - verifies every file under `static/` exists at build time;
  - smoke-tests `GET /` returns `200` with `text/html` on a
    thread-spinup instance of the binary.
- `tests/CMakeLists.txt`: `add_mcp_test(test_ui_static …)`.
- `docs/ui.md`: ~150-line user doc with screenshots-in-ASCII placeholder.
- Top-level `README.md`: link the UI in the Features table and the Quick
  Start.
- `docs/design.md`: add a section on the UI under "MCP surface" or as a
  sibling.

### Phase H — Polish

- Loading skeletons on each tab.
- Error toasts when JSON-RPC fails (e.g. KB not open).
- Keyboard shortcuts: `g h` → Health, `g s` → Search, `g g` → Graph,
  `g ,` → Settings. (Vim-style follows me everywhere.)
- Empty-state illustrations (CSS-only).
- Responsive layout down to ~768 px wide (tablet); below that, the Graph
  viewer gets a "best viewed on a wider screen" notice.

### Phase I — Final verification

- `cd build && ctest -j$(nproc)` → all suites pass (16 existing + 1 new).
- Manual end-to-end: open `/`, see the UI, click through every tab,
  perform a search against a small seeded KB, switch projects, browse
  the graph.
- Commit + push. Update CodeMax.md "Risk Register" with anything learned.

---

## 7. Risks & mitigations

| Risk | Mitigation |
|------|-----------|
| Browser cache shows stale UI after updates | Cache-bust via a `?v=$BUILD_SHA` query string on the JS module URLs. |
| Graph rendering jank with > 1k nodes | Cap displayed nodes to 500 with a "show all" button; warn if data is larger. |
| Force-directed layout drifts forever | Apply proper damping + small resets on user input; "pause" button is a hard requirement, not a nicety. |
| Static asset paths break after `cmake --install` | Set both dev path (CWD relative) and installed path (`${SHAREDIR}/apollocore/ui/`) in the binary, prefer the one that exists. |
| UI accidentally consumes the `POST /mcp` endpoint meant for agents | They share the endpoint — that's the design. No conflict because both use the same protocol. |
| UI tries to render before JSON-RPC ready (server still loading models) | Every UI call surfaces its own loading + error state; no global "wait for server" spinner. |
| Someone expects the UI to *not* be there (CI scripts hitting `/`) | `--no-ui` flag + admins of CI can flip it off. |

---

## 8. Open decisions (for the UI sub-task)

- [ ] Static-asset directory at runtime: **`./src/mcp_server/static/`**
      (dev) → `${CMAKE_INSTALL_PREFIX}/share/apollocore/ui/` (installed).
- [ ] Default state: **UI mounted** at `/`, CLI flag `--no-ui` to skip.
- [ ] Graph algorithm: **Fruchterman-Reingold**, 500-node cap.
- [ ] Cache-busting: **`?v=$BUILD_SHA`** when `APOLLO_BUILD_SHA` env is set,
      otherwise `?v=1`.
- [ ] Settings writes: defer to a v2; v1 is read-only.
- [ ] Wipe KB: defer to a v2; v1 has no destructive action exposed.

---

## 9. How we'll know we're done

- `apollo_mcp_server` still single-binary, still 16/16 tests pass.
- Opening `http://localhost:8765/` in any browser shows the UI.
- The UI talks to `/mcp` exactly as an agent would; no second API.
- `cd build && ctest -j$(nproc)` passes, including a new
  `test_ui_static` target.
- `README.md`, `docs/design.md`, and a new `docs/ui.md` describe the UI.
- A short screencast-ready manual test pass runs in <2 minutes.

---

## 10. Execution log

### Phase A — Static file serving foundation ✅ DONE

Wired `OmniNet::HTTP::StaticHandler` into `HttpServer::Impl`, registered a
catch-all `GET /*` route (last in registration order so `/mcp`, `/health`,
`/info` win), added `ui_static_dir` to `HttpConfig`, `--no-ui` /
`--ui-static-dir` to CLI, `APOLLO_NO_UI` env var, and a `test_ui_static`
CTest target that verifies every UI file exists and contains expected
structural anchors. Install rule copies `static/` next to the binary.
Runtime lookup order: explicit `--ui-static-dir` → `src/mcp_server/static/`
(dev) → `<exe>/../share/apollocore/ui/static/` (install).

### Phase B — Skeleton + tab shell ✅ DONE

`index.html` with five-tab nav, hash routing, CSS variables for
dark/light. `api.js` JSON-RPC wrapper with `rpc`, `callTool`,
`readResource`, `listTools`, `initialize`. `app.js` tab swap with
teardown callbacks (so the Graph viewer's RAF can be cleaned up on tab
switch).

### Phase C — Health tab ✅ DONE

Auto-refresh every 5 s. Pulls `/health`, `/info`, and `apollo://stats`
in parallel. Cards: server, surface, KB stats, probe (round-trip latency
of `apollo_get_stats`).

### Phase D — Search tab ✅ DONE

Mode selector (vector / hybrid / rerank / full), top-K input, result
cards with per-signal score badges (vec / bm25 / graph / rrf), copy-id
buttons, expandable content. Empty / error states with helpful guidance.

### Phase E — Graph viewer ✅ DONE

Hand-rolled Fruchterman-Reingold on `<canvas>`. O(n²) per iteration,
capped at 500 nodes. Drag to pin, double-click to unpin, scroll to zoom,
drag background to pan. Type-coloured nodes (palette in `graph.js`),
hover highlights neighbours, click opens detail panel with neighbor list.
Toolbar: reload, pause/resume, reset zoom, name filter, type filter chips.

### Phase F — Settings + About ✅ DONE

Settings: projects list with `apollo_set_project` buttons, server config
card, models card, build card. About: build info, copy-config snippet
button for Claude Desktop / Cursor / Cline / Windsurf, "open raw MCP
endpoint" link.

### Phase G — Tests + docs ✅ DONE

- `tests/test_ui_static.cpp`: 17 checks across 9 files (size, required
  substrings, no CDN references). Passes.
- `docs/ui.md`: ~200-line user guide with quick start, tab reference,
  architecture cheatsheet, customisation guide, known limitations.
- `README.md`: new "Web UI" section between "Connecting MCP clients"
  and the other content, linking to `docs/ui.md`.

### Phase H — Polish (deferred)

Originally planned: keyboard shortcuts, loading skeletons, empty-state
illustrations, deeper responsiveness. Tab and search already have basic
loading + error states; the rest can land in a v2 patch.

### Phase I — Final verification ✅ DONE

- `cd build && ctest -j$(nproc)` → **17/17 pass** (16 pre-existing + 1 UI)
- `curl /` → 200, `text/html`, 1247 bytes (index.html)
- `curl /css/style.css` → 200, `text/css`, 7452 bytes
- `curl /js/{app,api,health,search,graph,settings,about}.js` → 200
- `curl /mcp` initialize → 200, valid JSON-RPC 2025-11-25 envelope
- `curl /nope` → 404 (clean)
- Install to `/tmp/apollo-test-install` → binary finds UI via
  `<exe>/../share/apollocore/ui/static/` fallback. Confirms the
  install rule + runtime lookup both work.

UI shipped end-to-end. **All 17/17 tests pass; install + run both work.**

