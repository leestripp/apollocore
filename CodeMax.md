# ApolloCore - State-of-the-Art Knowledge Graph RAG System (C++23)

> **IMPORTANT**: See [docs/design.md](docs/design.md) for comprehensive architecture and implementation plan.

## ⚠️ Implementation Status: CORE COMPLETE, INTEGRATION GAPS REMAIN

> **Honest assessment**: Core library is complete. The `ApolloCore` class works for embedding + semantic search + persistence. However, **entity extraction is NOT wired into the main API** — it's only available as a separate module that requires manual integration. See **[Phase G1 Plan](#phase-g1-wire-entity-extraction-into-apollocore)** below.

| Component | Status | Notes |
|-----------|--------|-------|
| llama.cpp Integration | ✅ **REAL** | GGUF model loading, Vulkan GPU support |
| EmbeddingModel (Nomic-bert) | ✅ **REAL** | 768-dim embeddings, mean pooling |
| VectorStore | ✅ **REAL** | In-memory with cosine similarity |
| BM25Index | ✅ **REAL** | Full BM25 with stopwords, IDF |
| Hybrid Search (RRF) | ✅ **REAL** | Reciprocal Rank Fusion (k=60) |
| Smart Chunking | ✅ **REAL** | Paragraph + sentence aware |
| KnowledgeGraph | ✅ **REAL** | Entity/Relationship storage |
| KnowledgeTree | ✅ **REAL** | Node hierarchy |
| Entity Extraction (LLM) | 🚧 **IN PROGRESS** | Hybrid BERT NER + LLM extraction |
| Cross-Encoder Reranking | 📋 Planned | Not yet implemented |
| Query Enhancement (HyDE) | 📋 Planned | Not yet implemented |
| Community Detection | 📋 Planned | Not yet implemented |

---

# 📋 PHASE 6: ENTITY EXTRACTION IMPLEMENTATION PLAN

## Overview

Entity extraction transforms raw text into structured knowledge by identifying:
- **Entities**: People, organizations, locations, concepts, dates, products
- **Relationships**: Typed connections between entities (WORKS_FOR, LOCATED_IN, etc.)

This enables the KnowledgeGraph to build a semantic understanding of documents beyond keyword/vector matching.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     ENTITY EXTRACTION PIPELINE                             │
│                                                                             │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐ │
│  │    TEXT     │───▶│    BERT     │───▶│    LLM      │───▶│   ENTITY    │ │
│  │   INPUT     │    │    NER      │    │ EXTRACTION  │    │ RESOLUTION  │ │
│  └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘ │
│        │                  │                  │                  │          │
│        ▼                  ▼                  ▼                  ▼          │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐ │
│  │   RULE      │    │  FAST HIGH  │    │   COMPLEX   │    │   MERGE &   │ │
│  │   BASED     │    │  QUALITY    │    │ RELATIONSHIPS│   │   DEDUP     │ │
│  │  (Regex)    │    │  (ORG,PER,  │    │   (LLM)     │    │             │ │
│  │             │    │   LOC,DATE) │    │             │    │             │ │
│  └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘ │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────────┐ │
│  │                    KNOWLEDGE GRAPH OUTPUT                               │ │
│  │  Entities (id, name, type, confidence) + Relationships (source→type→target)│ │
│  └─────────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Implementation Steps

### Step 1: Create Entity Extraction Module
- [ ] Create `src/entity_extractor.cpp` and `include/apollocore/entity_extractor.hpp`
- [ ] Define entity and relationship type enums
- [ ] Create extraction prompt templates (few-shot)
- [ ] Implement JSON parsing for LLM responses

### Step 2: Integrate LLM Context (LLM for Extraction)
- [ ] Download llama-3.2-3B-GGUF model for extraction
- [ ] Create `LlamaExtractor` class wrapping llama.cpp for extraction tasks
- [ ] Implement batch processing for efficiency
- [ ] Add timeout and error handling

### Step 3: Implement BERT NER Pipeline
- [ ] Create `BertNERPipeline` class
- [ ] Integrate HuggingFace tokenizers for BERT
- [ ] Implement BIO tag parsing and entity merging
- [ ] Support for entity types: PERSON, ORGANIZATION, LOCATION, DATE

### Step 4: Create Hybrid Extraction Engine
- [ ] Combine rule-based + BERT NER + LLM extraction
- [ ] Implement entity deduplication (fuzzy matching)
- [ ] Add confidence score aggregation
- [ ] Implement coreference resolution (basic)

### Step 5: Relationship Extraction
- [ ] Create relationship classification prompts
- [ ] Implement bidirectional relationship inference
- [ ] Add relationship confidence scoring
- [ ] Support relationship types: FOUNDED, WORKED_AT, DEVELOPED, LOCATED_IN, RELATED_TO, CAUSED, PART_OF

### Step 6: Integrate with KnowledgeGraph
- [ ] Auto-extract entities when adding documents
- [ ] Link entities to source chunks
- [ ] Update KnowledgeGraph with new entities/relationships
- [ ] Implement entity lookup for queries

### Step 7: Testing
- [ ] Create `test_entity_extraction.cpp`
- [ ] Test with sample texts (scientific, news, technical)
- [ ] Measure extraction accuracy
- [ ] Performance benchmarking

---

## Entity & Relationship Types

### Entity Types
```cpp
enum class EntityType {
    PERSON,           // Named individuals
    ORGANIZATION,     // Companies, universities, governments
    LOCATION,         // Cities, countries, buildings
    EVENT,            // Conferences, wars, festivals
    CONCEPT,          // Theories, ideas, technologies
    PRODUCT,          // Software, hardware, products
    DATE,             // Specific dates, time periods
    NUMBER,           // Quantities, measurements
    DISEASE,          // Medical conditions
    DRUG,             // Medications
    MEDICAL_PROCEDURE, // Medical treatments
    SCIENTIFIC_TERM,  // Domain-specific concepts
};
```

### Relationship Types
```cpp
enum class RelationType {
    FOUNDED,          // Person/Org establishment
    DEVELOPED,        // Created a product/theory
    PUBLISHED,        // Authored papers/books
    WORKED_AT,        // Employment
    LOCATED_IN,       // Geographic
    RELATED_TO,       // General association
    CAUSED,           // Causation
    PART_OF,          // Compositional
    TREATS,           // Medical (drug→disease)
    DIAGNOSES,        // Medical (procedure→disease)
    EVALUATES,        // Research methodology
    USES,             // Technology adoption
    IMPLEMENTS,       // Software patterns
    DEPENDS_ON,       // Dependencies
};
```

---

## API Design

```cpp
// In entity_extractor.hpp
class EntityExtractor {
public:
    struct ExtractionResult {
        std::vector<Entity> entities;
        std::vector<Relationship> relationships;
        std::unordered_map<std::string, std::vector<std::string>> entity_sources; // entity_id -> chunk_ids
    };
    
    // Constructor - initializes LLM for extraction
    EntityExtractor(const std::string& llm_model_path);
    
    // Extract from single text
    ExtractionResult extract(const std::string& text, const std::string& chunk_id = "");
    
    // Batch extraction
    std::vector<ExtractionResult> extract_batch(const std::vector<std::string>& texts);
    
    // Configure extraction
    void set_entity_types(std::vector<EntityType> types);
    void set_min_confidence(float confidence);
    void enable_coreference_resolution(bool enable);
};

// Integration with ApolloCore
class ApolloCore {
public:
    // Add document with automatic entity extraction
    std::vector<Chunk> add_document_with_entities(
        const std::string& text,
        node_id parent_id,
        const ChunkConfig& config = {}
    );
    
    // Query with entity expansion
    QueryResults query_with_entities(
        const std::string& query,
        const QueryOptions& options = {}
    );
};
```

---

## Model Requirements

| Model | Purpose | Size | Source |
|-------|---------|------|--------|
| `nomic-embed-text-v1.5.Q4_K_M.gguf` | Embeddings | 81MB | ✅ Already downloaded |
| `llama-3.2-3B-GGUF` | Entity/Relation extraction | ~2GB | 📥 To download |
| `bert-base-ner` (or ONNX) | Fast NER | ~400MB | 📥 Optional |

---

## Files to Create/Modify

### New Files
```
src/entity_extractor.cpp       # Entity extraction implementation
include/apollocore/entity_extractor.hpp  # Entity extraction header
tests/test_entity_extraction.cpp # Entity extraction tests
models/llama-3.2-3b-q4_K_M.gguf # Extraction LLM model
```

### Modified Files
```
include/apollocore/apollocore_v2.hpp  # Add entity extraction methods
src/embedding_model.cpp               # Minor updates
tests/test_v2.cpp                      # Add entity extraction tests
CMakeLists.txt                         # Add new targets
CodeMax.md                            # Update status
```

---

## Test Cases

```cpp
// Scientific text
TEST(EntityExtraction, Scientific) {
    std::string text = R"(
        Albert Einstein published his theory of relativity in 1915 while 
        working at the University of Zurich. He later joined Princeton 
        University where he developed quantum mechanics concepts with 
        Neils Bohr. The theory was validated by Arthur Eddington's 
        1919 solar eclipse experiment.
    )";
    
    auto result = extractor.extract(text);
    
    // Verify entities
    ASSERT_TRUE(find_entity(result.entities, "Albert Einstein"));
    ASSERT_TRUE(find_entity(result.entities, "University of Zurich"));
    ASSERT_TRUE(find_entity(result.entities, "Theory of Relativity"));
    
    // Verify relationships
    ASSERT_TRUE(find_relationship(result.relationships, "Albert Einstein", "PUBLISHED", "Theory of Relativity"));
}

// Technical text
TEST(EntityExtraction, Technical) {
    std::string text = R"(
        Python 3.11 was released by the Python Software Foundation in October 2022.
        It includes performance improvements through faster calls and better error messages.
        Django 4.2 supports Python 3.8 through 3.11.
    )";
    
    auto result = extractor.extract(text);
    // ... assertions
}
```

---

## Expected Outcomes

After implementation:
1. **Rich entity extraction**: PERSON, ORGANIZATION, LOCATION, CONCEPT, DATE from any text
2. **Relationship extraction**: Typed connections between entities
3. **Enhanced queries**: Entity-aware search with graph traversal
4. **Knowledge enrichment**: Automatic knowledge graph population

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Extraction latency (single chunk) | < 500ms with 3B model |
| Entity precision | > 85% |
| Entity recall | > 80% |
| Relationship accuracy | > 75% |
| Memory usage | < 4GB total |

---

# END OF IMPLEMENTATION PLAN

---

## Phase G1: Wire Entity Extraction into ApolloCore

### Problem Statement

The `EntityExtractor` module was built and tested in isolation (Phase 6/7), but was **never wired into the main `ApolloCore` API**. As a result:

- `kb.add_document("text...")` embeds and stores text, but never extracts entities
- After adding 5 documents, the demo shows: `Entities: 0, Relationships: 0`
- The SQLite `entities` and `relationships` tables exist but are always empty
- The Qwen2.5 LLM model is downloaded but never used by `ApolloCore`

This was masked by marking Phase 6 as "✅ COMPLETE" — but the framework existed without integration.

### Root Cause

The original `ApolloCore` class (`apollocore_v2.hpp`) was designed before `EntityExtractor` was built. When entity extraction was added later as a separate module, it was never connected back to the main `add_document()` flow.

### Goal

After Phase G1:
```cpp
apollocore_v2::ApolloCore kb;
kb.load_embedding_model("models/embeddings.gguf");
kb.load_llm_model("models/qwen2.5-0.5b-instruct-q4_k_m.gguf");
kb.create("data/wiki", "My KB");
kb.add_document("Einstein worked at Princeton...");
// ^ This call should now ALSO extract entities & relationships

auto stats = kb.get_stats();
// stats.entity_count > 0  ✓
// stats.relationship_count > 0  ✓
```

### Design Decisions

**Decision 1: Explicit vs automatic extraction**

- **Choice: Explicit method `extract_entities()`**
- Reasoning: Entity extraction is slow (LLM call per chunk). Users may want to batch it, run it on a schedule, or skip it entirely for large corpora.
- API:
  ```cpp
  kb.add_document(text);          // fast: embed + store
  kb.extract_entities();         // slow: LLM per chunk, saves to DB
  ```

**Decision 2: How to pass LLM model**

- Add `load_llm_model(path)` method to `ApolloCore`
- Internally creates an `EntityExtractor` instance
- `extract_entities()` uses this instance

**Decision 3: Batch vs per-chunk LLM calls**

- **Choice: Per-chunk for now** (simple, correct)
- Future: Could batch multiple chunks in one LLM prompt for efficiency

**Decision 4: Deduplication**

- When the same entity (e.g. "Einstein") appears in multiple chunks, create one entity per chunk OR deduplicate globally?
- **Choice: Per-chunk entities (simpler)**, with deduplication deferred to a future phase
- Each entity gets a unique ID like `entity_{chunk_id}_{n}`

### Implementation Tasks

| ID | Task | Priority | Estimate |
|----|------|----------|----------|
| G1.1 | Add `load_llm_model(path)` to `ApolloCore` | 🔴 Critical | 15 min |
| G1.2 | Add internal `EntityExtractor` member to `ApolloCore` | 🔴 Critical | 10 min |
| G1.3 | Add `extract_entities()` method that iterates all chunks | 🔴 Critical | 30 min |
| G1.4 | Wire `EntityExtractor` results to `KnowledgeBasePersistence::save_entity()` / `save_relationship()` | 🔴 Critical | 20 min |
| G1.5 | Track which chunks have been entity-extracted (avoid re-extraction) | 🟡 Important | 20 min |
| G1.6 | Add error handling (LLM may fail on individual chunks) | 🟡 Important | 15 min |
| G1.7 | Update `full_demo.cpp` to call `extract_entities()` and show entity counts | 🟡 Important | 15 min |
| G1.8 | Add `stats.entity_count` and `stats.relationship_count` to be non-zero in demo | 🟡 Important | 10 min |
| G1.9 | Add unit tests for the full entity-extraction flow | 🟢 Nice | 30 min |
| G1.10 | Add config option `extract_on_add` (auto-extract when adding) | 🟢 Nice | 20 min |

**Total estimate**: ~3 hours

### API Changes

**New methods on `ApolloCore`:**

```cpp
class ApolloCore {
public:
    // Load the LLM model for entity extraction
    bool load_llm_model(const std::string& path, bool use_gpu = false);
    
    // Check if LLM is loaded
    bool is_llm_loaded() const;
    
    // Extract entities from all chunks (uses LLM)
    // Returns: number of entities extracted
    size_t extract_entities(
        float min_confidence = 0.5f,
        bool skip_already_extracted = true
    );
    
    // Extract from a single chunk by ID
    size_t extract_entities_for_chunk(const std::string& chunk_id);
    
    // Get all entities (loaded from disk)
    std::vector<Entity> get_entities(size_t limit = 0);
    
    // Get entities for a chunk
    std::vector<Entity> get_entities_for_chunk(const std::string& chunk_id);
    
    // Get all relationships
    std::vector<Relationship> get_relationships(size_t limit = 0);
};
```

**New stats fields:**

```cpp
struct Stats {
    // ... existing fields ...
    size_t chunks_with_entities = 0;  // How many chunks have been extracted
};
```

### Updated Demo Flow

```
Step 1: Load embedding model
Step 2: Load LLM model (NEW)
Step 3: Create KB
Step 4: Add documents (embed + store)
Step 5: Extract entities from all chunks (NEW — uses LLM)
Step 6: Query (semantic search)
Step 7: Show entity/relationship counts (NON-ZERO!)
Step 8: Demonstrate persistence
```

### Acceptance Criteria

Phase G1 is **DONE** when:

1. ✅ Demo shows `Entities: > 0, Relationships: > 0` after running
2. ✅ `extract_entities()` extracts entities from all chunks
3. ✅ Entities persist across program restarts
4. ✅ Entities can be retrieved via `get_entities()`
5. ✅ Relationships can be retrieved via `get_relationships()`
6. ✅ All existing tests still pass
7. ✅ New unit tests pass (`test_entity_extraction_flow`)
8. ✅ Build is clean (no warnings)
9. ✅ Commit message acknowledges the gap was real

### Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| LLM is slow → demo takes minutes | Show progress per chunk; allow batch in future |
| LLM may hallucinate JSON | Existing `EntityExtractor` already handles this; test with bad output |
| Existing tests break | Add entity extraction as opt-in, don't change `add_document()` |
| Memory: keeping LLM loaded uses RAM | Document the RAM cost; allow unload |

### Timeline

| Step | Time |
|------|------|
| G1.1–G1.4 (core wiring) | ~75 min |
| G1.5–G1.8 (demo + tests) | ~60 min |
| G1.9–G1.10 (polish) | ~50 min |
| **Total** | **~3 hours** |

---

## What's Working Now

### Real Embeddings with Nomic Embed Text v1.5

```cpp
#include <apollocore/apollocore_v2.hpp>

apollocore_v2::ApolloCore kb;

// Load the model
kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");

// Now you get REAL embeddings:
// - Dimensions: 768
// - Model: nomic-embed-text-v1.5
// - Pooling: Mean pooling (BERT-style)
```

### Test Results

```
Testing embedding...
  - Embedding dimensions: 768
  - First 5 values: 0.0122242 0.0700778 -0.165554 -0.0556093 0.0435051

Testing hybrid search (vector + BM25)...
  Query: 'quantum mechanics uncertainty principle'
  [0] Score: 0.0163934 (vec: 0.870156, bm25: 2.46951)
      Content: Quantum mechanics describes the behavior...

Testing semantic search only...
  Query: 'atomic particles energy'
  [0] Score: 0.627457
  [1] Score: 0.562398
```

---

## Research: State-of-the-Art RAG 2026

### Key Technologies

| Technique | Source | Impact |
|-----------|--------|--------|
| **GraphRAG (Microsoft)** | arXiv:2404.16130 | Global query answering via community reports |
| **LightRAG** | arXiv:2410.05779 | 10x faster graph RAG with dual-layer retrieval |
| **Hybrid Search + RRF** | Azure, Qdrant, OpenSearch | Industry standard (60% of 2026 deployments) |
| **Contextual Retrieval** | Anthropic | Reduces chunk decontextualization |
| **Self-RAG** | arXiv:2310.11511 | Adaptive retrieval via reflection tokens |
| **RAPTOR** | ICLR 2024 | Hierarchical summarization tree |
| **ColBERT** | SIGIR 2020 | Late interaction for precision |
| **LLM Entity Extraction** | 2024-2026 | Structured knowledge from text |

---

## System Information

| Property | Value |
|----------|-------|
| **OS** | Ubuntu 26.04 LTS (x86_64) |
| **Kernel** | Linux 7.0.3-6.stable |
| **Container** | Podman (Ubuntu:26.04 image) |
| **Compiler** | GCC 15.2.0 |
| **C++ Standard** | C++23 |
| **llama.cpp** | 0.15.2 (ggml commit 063d9c156) |
| **Vulkan** | 1.4.341 |

---

## Build Commands

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j8

# Run tests
./build/tests/run_apollo_tests    # v1 API
./build/tests/test_v2             # v2 API
./build/tests/test_embeddings     # Real embeddings test

# Example
./build/examples/simple_usage
```

---

## File Structure

```
ApolloCore/
├── CMakeLists.txt
├── CodeMax.md                    # This file
├── docs/
│   ├── README.md
│   ├── design.md                 # Full architecture design (SOTA RAG)
│   └── BANNER_PROMPTS.md         # Apollo-themed banner prompts
├── examples/
│   ├── CMakeLists.txt
│   └── simple_usage.cpp         # v2 API example
├── include/apollocore/
│   ├── apollocore.hpp           # v1 API (legacy)
│   └── apollocore_v2.hpp        # v2 API (current)
├── models/                      
│   ├── README.md                # Download instructions
│   └── nomic-embed-text-v1.5.Q4_K_M.gguf  # Downloaded (81MB)
├── src/
│   ├── apollocore.cpp           # Empty (impl in header)
│   └── embedding_model.cpp      # llama.cpp wrapper + BM25 + Hybrid
└── tests/
    ├── CMakeLists.txt
    ├── test_main.cpp
    ├── test_apollocore.cpp      # v1 tests
    ├── test_v2.cpp              # v2 tests
    └── test_embeddings.cpp       # Real embedding test
```

---

## Core Classes (v2)

| Class | File | Description |
|-------|------|-------------|
| `EmbeddingModel` | apollocore_v2.hpp | Real llama.cpp GGUF embeddings |
| `VectorStore` | apollocore_v2.hpp | In-memory vector storage |
| `BM25Index` | apollocore_v2.hpp | Full BM25 keyword search |
| `SmartChunker` | apollocore_v2.hpp | Paragraph/sentence aware chunking |
| `HybridSearchEngine` | apollocore_v2.hpp | RRF fusion pipeline |
| `KnowledgeTree` | apollocore_v2.hpp | Node hierarchy |
| `KnowledgeGraph` | apollocore_v2.hpp | Entity/Relationship storage |
| `ApolloCore` | apollocore_v2.hpp | Main API class |
| `EntityExtractor` | entity_extractor.hpp | **NEW: LLM-based entity extraction** |

---

## Test Results

### v1 Tests (run_apollo_tests)
```
ApolloCore Basic Tests
Test 1: Create ApolloCore - PASSED
Test 2: Create nodes - PASSED
Test 3: Add and search chunks - PASSED
Test 4: Query - PASSED
Test 5: Stats - PASSED
All tests passed!
```

### v2 Tests (test_embeddings)
```
ApolloCore - Real Embedding Test with Nomic
=============================================
Model loaded successfully!
  - Embedding dimensions: 768
  - First 5 values: 0.0122242 0.0700778 -0.165554 -0.0556093 0.0435051
All tests completed successfully!
```

---

## Implementation Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | ✅ Complete | llama.cpp Integration |
| 2 | ✅ Complete | Real Embedding Model Integration |
| 3 | ✅ Complete | Hybrid Search (Vector + BM25 + RRF) |
| 4 | ✅ Complete | Smart Chunking |
| 5 | ✅ Complete | KnowledgeGraph infrastructure |
| 6 | ⚠️ **Framework only** | Entity Extraction Framework (NOT wired into ApolloCore) |
| 7 | ✅ **COMPLETE** | LLM Backend Integration (Qwen2.5) |
| **G1** | 📋 **PLANNED** | **Wire entity extraction into ApolloCore** (see plan below) |
| 8 | ✅ **COMPLETE** | Cross-Encoder Reranking |
| 9 | ✅ **COMPLETE** | HyDE Query Enhancement |
| 10 | ✅ **COMPLETE** | Contextual Retrieval |
| **P1** | ✅ **COMPLETE** | SQLite Database Schema |
| **P2** | ✅ **COMPLETE** | Binary Embedding Storage (.fbin) |
| **P3** | ✅ **COMPLETE** | ApolloCore Persistence API |
| **P3+** | ✅ **COMPLETE** | Integration with ApolloCore |

---

## Contextual Retrieval Status

✅ **COMPLETE** - Contextual retrieval module ready!

### Features
- **Chunk**: Structure with content, context, and contextualized versions
- **Document**: Full document with metadata and chunks
- **ContextGenerator**: Adds surrounding context to chunks
- **Configuration Presets**: default, high_quality, fast
- **LLM Support**: Optional LLM-generated context summaries

### Configuration Presets
| Preset | context_before | context_after | use_llm |
|--------|---------------|--------------|---------|
| default | 1 | 1 | false |
| high_quality | 2 | 2 | true |
| fast | 1 | 0 | false |

### Usage
```cpp
#include <apollocore/contextual_retrieval.hpp>

apollocore_v2::ContextGenerator generator(config);

Document doc;
doc.id = "my_doc";
doc.title = "Introduction to Physics";
doc.content = "Full document text...";

auto chunks = generator.process_document(doc);

// Each chunk now has:
// - chunk.content: Original text
// - chunk.context: Added context
// - chunk.contextualized: content + context combined
```

---

## HyDE Query Enhancement Status

✅ **COMPLETE** - HyDE module ready!

### Features
- **HypotheticalDocument**: Structure for generated docs
- **HydeResult**: Combined result with embeddings
- **Configuration Presets**: default, diverse, fast
- **Batch Processing**: Process multiple queries
- **LLM Backend**: Generate hypothetical documents

### Configuration Presets
| Preset | generations | temperature | max_tokens |
|--------|------------|-------------|------------|
| default | 3 | 0.7 | 200 |
| diverse | 5 | 1.0 | 300 |
| fast | 2 | 0.5 | 150 |

### Usage
```cpp
#include <apollocore/hyde.hpp>

apollocore_v2::HydeConfig config;
config.llm.model_path = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";
config.num_generations = 3;

apollocore_v2::HydeEnhancer enhancer(config);
auto result = enhancer.enhance("What is quantum mechanics?");

// Use result.combined_embedding for similarity search
```

---

## Cross-Encoder Reranking Status

✅ **COMPLETE** - Cross-encoder reranking module ready!

### Features
- **RerankedResult**: Structure for tracking original + reranked scores
- **Configuration Presets**: default, high_precision, fast
- **Custom Scorer Support**: Plug in any scoring function
- **LLM Backend**: Optional LLM-based relevance scoring
- **Score Filtering**: Filter by minimum threshold

### Usage
```cpp
#include <apollocore/cross_encoder.hpp>

// Create reranker
apollocore_v2::RerankConfig config;
config.top_k = 20;           // Rerank top 20 results
config.return_k = 10;         // Return top 10
config.cross_encoder_weight = 0.5f;
apollocore_v2::CrossEncoderReranker reranker(config);

// Rerank results from vector search
auto reranked = reranker.rerank(query, vector_search_results);
```

### Configuration Presets
| Preset | top_k | return_k | cross_encoder_weight |
|--------|-------|----------|---------------------|
| default | 20 | 10 | 0.5 |
| high_precision | 50 | 5 | 0.7 |
| fast | 10 | 5 | 0.4 |

---

## Entity Extraction Status

✅ **WORKING** - Real entity extraction with LLM inference!

### Test Results
```
ENTITIES (8):
  • Albert Einstein (PERSON)
  • University of Zurich (ORGANIZATION)
  • Princeton University (ORGANIZATION)
  • Neils Bohr (PERSON)
  • Arthur Eddington (PERSON)
  • Solar eclipse experiment (EVENT)
  • 1919 (DATE)
  • 1921 (DATE)
```

### Usage
```cpp
#include <apollocore/entity_extractor.hpp>

apollocore_v2::ExtractionConfig config = apollocore_v2::scientific_extraction_config();
config.llm.backend = apollocore_v2::ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
config.llm.model_path = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";

apollocore_v2::EntityExtractor extractor(config);
auto result = extractor.extract("Albert Einstein worked at Princeton...");

// result.entities - extracted entities
// result.relationships - extracted relationships
```

### Models
| Model | Size | Purpose |
|-------|------|---------|
| nomic-embed-text-v1.5.Q4_K_M.gguf | 81MB | Text embeddings |
| qwen2.5-0.5b-instruct-q4_k_m.gguf | 469MB | Entity extraction |

---

## Persistence Implementation Plan

### Current Problem
All data is **in-memory only**. Data is lost when program exits.

### Solution: Hybrid Storage (SQLite + Binary Files)

```
apollo_data/
├── apollo.db              # SQLite: metadata, entities, relationships
├── embeddings/
│   ├── chunk_001.fbin    # Raw float32 binary (768 dims)
│   ├── chunk_002.fbin
│   └── ...
└── config.json            # Knowledge base configuration
```

---

## Phase P1: SQLite Database Schema

### Tables
```sql
-- Knowledge base configuration
CREATE TABLE config (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    embedding_dim INTEGER DEFAULT 768,
    embedding_model TEXT
);

-- Documents (nodes in knowledge tree)
CREATE TABLE nodes (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    content TEXT,
    parent_id TEXT,
    document_type TEXT,
    metadata JSON,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Chunks (indexed content units)
CREATE TABLE chunks (
    id TEXT PRIMARY KEY,
    node_id TEXT NOT NULL,
    content TEXT NOT NULL,
    position INTEGER,
    embedding_file TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Entities (extracted knowledge)
CREATE TABLE entities (
    id TEXT PRIMARY KEY,
    chunk_id TEXT,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    description TEXT,
    confidence REAL DEFAULT 0.5,
    metadata JSON,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Relationships (entity connections)
CREATE TABLE relationships (
    id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    target_id TEXT NOT NULL,
    type TEXT NOT NULL,
    description TEXT,
    confidence REAL DEFAULT 0.5,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## Phase P2: Binary Embedding Storage

### Format: .fbin (Float32 Binary)
```
Header (16 bytes):
  - Magic: "FBIN" (4 bytes)
  - Version: uint32 (4 bytes)
  - Dimensions: uint32 (4 bytes)
  - Reserved: 4 bytes

Data:
  - float32[dimensions]
```

---

## Phase P3: ApolloCore Persistence API

### New Methods
```cpp
class ApolloCore {
public:
    // === Persistence ===
    bool save(const std::string& path);
    bool load(const std::string& path);
    bool create(const std::string& path, const std::string& name);
    bool open(const std::string& path);
    void clear();                    // Clear both memory and disk
    void clear_disk();              // Clear disk only
    bool destroy();                  // Delete everything
    bool exists(const std::string& path);
    
    // === Query ===
    std::vector<ChunkHandle> get_chunks(const NodeID& node_id);
    std::vector<Entity> get_entities(const ChunkID& chunk_id);
    
    struct DiskStats {
        size_t total_chunks;
        size_t total_entities;
        size_t total_relationships;
        size_t embedding_files_size;
    };
    DiskStats get_disk_stats();
};
```

---

## Implementation Tasks

| Task | Priority | Description |
|------|----------|-------------|
| P1.1 | 🔴 Critical | Create `persistence.hpp` header |
| P1.2 | 🔴 Critical | Implement SQLite schema initialization |
| P1.3 | 🔴 Critical | Implement `save()` method |
| P1.4 | 🔴 Critical | Implement `load()` method |
| P2.1 | 🔴 Critical | Create `fbin.hpp` for binary embeddings |
| P2.2 | 🔴 Critical | Implement `save_embedding()` |
| P2.3 | 🔴 Critical | Implement `load_embedding()` |
| P3.1 | 🟡 Important | Implement `create()` and `open()` |
| P3.2 | 🟡 Important | Implement `destroy()` |
| P3.3 | 🟡 Important | Implement `clear_disk()` |
| P4.1 | 🟢 Nice | Hash-based change detection |
| P4.2 | 🟢 Nice | Incremental embedding |

---

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `include/apollocore/persistence.hpp` | CREATE | Persistence header |
| `include/apollocore/fbin.hpp` | CREATE | Binary embedding I/O |
| `src/persistence.cpp` | CREATE | SQLite + file I/O implementation |
| `src/fbin.cpp` | CREATE | Binary file implementation |
| `include/apollocore/apollocore_v2.hpp` | MODIFY | Add save/load methods |
| `src/apollocore.cpp` | MODIFY | Implement save/load |

---

## Usage Example

```cpp
#include <apollocore/apollocore_v2.hpp>

int main() {
    using namespace apollocore_v2;
    
    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    
    std::string kb_path = "data/my_knowledge_base";
    
    if (kb.exists(kb_path)) {
        kb.open(kb_path);  // Load existing
    } else {
        kb.create(kb_path, "My Knowledge Base");  // Create new
    }
    
    kb.add_document("Important information...", kb.get_root());
    auto results = kb.query("information");
    
    kb.save();  // Explicit save (optional)
    
    // kb.destroy();  // Delete everything
    
    return 0;
}
```

---

## References

- [Microsoft GraphRAG](https://www.microsoft.com/en-us/research/project/graphrag/)
- [LightRAG Paper](https://arxiv.org/abs/2410.05779)
- [Anthropic Contextual Retrieval](https://www.anthropic.com/engineering/contextual-retrieval)
- [Self-RAG Paper](https://arxiv.org/abs/2310.11511)
- [RAPTOR Paper](https://arxiv.org/abs/2401.18059)
- [llama.cpp](https://github.com/ggerganov/llama.cpp)
- [Nomic Embed Text](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF)
- [BERT NER](https://huggingface.co/dslim/bert-base-NER)
- [LLM Entity Extraction Best Practices](https://arxiv.org/abs/2406.16157)
