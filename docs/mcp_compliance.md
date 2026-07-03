# MCP 2025-11-25 Conformance Matrix

This document tracks which parts of the
[Model Context Protocol specification 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25)
ApolloCore's MCP server implements and how. Updated whenever the
implementation changes.

Legend: ✅ done · 🟡 partial / documented deviation · ❌ not yet · 🚫 N/A

---

## Transport

| Requirement                                                  | Status | Notes                                               |
|--------------------------------------------------------------|:------:|-----------------------------------------------------|
| Single unified `/mcp` endpoint                               | ✅     | POST/GET/DELETE on one path                         |
| POST = JSON-RPC request (returns JSON or SSE)                | ✅     | Returns JSON-RPC JSON                                |
| GET = `text/event-stream`                                    | ✅     | Phase B1 — opens SSE stream for server→client        |
| Accept header must include both `application/json` and `text/event-stream` | ✅     | Enforced in `on_upgrade`                            |
| `MCP-Protocol-Version` header validation (post-init)        | ✅     | 400 on unknown version                              |
| `MCP-Session-Id` round-trip on initialize                    | ✅     | Echoed in response header                           |
| Strict session validation: unknown id → 400                 | ✅     | Phase D1                                            |
| Origin header validation (DNS-rebind defense)                | ✅     | Phase A1 — 403 on evil origin                       |
| Empty Origin allowed for CLI callers                         | 🟡     | Documented deviation: spec says "must reject invalid", we allow empty. Matches mainstream impls. |
| Bearer token authentication                                  | 🟡     | Phase A3 — `APOLLO_AUTH_TOKEN` env var, opt-in        |
| `DELETE` ends a session, 404 on subsequent                  | ✅     | Phase A4                                            |
| POST notifications → 202 Accepted                            | ✅     | Spec-compliant 202, no body                         |
| Resume from `Last-Event-ID` on SSE reconnect                 | ❌     | Phase 6 future — `id:` line emitted but not used yet |

---

## Lifecycle

| Method                            | Status | Notes                                       |
|-----------------------------------|:------:|---------------------------------------------|
| `initialize`                      | ✅     | Phase 2 — full handshake with capabilities  |
| `notifications/initialized`        | ✅     | Acknowledged, no reply                       |
| `ping`                            | ✅     | Empty object response                       |
| Shutdown via transport close       | ✅     | TCP close → cleanup                         |

---

## Capabilities

| Capability             | Declared | Implemented | Notes                                          |
|------------------------|:--------:|:-----------:|------------------------------------------------|
| `tools.listChanged`     | ✅       | ✅          | Plumbed (Phase B2); emitters not wired yet     |
| `resources.subscribe`   | false    | N/A         | We don't expose subscribe-capable resources    |
| `resources.listChanged` | ✅       | ✅          | Plumbed (Phase B2); emitters not wired yet     |
| `prompts.listChanged`   | ✅       | ✅          | Plumbed (Phase B2); emitters not wired yet     |
| `logging`               | ✅       | ✅          | `logging/setLevel` handler, per-session level  |
| `completions`           | ✅       | ✅ (stub)   | `completion/complete` returns empty values     |

---

## Tools

| Tool                        | inputSchema | outputSchema | Annotations                                  |
|-----------------------------|:-----------:|:------------:|----------------------------------------------|
| `apollo_query_kb`           | ✅          | ❌ text      | readOnly, idempotent                         |
| `apollo_add_document`       | ✅          | ❌ text      |                                              |
| `apollo_extract_entities`   | ✅          | ❌ text      | non-readOnly, idempotent                     |
| `apollo_ingest_codebase`    | ✅          | ❌ text      | non-readOnly                                 |
| `apollo_search_symbols`     | ✅          | ❌ text      | readOnly, idempotent                         |
| `apollo_get_neighbors`      | ✅          | ✅ structured | readOnly, idempotent                         |
| `apollo_create_kb`          | ✅          | ❌ text      | destructive                                  |
| `apollo_open_kb`            | ✅          | ❌ text      | destructive                                  |
| `apollo_save_kb`            | ✅          | ❌ text      | non-readOnly, idempotent                     |
| `apollo_get_stats`          | ✅          | ✅ structured | readOnly, idempotent                         |
| `apollo_set_project`        | ✅          | ❌ text      | destructive                                  |
| `apollo_list_projects`      | ✅          | ✅ structured | readOnly, idempotent                         |
| `apollo_set_config`         | ✅          | ❌ text      | destructive, idempotent                      |

`structured` = the tool emits a matching `structuredContent` JSON
object alongside its text dump.

---

## Resources

| URI pattern                       | mimeType             | Description             |
|-----------------------------------|----------------------|-------------------------|
| `apollo://projects`               | application/json     | List of projects        |
| `apollo://stats`                  | application/json     | KB statistics           |
| `apollo://entities`               | application/json     | All extracted entities |
| `apollo://chunks/{chunk_id}`      | text/plain           | Single chunk content   |
| `apollo://search?q=...`           | application/json     | Query results           |
| `apollo://symbol/{name}`          | application/json     | Code symbols by name    |

`resources/subscribe` is NOT supported (declare `subscribe: false`).
`resources/list` returns the registered set.

---

## Prompts

| Name                     | Arguments                                       |
|--------------------------|-------------------------------------------------|
| `summarize`              | text, style (short\|detailed)                   |
| `extract_entities`       | text                                           |
| `answer_question`        | question, context_chunks                        |
| `code_review`            | file_path                                       |

`prompts/list` returns the registered set.
`prompts/get` returns messages with text content.

---

## Notifications (server → client)

| Notification                              | Status | Notes                                         |
|-------------------------------------------|:------:|-----------------------------------------------|
| `notifications/cancelled`                | ✅     | Phase D3 — accepts requestId, no-op if unknown |
| `notifications/progress`                 | ✅     | Phase D4 — emitted around `tools/call`        |
| `notifications/tools/list_changed`       | ✅     | Phase B2 — plumbing ready, no emitters        |
| `notifications/resources/list_changed`   | ✅     | Phase B2 — plumbing ready, no emitters        |
| `notifications/prompts/list_changed`     | ✅     | Phase B2 — plumbing ready, no emitters        |
| `notifications/message` (logging)        | ❌     | Phase 6 future                                 |

---

## Errors

| Code | Meaning              | Used? |
|------|----------------------|:-----:|
| -32700 | Parse error         | ✅    |
| -32600 | Invalid request      | ✅    |
| -32601 | Method not found    | ✅    |
| -32602 | Invalid params      | ✅    |
| -32603 | Internal error      | ✅    |
| -32001 | Unauthorized (custom) | ✅  |
| -32002..-32099 | Server-defined | reserved |

---

## JSON-RPC compliance

| Item                                          | Status |
|-----------------------------------------------|:------:|
| `jsonrpc: "2.0"` literal                      | ✅     |
| `id` is string, integer, or null              | ✅     |
| `id` is required on requests                  | ✅     |
| Notifications have no `id`                   | ✅     |
| Successful response has `result` (no `error`) | ✅     |
| Error response has `error` (no `result`)     | ✅     |
| `_meta` reserved prefix `modelcontextprotocol/` | unused (we don't ship extensions) |
| `isError: true` in result for tool-level errors | ✅ |

---

## Deviations from the spec

These are intentional, documented deviations:

1. **Empty Origin allowed.** Spec says reject invalid Origin; we allow
   missing/empty (matches mainstream implementations like the official
   TypeScript SDK reference server, so curl users can connect without
   setting Origin). Evil non-loopback origins are still 403'd.

2. **Default bind host 127.0.0.1 (was 0.0.0.0).** Spec §6.2 recommends
   loopback-only. We default to it; pass `--host 0.0.0.0` for LAN.

3. **No OAuth 2.1.** Spec §7 recommends OAuth for HTTP transports.
   We ship `APOLLO_AUTH_TOKEN` as a simpler alternative. Full OAuth
   is in the Phase 6 roadmap.

4. **`ping` rejected pre-init.** Spec §Lifecycle allows ping before
   initialize. Our strict session validation (Phase D1) rejects it.
   Acceptable deviation; clients should initialize before pinging.

---

## Test coverage

| Test suite                                  | Coverage                |
|---------------------------------------------|-------------------------|
| `ctest` (18 unit tests)                     | Dispatcher, tools, persistence, embeddings, entity extraction, code extraction, hyde, hybrid search, UI static |
| `tests/test_mcp_conformance.py`             | A1 + A2 + A4 + D1 + D2 + B1 + E3 (24 cases) |
| `tests/test_sse_streaming.py`               | B1 — ': connected' + keep-alive |
| `tests/test_progress_notification.py`       | D4 — start + finish progress frames |

---

Last reviewed: 2026-07-02. Update this file when capabilities change.