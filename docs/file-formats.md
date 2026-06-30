# ApolloCore File Formats

This document describes all file formats used by ApolloCore for persistence,
storage, and inter-process communication.

---

## Table of Contents

1. [FBIN — Binary Embedding Format](#fbin--binary-embedding-format)
2. [SQLite Database Schema](#sqlite-database-schema)
3. [GGUF — AI Model Format](#gguf--ai-model-format)
4. [JSON — Configuration & Metadata](#json--configuration--metadata)
5. [Format Selection Guide](#format-selection-guide)

---

## FBIN — Binary Embedding Format

**Extension:** `.fbin`
**Purpose:** Compact, fast storage of float32 embedding vectors
**Used by:** Embedding persistence, vector search

### File Layout

```
┌─────────────────────────────────────────────────────────────┐
│                         HEADER (16 bytes)                    │
├──────────┬──────────┬──────────┬──────────┬──────────────────┤
│  Magic   │ Version  │   Dims   │ Reserved │     Data         │
│ "FBIN"   │ uint32   │ uint32   │ uint32   │  float32[]       │
│  4 bytes │ 4 bytes  │ 4 bytes  │ 4 bytes  │  Dims × 4 bytes  │
└──────────┴──────────┴──────────┴──────────┴──────────────────┘
```

### Field Specifications

| Field | Type | Size | Description |
|-------|------|------|-------------|
| Magic | char[4] | 4 | Always `"FBIN"` (0x4642494E) |
| Version | uint32 | 4 | Format version, currently `1` |
| Dimensions | uint32 | 4 | Number of float32 values in data |
| Reserved | uint32 | 4 | Zero-filled, for future use |
| Data | float32[] | Dims × 4 | Embedding values, little-endian |

### Example: 768-Dimensional Embedding

```
Header:  46 42 49 4E   01 00 00 00   00 03 00 00   00 00 00 00
         "FBIN"       version=1     dims=768       reserved=0
Data:    3F 8C CC CD   3F 8F 5C 29   ... (768 × 4 = 3,072 bytes)
         1.10          1.12
Total:   16 + 3,072 = 3,088 bytes
```

### File Sizes

| Dimensions | File Size |
|------------|-----------|
| 128 | 528 bytes |
| 384 | 1,552 bytes |
| 512 | 2,064 bytes |
| 768 | 3,088 bytes |
| 1024 | 4,112 bytes |
| 4096 | 16,400 bytes |

### Usage

```cpp
#include <apollocore/fbin.hpp>

// Save
std::vector<float> embedding(768, 0.5f);
save_fbin("chunk_001.fbin", embedding);

// Load
auto loaded = load_fbin("chunk_001.fbin");

// Validate
if (is_valid_fbin("chunk_001.fbin")) { /* ... */ }

// Memory-mapped (fastest for large files)
size_t size;
const float* data = mmap_fbin("chunk_001.fbin", size);
unmap_fbin(data, size);
```

### Design Rationale

- **No compression**: Decompression overhead not worth it for vectors that are already dense
- **No checksums**: Embeddings are ephemeral — corruption is detected at comparison time
- **Raw float32**: Direct mapping to memory, no conversion needed
- **Little-endian**: Matches x86_64 native byte order
- **Fixed header**: Allows O(1) random access via mmap

---

## SQLite Database Schema

**Extension:** `.db`
**Purpose:** Structured storage of metadata, relationships, and document tree
**Library:** SQLite 3 (linked via `SQLite::SQLite3`)

### Storage Location

```
<knowledge_base_path>/
├── apollo.db           # Main database file
├── apollo.db-wal       # Write-Ahead Log (created at runtime)
├── apollo.db-shm       # Shared memory file (created at runtime)
├── embeddings/         # Binary embedding files (.fbin)
└── config.json         # Optional config
```

### Schema

#### `config` Table

```sql
CREATE TABLE config (
    id              INTEGER PRIMARY KEY,
    name            TEXT    NOT NULL,
    embedding_dim   INTEGER DEFAULT 768,
    embedding_model TEXT,
    created_at      TEXT    DEFAULT CURRENT_TIMESTAMP,
    updated_at      TEXT    DEFAULT CURRENT_TIMESTAMP
);
```


#### `entities` Table

```sql
CREATE TABLE entities (
    id          TEXT    PRIMARY KEY,
    chunk_id    TEXT,
    name        TEXT    NOT NULL,
    type        TEXT    NOT NULL,  -- PERSON, FUNCTION, CLASS, etc.
    description TEXT,
    confidence  REAL    DEFAULT 0.5,
    metadata    TEXT,              -- JSON string
    project_id  TEXT    NOT NULL DEFAULT 'default',  -- M3.C: project scoping
    created_at  TEXT    DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_entities_chunk    ON entities(chunk_id);
CREATE INDEX idx_entities_type     ON entities(type);
CREATE INDEX idx_entities_name     ON entities(name);
CREATE INDEX idx_entities_project  ON entities(project_id);  -- M3.C
```

#### `relationships` Table

```sql
CREATE TABLE relationships (
    id          TEXT    PRIMARY KEY,
    source_id   TEXT    NOT NULL,
    target_id   TEXT    NOT NULL,
    type        TEXT    NOT NULL,  -- FOUNDED, CALLS, INHERITS, etc.
    description TEXT,
    confidence  REAL    DEFAULT 0.5,
    project_id  TEXT    NOT NULL DEFAULT 'default',  -- M3.C: project scoping
    created_at  TEXT    DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_rels_source   ON relationships(source_id);
CREATE INDEX idx_rels_target   ON relationships(target_id);
CREATE INDEX idx_rels_type     ON relationships(type);
CREATE INDEX idx_rels_project  ON relationships(project_id);  -- M3.C
```

#### `projects` Table (M3.C — multi-agent scoping)

```sql
CREATE TABLE projects (
    id         TEXT    PRIMARY KEY,
    name       TEXT    NOT NULL UNIQUE,
    description TEXT,
    path       TEXT    NOT NULL,         -- Project's KB path
    is_shared  INTEGER NOT NULL DEFAULT 0,  -- Readable by other projects
    owner      TEXT,                    -- Agent name or user
    metadata   TEXT,                    -- JSON
    created_at TEXT    DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT    DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_projects_name   ON projects(name);
CREATE INDEX idx_projects_shared ON projects(is_shared);
```

**M3.C Migration:** All existing tables gain a `project_id TEXT NOT NULL
DEFAULT 'default'` column. The `projects` table is created. Pre-existing
data is automatically placed in the `default` project. Schema version is
tracked via `PRAGMA user_version` (set to `1` after M3.C migration).

#### `nodes` Table (with M3.C)

```sql
CREATE TABLE nodes (
    id            TEXT    PRIMARY KEY,
    title         TEXT    NOT NULL,
    content       TEXT,
    parent_id     TEXT,
    document_type TEXT,
    metadata      TEXT,    -- JSON string
    project_id    TEXT    NOT NULL DEFAULT 'default',  -- M3.C
    created_at    TEXT    DEFAULT CURRENT_TIMESTAMP,
    updated_at    TEXT    DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_nodes_parent   ON nodes(parent_id);
CREATE INDEX idx_nodes_project  ON nodes(project_id);  -- M3.C
```

#### `chunks` Table (with M3.C)

```sql
CREATE TABLE chunks (
    id             TEXT    PRIMARY KEY,
    node_id        TEXT    NOT NULL,
    content        TEXT    NOT NULL,
    position       INTEGER,
    embedding_file TEXT,    -- Path to .fbin file
    content_hash   TEXT,    -- MD5 of content (for incremental updates)
    project_id     TEXT    NOT NULL DEFAULT 'default',  -- M3.C
    created_at     TEXT    DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_chunks_node    ON chunks(node_id);
CREATE INDEX idx_chunks_hash    ON chunks(content_hash);
CREATE INDEX idx_chunks_project  ON chunks(project_id);  -- M3.C
```

### PRAGMA Settings

```sql
PRAGMA foreign_keys = ON;        -- Enforce referential integrity
PRAGMA journal_mode = WAL;       -- Write-Ahead Logging for concurrency
```

### Usage

```cpp
#include <apollocore/persistence.hpp>

SQLiteDB db;
if (db.open("apollo.db")) {
    initialize_schema(db);
    
    // Insert
    db.execute(
        "INSERT INTO chunks (id, node_id, content) VALUES (?, ?, ?);",
        {"chunk_001", "node_001", "Hello world"}
    );
    
    // Query
    sqlite3_stmt* stmt = db.prepare("SELECT content FROM chunks WHERE id = ?;");
    db.bind_string(stmt, 1, "chunk_001");
    if (db.step(stmt)) {
        std::string content = db.column_string(stmt, 0);
    }
    db.finalize(stmt);
}
```

### Design Rationale

- **WAL mode**: Allows concurrent readers while a writer is active
- **TEXT primary keys**: Human-readable IDs (e.g., "chunk_001") for debugging
- **No foreign key constraints**: SQLite is in WAL mode with FK enabled, but we use TEXT refs for flexibility
- **JSON in TEXT columns**: Avoids need for separate JSON table; SQLite has JSON1 extension if needed
- **content_hash**: Enables incremental updates — skip re-embedding if content unchanged

---

## GGUF — AI Model Format

**Extension:** `.gguf`
**Purpose:** Storage of LLM and embedding model weights
**Library:** llama.cpp (consumed, not produced)

> **Note:** ApolloCore does **not** generate GGUF files. It only reads them.
> This section documents the format for reference.

### File Layout

```
┌─────────────────────────────────────────────┐
│              Header (GGUF v3)                │
├─────────────────────────────────────────────┤
│  Magic: "GGUF"                              │
│  Version: 3                                 │
│  Tensor count                               │
│  Metadata key-value count                   │
├─────────────────────────────────────────────┤
│           Metadata KV Pairs                  │
│  (general.architecture, tokenizer.*, etc.)  │
├─────────────────────────────────────────────┤
│           Tensor Infos                      │
│  (name, dimensions, type, offset)           │
├─────────────────────────────────────────────┤
│           Tensor Data                       │
│  (quantized weights)                        │
└─────────────────────────────────────────────┘
```

### Quantization Types

| Type | Bits/Weight | Use Case |
|------|-------------|----------|
| F32 | 32 | Full precision (large files) |
| F16 | 16 | Half precision |
| Q8_0 | ~8 | High quality, small reduction |
| Q4_K | ~4.5 | **Most common for local LLMs** |
| Q5_K | ~5.5 | Better quality than Q4 |
| Q6_K | ~6.75 | Near-F16 quality |

### Models Used by ApolloCore

| Model | Type | Size | Purpose |
|-------|------|------|---------|
| nomic-embed-text-v1.5-Q4_K_M | Embedding | 81 MB | Text → 768-dim vectors |
| qwen2.5-0.5b-instruct-q4_k_m | LLM | 469 MB | Entity extraction |

### Usage

```cpp
#include "llama.h"  // From llama.cpp

llama_model_params params = llama_model_default_params();
params.n_gpu_layers = 0;  // CPU only

llama_model* model = llama_model_load_from_file("model.gguf", params);
llama_context* ctx = llama_init_from_model(model, ctx_params);
// ... use model ...
llama_model_free(model);
```

### Design Rationale

- **Use existing standard**: GGUF is the de facto standard for local LLMs
- **Quantized models**: Q4_K_M is the sweet spot for quality vs. file size
- **No training**: ApolloCore is inference-only; no need to write GGUF

---

## JSON — Configuration & Metadata

**Library:** nlohmann/json (header-only)
**Purpose:** Human-readable configuration and metadata

### Locations

| File | Purpose |
|------|---------|
| `config.json` | Knowledge base configuration |
| `metadata` columns | Per-record JSON metadata |
| `chunk_metadata.json` | Optional per-chunk metadata |

### Example: `config.json`

```json
{
  "name": "My Knowledge Base",
  "created_at": "2026-06-21T16:51:20AEST",
  "embedding_model": "nomic-embed-text-v1.5-Q4_K_M",
  "embedding_dim": 768,
  "chunk_size": 512,
  "chunk_overlap": 50,
  "extraction": {
    "enabled": true,
    "model": "qwen2.5-0.5b-instruct-q4_k_m",
    "min_confidence": 0.7
  },
  "search": {
    "semantic_weight": 0.5,
    "keyword_weight": 0.5
  }
}
```

### Example: Metadata in SQLite

```json
{
  "author": "Jane Smith",
  "year": 2024,
  "tags": ["physics", "quantum"],
  "language": "en"
}
```

### Usage

```cpp
#include <nlohmann/json.hpp>

using json = nlohmann::json;

json config = {
    {"name", "My KB"},
    {"embedding_dim", 768}
};

std::string serialized = config.dump();
json parsed = json::parse(serialized);
```

### Design Rationale

- **Human-readable**: Easy to inspect and edit manually
- **Flexible schema**: Add new fields without migration
- **Standard**: Every language has JSON support
- **Nesting**: Supports complex configurations

---

## Format Selection Guide

When to use each format:

```
                    ┌─────────────────────┐
                    │  What are you       │
                    │  storing?           │
                    └──────────┬──────────┘
                               │
           ┌───────────────────┼───────────────────┐
           ▼                   ▼                   ▼
    ┌──────────┐        ┌──────────┐         ┌──────────┐
    │  Float   │        │  Text/   │         │  AI      │
    │  arrays  │        │  Relations│         │  Model   │
    └────┬─────┘        └────┬─────┘         └────┬─────┘
         │                   │                   │
         ▼                   ▼                   ▼
      ┌──────┐         ┌──────────┐         ┌──────────┐
      │ FBIN │         │ SQLite   │         │  GGUF    │
      └──────┘         └──────────┘         └──────────┘
      Embeddings       Metadata             Models
      (binary)         (structured)         (weights)
```

### Decision Matrix

| Use Case | Format | Why |
|----------|--------|-----|
| Embedding vectors | FBIN | Compact, fast, mappable |
| Knowledge graph | SQLite | Joins, indexes, ACID |
| Configuration | JSON | Human-editable |
| Document metadata | SQLite (JSON col) | Queryable + flexible |
| LLM weights | GGUF | Standard for local inference |
| Temporary cache | FBIN | Fast load/save |

---

## Versioning

| Format | Current Version | Backward Compatible |
|--------|-----------------|---------------------|
| FBIN | 1 | Yes (version field reserved) |
| SQLite | 3.46 | Yes (use migrations) |
| GGUF | 3 | Yes (llama.cpp handles) |
| JSON | n/a | Yes (additive only) |

---

## File Size Estimates

For a knowledge base with **N** documents of **M** chunks each:

| Component | Size Formula | Example (1000 docs × 10 chunks) |
|-----------|--------------|----------------------------------|
| SQLite | ~1 KB/chunk | ~10 MB |
| FBIN files | 3 KB/chunk (768d) | ~30 MB |
| JSON metadata | ~500 B/chunk | ~5 MB |
| **Total** | | **~45 MB** |

Add embedding model: +81 MB (nomic-embed)
Add LLM model: +469 MB (qwen2.5)

---

## References

- [FBIN Specification](https://github.com/facebookresearch/faiss/wiki/Indexes-(reader,-writer)) — inspired by FAISS indexes
- [SQLite Documentation](https://www.sqlite.org/docs.html)
- [GGUF Format Spec](https://github.com/ggerganov/ggml/blob/master/docs/gguf.md)
- [nlohmann/json](https://github.com/nlohmann/json)
- [llama.cpp](https://github.com/ggerganov/llama.cpp)
