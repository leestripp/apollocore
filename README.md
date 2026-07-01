# ApolloCore

A C++23 MCP (Model Context Protocol) server that exposes a local
retrieval-augmented knowledge base to AI agents. Any MCP-aware client —
Claude Desktop, Cursor, Cline, Windsurf, Continue.dev — points at it and
gets 12 ready-made tools, 5 resources, and 4 pre-built prompts for searching,
ingesting, and walking documents and codebases.

```text
Claude Desktop ─┐
Cursor          ├──►  POST /mcp  ──►  apollo_mcp_server  ──►  SQLite + FBIN
Cline           ─┘                                  └──►  llama.cpp (embeddings + LLM)
                                                   └──►  tree-sitter (code AST)
```

The server runs entirely on your machine. No API keys, no cloud, no data
leaving the box.

---

## Features

- **Hybrid search** — vector similarity + BM25 keyword + knowledge-graph
  expansion fused with Reciprocal Rank Fusion.
- **Cross-encoder reranking** — opt-in precision boost using the local LLM.
- **HyDE** — Hypothetical Document Embeddings for query expansion.
- **Contextual retrieval** — Anthropic-style context-injected chunks.
- **Knowledge graph** — entities and relationships extracted by the LLM,
  plus tree-sitter-based extraction for code (`FUNCTION`, `CLASS`,
  `NAMESPACE`, …).
- **Multi-project scoping** — multiple agents can share one KB directory,
  each isolated in its own project with optional cross-project reads.
- **Persistent storage** — SQLite for metadata, binary `.fbin` files for
  vectors, one directory per knowledge base.
- **Pluggable backends** — Vulkan / CUDA / Metal / CPU (auto-selected by
  llama.cpp).
- **Single binary** — `apollo_mcp_server` is the entire deliverable.
  Link the static library if you must, but you shouldn't need to.

---

## Quick start

### Prerequisites

Ubuntu 24.04+ (or Debian 13+, or macOS, or WSL):

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
                    libssl-dev zlib1g-dev libsqlite3-dev git
```

Compiler: GCC 13+ or Clang 17+ (C++23). Tested against GCC 15.

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target apollo_mcp_server
```

First build takes ~5 minutes (llama.cpp + OmniNet + their transitive deps).
Subsequent builds are incremental.

### Run

```bash
# Make sure you have models (see "Models" below)
mkdir -p models
# Download Nomic embed (required) and Qwen 0.5B (optional, for extraction/rerank)
./build/src/mcp_server/apollo_mcp_server --port 8765
```

CLI flags: `--host`, `--port`, `--data-dir`, `--embed-model`,
`--llm-model`, `--admin`, `-v`/`--verbose`. Environment variables
(`APOLLO_DATA_DIR`, `APOLLO_EMBED_MODEL`, `APOLLO_LLM_MODEL`,
`OMNINET_DATA_DIR`) are honored.

The server is now listening on `http://0.0.0.0:8765`. Probe it:

```bash
curl -s http://127.0.0.1:8765/health | jq
# → { "status": "ok", "tools": 12, "resources": 5, "prompts": 4, ... }
```

---

## Web UI

Browse to `http://localhost:8765/` and you get a five-tab operator
dashboard served by the same executable:

- **Health** — server info, model status, KB stats (auto-refresh every 5s)
- **Search** — query the KB with the same four modes an agent uses
  (`vector` / `hybrid` / `rerank` / `full`), with per-signal score badges
- **Graph** — force-directed visualisation of the knowledge graph,
  type-coloured nodes, pan/zoom/drag, hover to highlight neighbours
- **Settings** — list and switch projects, see model paths and server config
- **About** — build info and a one-click copy of the agent config snippet

The UI is a regular MCP client — every number on screen was loaded by a
real `POST /mcp` JSON-RPC call. No second API. No CDN. No framework.
~1,500 lines of plain HTML/CSS/ES modules.

Disable with `--no-ui` (the `/mcp`, `/health`, `/info` endpoints keep
working).

Full guide: [`docs/ui.md`](docs/ui.md).

---

## Connecting MCP clients

Any MCP client with the Streamable HTTP transport works.

**Claude Desktop** — edit `~/.config/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "apollocore": {
      "url": "http://127.0.0.1:8765/mcp",
      "transport": "streamable-http"
    }
  }
}
```

Restart Claude Desktop. The 12 `apollo_*` tools appear in every conversation.

**Cursor / Cline / Windsurf / Continue.dev** — add an MCP server entry with
URL `http://127.0.0.1:8765/mcp`. Exact location varies by client; consult
their docs for "custom MCP server URL".

---

## Tools

All tools are JSON-RPC 2.0 methods on `POST /mcp` with JSON Schema 2020-12
inputs. They carry `readOnlyHint` / `destructiveHint` / `idempotentHint`
annotations so the model knows what's safe.

| Tool | What it does |
|------|--------------|
| `apollo_query_kb` | Search the KB. `mode`: `vector` / `hybrid` / `rerank` / `full`. |
| `apollo_add_document` | Ingest a string as a document. Chunks automatically. |
| `apollo_extract_entities` | Run entity extraction over the KB (needs LLM). |
| `apollo_ingest_codebase` | Walk a directory; tree-sitter pulls FUNCTION/CLASS/etc. for code, embeddings for everything else. |
| `apollo_search_symbols` | Find code symbols by name. |
| `apollo_get_neighbors` | Approximate neighborhood lookup in the knowledge graph. |
| `apollo_create_kb` | Create an empty KB at a path. |
| `apollo_open_kb` | Open an existing KB. |
| `apollo_save_kb` | Persist the KB to disk. |
| `apollo_get_stats` | Document / chunk / entity counts. |
| `apollo_set_project` | Switch active project (multi-tenant). |
| `apollo_list_projects` | List projects in the current KB. |

## Resources

URI-addressed read-only data:

| URI | Contents |
|-----|----------|
| `apollo://projects` | Project list (JSON) |
| `apollo://stats` | Per-project stats |
| `apollo://entities` | Entities in the current project |
| `apollo://graph` | Knowledge graph (entities + relationships) |
| `apollo://chunk/{id}` | A single chunk by id |

## Prompts

Pre-built workflows the user can invoke:

| Prompt | Args |
|--------|------|
| `summarize_kb` | — |
| `answer_with_citations` | `question` |
| `code_review_summary` | `path` |
| `entity_research` | `entity` |

---

## Models

ApolloCore needs at least an embedding model. The default is
**nomic-embed-text-v1.5** (768-dim, ~81 MB), quantized Q4_K_M.

For entity extraction, cross-encoder reranking, HyDE, and contextual
retrieval, load an LLM. The default is **Qwen2.5-0.5B-Instruct** (~469 MB).

Download both into `models/`:

```bash
mkdir -p models && cd models

curl -L -O "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf"

curl -L -O "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"
```

Or point at your own paths:

```bash
./apollo_mcp_server \
  --embed-model /path/to/my-embed.gguf \
  --llm-model /path/to/my-llm.gguf
```

Both fields are GGUF. Any model llama.cpp can load with an embedding head
works for the embed slot; any instruct-tuned GGUF works for the LLM slot.

---

## Configuration

| Flag / env var | Default | Notes |
|----------------|---------|-------|
| `--host` / `APOLLO_HOST` | `0.0.0.0` | Bind address |
| `--port` / `APOLLO_PORT` | `8765` | TCP port |
| `--data-dir` / `APOLLO_DATA_DIR` | `data` | KB root directory |
| `--embed-model` / `APOLLO_EMBED_MODEL` | `models/nomic-embed-text-v1.5.Q4_K_M.gguf` | Embedding GGUF |
| `--llm-model` / `APOLLO_LLM_MODEL` | `models/qwen2.5-0.5b-instruct-q4_k_m.gguf` | LLM GGUF (optional) |
| `--admin` | off | Mount OmniNet admin dashboard at `/omninet` |
| `-v` / `--verbose` | off | Debug logging |

The knowledge base itself lives at `<data-dir>/<name>`. Create one with
`apollo_create_kb` or `apollo_open_kb`.

---

## Storage

On disk a KB looks like:

```
my_kb/
├── apollo.db           # SQLite: nodes, chunks, entities, relationships
├── apollo.db-wal       # Write-Ahead Log (auto-managed)
└── embeddings/
    ├── chunk_001.fbin  # 16-byte header (u32 ndim, u32 count) + float32[] vector
    └── chunk_002.fbin
```

About 1 KB of metadata per chunk plus 3,088 bytes per embedding
(768-dim). ~45 MB total for 1,000 documents averaging 10 chunks each.

---

## Build options

```bash
# GPU backends (auto-detected, pick what you have)
cmake -S . -B build -DLLAMA_VULKAN=ON   # AMD / Intel
cmake -S . -B build -DLLAMA_CUBLAS=ON   # NVIDIA
cmake -S . -B build -DLLAMA_METAL=ON    # macOS

# Use system libs instead of FetchContent
cmake -S . -B build -DTREE_SITTER_SKIP_FETCH=ON
cmake -S . -B build -DOMNINET_SKIP_FETCH=ON

# Debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

When the build is done, install the executable:

```bash
cmake --install build --prefix /usr/local
# /usr/local/bin/apollo_mcp_server
```

---

## Development

### Layout

```
ApolloCore/
├── CMakeLists.txt              Top-level build (server, library, tests, models)
├── include/apollocore/         Public headers of the internal apollo_core target
├── src/                        Library: RAG, KG, persistence, hybrid search, …
│   └── mcp_server/             MCP server: dispatcher, tools, resources, prompts
├── tests/                      CTest targets (14 pre-existing + 2 MCP-specific)
├── examples/full_demo.cpp       Standalone CLI demo of the library
├── docs/                       design.md, file-formats.md, mcp-quickstart.md
└── models/                     GGUF files (gitignored)
```

### Running tests

```bash
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

16 test executables, all passing:

- 14 pre-existing (`test_persistence`, `test_fbin`, `test_kb_persistence`,
  `test_apollo_persistence`, `test_v2`, `test_embeddings`,
  `test_entity_extraction`, `test_cross_encoder`, `test_hyde`,
  `test_contextual_retrieval`, `test_hybrid_search`, `test_code_extractor`,
  `test_multi_project`, `test_coverage_holes`)
- 2 MCP-specific (`mcp_dispatcher` — JSON-RPC method routing,
  `mcp_tools` — handler-level tests using a real KB in tmpdir)

### Worked example

See `docs/mcp-quickstart.md` for a ten-step walkthrough from `cmake
--build` to a running Claude Desktop session.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      AI Agent (Claude, Cursor, ...)                 │
│                   speaks MCP JSON-RPC over HTTP                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  POST /mcp  (Streamable HTTP)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                apollo_mcp_server   ←  THE deliverable               │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              OmniNet HTTP+WS server                          │  │
│  │  POST /mcp   → JSON-RPC dispatcher                            │  │
│  │  GET  /mcp   → SSE stream (server-pushed notifications)       │  │
│  │  GET  /health→ JSON {status, tools, resources, prompts}       │  │
│  │  GET  /info  → Server card                                   │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  Dispatcher       — routes methods to registries              │  │
│  │  ServerState      — owns one ApolloCore, manages sessions     │  │
│  │  ToolRegistry     — 12 tools                                 │  │
│  │  ResourceRegistry —  5 resources                             │  │
│  │  PromptRegistry   —  4 prompts                               │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────────────┘
                          │  links against (internal only)
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│   apollo_core  (STATIC LIBRARY — internal CMake target, not installed) │
│       RAG, hybrid search, knowledge graph, entity extraction          │
└─────────────────────────────────────────────────────────────────────┘
                          │  SQLite + FBIN + llama.cpp + tree-sitter
                          ▼
                   files on disk
```

The `apollo_core` static library exists only to organize the RAG code in
one place. It is not installed, not exported, and not meant to be linked
into downstream code.

---

## Stack

| | |
|---|---|
| Language | C++23 (GCC 13+ / Clang 17+) |
| Build | CMake ≥ 3.20, FetchContent for everything |
| HTTP / WS | [OmniNet](https://gitlab.com/leestripp/omninet) (C++23, MIT) |
| Inference | [llama.cpp](https://github.com/ggerganov/llama.cpp) (Vulkan / CUDA / Metal / CPU) |
| Code parsing | [tree-sitter](https://tree-sitter.github.io/) v0.26 |
| Metadata | SQLite 3.46 |
| Vector storage | sqlite-vec (loaded as a SQLite extension) |
| JSON | nlohmann/json 3.11 |
| JSON-RPC | hand-rolled (`src/mcp_server/jsonrpc.hpp`) |
| TLS | OpenSSL 3 |

---

## Project status

All planned phases of the RAG pipeline are done and tested:

- llama.cpp integration ✔
- Embedding model (nomic-bert) ✔
- Hybrid search (vector + BM25 + RRF) ✔
- Smart chunking ✔
- Knowledge graph ✔
- Entity extraction (LLM-driven) ✔
- Cross-encoder reranking ✔
- HyDE ✔
- Contextual retrieval ✔
- SQLite persistence ✔
- FBIN embedding storage ✔
- tree-sitter code extraction (M3.B) ✔
- Multi-project scoping (M3.C) ✔
- MCP server (Streamable HTTP, OmniNet) ✔

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

Built on:

- [llama.cpp](https://github.com/ggerganov/llama.cpp) by Georgi Gerganov
- [OmniNet](https://gitlab.com/leestripp/omninet) by Lee Stripp
- [nlohmann/json](https://github.com/nlohmann/json) by Niels Lohmann
- [SQLite](https://www.sqlite.org/) by D. Richard Hipp
- [tree-sitter](https://tree-sitter.github.io/) by Max Brunsfeld
- [Nomic AI](https://www.nomic.ai/) for the embedding model
- [Qwen team](https://qwenlm.github.io/) for the language model

Inspired by:

- [Anthropic's Contextual Retrieval](https://www.anthropic.com/engineering/contextual-retrieval)
- [Microsoft GraphRAG](https://www.microsoft.com/en-us/research/project/graphrag/)
- [LightRAG](https://arxiv.org/abs/2410.05779)