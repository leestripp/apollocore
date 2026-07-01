# ApolloCore Web UI

A small in-process operator dashboard for the MCP server. Browse to
`http://localhost:8765/` after starting `apollo_mcp_server` and you get a
five-tab console for inspecting and operating the knowledge base.

The UI is served by the same `apollo_mcp_server` executable, on the same
port, as a separate process. No bundler, no CDN, no framework. Pure HTML,
CSS, ES modules.

---

## Quick start

```bash
# 1. Build (one-time)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target apollo_mcp_server

# 2. Run (will mount the UI at http://localhost:8765/)
mkdir -p models
./build/src/mcp_server/apollo_mcp_server --port 8765

# 3. Open in a browser
xdg-open http://localhost:8765/      # Linux
open      http://localhost:8765/      # macOS
start     http://localhost:8765/      # Windows
```

That's it. The UI is enabled by default whenever the `static/` directory
is found next to the executable (or in `src/mcp_server/static/` during
development).

---

## Tabs

### Health

Auto-refreshes every 5 seconds. Shows:

- Server card: name, version, protocol (2025-11-25), transport, endpoint.
- Surface counts: 12 tools, 5 resources, 4 prompts (live numbers).
- Active project + KB stats (documents, chunks, entities, relationships).
- Probe panel: round-trip latency of a single `tools/call apollo_get_stats`.

### Search

Operator-facing search against the active KB. Same four modes as the
`apollo_query_kb` tool (`vector`, `hybrid`, `rerank`, `full`). Each
result card shows the chunk id, source document title, content excerpt,
and per-signal score badges (vector, BM25, graph) plus the fused RRF
score. Click "Show full" to expand a card. Click the chunk id to copy it.

Tip: open a KB in **Settings** first, and load an embedding model.

### Graph

Force-directed visualisation of the knowledge graph. Reads from
`apollo://graph` resource, renders entities as type-coloured nodes and
relationships as lines.

- Drag to pan, scroll to zoom, drag a node to pin it (double-click to
  unpin). Hover to highlight neighbours, click for detail panel.
- Filter by name (live search) or by entity type (chips).
- Pause/resume the physics loop, reset the view.
- Capped at 500 nodes by default (UI stays smooth).

### Settings

Read-mostly view of the server config:

- Projects list (from `apollo://projects`); each has an "Open" button
  that calls `apollo_set_project`.
- Live server config (host, port, capabilities, model paths).
- Build info (MCP protocol, vendor, server version).

### About

Build info, license, and a one-click "copy agent config" snippet that
produces the JSON you paste into Claude Desktop, Cursor, Cline, etc.

---

## How it's wired

```
Browser (operator)
        │  HTTP
        ▼
apollo_mcp_server (OmniNet)
  ├── POST /mcp      → JSON-RPC dispatcher → ApolloCore methods
  ├── GET  /health   → JSON
  ├── GET  /info     → JSON
  └── GET  /*        → StaticHandler(src/mcp_server/static/)
                       ├── /           → index.html
                       ├── /css/*      → style.css
                       ├── /js/app.js  → module
                       ├── /js/api.js  → module
                       └── /js/{health,search,graph,settings,about}.js
```

The UI is a regular MCP client. Every number on screen was loaded by
sending a real JSON-RPC request to `POST /mcp`, the same endpoint an
agent uses. We don't have a separate API.

The static files live in `src/mcp_server/static/` during development
and are installed to `${CMAKE_INSTALL_PREFIX}/share/apollocore/ui/`.
The binary searches both locations at startup.

---

## Manual smoke test

```bash
# Terminal 1
./build/src/mcp_server/apollo_mcp_server --port 8765 --data-dir /tmp/ui-smoke

# Terminal 2
curl -sI http://localhost:8765/                 # 200 text/html
curl -sI http://localhost:8765/css/style.css    # 200 text/css
curl -sI http://localhost:8765/js/app.js        # 200 text/javascript
curl -s  http://localhost:8765/health | jq      # JSON
curl -s  -X POST -H "Content-Type: application/json" \
       -d '{"jsonrpc":"2.0","id":1,"method":"ping"}' \
       http://localhost:8765/mcp | jq
```

If a static file 404s, the binary is probably running from a directory
without `src/mcp_server/static/`. Pass `--ui-static-dir=/abs/path/to/static`
or `cmake --install` it.

---

## Disabling the UI

```bash
apollo_mcp_server --no-ui                # CLI
APOLLO_NO_UI=1 apollo_mcp_server         # env
```

With the UI disabled:

- `GET /` returns 404
- `GET /css/style.css` returns 404 (or empty JSON)
- `POST /mcp`, `GET /health`, `GET /info` still work as before

Useful for headless deployments (containers, CI scripts) where you want
the smallest possible attack surface.

---

## Architecture cheatsheet

```
src/mcp_server/static/
├── index.html            single-page shell, 4 KB
├── css/
│   └── style.css         ~270 lines, CSS variables, dark-first
└── js/
    ├── api.js            170 lines, JSON-RPC wrapper
    ├── app.js             80 lines, tab switching + hash routing
    ├── health.js         170 lines, periodic refresh
    ├── search.js         180 lines, query form + result cards
    ├── graph.js          420 lines, Fruchterman-Reingold + canvas
    ├── settings.js       130 lines, projects + models
    └── about.js           85 lines, copy-config snippet
```

Total: ~1,500 lines of UI code, zero dependencies, ~50 KB shipped.

---

## Customising

The UI prefers user-agent-driven values where it can: the active tab is
mirrored to the URL hash (`#health`, `#search` etc.) so you can bookmark
or link-share a specific view.

- CSS custom properties (`--bg`, `--text`, `--accent`, `--font`,
  `--pad`, `--gap`, `--radius`) are the main knobs. They're at the top
  of `css/style.css`.
- Add a new tab by:
  1. creating `js/foo.js` that exports `renderFoo(panel)`;
  2. importing it in `js/app.js`;
  3. adding a tab in `index.html` (`<a class="tab" data-tab="foo">`);
  4. registering the tab in the `TABS` map in `app.js`.

That's the entire extension contract. No build step.

---

## Known limitations (v1)

- The graph viewer's force-directed loop pauses when the tab is hidden
  (RAF on a hidden canvas is wasted CPU). Switch back to the Graph tab to
  resume.
- The Settings tab is read-only except for "Open project". The plan
  defers settable configuration to v2.
- The UI does not display SSE server-pushed notifications; tabs refresh
  themselves with timers. SSE is wired but not yet used.
- The graph viewer does not paginate beyond 500 nodes. If a project has
  more, the UI shows the first 500 and a "show more" hint would be the
  next step.