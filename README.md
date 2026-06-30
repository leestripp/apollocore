![ApolloCore banner image](resources/banner.png)

# ApolloCore

> **A state-of-the-art, production-ready C++23 RAG (Retrieval-Augmented Generation) knowledge base framework with local AI inference.**

ApolloCore is a self-contained, embeddable knowledge base that runs **100% locally** with no external API dependencies. It combines vector search, keyword search, knowledge graphs, entity extraction, and LLM-powered query enhancement into a single high-performance C++23 library.

---

## 🌟 Features

| Feature | Description |
|---------|-------------|
| 🧠 **Local LLM Inference** | Run embedding models and LLMs locally via llama.cpp |
| 🔍 **Hybrid Search** | Combines vector similarity (semantic) + BM25 (keyword) via RRF |
| 💾 **Persistent Storage** | SQLite (metadata) + binary embeddings (.fbin) |
| 🌐 **Knowledge Graph** | Entity & relationship extraction (LLM + manual + tree-sitter) |
| 🎯 **Cross-Encoder Reranking** | LLM-based result reranking for precision |
| 💡 **HyDE** | Hypothetical Document Embeddings for query enhancement |
| 📄 **Contextual Retrieval** | Context-aware chunk processing |
| 🌳 **Code-Aware Extraction** | tree-sitter AST parsing auto-extracts FUNCTION/CLASS/etc. |
| 👥 **Multi-Project Scoping** | Multiple agents share one KB, isolated by project |
| 📚 **Static Library** | Drop into your agent — single `libapollo_core.a`, no MCP/HTTP deps |
| ⚡ **High Performance** | C++23, zero-copy, mmap-backed embeddings, Vulkan GPU |

---

## 📋 Table of Contents

- [Quick Start](#-quick-start)
- [Installation](#-installation)
- [Architecture](#-architecture)
- [API Reference](#-api-reference)
- [Examples](#-examples)
- [File Formats](#-file-formats)
- [Building](#-building)
- [Testing](#-testing)
- [Project Status](#-project-status)
- [Contributing](#-contributing)
- [License](#-license)

---

## 🚀 Quick Start

### 1. Build ApolloCore

```bash
git clone https://github.com/yourname/ApolloCore.git
cd ApolloCore

# Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 2. Download a Model

```bash
# Create models directory
mkdir -p models
cd models

# Download embedding model (~81MB)
curl -L -O "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf"

# (Optional) Download LLM for entity extraction (~469MB)
curl -L -O "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"
```

### 3. Run the Example

```bash
./build/examples/full_demo
```

### 4. Minimal Code Example

```cpp
#include <apollocore/apollo_persistence.hpp>
#include <iostream>

using namespace apollocore_v2;

int main() {
    ApolloCore kb;
    
    // Load embedding model
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    
    // Create a knowledge base
    kb.create("data/my_kb", "My Knowledge Base");
    
    // Add documents (auto-persisted to disk)
    kb.add_document(
        "Albert Einstein developed the theory of relativity. "
        "He worked at Princeton University. "
        "His theory changed modern physics.",
        "doc_einstein",  // custom doc_id
        512              // chunk size in characters
    );
    
    // Query the knowledge base
    auto results = kb.query("relativity Einstein", 5);
    for (const auto& [content, score] : results) {
        std::cout << "Score " << score << ": " << content << "\n";
    }
    
    return 0;
}
```

---

## 📦 Installation

### System Requirements

- **OS**: Linux (Ubuntu 20.04+), macOS, or Windows with WSL
- **Compiler**: GCC 13+ or Clang 17+ (C++23 support required)
- **CMake**: 3.20+
- **RAM**: 4GB minimum, 8GB recommended
- **Disk**: 1GB for models + storage for your data

### Dependencies

All dependencies are automatically fetched by CMake:

| Dependency | Version | Purpose |
|------------|---------|---------|
| [llama.cpp](https://github.com/ggerganov/llama.cpp) | master | Local LLM/embedding inference |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON serialization |
| [SQLite3](https://www.sqlite.org/) | 3.46+ | Metadata storage |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) | v0.26.9 | C++ AST parsing for code entities |

Quick install on Ubuntu/Debian:
```bash
sudo apt install -y libssl-dev zlib1g-dev libsqlite3-dev cmake build-essential
```

### Build Options

```bash
# Release build (default, recommended)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# With specific GPU support
cmake -S . -B build -DLLAMA_CUBLAS=ON  # NVIDIA CUDA
cmake -S . -B build -DLLAMA_VULKAN=ON  # AMD/Intel Vulkan
cmake -S . -B build -DLLAMA_METAL=ON   # Apple Metal
```

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     ApolloCore API                          │
│           (apollo_persistence.hpp — single header)          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   Hybrid     │  │   Code       │  │   Context    │     │
│  │   Search     │  │   Extractor  │  │   Retrieval  │     │
│  │  (Vector+BM25)│ │  (tree-sitter)│  │              │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                 │                 │              │
│  ┌──────┴─────────────────┴─────────────────┴───────┐     │
│  │              Knowledge Graph                       │     │
│  │   (entities + relationships + project scoping)    │     │
│  └──────────────────────┬─────────────────────────────┘     │
│                         │                                   │
│  ┌──────────────────────┴─────────────────────────────┐    │
│  │            Embedding & Inference Engine              │    │
│  │    (llama.cpp wrapper for local models)              │    │
│  └──────────────────────┬─────────────────────────────┘    │
│                         │                                   │
│  ┌──────────────────────┴─────────────────────────────┐    │
│  │              Persistence Layer                       │    │
│  │   ┌──────────┐  ┌──────────┐  ┌──────────┐         │    │
│  │   │ SQLite   │  │  FBIN    │  │ Projects │         │    │
│  │   │  (data)  │  │(vectors) │  │  (M3.C)  │         │    │
│  │   └──────────┘  └──────────┘  └──────────┘         │    │
│  └──────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                              │
                  ┌───────────┴───────────┐
                  │  libapollo_core.a    │  ← Single static library
                  │  (link this)         │
                  └─────────────────────┘
```

### Module Overview

| Module | Header | Purpose |
|--------|--------|---------|
| `ApolloCore` | `apollo_persistence.hpp` | Unified high-level API (with persistence) |
| `EntityExtractor` | `entity_extractor.hpp` | Extract entities/relationships via LLM |
| `HybridSearch` | `apollo_hybrid_search.hpp` | Vector + BM25 + graph combined via RRF |
| `CrossEncoderReranker` | `cross_encoder.hpp` | LLM-based reranking for precision |
| `HydeEnhancer` | `hyde.hpp` | Hypothetical Document Embeddings |
| `ContextGenerator` | `contextual_retrieval.hpp` | Anthropic-style contextual retrieval |
| `EmbeddingModel` | `embedding_model.hpp` | GGUF text → 768-dim vectors |
| `OnnxEmbeddingModel` | `onnx_embedding.hpp` | ONNX-based embeddings |
| `KnowledgeBasePersistence` | `kb_persistence.hpp` | SQLite + FBIN + project registry |
| `CodeExtractor` | `apollo_code_extractor.hpp` | tree-sitter AST → entities/relationships |
| `TSParser` / `TSNode` | `apollo_ts_wrapper.hpp` | tree-sitter C++ RAII wrapper |

---

## 📚 API Reference

### Core Classes

#### `ApolloCore`

The main entry point. Provides a unified API combining embeddings, search, knowledge graph, persistence, and project scoping.

```cpp
#include <apollocore/apollo_persistence.hpp>

apollocore_v2::ApolloCore kb;
```

**Model Management:**
```cpp
bool load_embedding_model(const std::string& path, bool use_gpu = false);
bool load_llm_model(const std::string& path, bool use_gpu = false);
bool is_llm_loaded() const;
```

**Knowledge Base Management:**
```cpp
static bool kb_exists(const std::string& path);
bool create(const std::string& path, const std::string& name = "Untitled");
bool open(const std::string& path);
void close();
bool save();
```

**Project Scoping (M3.C — multi-agent):**
```cpp
bool set_current_project(const std::string& project_name);
std::string current_project() const;
bool register_project(const std::string& name,
                      const std::string& description = "",
                      bool is_shared = false,
                      const std::string& owner = "");
nlohmann::json list_projects() const;
bool add_shared_read_source(const std::string& project_name);
bool delete_project(const std::string& name, bool confirm);
```

**Data Operations:**
```cpp
size_t add_document(const std::string& text, size_t chunk_size = 512);
size_t add_document(const std::string& text,
                    const std::string& custom_doc_id,
                    size_t chunk_size = 512);
std::string add_entity(const std::string& name,
                       const std::string& type,
                       const std::string& chunk_id = "",
                       const std::string& description = "",
                       float confidence = 1.0f);
std::string add_relationship(const std::string& source,
                             const std::string& target,
                             const std::string& type,
                             const std::string& description = "",
                             float confidence = 1.0f);
std::vector<std::pair<std::string, float>> query(
    const std::string& query_text, size_t top_k = 5);
std::vector<search::HybridResult> query_hybrid(
    const std::string& query_text, size_t top_k = 5,
    double alpha = 0.5, double beta = 0.3, double gamma = 0.2);
std::vector<RerankedResult> query_with_rerank(
    const std::string& query_text, size_t top_k = 5,
    size_t candidate_k = 0);
std::vector<RerankedResult> query_full(
    const std::string& query_text, size_t top_k = 5);
std::vector<Entity> get_entities(size_t limit = 100);
std::vector<Relationship> get_relationships(size_t limit = 100);
std::vector<Entity> get_neighbors(const std::string& entity_name, size_t hops = 1);
```

**Query Modes — pick the right one:**

| Method | Signals | Speed | Best for |
|--------|---------|-------|----------|
| `query()` | Vector only | Fastest | Pure semantic similarity |
| `query_hybrid()` | Vector + BM25 + graph | Fast | Better recall (catches exact matches) |
| `query_with_rerank()` | Vector → LLM rerank | Slower | Precision (filters false positives) |
| `query_full()` | Hybrid → LLM rerank | Slowest | Highest quality |

**Statistics:**
```cpp
struct Stats {
    size_t document_count;
    size_t chunk_count;
    size_t entity_count;
    size_t relationship_count;
    size_t chunks_with_entities;
    size_t embedding_files_size;
};
Stats get_stats();
```

### Code Extraction (M3.B — tree-sitter)

For automatic code-aware entity extraction:

```cpp
#include <apollocore/apollo_code_extractor.hpp>

apollocore_v2::CodeModel model = apollocore_v2::extract_cpp(source, "src/foo.cpp");
for (const auto& e : model.entities) {
    std::cout << "[" << e.type << "] " << e.name
              << " (line " << e.start_line << ")\n";
}
for (const auto& r : model.relationships) {
    std::cout << r.source << " --[" << r.type << "]--> " << r.target << "\n";
}
```

### Entity Extraction

```cpp
#include <apollocore/entity_extractor.hpp>

apollocore_v2::ExtractionConfig config;
config.llm.backend = apollocore_v2::ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
config.llm.model_path = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";

apollocore_v2::EntityExtractor extractor(config);
auto result = extractor.extract("Albert Einstein worked at Princeton...");

for (const auto& entity : result.entities) {
    std::cout << entity.name << " (" << entity.type << ")\n";
}
```

### Persistence (Lower-level)

For more control, use `KnowledgeBasePersistence` directly:

```cpp
#include <apollocore/kb_persistence.hpp>

apollocore_v2::KnowledgeBasePersistence kb;

// Create or open
kb.create("data/my_kb", "My KB");
kb.open("data/my_kb");

// Save data
kb.save_chunk("chunk_001", "node_001", "content", 0, embedding);
kb.save_entity("e1", "chunk_001", "Einstein", "PERSON", "physicist", 0.95f);
kb.save_relationship("r1", "e1", "e2", "WORKED_AT", "desc", 0.9f);

// Load data
auto chunks = kb.load_all_chunks();
auto entities = kb.load_all_entities();
```

---

## 💡 Examples

### Example 1: Build a Document Search Engine

```cpp
#include <apollocore/apollo_persistence.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace apollocore_v2;

int main() {
    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");

    kb.create("data/research", "Research Papers");

    // Read all text files from a directory
    for (const auto& entry : std::filesystem::directory_iterator("papers/")) {
        if (entry.path().extension() == ".txt") {
            std::ifstream f(entry.path());
            std::stringstream ss;
            ss << f.rdbuf();
            kb.add_document(ss.str(), entry.path().string(), 512);
        }
    }

    // Search
    std::cout << "Enter query: ";
    std::string query;
    std::getline(std::cin, query);

    auto results = kb.query(query, 5);
    for (const auto& [content, score] : results) {
        std::cout << "\n--- Score: " << score << " ---\n"
                  << content << "\n";
    }

    return 0;
}
```

### Example 2: Codebase Knowledge Graph (M3.B + M3.C)

```cpp
#include <apollocore/apollo_persistence.hpp>
#include <apollocore/apollo_code_extractor.hpp>
#include <filesystem>

using namespace apollocore_v2;

int main() {
    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");

    // M3.C: Use project scoping so this agent doesn't see others' data
    kb.create("data/myagent_kb", "MyAgentKB");
    kb.register_project("vortxGE", "Game engine", true /*shared*/);

    // Walk a codebase and extract everything
    for (const auto& entry : std::filesystem::recursive_directory_iterator("src/")) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        std::ifstream f(path);
        std::stringstream ss; ss << f.rdbuf();
        std::string source = ss.str();

        // M3.B: tree-sitter extracts FUNCTION, CLASS, NAMESPACE, CALLS, INHERITS
        CodeModel model = extract_code(source, path.string());
        for (const auto& e : model.entities) {
            kb.add_entity(e.name, e.type, "", e.signature);
        }
        for (const auto& r : model.relationships) {
            kb.add_relationship(r.source, r.target, r.type);
        }

        // Also add the raw source as a document for semantic search
        kb.add_document(source, path.string(), 512);
    }

    // Query the call graph
    auto neighbors = kb.get_neighbors("authenticate", 2);
    std::cout << "Things that call or are called by 'authenticate':\n";
    for (const auto& e : neighbors) {
        std::cout << "  - [" << entity_type_to_string(e.type) << "] " << e.name << "\n";
    }

    // Search by meaning
    auto hits = kb.query("how does user authentication work?", 5);

    // Read shared project (M3.C)
    kb.set_current_project("vortxGE");
    auto vortx_entities = kb.get_entities(100);

    return 0;
}
```

### Example 3: Entity Extraction (LLM-based)

```cpp
#include <apollocore/entity_extractor.hpp>

using namespace apollocore_v2;

int main() {
    ExtractionConfig config = scientific_extraction_config();
    config.llm.model_path = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";

    EntityExtractor extractor(config);

    std::string text = R"(
        Albert Einstein published his theory of relativity in 1915
        while working at the University of Zurich. He later joined
        Princeton University where he developed quantum mechanics
        concepts with Neils Bohr.
    )";

    auto result = extractor.extract(text, "science_chunk_001");

    std::cout << "Entities found: " << result.entity_count() << "\n";
    for (const auto& e : result.entities) {
        std::cout << "  - " << e.name << " (" << e.type << ")\n";
    }

    return 0;
}
```

---

## 🤖 Model Selection

ApolloCore uses two GGUF models, both auto-downloaded by CMake.

### Required Models

| Model | Purpose | Size | Dimensions |
|-------|---------|------|------------|
| `nomic-embed-text-v1.5.Q4_K_M.gguf` | Text embeddings | ~81 MB | 768 |
| `qwen2.5-0.5b-instruct-q4_k_m.gguf` | Entity extraction (LLM) | ~469 MB | — |

### Why these models?

- **Nomic Embed Text v1.5** — Best open-source embedding model at this size. Rivals OpenAI's `text-embedding-ada-002` on MTEB benchmark.
- **Qwen2.5 0.5B Instruct** — Small but capable instruction-tuned LLM. Fast enough for entity extraction on CPU, good enough for JSON output.

### 0.5B vs 3B Comparison

We tested both Qwen2.5 0.5B and 3B for entity extraction. The **stop condition bug** in the generation loop was the real blocker — once fixed (waiting for `}]}` instead of `}]`), the 0.5B model produces real relationships.

| Model | Size | Entities | Relationships | Time |
|-------|------|----------|----------------|------|
| Qwen2.5 0.5B | 469 MB | 10–42 | 0–4 | ~60s |
| Qwen2.5 3B | 2.1 GB | 18–20 | 6 | ~280s |

**Recommendation:** Use **0.5B** (the default). It's 4.5× smaller and 4× faster with nearly the same quality. Switch to 3B+ only if you need higher accuracy on complex relationships and can afford the slowdown.

## 📁 File Formats

ApolloCore uses a hybrid storage approach. See [docs/file-formats.md](docs/file-formats.md) for details.

```
my_kb/
├── apollo.db           # SQLite: nodes, chunks, entities, relationships
├── apollo.db-wal       # Write-Ahead Log (auto-generated)
└── embeddings/
    ├── chunk_001.fbin  # Binary embedding (16-byte header + float32[])
    └── chunk_002.fbin
```

**Storage sizes:**
- SQLite: ~1 KB per chunk
- FBIN: 3,088 bytes for 768-dim embedding
- Total: ~45 MB for 1000 docs × 10 chunks

---

## 🔨 Building

### Standard Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This produces `build/libapollo_core.a` — link this static library
into your agent.

### Run the Example

```bash
./build/examples/full_demo
```

### Build Only the Library

```bash
cmake --build build --target apollo_core -j$(nproc)
# Produces: build/libapollo_core.a
```

### Build and Run Tests

```bash
cmake --build build --target test_code_extractor test_multi_project -j$(nproc)
./build/tests/test_code_extractor   # 12 tests
./build/tests/test_multi_project    # 15 tests
```

### Install System-Wide

```bash
cmake --install build --prefix /usr/local
# Installs: libapollo_core.a + headers to /usr/local/include/apollocore/
```

### Link ApolloCore Into Your Agent

```cmake
# In your agent's CMakeLists.txt:
add_executable(my_agent main.cpp)
target_link_libraries(my_agent PRIVATE apollo_core)
```

The library exposes a single header (`<apollocore/apollo_persistence.hpp>`)
plus optional code-extraction (`<apollocore/apollo_code_extractor.hpp>`).

---

## 🧪 Testing

```bash
# Build and run all tests
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# Or run individual tests
./build/tests/test_entity_extraction
./build/tests/test_persistence
./build/tests/test_kb_persistence
./build/tests/test_apollo_persistence
./build/tests/test_multi_project
./build/tests/test_code_extractor
./build/tests/test_fbin
./build/tests/test_cross_encoder
```

### Test Coverage

| Module | Tests | File |
|--------|-------|------|
| Persistence (SQLite) | 7 | `tests/test_persistence` |
| FBIN (embeddings) | 12 | `tests/test_fbin` |
| Knowledge Base Persistence | 14 | `tests/test_kb_persistence` |
| ApolloCore Integration | 8 | `tests/test_apollo_persistence` |
| Hybrid Search (v2) | — | `tests/test_v2` |
| Embedding Model | — | `tests/test_embeddings` |
| LLM Entity Extraction | 14 | `tests/test_entity_extraction` |
| Cross-Encoder | 7 | `tests/test_cross_encoder` |
| HyDE | — | `tests/test_hyde` |
| Contextual Retrieval | — | `tests/test_contextual_retrieval` |
| **Code Extraction (M3.B)** | **12** | `tests/test_code_extractor` ✅ |
| **Multi-Project Scoping (M3.C)** | **15** | `tests/test_multi_project` ✅ |
| **Hybrid + Rerank wiring** | **6** | `tests/test_hybrid_search` ✅ |
| **Active tests** | **33+** | **all passing** |

---

## 📊 Project Status

### Implementation Phases

| Phase | Module | Status |
|-------|--------|--------|
| 1 | llama.cpp Integration | ✅ |
| 2 | Embedding Model (nomic-bert) | ✅ |
| 3 | Hybrid Search (Vector + BM25 + RRF) | ✅ |
| 4 | Smart Chunking | ✅ |
| 5 | Knowledge Graph | ✅ |
| 6 | Entity Extraction Framework | ✅ |
| 7 | LLM Backend (Qwen2.5) | ✅ |
| 8 | Cross-Encoder Reranking | ✅ |
| 9 | HyDE Query Enhancement | ✅ |
| 10 | Contextual Retrieval | ✅ |
| P1 | SQLite Database Schema | ✅ |
| P2 | Binary Embedding Storage (.fbin) | ✅ |
| P3 | Persistence API | ✅ |
| P3+ | Integration with ApolloCore | ✅ |
| M3.A | LLM Entity Extraction (Qwen) | ✅ |
| **M3.B** | **tree-sitter Code Extraction** | ✅ |
| **M3.C** | **Multi-Project Scoping** | ✅ |
| — | **Static Library Refactor** (no MCP/HTTP) | ✅ |

**Status: Production-ready core. All phases complete. 13 test suites, all passing.**

### Library vs MCP History

ApolloCore started as an MCP server (Model Context Protocol addon) but has
been refactored into a **static library** suitable for linking directly into
an AI agent. The MCP transport layer was removed because:

- An agent needs the KB as a **library**, not a separate process
- Network IPC adds latency and complexity
- Static linking gives the agent full control

If you need MCP transport, write a thin wrapper around `libapollo_core.a`
in your own codebase (~200 lines).

---

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes with tests
4. Ensure all tests pass
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to the branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

### Code Style

- C++23 features encouraged
- `clang-format` configuration provided
- All public APIs must have doxygen comments
- All new code must have unit tests

### Reporting Bugs

Please use GitHub Issues and include:
- ApolloCore version
- Compiler & version
- OS & version
- Minimal reproduction example
- Expected vs actual behavior

---

## 📜 License

MIT License - see [LICENSE](LICENSE) for details.

---

## 🙏 Acknowledgments

Built on top of:
- [llama.cpp](https://github.com/ggerganov/llama.cpp) by Georgi Gerganov
- [nlohmann/json](https://github.com/nlohmann/json) by Niels Lohmann
- [SQLite](https://www.sqlite.org/) by D. Richard Hipp
- [tree-sitter](https://tree-sitter.github.io/) by Max Brunsfeld
- [Nomic AI](https://www.nomic.ai/) for the embedding model
- [Qwen Team](https://qwenlm.github.io/) for the language model

Inspired by:
- [Anthropic's Contextual Retrieval](https://www.anthropic.com/engineering/contextual-retrieval)
- [Microsoft GraphRAG](https://www.microsoft.com/en-us/research/project/graphrag/)
- [LightRAG](https://arxiv.org/abs/2410.05779)

---

## 📞 Support

- 📖 [Documentation](docs/)
- 🐛 [Issue Tracker](https://github.com/yourname/ApolloCore/issues)
- 💬 [Discussions](https://github.com/yourname/ApolloCore/discussions)

---

**Built with ❤️ for the local-AI community**
