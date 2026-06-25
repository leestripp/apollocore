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
| 🌐 **Knowledge Graph** | Entity & relationship extraction from text |
| 🎯 **Cross-Encoder Reranking** | LLM-based result reranking for precision |
| 💡 **HyDE** | Hypothetical Document Embeddings for query enhancement |
| 📄 **Contextual Retrieval** | Context-aware chunk processing |
| ⚡ **High Performance** | C++23, zero-copy, mmap-backed embeddings |

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
./build/examples/simple_usage
```

### 4. Minimal Code Example

```cpp
#include <apollocore/apollo_persistence.hpp>
#include <iostream>

using namespace apollocore_v2;

int main() {
    ApolloCoreWithPersistence kb;
    
    // Load embedding model
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    
    // Create a knowledge base
    kb.create("data/my_kb", "My Knowledge Base");
    
    // Add documents (auto-persisted to disk)
    kb.add_document(
        "Albert Einstein developed the theory of relativity. "
        "He worked at Princeton University. "
        "His theory changed modern physics.",
        512  // chunk size in characters
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
│  (apollo_persistence.hpp | apollocore_v2.hpp)             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │   Hybrid     │  │   Entity     │  │   Context    │     │
│  │   Search     │  │   Extractor  │  │   Retrieval  │     │
│  │  (Vector+BM25)│  │  (LLM-based) │  │              │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                 │                 │              │
│  ┌──────┴─────────────────┴─────────────────┴───────┐     │
│  │              Knowledge Graph                       │     │
│  │       (entities + relationships)                  │     │
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
│  │   │ SQLite   │  │  FBIN    │  │  JSON    │         │    │
│  │   │  (meta)  │  │(vectors) │  │  (config)│         │    │
│  │   └──────────┘  └──────────┘  └──────────┘         │    │
│  └──────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### Module Overview

| Module | File | Purpose |
|--------|------|---------|
| `EmbeddingModel` | `embedding_model.hpp` | Text → 768-dim vectors (nomic-bert) |
| `VectorStore` | `apollocore_v2.hpp` | Vector similarity search |
| `BM25Index` | `apollocore_v2.hpp` | Keyword search (BM25) |
| `HybridSearchEngine` | `apollocore_v2.hpp` | Combines vector + BM25 via RRF |
| `EntityExtractor` | `entity_extractor.hpp` | Extract entities/relationships via LLM |
| `CrossEncoderReranker` | `cross_encoder.hpp` | Rerank results with LLM |
| `HydeEnhancer` | `hyde.hpp` | Generate hypothetical documents |
| `ContextGenerator` | `contextual_retrieval.hpp` | Add context to chunks |
| `KnowledgeBasePersistence` | `kb_persistence.hpp` | SQLite + FBIN storage |
| `ApolloCoreWithPersistence` | `apollo_persistence.hpp` | Unified high-level API |

---

## 📚 API Reference

### Core Classes

#### `ApolloCoreWithPersistence`

The main entry point. Provides a unified API combining all features.

```cpp
#include <apollocore/apollo_persistence.hpp>

apollocore_v2::ApolloCoreWithPersistence kb;
```

**Model Management:**
```cpp
bool load_embedding_model(const std::string& path, bool use_gpu = false);
```

**Knowledge Base Management:**
```cpp
static bool kb_exists(const std::string& path);
bool create(const std::string& path, const std::string& name = "Untitled");
bool open(const std::string& path);
bool save();
void close();
void clear();
bool destroy();
bool is_open() const;
std::string get_kb_path() const;
std::string get_kb_name() const;
```

**Data Operations:**
```cpp
size_t add_document(const std::string& text, size_t chunk_size = 512);
std::vector<std::pair<std::string, float>> query(
    const std::string& query_text, 
    size_t top_k = 5
);
```

**Statistics:**
```cpp
struct Stats {
    size_t document_count;
    size_t chunk_count;
    size_t entity_count;
    size_t relationship_count;
    size_t embedding_files_size;
};
Stats get_stats();
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
#include <fstream>
#include <sstream>

using namespace apollocore_v2;

int main() {
    ApolloCoreWithPersistence kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    
    kb.create("data/research", "Research Papers");
    
    // Read all text files from a directory
    for (const auto& entry : std::filesystem::directory_iterator("papers/")) {
        if (entry.path().extension() == ".txt") {
            std::ifstream f(entry.path());
            std::stringstream ss;
            ss << f.rdbuf();
            kb.add_document(ss.str(), 512);
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

### Example 2: Persistent Knowledge Graph

```cpp
#include <apollocore/kb_persistence.hpp>

using namespace apollocore_v2;

int main() {
    KnowledgeBasePersistence kb;
    
    if (kb_exists("data/wiki")) {
        kb.open("data/wiki");
    } else {
        kb.create("data/wiki", "Wikipedia Knowledge");
    }
    
    // Add structured knowledge
    kb.save_node("article_einstein", "Albert Einstein", 
                 "German-born theoretical physicist", 
                 "root", "article", "");
    
    std::vector<float> emb(768, 0.5f);
    kb.save_chunk("chunk_001", "article_einstein",
                  "Einstein developed the theory of relativity", 0, emb);
    
    kb.save_entity("e_einstein", "chunk_001", 
                   "Albert Einstein", "PERSON", "Physicist", 0.99f);
    
    auto stats = kb.get_stats();
    std::cout << "Nodes: " << stats.total_nodes << "\n"
              << "Chunks: " << stats.total_chunks << "\n"
              << "Entities: " << stats.total_entities << "\n";
    
    return 0;
}
```

### Example 3: Entity Extraction

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

### Run Example

```bash
./build/examples/simple_usage
```

### Build Only the Library

```bash
cmake --build build --target apollo_core -j$(nproc)
```

### Install System-Wide

```bash
cmake --install build --prefix /usr/local
```

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
./build/tests/test_fbin
./build/tests/test_hyde
./build/tests/test_cross_encoder
./build/tests/test_contextual_retrieval
```

### Test Coverage

| Module | Tests | Status |
|--------|-------|--------|
| Entity Extraction | 14 | ✅ All passing |
| Persistence (SQLite) | 7 | ✅ All passing |
| Knowledge Base Persistence | 14 | ✅ All passing |
| ApolloCore Integration | 10 | ✅ All passing |
| FBIN (embeddings) | 12 | ✅ All passing |
| HyDE | 7 | ✅ All passing |
| Cross-Encoder | 7 | ✅ All passing |
| Contextual Retrieval | 9 | ✅ All passing |
| **Total** | **80+** | **✅** |

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

**Status: Production-ready core functionality. All 14 phases complete.**

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
