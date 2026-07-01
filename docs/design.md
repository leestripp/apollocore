# ApolloCore Design Document

The architectural design behind `apollo_mcp_server` and the RAG pipeline it
exposes. Read this if you want to understand *why* the code is shaped this
way, not just *what it does*.

---

## Table of Contents

1. [Goals & non-goals](#goals--non-goals)
2. [RAG landscape in 2026](#rag-landscape-in-2026)
3. [System architecture](#system-architecture)
4. [Module breakdown](#module-breakdown)
5. [The query pipeline](#the-query-pipeline)
6. [The ingestion pipeline](#the-ingestion-pipeline)
7. [Storage layout](#storage-layout)
8. [Embedding model integration](#embedding-model-integration)
9. [MCP surface — how the agent sees ApolloCore](#mcp-surface)
10. [Build system](#build-system)
11. [Future work](#future-work)
12. [References](#references)

---

## Goals & non-goals

**Goals:**

- One executable (`apollo_mcp_server`) that exposes a complete RAG
  knowledge base to MCP-aware agents — no library to link, no SDK to embed.
- Local-first. No API keys, no cloud, no data leaving the machine.
- High recall from hybrid search (vector + BM25 + graph) fused with RRF.
- Real, executable code at every layer — no aspirational abstractions.

**Non-goals:**

- Server-side answer generation. Agents generate answers themselves with
  retrieved chunks as context. ApolloCore exposes the retrieval and the
  knowledge graph; the LLM that synthesises the answer is the agent's.
- Distributed deployment. One process per agent or shared agent pool.
- Multi-language model serving other than what llama.cpp can load (GGUF).
- Pre-2014 transport protocols (SSE-only, stdio MCP). We speak Streamable
  HTTP only — clients catch up when they want to.

---

## RAG landscape in 2026

### Findings from research

| Technique | Status | Impact | Source |
|-----------|--------|--------|--------|
| **Hybrid Search (BM25 + Vector)** | Industry Standard | +40% recall improvement | MongoDB, Qdrant, OpenSearch |
| **Reciprocal Rank Fusion (RRF)** | Dominant Fusion Method | No score normalization needed | Azure AI Search, OpenSearch |
| **GraphRAG (Microsoft)** | Production Ready | Better global query answering | Microsoft Research |
| **LightRAG** | Rising | 10× faster than GraphRAG | arXiv:2410.05779 |
| **Contextual Retrieval (Anthropic)** | Best Practice | Reduces chunk decontextualization | anthropic.com/engineering/contextual-retrieval |
| **Cross-Encoder Reranking** | Production | Top-5 precision boost | Cohere, Jina AI |
| **HyDE** | Established | Query expansion via hypothetical answer | [HyDE paper](https://arxiv.org/abs/2212.10496) |
| Self-RAG | Academic | Adaptive retrieval/critique | arXiv:2310.11511 |
| RAPTOR | Academic | Hierarchical summarization | ICLR 2024 |
| ColBERT | Emerging | Token-level late interaction | SIGIR 2020 |

### What ships in ApolloCore

- Hybrid search: vector + BM25 + graph, fused with RRF (M2.7).
- Cross-encoder reranking using the local LLM (M2.8).
- HyDE query enhancement.
- Anthropic-style contextual retrieval.
- LLM-driven entity & relationship extraction.
- Tree-sitter-based code symbol extraction (M3.B).
- Multi-project scoping with optional cross-project reads (M3.C).

### What doesn't ship (yet)

- Hierarchical community detection (Leiden/Louvain) and global queries.
- Self-RAG reflection tokens.
- ColBERT late-interaction reranking.
- Production RAGAS / BEIR benchmarks.
- Multi-language GGUF "anything works" — we ship with sensible defaults;
  power users supply their own models.

---

## System architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      AI Agent (Claude, Cursor, ...)                 │
│                   speaks MCP JSON-RPC over HTTP                      │
└──────────────────────────────┬──────────────────────────────────────┘
                               │  POST /mcp  (Streamable HTTP)
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                apollo_mcp_server                                    │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              OmniNet HTTP+WS server                          │  │
│  ├──────────────────────────────────────────────────────────────┤  │
│  │  Dispatcher       — routes JSON-RPC methods to handlers     │  │
│  │  ServerState      — owns one ApolloCore, maps session → pro… │  │
│  │  ToolRegistry     — 12 outcome-oriented tools                │  │
│  │  ResourceRegistry —  5 URI-addressed data sources             │  │
│  │  PromptRegistry   —  4 pre-built prompt templates            │  │
│  └──────────────────────┬───────────────────────────────────────┘  │
└─────────────────────────┼───────────────────────────────────────────┘
                          │  internal link (organizational only)
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│   apollo_core  (STATIC LIBRARY — internal CMake target, not installed) │
│       RAG, hybrid search, knowledge graph, entity extraction          │
└─────────────────────────────────────────────────────────────────────┘
                          │  SQLite + FBIN + llama.cpp + tree-sitter
                          ▼
                   files on disk
```

The `apollo_core` static library is a build-system organization primitive
only. It is not installed, not exported, not meant to be linked into
downstream code. Removing it as a target would mean inlining its 14
sources into the server executable — would make a 6,000-line monolith
harder to navigate. As a private library it stays as a CMake nicety.

---

## Module breakdown

### Top of stack (`src/mcp_server/`)

| File | Purpose |
|------|---------|
| `main.cpp` | CLI parsing, model loading, OmniNet bootstrap |
| `http_server.cpp` | OmniNet HTTP routes: `/mcp`, `/health`, `/info` |
| `dispatcher.cpp` | Routes JSON-RPC methods to registries |
| `jsonrpc.cpp` | Envelope parser/serializer |
| `server_state.cpp` | Owns `ApolloCore`, manages `session_id → project_id` |
| `tools.cpp` | 12 `apollo_*` tool handlers |
| `resources.cpp` | 5 `apollo://*` resource handlers |
| `prompts.cpp` | 4 prompt templates |
| `CMakeLists.txt` | Links server, library, OmniNet, threads |

The stack is layered: HTTP → JSON-RPC → dispatcher → registries →
ApolloCore methods. Each layer only knows about the one below.

### Library (`src/` — internal `apollo_core` target)

| File | Purpose |
|------|---------|
| `embedding_model.cpp` | llama.cpp-backed embeddings (768-dim default) |
| `entity_extractor.cpp` | LLM-driven entity & relationship extraction |
| `cross_encoder.cpp` | LLM-based reranking |
| `hyde.cpp` | Hypothetical Document Embeddings |
| `contextual_retrieval.cpp` | Anthropic-style chunk enrichment |
| `persistence.cpp` | SQLite schema + migrations |
| `kb_persistence.cpp` | Project registry + M3.C scoping |
| `apollo_persistence.cpp` | The unified `ApolloCore` class (v2 wiring) |
| `apollo_hybrid_search.cpp` | Vector + BM25 + RRF fusion |
| `apollo_ts_wrapper.cpp` | tree-sitter C++ RAII |
| `apollo_code_extractor.cpp` | AST → entities/relationships |
| `fbin.cpp` | Binary embedding read/write |
| `apollo_core.cpp` | Intentionally empty (legacy header-only API) |

### Public headers (`include/apollocore/`)

These describe the internal library's surface. Even though external code
shouldn't link `apollo_core`, the headers are still the canonical
documentation of "what methods exist on ApolloCore":

| Header | Class / concept |
|--------|------------------|
| `apollocore.hpp` | Legacy single-header API with `HashEmbedding` fallback |
| `apollocore_v2.hpp` | V2 umbrella (legacy; superseded) |
| `apollo_persistence.hpp` | The actual `ApolloCore` class — where everything wires up |
| `kb_persistence.hpp` | `KnowledgeBasePersistence` (lower level) |
| `entity_extractor.hpp` | LLM entity/relationship extraction |
| `cross_encoder.hpp` | Cross-encoder reranker |
| `hyde.hpp` | HyDE query enhancer |
| `contextual_retrieval.hpp` | Contextual chunking |
| `persistence.hpp` | Schema & migrations |
| `fbin.hpp` | Binary embedding I/O |
| `apollo_hybrid_search.hpp` | `BM25Index`, `HybridResult` |
| `apollo_ts_wrapper.hpp` | `TSParser`, `TSNode` |
| `apollo_code_extractor.hpp` | `extract_code()` |
| `apollo_codebase_ingest.hpp` | `ingest_directory()` (declared; impl is in server) |
| `apollo_hybrid_search.hpp` | Already listed |

---

## The query pipeline

Four pipeline modes are exposed, ordered by cost vs. quality.

### Mode: `vector` (fastest, pure semantic)

```text
query  ──►  embed (llama.cpp)  ──►  cosine over FBIN files  ──►  top-K
```

Single signal: dense semantic similarity. Good for "find me passages like
this one". Misses exact keyword matches that don't appear in the
embedding's training data.

### Mode: `hybrid` (default)

```text
                              ┌──►  vector cosine  ──► rank₁ ──┐
query  ──►  embed (llama.cpp)  ──┤                              │
       └──►  BM25 over chunks  ────►  rank₂ ──┤
                              └──►  graph expand ──►  rank₃ ──┘
                                                ▼
                                       RRF fusion (k=60)
                                                ▼
                                            top-K
```

Three independent signals are searched in parallel:

- **Vector:** cosine similarity over chunk embeddings (magnitude-aware).
- **BM25:** classic Okapi BM25 over a tokenized index of every chunk in the
  active project. Tokens are lowercased, alphanumeric-only, with a small
  built-in stop-word set.
- **Graph:** a lightweight expansion — chunks containing query-relevant
  entities score higher via the entity embedding lookup in
  `kb_persistence`.

The three ranked lists are fused with Reciprocal Rank Fusion:

```
RRF_score(d) = Σᵢ  1 / (k + rankᵢ(d))      with k = 60
```

The same formula is used by Azure AI Search, OpenSearch, Qdrant, and most
production hybrid-search stacks — it sidesteps score normalization entirely.

### Mode: `rerank`

Hybrid scoring, then a single round of LLM-based reranking. The local LLM
scores each candidate as a relevance judgment; candidates are re-ordered
and the top-K returned.

### Mode: `full`

Hybrid + rerank, end to end. Highest precision, slowest.

### Why no "complete answer"

ApolloCore returns chunks with score, not prose. Generation is the agent's
job — the agent already has an LLM and it sees the chunks verbatim.
Returning synthesized text would (a) duplicate the LLM work, (b) make
context citations harder, (c) preclude RAG architectures where multiple
agents gather chunks independently.

---

## The ingestion pipeline

### `apollo_add_document(text, doc_id, chunk_size)` — happy path

```
text  ──►  chunk by character window  ──►  embed each chunk (llama.cpp)
       ──►  save to FBIN
       ──►  write SQLite row in `nodes` + `chunks` (scoped to current project)
```

The default chunk strategy is a 512-character sliding window per the
existing chunker. `chunk_size` is configurable per call.

### `apollo_ingest_codebase(path, project)` — code-aware path

```
path  ──►  walk recursively  ──►  per file:
            ├──  .cpp/.h/.py/etc. ──►  tree-sitter AST  ──►  entities
            └──  anything else      ──►  chunk + embed
       ──►  optionally run entity extraction (LLM)
```

This is the inverse of "tools that hide the implementation": the agent
walks its own codebase, then asks the KB about what it just walked. We do
this rather than training fine-tuned embeddings because:

- The model keeps full source-level ground truth (chunks are byte-exact
  source, not summaries).
- Re-indexing is cheap and deterministic.
- No retraining lag when a function changes.

### Entity extraction (`apollo_extract_entities`)

Walks every chunk in the KB and asks the local LLM to emit entities with
`type ∈ {PERSON, ORGANIZATION, LOCATION, EVENT, CONCEPT, PRODUCT, DATE,
NUMBER, DISEASE, DRUG, MEDICAL_PROCEDURE, SCIENTIFIC_TERM, FUNCTION,
CLASS, FILE, MODULE, NAMESPACE, VARIABLE, ENUM, MACRO}`. Type coverage
includes code-aware categories so the same extractor works on prose and
on source code.

The agent can then ask for neighbors of an entity via
`apollo_get_neighbors`, which is implemented as: find chunks that mention
the entity, list the other entities in those chunks.

---

## Storage layout

```
my_kb/
├── apollo.db           # SQLite: nodes, chunks, entities, relationships, projects
├── apollo.db-wal       # Write-Ahead Log (auto-managed by SQLite)
└── embeddings/
    ├── chunk_001.fbin  # 16-byte header + float32[] vector
    └── chunk_002.fbin
```

About 1 KB of SQLite metadata per chunk + 3 KB per 768-dim FBIN vector.
Roughly **45 MB total** for 1,000 docs averaging 10 chunks each.

### SQLite tables (current schema)

```
nodes           (id PK, title, content, source_path, project_id, created_at)
chunks          (id PK, node_id FK, content, project_id, embedding_file)
entities        (id PK, name, type, description, project_id, source_chunk_id)
relationships   (id PK, source, target, type, project_id, source_chunk_id)
projects        (name PK, description, is_shared, owner, created_at)
migrations      (version INT PK, applied_at)
```

All node/scoped tables have a `project_id` column added in the v1
migration that introduced M3.C. Reads can optionally include data from
projects marked `is_shared = 1`.

Full schema in `docs/file-formats.md`. Field-level documentation lives in
the SQLite migration code (`src/persistence.cpp`).

---

## Embedding model integration

### Default embedding model

`nomic-embed-text-v1.5.Q4_K_M` — 768 dimensions, ~81 MB on disk.
Quantized Q4_K_M, near-lossless for retrieval. Hosted in
`models/nomic-embed-text-v1.5.Q4_K_M.gguf` (auto-downloaded by
`models/CMakeLists.txt`).

### Default LLM

`qwen2.5-0.5b-instruct-q4_k_m.gguf` — used for entity extraction,
cross-encoder reranking, HyDE, contextual retrieval. ~469 MB on disk.

### GPU selection

llama.cpp picks the first available device, in this order:
Vulkan → CUDA → Metal → CPU. Selection is controllable via the build
options (`-DLLAMA_VULKAN=ON`, `-DLLAMA_CUBLAS=ON`, etc.).

ApolloCore doesn't manage model lifecycle: it's "load once at startup,
keep warm, unload on shutdown". The MCP server owns one model pair; a
single process is the design, not a model-pool abstraction.

---

## MCP surface

This section is the **public contract** of the project — what every
agent-compatible client sees.

### Transport

`POST /mcp` over Streamable HTTP. JSON-RPC 2.0 in, JSON-RPC 2.0 out.
Optional `Mcp-Session-Id` header round-trips a UUID v4 session id.

### Tools (12)

All service-prefixed `apollo_*`. Each has a JSON Schema 2020-12 input
schema and the appropriate `readOnlyHint`/`destructiveHint`/`idempotentHint`
annotations.

| Tool | Reads | Writes | Idempotent |
|------|-------|--------|------------|
| `apollo_query_kb` | ✓ | | ✓ |
| `apollo_add_document` | | ✓ | |
| `apollo_extract_entities` | | ✓ | ✓ |
| `apollo_ingest_codebase` | | ✓ | |
| `apollo_search_symbols` | ✓ | | ✓ |
| `apollo_get_neighbors` | ✓ | | ✓ |
| `apollo_create_kb` | | ✓ | |
| `apollo_open_kb` | | ✓ | |
| `apollo_save_kb` | | ✓ | ✓ |
| `apollo_get_stats` | ✓ | | ✓ |
| `apollo_set_project` | | ✓ | |
| `apollo_list_projects` | ✓ | | ✓ |

### Resources (5)

`apollo://projects`, `apollo://stats`, `apollo://entities`,
`apollo://graph`, `apollo://chunk/{id}`. URIs the agent can subscribe to
or fetch on demand.

### Prompts (4)

`summarize_kb`, `answer_with_citations`, `code_review_summary`,
`entity_research`. Pre-built workflows the user can invoke from the
client UI.

### Error policy

- Transport-level failures (bad JSON, missing id) → JSON-RPC error envelope
  (`code`, `message`, `data`).
- Tool-level failures (bad args, KB not open, no model loaded) →
  `CallToolResult { isError: true, content: [{type:"text", text: "..."}] }`
  with an actionable message.

The LLM then sees the tool error as an observation and self-corrects on
the next turn. Transport-level errors don't carry enough domain context
for the model to recover.

Full worked example in `docs/mcp-quickstart.md`.

---

## Build system

Top-level `CMakeLists.txt` uses `FetchContent` for everything except
SQLite3 and (optionally) Vulkan. The fetch graph:

```
nlohmann_json (via URL)
    │
llama.cpp  (GIT master)
    └── ggml + Vulkan compute shaders
tree-sitter (GIT v0.26.9)
tree-sitter-cpp (GIT master)
OmniNet  (GIT main) ───►  nlohmann_json, sqlite-vec, OpenSSL, sqlite3
    │
ApolloCore ───►  apollo_core (static lib, internal)
                 apollo_mcp_server (executable, the only product)
                 tests/ (16 CTest binaries)
                 examples/full_demo
                 models/ (auto-downloads GGUF on first configure)
```

Build options:

- `-DCMAKE_BUILD_TYPE=Release|Debug`
- `-DLLAMA_VULKAN=ON` (AMD/Intel), `-DLLAMA_CUBLAS=ON` (NVIDIA),
  `-DLLAMA_METAL=ON` (Apple)
- `-DTREE_SITTER_SKIP_FETCH=ON` / `-DOMNINET_SKIP_FETCH=ON` (use system libs)

First configure is ~5 min (builds llama.cpp + OmniNet + transitive deps);
subsequent builds are incremental.

---

## Future work

- Hierarchical community detection (Leiden/Louvain) and global queries.
  Requires a graph-traversal API surface beyond the current `get_neighbors`.
- Self-RAG reflection tokens. Would require plumbing a second LLM call
  per token of output.
- ColBERT late interaction for token-level reranking precision.
- Production RAGAS / BEIR benchmarks — needs a curated eval dataset.
- MCP 2026-07-28 migration. The current dispatcher is architected so
  swapping the `initialize` handshake for an `_meta`-per-request model
  is mostly a one-file change.

---

## References

1. **MCP 2025-11-25 spec** — modelcontextprotocol.io/specification/2025-11-25
2. **MCP 2026-07-28 RC** — blog.modelcontextprotocol.io/posts/2026-07-28-release-candidate/
3. **GraphRAG** — Microsoft Research, "From Local to Global: A GraphRAG
   Approach to Query-Focused Summarization" (arXiv:2404.16130)
4. **LightRAG** — "LightRAG: Simple and Fast Retrieval-Augmented Generation"
   (arXiv:2410.05779)
5. **RAPTOR** — "Recursive Abstractive Processing for Tree-Organized
   Retrieval" (ICLR 2024)
6. **Self-RAG** — arXiv:2310.11511
7. **Anthropic Contextual Retrieval** — anthropic.com/engineering/contextual-retrieval
8. **RRF** — Azure AI Search, OpenSearch, Qdrant documentation
9. **ColBERT** — SIGIR 2020
10. **HyDE** — arXiv:2212.10496
11. **llama.cpp** — github.com/ggerganov/llama.cpp
12. **OmniNet** — gitlab.com/leestripp/omninet