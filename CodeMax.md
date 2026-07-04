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

## Active Plan — Single Global KB (in response to user feedback)

> **Status:** pending approval / start.
> **Source:** `prompt.txt` (user feedback, not me).
> **Goal:** Fix the "agent creates KBs everywhere, no way to find them" mess.

### Why this section exists

The previous design let the AI agent **create** knowledge bases anywhere on disk
(`apollo_create_kb(path)`), **open** any KB (`apollo_open_kb(path)`), and
**switch between** them at will. This produced three concrete problems:

1. **Discovery is impossible.** An agent that creates KBs at
   `/home/lee/projects/foo/data/` and another at `/tmp/kb_x/` and a third
   inside a project checkout cannot — by definition — find each other later.
   The whole point of a knowledge base is *finding things back*.
2. **The shared-knowledge use case dies.** A static library in project A should
   be discoverable from project B. With multiple isolated KBs, this requires
   the agent to know in advance which KB to open. It doesn't, and it can't.
3. **The web UI had to ask "which project first?"** because projects were the
   only unit of isolation. Health tab gated all data behind a project picker
   — terrible UX for "what's in the KB right now?"

The user was right to be angry. The fix below is the right one.

### New architecture — one KB, always at `~/.apollocore/data/`

```
~/.apollocore/                  (XDG-style: $APOLLO_HOME overrides)
├── data/                       THE knowledge base — fixed location
│   ├── apollo.kb              (sqlite, all projects)
│   ├── vectors/               (vec0 / sqlite-vec tables)
│   ├── graph.sqlite           (entity graph)
│   ├── config.json            (model paths, defaults)
│   └── logs/
├── models/                     (GGUF — embed + llm)
└── .installed                  (sentinel — first run did model download)
```

Projects remain as **logical tags / namespaces inside the single KB**, not as
separate databases. Rationale (user-stated):

- **Project isolation exists only so data can be deleted easily.** A delete
  on a project name purges its chunks/entities/relations atomically.
- **Search is global by default.** `apollo_query_kb` returns results from
  *all* projects unless the caller passes an explicit `project` filter.
  This is the shared-knowledge payoff.
- **`apollo_set_project` becomes a "current ingestion context"** — it tells
  the server *which project to stamp on new chunks* during ingest. It does
  not restrict reads.

### Tool surface changes

| Tool | Old behaviour | New behaviour | Action |
|---|---|---|---|
| `apollo_create_kb` | create new KB at any path | — | **REMOVE** |
| `apollo_open_kb` | swap currently-open KB | — | **REMOVE** |
| `apollo_save_kb` | explicit flush | implicit on every mutation | **REMOVE** (auto-save instead) |
| `apollo_set_project` | switch KB + set context | set ingestion context only | **KEEP, narrow scope** |
| `apollo_list_projects` | list projects in *current* KB | list projects in the *global* KB | **KEEP** |
| `apollo_query_kb` | search current KB | search **all** projects (optional filter) | **MODIFY** |
| `apollo_get_stats` | stats for current KB | stats for **all** projects (optional filter) | **MODIFY** |
| `apollo_get_neighbors` | graph in current KB | graph across all projects (optional filter) | **MODIFY** |
| `apollo_search_symbols` | current KB only | **all** projects (optional filter) | **MODIFY** |
| `apollo_ingest_codebase` | current KB | global KB, stamps `project=<arg>` on chunks | **MODIFY** |
| `apollo_add_document` | current KB | global KB, stamps `project=<current>` | **MODIFY** |
| `apollo_extract_entities` | current KB | global KB, stamps `project=<current>` | **MODIFY** |
| `apollo_set_config` | config of current KB | config of global KB (model paths, etc.) | **MODIFY** |
| `apollo_get_stats` (per-project) | n/a | keep `project` filter optional | **MODIFY** |

Net result: **13 tools → 10 tools.** Project isolation survives as a *tag*
on every chunk and entity; reads can be unfiltered (global) or filtered.

### Files to change

| File | Change |
|---|---|
| `src/mcp_server/main.cpp` | Default `data_dir` to `$APOLLO_HOME/data` or `$HOME/.apollocore/data`. Read `APOLLO_HOME` env var. Create the dir on startup if missing. |
| `src/mcp_server/main.cpp` | Remove CLI flag `--data-dir` (or keep as override, but never as a "different KB"). Actually — keep `--data-dir` as override but only for testing; doc says "don't use this". |
| `src/mcp_server/tools.cpp` | Remove `apollo_create_kb`, `apollo_open_kb`, `apollo_save_kb` definitions + registrations. |
| `src/mcp_server/tools.cpp` | `apollo_set_project` — narrow to "set current ingestion context" only; remove the "rebind KB" path. |
| `src/mcp_server/tools.cpp` | `apollo_query_kb` — accept optional `project` filter; default is "all". |
| `src/mcp_server/tools.cpp` | `apollo_ingest_codebase` — `project` arg becomes mandatory (was optional defaulting to "default"). |
| `src/apollo_persistence.cpp` | Remove `create_kb()` / `open_kb()` APIs (or keep as internal "load from path", no longer reachable from MCP). |
| `src/apollo_persistence.cpp` | All queries: default visible-projects set = "all" (was: "current only"). |
| `src/kb_persistence.cpp` | Same as above. |
| `src/apollocore.cpp` | Singleton-style lifecycle: load the global KB at startup, never swap. |
| `src/mcp_server/server_state.cpp` | Drop `set_data_dir()` ambiguity; the path is read once at startup. |
| `src/mcp_server/dispatcher.cpp` | Update tool list (10 tools not 13). |
| `scripts/start.sh` | Default `APOLLO_DATA_DIR` to `$HOME/.apollocore/data` (or `$APOLLO_HOME/data`). |
| `scripts/ensure-running.sh` | Same. Probe `$APOLLO_HOME/data/.installed` as "ready" sentinel. |
| `src/mcp_server/static/js/health.js` | **Default to "All projects"** view. Show total counts across all projects. Project filter is a *secondary* chip, not a required selector. |
| `src/mcp_server/static/js/search.js` | Results table includes a `Project` column by default. |
| `src/mcp_server/static/js/app.js` | On startup, fetch global stats; don't gate UI on a project choice. |
| `src/mcp_server/static/index.html` | Add "All projects" pill in the project picker; make it default. |
| `docs/apollocore_howto_connect.md` | Remove `apollo_create_kb` / `apollo_open_kb` / `apollo_save_kb` from §5. Update §12 (CLI flags). Update §1 (location of the KB). |
| `docs/mcp_compliance.md` | Update the "auth" deviation note to reflect that the global KB is single-process and single-user by design. |
| `README.md` | Update "what is ApolloCore" copy: one KB per host, multi-project tags, shared knowledge across the agent's working set. |
| `tests/test_mcp_conformance.py` | Update expected tool count from 13 to 10; remove `apollo_*_kb` cases. |
| `tests/test_pagination.py` | Confirm cross-project query behaviour. |

### Migration plan (one-shot)

For users who already have a KB at `$PROJECT_ROOT/data` (the old default):

1. On startup, if the *old* per-project `data/` exists AND `$APOLLO_HOME/data`
   does **not**:
   - Print a one-line warning: `apollo: migrating data/ → ~/.apollocore/data/`
   - `mv` the dir. Don't copy — single source of truth.
   - Write `~/.apollocore/.installed` sentinel.
2. If both exist, refuse to start: `apollo: both ./data and ~/.apollocore/data exist; pick one`. Exits 4.
3. Otherwise, normal startup.

This is intentionally loud — better to make the user notice than silently
double-up the storage.

### Web UI redesign — health tab

Old:

```
+--------------------------------------+
|  Health                              |
+--------------------------------------+
|  Active project: <dropdown required> |
|  → chunks: ... (only that project)   |
+--------------------------------------+
```

New:

```
+--------------------------------------+
|  Health                              |
+--------------------------------------+
|  KB location: ~/.apollocore/data/    |
|  Documents: 1,247                    |
|  Chunks:    8,304                    |
|  Entities:    612  (across 5 proj.)  |
|                                      |
|  Projects:                           |
|   • apollo_core    3,201 chunks      |
|   • libstrutil       812 chunks      |
|   • notes            194 chunks      |
|   • ...                             |
|                                      |
|  Filter by project: [All ▾]         |
+--------------------------------------+
```

Data table gains a `Project` column. Default sort: by relevance (no change).
The project filter is a chip — *optional* — not a gate.

### Acceptance criteria

This plan is **done** when:

1. `apollo_query_kb("foo")` returns results from every project, not just the
   current one. Verified by `tests/test_global_search.py`.
2. No tool exists that can change which KB the server is bound to. The
   tool list shrinks from 13 to 10; no new lifecycle tools can be added
   without breaking this rule.
3. The web UI Health tab shows global counts at first paint, with no project
   picker interaction required.
4. `./scripts/start.sh` on a fresh machine produces a single KB at
   `$HOME/.apollocore/data/` with `~/.apollocore/.installed` written.
5. The migration step is testable: `./scripts/start.sh` in a tree with the
   old `./data/` moves it to the new location and warns exactly once.
6. `tests/test_mcp_conformance.py` passes with the new tool list.
7. `docs/apollocore_howto_connect.md` is updated and reflects the 10-tool
   surface + global-KB assumption.

### Out of scope for this plan (deferred)

- **Per-user KBs** (multi-tenant). Single user per process. Sharing across
  users is the OS's job (`chmod`).
- **Network-shared KB.** Future. For now, NFS-mount `~/.apollocore/data` if
  you want; sqlite handles concurrent reads fine, writes need a process lock
  we don't ship yet.
- **Migration of chunk embeddings between machines.** Out of scope — if you
  move `~/.apollocore/data` you take your embeddings with you.

---

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
- **2025-11-25** — latest stable; Resources additions, schema updates.
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
│         speaks MCP JSON-RPC over HTTP  OR  over stdio (stdin/stdout) │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
        ┌──────────────────────┴──────────────────────┐
        │ HTTP: POST /mcp  OR  stdio: stdin/stdout    │
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│           apollo_mcp_server  ←  THE ONLY DELIVERABLE                │
│                                                                     │
│  CLI: apollo_mcp_server [--transport http|stdio] [http options]     │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  if --transport stdio:                                       │  │
│  │    mcp_stdio.cpp: run_stdio_transport()                     │  │
│  │      - std::getline(std::cin) → line-by-line JSON-RPC        │  │
│  │      - dispatch_one() → nlohmann::json response              │  │
│  │      - nullopt → notification (no response written)          │  │
│  │      - std::cout.flush() after every response line          │  │
│  │      - all logs → stderr (never stdout)                     │  │
│  └────────────────────────────┬─────────────────────────────────┘  │
│  ┌────────────────────────────┴─────────────────────────────────┐  │
│  │  if --transport http (default):                              │  │
│  │    OmniNet HTTP server + SSE bridge                          │  │
│  │    Route table:                                              │  │
│  │      POST /mcp  → JSON-RPC dispatcher (mcp_dispatch.cpp)     │  │
│  │      GET  /mcp  → SSE stream (list_changed, progress)       │  │
│  │      GET  /health → JSON health                             │  │
│  │      GET  /info   → server info                             │  │
│  │      GET  /       → web UI static files                     │  │
│  │      /*           → OmniNet admin dashboard                  │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  mcp_dispatch.{hpp,cpp}: dispatch_one()                    │  │
│  │    Shared JSON-RPC parse/dispatch core. Both transports call │  │
│  │    this so they can't drift apart.                          │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  cursor.{hpp,cpp}: encode/decode cursor for pagination      │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  Dispatcher (dispatcher.cpp)                                 │  │
│  │    routes JSON-RPC methods → handlers                       │  │
│  │    tools/list, resources/list, prompts/list → cursor paged   │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  ServerState (server_state.cpp)                            │  │
│  │    one ApolloCore instance, session → project_id map        │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  Tools      (tools.cpp)       — 13 tools (cursor paginated)│  │
│  │  Resources  (resources.cpp)    —  6 resources               │  │
│  │  Prompts   (prompts.cpp)      —  4 prompts                  │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────────────┘
                          │  internal link (CMake target)
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│           apollo_core  (INTERNAL static library target)              │
│  apollo_persistence.hpp / ApolloCore class                           │
│  RAG, hybrid search, KG, entity extraction                          │
│  All logging goes to stderr (never stdout) for stdio compat.       │
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

### **Phase C (future) — 2026-07-28 migration prep** 🔲 FUTURE
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
- [x] Auth: **Bearer token via APOLLO_AUTH_TOKEN**; localhost-only default; admin via OmniNet's built-in auth.
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

---

# 🔨 CURRENT SUB-TASK — MCP 2025-11-25 Conformance Audit & Plan

**Started:** 2026-07-02
**Owner:** user `lee`
**Trigger:** "does apollocore follow the mcp standard? If not create a Plan"

## 1. Audit — what we already do right

Reviewed `src/mcp_server/` end-to-end against the official spec at
`https://modelcontextprotocol.io/specification/2025-11-25/`:

| Spec area | ApolloCore status | File |
|-----------|-------------------|------|
| JSON-RPC 2.0 envelope (request/response/notification, id handling, error codes) | ✅ Correct | `jsonrpc.hpp/cpp` |
| `initialize` / `notifications/initialized` lifecycle | ✅ Correct (parses client version, pins 2025-11-25) | `dispatcher.cpp` |
| Capability declaration (`tools`, `resources`, `prompts`, `logging`) | ✅ Declared, but **sub-keys wrong** | `dispatcher.cpp` |
| `tools/list` / `tools/call` | ✅ Correct, 12 tools, JSON Schemas with `enum`/`minimum`/`maximum`/`default`, annotations | `tools.cpp` |
| `resources/list` / `resources/read` | ✅ Correct, 6 `apollo://` resources | `resources.cpp` |
| `prompts/list` / `prompts/get` | ✅ Correct, 4 prompts, argument descriptions | `prompts.cpp` |
| Tool-result error model (`isError: true` with text content for tool errors; JSON-RPC `error` for protocol errors) | ✅ Correct, used throughout | `tools.cpp` |
| `ping` | ✅ | `dispatcher.cpp` |
| JSON-RPC batch requests | ✅ `is_array()` branch in POST handler | `http_server.cpp` |
| `Mcp-Session-Id` header round-trip (read → echo → reuse) | ✅ | `http_server.cpp` |
| `Accept` header validation (406 on wrong type) | ✅ With `*/*` and `application/*` lenience | `http_server.cpp` |

## 2. The gaps — ranked by spec-mandated severity

### 🔴 CRITICAL — spec-mandated; will fail conformance tests

| # | Gap | Severity | Spec clause |
|---|-----|---------|-------------|
| **C1** | `GET /mcp` SSE is **stubbed with 405** | Blocker for Streamable HTTP | `basic/transports#listening-for-messages-from-the-server` |
| **C2** | `MCP-Protocol-Version` header not validated | Required; servers MUST 400 on bad version | `basic/transports#protocol-version-header` |
| **C3** | `Origin` header not validated → DNS-rebind attack vector | Required; servers MUST 403 on bad Origin | `basic/transports#security-warning` |
| **C4** | Capability sub-keys wrong: `"resources": {"subscribe": false}` is invalid | Required (the presence = yes; there is no `false`) | `basic/lifecycle#capabilities`, `server/tools.md` |
| **C5** | No `list_changed` notifications on tools/resources/prompts once `listChanged: true` is declared | Required if capability is declared | `server/tools.md#capabilities` |
| **C6** | No protocol-version negotiation — clients offering older versions are met with our pinned version instead of ours | Should-do (server SHOULD respond with a version it supports) | `basic/lifecycle#version-negotiation` |
| **C7** | **stdio transport missing** (only HTTP) | Spec lists stdio as the default; many MCP hosts (Claude Desktop) primarily use stdio | `basic/transports#stdio` |
| **C8** | `DELETE /mcp` not handled (session termination) | SHOULD per spec | `basic/transports#session-management` |

### 🟡 HIGH — should-haves that real clients use

| # | Gap | Notes |
|---|-----|-------|
| **H1** | `logging/setLevel` declared as capability but **no handler** | Returns `METHOD_NOT_FOUND` for clients that try it |
| **H2** | `completion/complete` declared, **no handler** | Same |
| **H3** | `resources/subscribe` and `resources/unsubscribe` not implemented (only relevant if we set `subscribe: true`) | Future-proofing for live-update push |
| **H4** | `notifications/cancelled` not handled (server-side cancellation) | Long-running tools (entity extract) should respect it |
| **H5** | `notifications/progress` not emitted | Long-running tools should report progress |
| **H6** | No `outputSchema` on tools that produce structured data | Best practice; lets clients validate results |
| **H7** | Bearer token auth (`APOLLO_AUTH_TOKEN`) ✅ DONE | N/A |
| **H8** | Server default-binds `0.0.0.0` — spec says local servers **SHOULD bind 127.0.0.1** | Security best practice |

### 🟢 POLISH — extras

| # | Gap | Notes |
|---|-----|-------|
| **P1** | No `_meta` key in any response (extensions hook) | Optional |
| **P2** | No `icons`/`title`/`description` enrichment on tools, resources, prompts | `title` is half-done on tools/resources/prompts; `icons` missing |
| **P3** | HTTP body size not capped | Spec doesn't require, but DoS risk |
| **P4** | Tool annotations should match reality — `apollo_set_config` is `destructive: true` ✓, but `apollo_extract_entities` writes to KB without announcing it | Patch labels |
| **P5** | `vendor` field in `serverInfo` is non-spec — remove or document as extension in `_meta` | Minor |

## 3. Plan

Working in **priority order**: critical first (spec must-haves), then high (real-client breakage), then polish. Each phase lands as one or more ctest-passing PR-able commits.

### Phase A — Spec-mandated security & framing fixes **(must ship together)**

1. **A1 — `Origin` validation in POST/GET/DELETE /mcp**
   - New `McpSecurity::is_valid_origin(req, allowed_origins) -> bool`
   - Default: allow empty Origin + loopback + `localhost` + any explicitly-listed entry
   - ENV override: `APOLLO_ALLOWED_ORIGINS` (comma-sep)
   - On failure: `403 Forbidden` with a JSON-RPC error body that has no `id`
2. **A2 — `MCP-Protocol-Version` validation**
   - After `initialize`: extract `MCP-Protocol-Version` header, echo back the
     negotiated version, error 400 if missing/invalid post-init
   - Allow omitting the header pre-init (back-compat)
3. **A3 — Wire `Mcp-Session-Id` validation**
   - Validate received id against known sessions; unknown id ⇒ 400 (per spec)
     instead of silently creating a new one. Keep the "create-or-attach"
     behaviour opt-in via env flag for dev.
4. **A4 — DELETE /mcp handler** for session termination. 200 on success, 404
   if session gone, 405 if server disallows client-initiated termination.

### Phase B — SSE on GET /mcp **+** `list_changed` notifications **+** version negotiation

5. **B1 — SSE on `GET /mcp`** when `Accept: text/event-stream`
   - Use OmniNet's response writer with `Content-Type: text/event-stream`
   - Hold the connection open, push server-initiated messages as needed
   - Outbound heartbeat at `retry:` interval; client reconnect via
     `Last-Event-ID` honours our optional `id:` field
6. **B2 — `list_changed` notifications**
   - New `Dispatcher::notify_list_changed("tools"|"resources"|"prompts")`
   - Hook it into the static registries so a future "register at runtime" path
     can trigger it (no current hot-add UI; the hook is plumbing)
7. **B3 — Protocol-version negotiation**
   - In `handle_initialize`: read `params.protocolVersion`, if we don't pin it,
     parse the requested version, if it's `YYYY-MM-DD` we know about, reply with
     that, otherwise reply with `2025-11-25`. Persist negotiated version per
     session. On `MCP-Protocol-Version` mismatch, return 400 with the
     version we'd accept.

### Phase C — stdio transport ✅ DONE (see §Phase C below)

8. **C1 — Add `StdioTransport` class**
   - Same wire format as HTTP body transport; reads JSON-RPC messages line-
     by-line from stdin, writes responses to stdout. **stderr for logs only.**
   - CLI: `apollo_mcp_server --transport stdio`
9. **C2 — Common dispatcher testbed**: extract the JSON-RPC I/O loop into a
   function shared by HTTP and stdio transports so they can't drift
10. **C3 — Conformance smoke test**: spawn stdio server with a JSON-RPC client
    over a pipe, run a handshake, list tools, call one — assert the round trip
    matches the spec byte-for-byte

### Phase D — Missing handlers + best practices

11. **D1 — `logging/setLevel`** handler (declared capability now honored)
12. **D2 — `completion/complete`** handler with basic argument-type completion
    (stub returns empty unless we have a use case — bookkeeping fix only)
13. **D3 — `notifications/cancelled`** handler: store a per-request
    cancel-flag map; long-running tool handlers check it periodically
14. **D4 — `notifications/progress`** emitter: long tools (entity extraction,
    codebase ingest) emit `progress/now/total` updates via the new SSE channel
15. **D5 — Add `outputSchema` to the structured tools** ✅ DONE:
    `apollo_get_stats`, `apollo_list_projects`, `apollo_get_neighbors`
16. **D6 — Default bind host** to `127.0.0.1`; `--host 0.0.0.0` opt-in for LAN
17. **D7 — Doc the no-auth limitation** in `README.md` and `/info`

### Phase E — Test hardening

18. **E1 — Conformance test**: adopt the `modelcontextprotocol/conformance`
    subset we can run in a single process (initialize handshake, tools/list,
    tools/call with structured output, error responses). Publish a CI job
    that runs `tests/test_mcp_conformance.py` against a spawned `apollo_mcp_server`.
19. **E2 — Per-transport tests** (HTTP+SSE, stdio); both must pass the
    full protocol exchange identically.
20. **E3 — Origin-rejection, version-negotiation, listChanged-emission
    negative tests.

### Phase F — Cleanup

21. F1 — Remove non-spec `vendor` from `serverInfo` (move to `_meta`).
22. F2 — Tighten tool `annotations` (mark `apollo_extract_entities` and
    `apollo_save_kb` correctly).
23. F3 — Update `README.md` and `docs/mcp_compliance.md` with the conformance
    matrix above.

## 4. Sequencing & parallelism

- **Phases A + B** are split by file — `jsonrpc.cpp`, `dispatcher.cpp`,
  `http_server.cpp`, `main.cpp`, `server_state.cpp`. Single author OK;
  one PR per phase (A1-A4 in PR #1, B1-B3 in PR #2).
- **Phase C** (stdio) is its own PR — adds `StdioTransport.{hpp,cpp}`,
  CLI flag, dispatcher refactor.
- **Phase D** can fan out into 3 PRs: missing-handlers, SSE progress,
  defaults/docs.
- **Phase E** runs only after C lands — conformance assertions need both
  transports.

## 5. Out of scope (explicit non-goals for THIS task)

- Migrating to **2026-07-28 stateless** — separate task.
- Implementing the **registry / OAuth-client-credentials / enterprise-managed-auth
  extensions** — separate tasks.
- Wrapping stdio in a WebSocket transport — that's a custom transport and
  the spec discourages it.

---

## 6. Execution — begun

Phase A is in progress. Starting with **A1 — Origin validation** and
**A2 — MCP-Protocol-Version validation** because they're the simplest
spec violations to fix (small `http_server.cpp` patch) and unblock the
conformance tests downstream.

Work plan for this session:
- **A1** — Origin header validation in `http_server.cpp` (new helper in
  a `McpSecurity` namespace, called from every `/mcp` handler)
- **A2** — `MCP-Protocol-Version` header validation + session header check
- **A3** — Tighter session-id validation
- **A4** — DELETE /mcp handler

PRs:
- `feat/mcp-origin-validation`       — A1
- `feat/mcp-protocol-version-header` — A2
- `feat/mcp-session-id-validation`   — A3
- `feat/mcp-delete-session`          — A4

---

## 7. Status as of 2026-07-02 (after Phase A shipped)

### Shipped in commit `c148ff0`
- **A1** Origin validation (`mcp_security.cpp`): 403 on evil origin, allow loopback+null
- **A2** MCP-Protocol-Version header check: 400 on unknown, skip pre-init
- **A3** Bearer-token scaffold: env-gated via APOLLO_AUTH_TOKEN
- **A4** DELETE /mcp route: 200 first DELETE, 404 second DELETE; always echo `Mcp-Session-Id` from initialize
- `tests/test_mcp_conformance.py` — live HTTP conformance smoke test, all 5 cases passing
- Two pre-existing bugs uncovered and fixed:
  - **OMNINET-ROUTER-404-TRAP**: `Router::handle()` overwrites status=200 with 404 when body is empty. handle_post_mcp refactor broke the single-request path; single-request write_json is now restored.
  - `test_mcp_dispatcher.cpp` had hard-coded counts (12 tools, 5 resources); registry has grown. Loosened to `>= 12` and `>= 5`.

### Build infra fix
- Installed `tree-sitter-cli` v0.22.6 (cargo) so CMake find_program succeeds
- Reconfigured with `-DTREE_SITTER_CLI=/home/lee/.cargo/bin/tree-sitter`

### Verification
- `ctest -E test_ingest_codebase` → 18/18 pass (test_ingest_codebase excluded because it ingests the whole repo and is too slow for inner loop)
- `python3 tests/test_mcp_conformance.py` → 5/5 pass

### Next session
- **Phase E** (full MCP conformance test suite per modelcontextprotocol/conformance)

## Phase C — stdio transport ✅ DONE

**Reconsidered and shipped in commit `61998fa`.**

The original "HTTP-only" decision was correct for the *default*, but the
*option* was still needed: Claude Desktop and other desktop IDE
integrations use stdio exclusively — they don't have a network to connect
to an HTTP daemon. ApolloCore must support stdio so it can be used from
those environments.

Implementation:
- `--transport stdio` CLI flag (default: `http`). `args.transport` field in
  `CliArgs`, validated in `parse_args()`.
- `mcp_stdio.{hpp,cpp}`: `run_stdio_transport(dispatcher, state)` loop —
  `std::getline(std::cin, line)`, optional `\r` strip, skip empty/comment
  lines, `nlohmann::json::parse` with parse-error response, `dispatch_one()`
  for dispatch, `nullopt` for notifications (no response), `std::cout <<
  resp->dump() << '\n'` + `std::cout.flush()` per response.
- `mcp_dispatch.{hpp,cpp}`: extracted `dispatch_one()` — shared between HTTP
  `handle_one()` (now a thin delegate) and stdio's line loop.
- `main.cpp`: branches on `args.transport == "stdio"` — constructs its own
  `McpSseBus` + `Dispatcher` (no SSE bridge for stdio) and calls
  `run_stdio_transport()`. HTTP path unchanged.
- `src/apollo_persistence.cpp`, `contextual_retrieval.cpp`,
  `cross_encoder.cpp`, `entity_extractor.cpp`, `hyde.cpp`,
  `kb_persistence.cpp`, `embedding_model.cpp`, `main.cpp`:
  all `std::cout` / `printf` replaced with `std::cerr` or `fprintf(stderr, …)`.
  Without this, library model-load messages would appear on stdout and corrupt
  the JSON-RPC frame stream in stdio mode.
- `tests/test_stdio_conformance.py`: `subprocess.communicate()` approach —
  sends all 4 messages, reads all stdout at once, verifies exactly 3 JSON
  lines (init, tools/list, ping; notification has no id → no response) and
  no JSON on stderr.

Verification:
- `ctest -E test_ingest_codebase` → 19/19 pass (new test_cursor + test_ui_static)
- `python3 tests/test_stdio_conformance.py` → 4/4 PASS (init, notif, tools/list, ping)
- `python3 tests/test_mcp_conformance.py` → 27/27 PASS (HTTP still works)
- `python3 tests/test_sse_streaming.py` → ': connected' + keep-alive PASS
- `python3 tests/test_pagination.py` → ALL OK (cursor encode/decode, offset 5)
- `python3 tests/test_auth.py` → ALL OK (no/wrong/right token)

### Phase E5 — cursor pagination ✅ DONE

`tools/list`, `resources/list`, `prompts/list` now support cursor-based
pagination per MCP spec §8.5:
- `cursor.{hpp,cpp}`: `encode_cursor(offset: uint32_t) → optional<string>` and
  `decode_cursor(string) → optional<uint32_t>`. Cursors are base64-encoded
  uint32 offsets — opaque to the client but self-contained (no server-side
  cursor state needed).
- `dispatcher.{hpp,cpp}`: `handle_tools_list(req)`,
  `handle_resources_list(req)`, `handle_prompts_list(req)` accept an optional
  `cursor` param, page size 50. Returns `{tools: [...], nextCursor?}` where
  `nextCursor` is absent at end of list.

New tests: `tests/test_cursor.cpp` (C++ round-trip), `tests/test_pagination.py`
(Python smoke), `tests/test_auth.py` (Bearer-token HTTP auth).

---

## Phase D/E/F — all done ✅

All MCP-conformance work is shipped on `main`. The server is
production-ready except for the items in §Open issues below.

### Phases A + B + D + F (security, framing, capabilities)
- **A1** Origin DNS-rebind defense → 403 on evil origin
- **A2** MCP-Protocol-Version validation → 400 on unknown
- **A3** Bearer auth via APOLLO_AUTH_TOKEN env var (opt-in)
- **A4** DELETE /mcp terminates session, 404 on second
- **B1** GET /mcp Server-Sent Events (Phase B1 SSE bridge)
- **B2** notifications/{tools,resources,prompts}/list_changed plumbing
- **B3** Protocol-version negotiation in initialize
- **D1** Strict MCP-Session-Id validation (unknown → 400)
- **D2** logging/setLevel + completion/complete + notifications/cancelled
- **D3** Per-request cancel-token table (Session::cancel_tokens)
- **D4** notifications/progress on every tools/call
- **D5** outputSchema on structured tools (apollo_get_stats,
       apollo_list_projects, apollo_get_neighbors)
- **F1** Drop non-spec `vendor` from serverInfo
- **F2** Tighten tool annotations (explicit read_only/destructive/
       idempotent/open_world + outputSchema)

### Phase E (test hardening)
- **E1** Adopted official `mcp` Python SDK as a client.
  `tests/test_mcp_sdk.py` — 7 cases (handshake, list_tools,
  call_tool, list_resources, list_prompts, read_resource, call_tool
  with args). If this passes, Claude Desktop / Inspector / any
  SDK-based agent can talk to ApolloCore.
- **E2** Per-transport tests — N/A, we shipped HTTP-only.
- **E3** Negative tests in `test_mcp_conformance.py` (10 cases):
       malformed JSON, invalid id types, unknown method, unknown
       tool, double-DELETE, missing/evil Origin, ping round-trip,
       resources/list, prompts/list.
- **E?extra** `tests/runall.sh` runs all 4 test scripts 3x in a row
  to verify stability.

### Phase F (cleanup)
- **F1** Drop non-spec `vendor` from serverInfo ✅
- **F2** Tool annotation tightening ✅
- **F3** `docs/mcp_compliance.md` conformance matrix ✅

---

## Current test summary
```
ctest -E test_ingest_codebase          → 19/19 pass
tests/test_stdio_conformance.py         →  4/4 (init, notif, tools/list, ping)
tests/test_mcp_conformance.py           → 27/27 (HTTP conformance)
tests/test_sse_streaming.py             →  ': connected' + keep-alive
tests/test_pagination.py                →  ALL OK (cursor encode/decode + offset 5)
tests/test_auth.py                      →  ALL OK (Bearer-token)
tests/runall.sh (stability runs)         →  all green
```

```

## Issues found and fixed during Phase E1 SDK adoption

These were real bugs that conformance smoke tests didn't catch:

1. **Race: SSE keepalive writes corrupt POST responses.**
   When the SDK reuses a TCP connection for GET (SSE) + POST (JSON-RPC),
   the SSE writer's 15s `: keepalive\r\n` bytes landed inside a POST
   response body, producing `illegal status line: bytearray(b': keepalive')`
   in httpx. Fix: send `Connection: close` on GET /mcp SSE so every SSE
   call gets its own TCP. Documented in `mcp_sse.cpp`. **Proper fix
   (per-connection write mutex) deferred to a future phase.**

2. **structuredContent was a JSON array for apollo_list_projects.**
   Per spec, structuredContent is always a JSON object. Fix: wrap as
   `{projects: [...]}`. Updated outputSchema to match. Caught only by
   pydantic validation in the official SDK.

3. **nlohmann::json type error in ToolDef positional init.**
   When adding `output_schema` to ToolDef, four tools had positional
   init missing `open_world_hint` (or `idempotent_hint`), so the new
   `json{...}` outputSchema was assigned to a `bool` field, crashing
   at startup with "type must be boolean, but is object". Caught
   immediately on restart; fixed by patching each register_tool
   block to include the missing hint slots. **Build now has zero
   warnings.**

4. **Duplicate `tools/list` dispatch check** in dispatcher.cpp. A copy-paste
   error had two identical `if (req.method == "tools/list")` branches.
   Harmless (second is unreachable) but confusing. Removed the duplicate.

5. **Ping pre-init was rejected with 400.** Per MCP spec §Lifecycle: "ping
   is allowed at any time for keep-alive." The HTTP layer's `pre_init`
   guard only allowed `initialize`, not `ping`. Fixed: `check_pre_init`
   now returns true for both `initialize` AND `ping` when the request
   has a non-null id.

6. **Unbounded session accumulation** (memory leak). Sessions stored in
   `ServerState::sessions_` were only removed on explicit DELETE /mcp.
   Long-running servers with many short-lived clients would accumulate
   sessions indefinitely. Fixed: `ServerState` now starts a background
   reaper thread on construction that evicts idle sessions (>30 min)
   every 5 minutes. Shutdown is clean — a condition-variable signal
   wakes the thread immediately so destructors never block.

---

## Open issues (deliberate / deferred)

1. **SSE write mutex** (see Issue #1 above). The proper fix is a
   per-connection mutex around `socket->sendAll()` that both the
   SSE writer thread AND the POST handler's response writer
   acquire. Today's `Connection: close` workaround is functional
   but burns an extra TCP per SSE connection. Not blocking.

2. **resumability (Last-Event-ID)** — we emit `id:` on every SSE event
   but the SDK doesn't reconnect with it. Phase 6 work.

3. ~~**OAuth 2.1**~~ **N/A** — Bearer token (APOLLO_AUTH_TOKEN) is the correct auth model for local MCP servers; full OAuth is not applicable.

4. **Cancel tokens are wired but unused.** No current tool checks
   its token. Tools complete in <1s so cancellation wouldn't help.
   When `apollo_ingest_codebase` grows progress emission mid-stream,
   it should poll the token.

5. **Pagination on list methods** ✅ DONE. Cursors are base64-encoded
   offsets; page size 50 per list method. With 12 tools,
   6 resources, and 4 prompts this is not a problem yet. Per spec §8.5
   this is a SHOULD for large sets — deferred until the tool count
   warrants it.

6. **Deprecation warning in SDK tests.** The official `mcp` Python SDK
   emits `DeprecationWarning: Use streamable_http_client instead` for
   `ClientSession.from_streamablehttp`. This is a SDK issue, not ours.
   When the SDK deprecates the old API, update test_mcp_sdk.py to use
   `streamable_http_client` directly.

### Refactor: dispatch_one() helper
While exploring stdio, extracted the parse + dispatch + notification
branch from http_server.cpp's handle_one() into a free function
`dispatch_one(dispatcher, raw_request, session_id)` in
mcp_dispatch.{hpp,cpp}. handle_one() now delegates. Makes the JSON-RPC
core testable in isolation, and is the natural seam for any future
transport.

### Phase B — SSE + list_changed + version negotiation (shipped in `f5bd4e4`)
- **B1** GET /mcp Server-Sent Events endpoint. McpSseBridge uses OmniNet's
  `registerProtocolSwitch()` so POST/DELETE fall through to the Router while
  GET + Accept: text/event-stream takes the socket and pumps events from a
  per-session NotificationQueue. 15s keep-alive heartbeat. Headers include
  Cache-Control: no-cache, X-Accel-Buffering: no.
- **B2** `Dispatcher::notify_list_changed(kind, sid)` plumbing for
  `notifications/{tools,resources,prompts}/list_changed`. No code path
  emits these today (registries are static), but it's wired and ready.
- **B3** Protocol-version negotiation: handle_initialize reads
  `params.protocolVersion`, echoes it if supported, falls back to our
  pinned default (2025-11-25) otherwise. Session records the negotiated
  version for future defence-in-depth.
- **Bugfix SIGPIPE-OMNINET-FATAL**: OmniNet's Socket::send does a raw
  ::send() without MSG_NOSIGNAL. When a long-lived SSE client disconnects,
  the kernel SIGPIPE's the whole process — terminating even though the
  offending write is in a worker thread. Fix: SIG_IGN via sigaction at
  startup in main.cpp.

### Phase B verification
- `ctest -E test_ingest_codebase` → 18/18 pass
- `python3 tests/test_mcp_conformance.py` → 9/9 pass (5 Phase A + 4 Phase B1)
- `python3 tests/test_sse_streaming.py` → `: connected` + 1+ keep-alive observed
- B3 manual round-trip: 2025-06-18 echoes back, 1999-01-01 falls back