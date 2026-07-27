# AGENTS.md — ApolloCore

Guidance for AI coding agents working in this repository. The reader is
assumed to know nothing about the project.

---

## Project overview

ApolloCore is a **C++23 MCP (Model Context Protocol) server** that exposes a
local retrieval-augmented knowledge base to AI agents (Claude Desktop, Cursor,
Cline, Windsurf, Continue.dev). It speaks **MCP protocol version 2025-11-25**
over the Streamable HTTP transport (JSON-RPC 2.0 on `POST /mcp`), served by
OmniNet. Everything runs locally: no API keys, no cloud, no data leaving the
machine.

Core capabilities:

- **Hybrid search** — vector similarity + BM25 keyword + knowledge-graph
  expansion, fused with Reciprocal Rank Fusion (RRF).
- **Cross-encoder reranking**, **HyDE** (Hypothetical Document Embeddings),
  and **contextual retrieval** (Anthropic-style), all powered by a local LLM.
- **Knowledge graph** — LLM-driven entity extraction, plus tree-sitter AST
  extraction for code (`FUNCTION`, `CLASS`, `NAMESPACE`, …).
- **Multi-project scoping** — one KB directory shared by multiple agents,
  each isolated in its own project. Note: projects are write-time provenance
  tags; reads span all data.
- **Persistence** — SQLite for metadata, binary `.fbin` files for embedding
  vectors, one directory per knowledge base.
- **Web UI** — a five-tab operator dashboard (Health / Search / Graph /
  Settings / About) served by the same executable from
  `src/mcp_server/static/` (plain HTML/CSS/ES modules, no framework, no CDN).
  It is itself a regular MCP client; every number comes from a real
  `POST /mcp` call. Disable with `--no-ui`.

The MCP surface is **9 tools** (`apollo_query_kb`, `apollo_add_document`,
`apollo_extract_entities`, `apollo_ingest_codebase`, `apollo_search_symbols`,
`apollo_get_neighbors`, `apollo_get_stats`, `apollo_list_projects`,
`apollo_set_config`), **5 resources** (`apollo://projects`,
`apollo://stats`, `apollo://entities`, `apollo://graph`,
`apollo://chunk/{id}`), and **4 prompts** (`summarize_kb`,
`answer_with_citations`, `code_review_summary`, `entity_research`).

**The only public artifact is the `apollo_mcp_server` executable.** The
`apollo_core` static library is an internal organization primitive — it is
not installed, not exported, and not meant to be linked downstream.

**The project is C++ only.** There is deliberately no Python, no shell
scripting layer, and no container/Podman tooling in this repo. Do not
reintroduce any of it — helper logic belongs in CMake or in the C++ code.

---

## Technology stack

| Component | Choice |
|---|---|
| Language | C++23 (GCC 13+; tested against GCC 15) |
| Build | CMake ≥ 3.20, FetchContent for nearly all deps |
| HTTP / WebSocket / TLS | OmniNet (gitlab.com/leestripp/omninet), OpenSSL 3 |
| Inference | llama.cpp (CPU by default; Vulkan/CUDA/Metal optional) |
| Code parsing | tree-sitter v0.26 (+ tree-sitter-cpp grammar), statically linked |
| Metadata | SQLite 3 (required system package) |
| Vector storage | sqlite-vec (SQLite extension) + `.fbin` binary files |
| JSON | nlohmann/json 3.11 |
| JSON-RPC | hand-rolled (`src/mcp_server/jsonrpc.hpp`) |
| Web UI | plain HTML/CSS/ES modules in `src/mcp_server/static/` |
| Testing | CTest (C++ only) |

Required GGUF models (downloaded automatically at CMake configure time by
`models/CMakeLists.txt` into `models/`, unless `-DAPOLLO_SKIP_MODEL_DOWNLOAD=ON`):

- `nomic-embed-text-v1.5.Q4_K_M.gguf` (~81 MB) — embeddings, **required**
- `qwen2.5-0.5b-instruct-q4_k_m.gguf` (~469 MB) — LLM for extraction/rerank,
  optional

---

## Repository layout

```
ApolloCore/
├── CMakeLists.txt              Top-level build: library, server, tests, models
├── include/apollocore/         Public headers of the internal apollo_core target
├── src/                        Library sources: RAG, KG, persistence, hybrid search
│   └── mcp_server/             MCP server: dispatcher, tools, resources, prompts,
│       └── static/             Web UI assets (HTML/CSS/JS)
├── tests/                      C++ CTest targets
├── examples/full_demo.cpp      Standalone CLI demo of the library
├── models/                     GGUF files (downloaded, gitignored)
└── docs/                       design.md, deployment.md, mcp-quickstart.md,
                                file-formats.md, mcp_compliance.md, ui.md, ui-testing.md
```

### Module division

- **`apollo_core` (static lib, `src/*.cpp` + `include/apollocore/*.hpp`)**:
  - `apollocore.cpp` — main engine/facade
  - `embedding_model.cpp`, `cross_encoder.cpp`, `hyde.cpp`,
    `contextual_retrieval.cpp` — inference-side RAG pieces
  - `apollo_hybrid_search.cpp` — vector + BM25 + graph fusion (Phase M2.7)
  - `entity_extractor.cpp` — LLM-driven entity extraction
  - `apollo_ts_wrapper.cpp` + `apollo_code_extractor.cpp` — tree-sitter AST →
    entities/relationships (Phase M3.B)
  - `text_chunker.cpp` — smart chunking
  - `persistence.cpp`, `kb_persistence.cpp`, `apollo_persistence.cpp` —
    SQLite layers; `fbin.cpp` — binary embedding format
- **`apollo_mcp_server` (executable, `src/mcp_server/`)**:
  - `main.cpp` — CLI entry, flags, env vars
  - `http_server.cpp` — OmniNet HTTP wiring (`POST /mcp`, `GET /mcp` SSE,
    `/health`, `/info`)
  - `dispatcher.cpp` + `mcp_dispatch.cpp` — JSON-RPC method routing
  - `tools.cpp`, `resources.cpp`, `prompts.cpp` — the MCP registries
  - `server_state.cpp` — owns the single ApolloCore instance, sessions
  - `mcp_security.cpp` — Origin validation (DNS-rebind defense),
    protocol-version header checks, session-id validation
  - `mcp_sse.cpp` / `mcp_sse_bus.cpp` — SSE streams and notifications
  - `mcp_stdio.cpp` — optional stdio transport (`--transport stdio`)
  - `cursor.cpp` — opaque pagination cursors

---

## Build and test commands

### Prerequisites (Ubuntu 24.04+ / Debian 13+)

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
                    libssl-dev zlib1g-dev libsqlite3-dev git
```

### Build

```bash
# Plain CMake (first build ~5 min: llama.cpp + OmniNet + tree-sitter)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target apollo_mcp_server
```

Output binary: `build/src/mcp_server/apollo_mcp_server`.

Useful CMake options: `-DLLAMA_VULKAN=ON` / `-DLLAMA_CUBLAS=ON` /
`-DLLAMA_METAL=ON` (GPU backends), `-DTREE_SITTER_SKIP_FETCH=ON`,
`-DOMNINET_SKIP_FETCH=ON` (use system libs), `-DAPOLLO_SKIP_MODEL_DOWNLOAD=ON`
(skip ~550 MB of GGUF downloads), `-DCMAKE_BUILD_TYPE=Debug`.

**Important:** the top-level `CMakeLists.txt` patches two fetched
dependencies at configure time, in pure CMake (idempotent — historically
these were Python scripts, which were deliberately removed):

- llama.cpp's `ggml/src/CMakeLists.txt` — propagate `GGML_USE_CPU` to
  `ggml-base` (without it `ggml_backend_reg_count() == 0` and model load
  fails with "no backends are loaded"), and add a `GGML_VULKAN`-guarded
  Vulkan backend activation.
- tree-sitter-cpp's `CMakeLists.txt` — guard its `parser.c` regeneration
  rule behind `if(TREE_SITTER_CLI)` (the repo ships a pre-generated
  `parser.c`; the tree-sitter CLI is not a build dependency).

If a configure fails in the FetchContent step, look at those patch blocks
first.

### Run

```bash
./build/src/mcp_server/apollo_mcp_server --port 8765
curl -s http://127.0.0.1:8765/health | jq
```

CLI flags: `--host`, `--port`, `--transport` (`http`|`stdio`), `--data-dir`,
`--embed-model`, `--llm-model`, `--ui-static-dir`, `--no-ui`, `--admin`,
`-v`/`--verbose`. Env vars: `APOLLO_HOST`, `APOLLO_PORT`, `APOLLO_DATA_DIR`,
`APOLLO_EMBED_MODEL`, `APOLLO_LLM_MODEL`, `APOLLO_NO_UI`, `OMNINET_DATA_DIR`,
`APOLLO_AUTH_TOKEN` (see Security below).

### Tests

```bash
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

- **C++ CTest targets** (each a small hand-rolled `main()`-style test binary,
  no test framework): `test_persistence`, `test_fbin`, `test_kb_persistence`,
  `test_apollo_persistence`, `test_v2`, `test_embeddings`,
  `test_entity_extraction`, `test_cross_encoder`, `test_hyde`,
  `test_contextual_retrieval`, `test_hybrid_search`, `test_code_extractor`,
  `test_multi_project`, `test_coverage_holes`, `test_text_chunker`,
  `test_ui_static`, `test_cursor`, plus MCP-specific `mcp_dispatcher`,
  `mcp_tools`, `mcp_security`, `mcp_dispatch`, `mcp_stdio`, and `mcp_sse`
  (these compile the MCP sources directly, bypassing OmniNet, to test the
  dispatcher/security/transport logic in isolation). Some tests need the
  GGUF models in `models/` and are run with
  `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`.
- **Line coverage**: `-DAPOLLO_COVERAGE=ON` instruments project targets
  (not deps) with gcov; `cmake --build build --target coverage` runs ctest
  and prints a per-file summary via `cmake/coverage_report.cmake`. GCC-only,
  no lcov/gcovr.

When adding a C++ test, register it in `tests/CMakeLists.txt` (use the
`add_apollo_test` / `add_mcp_test` helper functions).

---

## Code style guidelines

- **C++23**, compiled with `-Wall -Wextra -Wpedantic` on GCC (toolchain is
  CMake + GCC + cppcheck — no Clang/clang-tidy). Keep new
  code warning-clean.
- Namespaces: library code lives in `namespace apollocore`; MCP server code
  uses the headers directly in `src/mcp_server/`.
- Naming: `snake_case` for functions, variables, and files; `PascalCase` for
  types (`KnowledgeNode`, `QueryResults`); members use trailing underscore
  where applicable (e.g. `dims_`, `current_project_`).
- Heavy use of standard-library types in public interfaces: `std::string`,
  `std::string_view`, `std::optional`, `std::shared_ptr`, `std::unordered_map`.
  JSON at boundaries is `nlohmann::json`.
- Comments: block banner comments (`// ===...===`) for section dividers,
  and explanatory comments that record *why* a non-obvious decision was made
  (e.g. the llama.cpp patch rationale in `CMakeLists.txt`). Follow that
  pattern: document rationale, not mechanics.
- Headers live in `include/apollocore/` with `#pragma once`; implementations
  in `src/`. One module per file pair, named after the feature.
- Keep changes minimal and consistent with the surrounding file. Do not
  reformat unrelated code.
- The web UI is dependency-free vanilla JS ES modules — do not add npm,
  bundlers, or CDN references to it.

## Design conventions to respect

- **Single deliverable**: all new server-facing functionality goes through
  the MCP server (tools/resources/prompts), not through new library
  consumers. Do not install/export `apollo_core`.
- **C++ only, no scripting layer**: no Python, no shell wrappers, no
  container tooling in the repo. Build-time logic lives in CMake.
- **Reads span all projects**: `current_project_` / `shared_read_sources_`
  were deliberately removed; projects are write-time provenance only. Do not
  reintroduce per-project read filtering.
- **Tools carry MCP annotations** (`readOnlyHint`, `destructiveHint`,
  `idempotentHint`) — keep them accurate when adding/changing tools.
- **MCP spec compliance** is pinned to version `2025-11-25`
  (`mcp_security.hpp` `PINNED_VERSION`). Security guards (Origin check,
  protocol-version header, session-id validation) are enforced in
  `mcp_security.cpp`.
- Storage layout is documented in `docs/file-formats.md` (`apollo.db` SQLite
  + `embeddings/*.fbin` with a 16-byte header). Changing on-disk formats
  requires updating that doc.

---

## Security considerations

- **The server runs without authentication by default** and is designed for
  loopback use. When exposing on a network: set `APOLLO_AUTH_TOKEN` (Bearer
  token, RFC 6750), bind with `--host 0.0.0.0`, and front with a TLS reverse
  proxy. There is no rate limiting or multi-tenant isolation.
- `mcp_security.cpp` implements the MCP transport security requirements:
  `Origin` header validation (DNS-rebinding defense),
  `MCP-Protocol-Version` header checking, and strict `Mcp-Session-Id`
  handling. Extend these rather than bypassing them.
- Never commit secrets, model files, or KB data — `models/*.gguf`, `data/`,
  `.env`-style files, `apollo_mcp_server.log`, and the PID file are all
  gitignored.

---

## Deployment

There is no installer or packaging in the repo. Run the built binary
directly (`build/src/mcp_server/apollo_mcp_server --port 8765`), under your
own supervisor if you need one (write your own systemd unit / tmux session /
CI wrapper). See `docs/deployment.md` for ops notes (auth token, TLS
proxying, health checks).

---

## Key documentation

- `README.md` — features, quick start, MCP surface, client setup
- `docs/design.md` — architecture rationale, query/ingestion pipelines
- `docs/mcp-quickstart.md` — 10-step walkthrough to a running client session
- `docs/mcp_compliance.md` — MCP spec compliance notes
- `docs/file-formats.md` — on-disk storage formats
- `docs/deployment.md` — ops notes for running the binary
- `docs/ui.md`, `docs/ui-testing.md` — web UI guide and manual UI checks
