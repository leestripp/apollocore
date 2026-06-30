# ApolloCore Design Document
## State-of-the-Art Knowledge Graph RAG System (2026)

---

## Research Summary: RAG Landscape in 2026

### Key Findings from Web Research

| Technique | Status | Impact | Source |
|-----------|--------|--------|--------|
| **Hybrid Search (BM25 + Vector)** | Industry Standard | +40% recall improvement | MongoDB, Qdrant, OpenSearch |
| **Reciprocal Rank Fusion (RRF)** | Dominant Fusion Method | No score normalization needed | Azure AI Search, OpenSearch |
| **GraphRAG (Microsoft)** | Production Ready | Better global query answering | Microsoft Research, GitHub |
| **LightRAG** | Rising Star | 10x faster than GraphRAG | arXiv:2410.05779 |
| **Contextual Retrieval (Anthropic)** | Best Practice | Reduces chunk decontextualization | Anthropic Engineering Blog |
| **RAPTOR** | Academic | Hierarchical summarization tree | ICLR 2024 |
| **Self-RAG** | Adaptive | Reduces hallucinations | arXiv:2310.11511 |
| **ColBERT Late Interaction** | Emerging | Token-level precision | IBM, OpenSearch Blog |
| **Cross-Encoder Reranking** | Production | Top-5 precision boost | Cohere, Jina AI |

### What Works in Production (2026)

```
1. Hybrid Search = Vector + BM25 + RRF Fusion (default in 60% of new deployments)
2. Knowledge Graph extraction for entity relationships
3. Query rewriting (HyDE, step-back prompting)
4. Contextual chunking to preserve meaning
5. Cross-encoder reranking post-retrieval
```

---

## Current Code State: What We Have vs. What's Missing

### Current Implementation (as of June 2026)
| Component | Status | Implementation |
|-----------|--------|----------------|
| Knowledge graph | ✅ Implemented | SQLite + entity/relationship extraction |
| Embeddings | ✅ Real | llama.cpp / nomic-embed GGUF (768-dim) |
| Vector similarity | ✅ Real | Cosine similarity on real embeddings |
| BM25Index | ✅ Real | Proper BM25 implementation in `apollo_hybrid_search.hpp` |
| Hybrid search | ✅ Implemented | Vector + BM25 + graph, weighted-sum fusion |
| Hybrid search API | ✅ Wired | `ApolloCore::query_hybrid()` returns `HybridResult[]` |
| Cross-encoder reranking | ✅ Implemented | `cross_encoder.hpp` |
| Reranking API | ✅ Wired | `ApolloCore::query_with_rerank()` uses `CrossEncoderReranker` |
| Full pipeline | ✅ Wired | `ApolloCore::query_full()` = hybrid → rerank |
| LLM entity extraction | ✅ Implemented | Qwen2.5 via llama.cpp |
| Knowledge graph storage | ✅ Implemented | entities + relationships tables |
| Contextual chunking | ✅ Implemented | `contextual_retrieval.cpp` |
| Query enhancement (HyDE) | ✅ Implemented | `hyde.hpp` |
| Code extraction (M3.B) | ✅ Implemented | tree-sitter AST parsing |
| Multi-project scoping (M3.C) | ✅ Implemented | Schema-level `project_id` column |

### Aspirational / Not Yet Implemented
- RAPTOR-style hierarchical summarization
- Self-RAG adaptive retrieval
- ColBERT late interaction (token-level)
- Production eval benchmarks (RAGAS, BEIR)

### What We Need for True SOTA RAG

---

## Epic Architecture Design

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                           APOLLOCORE v2.0 - SOTA RAG                                │
│                                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐   │
│  │                          INGESTION PIPELINE                                 │   │
│  │                                                                           │   │
│  │  ┌──────────┐    ┌─────────────────┐    ┌──────────────┐                   │   │
│  │  │Document  │───▶│ Smart Chunking  │───▶│  Contextual  │                   │   │
│  │  │ (raw)    │    │ (semantic +     │    │  Enrichment  │                   │   │
│  │  │          │    │  overlap)       │    │  (LLM-based) │                   │   │
│  │  └──────────┘    └─────────────────┘    └──────────────┘                   │   │
│  │                                               │                              │   │
│  │                                               ▼                              │   │
│  │  ┌──────────────────────────────────────────────────────────────────────┐   │   │
│  │  │                    ENTITY EXTRACTION (LLM)                            │   │   │
│  │  │  • Extract entities (PERSON, ORG, LOC, CONCEPT)                      │   │   │
│  │  │  • Extract relationships between entities                             │   │   │
│  │  │  • Generate community summaries                                       │   │   │
│  │  └──────────────────────────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────────────────────────┘   │
│                                           │                                         │
│                    ┌──────────────────────┼──────────────────────┐                 │
│                    ▼                      ▼                      ▼                 │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐     │
│  │    VECTOR STORE     │  │   KNOWLEDGE GRAPH   │  │   BM25 INDEX        │     │
│  │  (embeddings +      │  │  (entities +        │  │  (keyword +         │     │
│  │   contextual chunks) │  │   relationships)     │  │   positional)       │     │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────────┘     │
│                                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────────┐   │
│  │                          QUERY PIPELINE                                     │   │
│  │                                                                           │   │
│  │  ┌──────────┐    ┌─────────────────┐    ┌──────────────────┐              │   │
│  │  │  Query   │───▶│  Query Rewrite  │───▶│  Multi-Query     │              │   │
│  │  │          │    │  (HyDE +        │    │  Expansion       │              │   │
│  │  │          │    │   step-back)    │    │                  │              │   │
│  │  └──────────┘    └─────────────────┘    └──────────────────┘              │   │
│  │                                               │                              │   │
│  │                    ┌──────────────────────────┼────────────────┐            │   │
│  │                    ▼                          ▼                ▼            │   │
│  │  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐ │   │
│  │  │   SEMANTIC SEARCH   │  │   GRAPH TRAVERSAL   │  │   KEYWORD SEARCH    │ │   │
│  │  │  (vector similarity)│  │  (entity expansion)│  │     (BM25)          │ │   │
│  │  └─────────────────────┘  └─────────────────────┘  └─────────────────────┘ │   │
│  │                    │                          │                │             │   │
│  │                    └──────────────────────────┼────────────────┘             │   │
│  │                                             ▼                                │   │
│  │                          ┌─────────────────────────────────┐                  │   │
│  │                          │    RRF FUSION (k=60)           │                  │   │
│  │                          │    Score = Σ 1/(k + rank)     │                  │   │
│  │                          └─────────────────────────────────┘                  │   │
│  │                                             │                                │   │
│  │                                             ▼                                │   │
│  │                          ┌─────────────────────────────────┐                  │   │
│  │                          │     CROSS-ENCODER RERANKING    │                  │   │
│  │                          │     (precision boost)           │                  │   │
│  │                          └─────────────────────────────────┘                  │   │
│  └─────────────────────────────────────────────────────────────────────────────┘   │
│                                           │                                         │
│                                           ▼                                         │
│                          ┌─────────────────────────────────┐                       │
│                          │         GENERATION               │                       │
│                          │   (context + citations)          │                       │
│                          └─────────────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Component Specifications

### 1. Intelligent Chunking Strategy

**Problem with naive chunking:**
- Context lost at boundaries
- Related concepts separated
- No overlap awareness

**Solution: Semantic Chunking with Overlap**

```cpp
struct ChunkConfig {
    size_t target_size = 512;           // tokens
    size_t overlap = 128;               // tokens overlap
    bool sentence_aware = true;          // don't split mid-sentence
    bool paragraph_aware = true;         // respect paragraph boundaries
    float semantic_threshold = 0.85f;   // similarity threshold for merge
    bool recursive = true;               // try smaller if too large
};
```

**Algorithm:**
1. Split by paragraphs first
2. Merge paragraphs until target_size reached
3. If paragraph alone exceeds target_size, split by sentences
4. Add overlap region (last N% of chunk prepended to next)
5. Store source position for citation

### 2. Contextual Enrichment (Anthropic Method)

**Why:** Chunks lose context when extracted from document.

**Solution:** For each chunk, generate contextual summary:

```
Original chunk: "The model achieved 95% accuracy."

Context-enriched chunk:
"From the paper 'ImageNet Classification with Deep CNNs':
[Context] This section discusses experimental results on the ImageNet dataset.
[Chunk] The model achieved 95% accuracy.
[Context] This result outperformed previous methods by 3%.
```

**Implementation:**
- LLM call to generate context for each chunk
- Embed the enriched chunk, not the raw chunk
- Store original + enriched for retrieval + display

### 3. Knowledge Graph Construction

**Microsoft GraphRAG Approach:**

```
┌────────────────────────────────────────────────────────────────────┐
│                      ENTITY EXTRACTION                              │
│                                                                     │
│  Input Text: "Apple Inc. was founded by Steve Jobs in California." │
│                                                                     │
│  Extracted Entities:                                                │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐       │
│  │ Apple Inc.     │  │ Steve Jobs     │  │ California     │       │
│  │ Type: ORG      │  │ Type: PERSON   │  │ Type: LOC      │       │
│  │ Confidence: 0.9│ │ Confidence: 0.95│ │ Confidence: 0.9│       │
│  └────────────────┘  └────────────────┘  └────────────────┘       │
│                                                                     │
│  Extracted Relationships:                                           │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ (Steve Jobs) --[founded]--> (Apple Inc.)    Confidence: 0.95 │  │
│  │ (Apple Inc.)  --[located_in]--> (California) Confidence: 0.8 │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────┘
```

**Community Detection (Leiden Algorithm):**
- Group related entities into communities
- Generate community summaries (LLM)
- Hierarchical structure: Local → Global queries

### 4. Hybrid Search with RRF

**Reciprocal Rank Fusion Formula:**

```
RRF_score(d) = Σ 1/(k + rank_i(d))

Where:
- d = document
- k = fusion constant (typically 60)
- rank_i = rank in result list i
```

**Why RRF over score-based fusion:**
- No need to normalize scores between systems
- Rank-based = more robust to score variance
- Proven effective in production (Azure, OpenSearch, Qdrant)

**Implementation:**

```cpp
struct SearchResult {
    std::string chunk_id;
    std::string content;
    std::string source_node_id;
    float hybrid_score;
    float vector_score;
    float bm25_score;
    float graph_score;
    std::vector<std::string> entities;
};

// See apollo_hybrid_search.hpp for the actual HybridResult struct.
// Returns std::vector<HybridResult> with per-source scores.
// Searchable via ApolloCore::query_hybrid(query, top_k, alpha, beta, gamma).
// Also wired into ApolloCore::query_with_rerank() (vector → LLM rerank) and
// ApolloCore::query_full() (hybrid → LLM rerank).
```

### 5. Query Enhancement Pipeline

**Techniques to implement:**

1. **HyDE (Hypothetical Document Embeddings)**
   - Generate hypothetical answer with LLM
   - Embed the hypothetical answer
   - Use for retrieval

2. **Step-back Prompting**
   - Abstract query to higher-level concept
   - Retrieve on abstract level
   - Refine with specific retrieval

3. **Multi-Query Expansion**
   - Generate N variations of query
   - Retrieve for each variation
   - Deduplicate results

```cpp
class QueryEnhancer {
public:
    struct EnhancementConfig {
        bool use_hyde = true;
        bool use_step_back = true;
        int multi_query_count = 3;
        float hyde_temperature = 0.7f;
    };
    
    // Generate enhanced queries
    std::vector<EnhancedQuery> enhance(const Query& original);
};

struct EnhancedQuery {
    std::string text;
    enum class Type { Original, HyDE, StepBack, MultiQuery };
    Type type;
    float weight;  // Some queries more important than others
};
```

### 6. Cross-Encoder Reranking

**Architecture:**

```
┌─────────────────────────────────────────────────────────────────┐
│                    RERANKING PIPELINE                          │
│                                                                 │
│  Top-100 from RRF ──▶ Cross-Encoder ──▶ Top-10 to Generator   │
│                       (BERT-based)                               │
│                                                                 │
│  Why Cross-Encoder over Bi-Encoder:                            │
│  • Full cross-attention between query and document              │
│  • Higher precision at cost of speed                            │
│  • Late interaction (ColBERT) balances speed/precision          │
└─────────────────────────────────────────────────────────────────┘
```

**Implementation Options:**
1. **Cohere Rerank** - API-based, high quality
2. **BERT cross-encoder** - Local, from HuggingFace
3. **Jina AI reranker** - Open source, GGUF support
4. **ColBERT late interaction** - Token-level precision

### 7. Self-RAG Integration (Future)

**Reflection Tokens:**
- `[Retrieval]` - Should we retrieve?
- `[Relevant]` - Is the chunk relevant?
- `[Fully Supported]` - Is the claim supported?
- `[Useful]` - Should we use this?

---

## Data Model Design

### Knowledge Graph Schema

```cpp
namespace apollocore {

// Core graph entities
struct Entity {
    std::string id;                    // UUID
    std::string name;                  // "Steve Jobs"
    std::string type;                  // PERSON, ORG, LOC, CONCEPT, EVENT
    std::string description;            // LLM-generated description
    float confidence;                   // Extraction confidence
    std::vector<std::string> aliases;   // Alternative names
    
    // Graph position
    std::string community_id;           // Community membership
    size_t community_level;             // Hierarchical level
    
    // Embedding
    std::vector<float> embedding;      // Entity embedding
    
    // Source tracking
    std::string source_chunk_id;
    std::string source_document_id;
};

struct Relationship {
    std::string id;
    std::string source_id;             // Entity ID
    std::string target_id;             // Entity ID
    std::string type;                  // founded, located_in, works_for, etc.
    std::string description;           // LLM-generated description
    
    float confidence;                   // Extraction confidence
    std::string source_chunk_id;
};

struct Community {
    std::string id;
    size_t level;                      // Hierarchy level (0=root)
    std::string summary;               // LLM-generated community summary
    std::vector<std::string> entity_ids;
    std::vector<std::string> child_community_ids;
    std::string parent_community_id;
    
    std::vector<float> summary_embedding;  // For global queries
};

struct CommunityReport {
    std::string community_id;
    std::string level;                 // "1", "2", "3" (full hierarchy)
    std::string title;                 // Short title
    std::string summary;               // Full LLM-generated report
    std::vector<float> report_embedding;
    
    // Findings
    std::vector<std::string> findings;
    std::vector<std::string> key_entities;
};

} // namespace
```

### Document & Chunk Schema

```cpp
struct Document {
    std::string id;
    std::string title;
    std::string source_path;
    std::string content;               // Full text
    
    // Metadata
    std::unordered_map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point created_at;
    
    // Processing state
    enum class ProcessingState { 
        Pending, 
        Chunking, 
        Extracting, 
        Embedding, 
        Complete,
        Failed 
    };
    ProcessingState state;
    std::string error_message;
};

struct Chunk {
    std::string id;
    std::string document_id;
    
    // Content
    std::string content;               // Original chunk
    std::string enriched_content;      // With contextual prefix
    size_t start_char;                 // Position in document
    size_t end_char;
    
    // Chunking metadata
    size_t chunk_index;
    bool is_continuation;              // Has overlap prefix
    
    // Embeddings (multiple for flexibility)
    std::vector<float> content_embedding;
    std::vector<float> enriched_embedding;
    
    // For reranking
    std::string formatted_for_reranker;  // "[Query] ... [Document] ..."
};

struct ChunkMetadata {
    std::string document_id;
    std::string title;
    std::vector<std::string> headings;    // Section hierarchy
    std::string entity_summary;           // Key entities in chunk
};
```

---

## API Design

### Core Interfaces

```cpp
namespace apollocore {

class ApolloCoreV2 {
public:
    // Document ingestion
    struct IngestOptions {
        bool extract_entities = true;
        bool extract_relationships = true;
        bool generate_communities = true;
        bool contextual_enrich = true;
        int community_levels = 3;
    };
    
    // Single document
    struct IngestResult {
        std::string document_id;
        size_t chunks_created;
        size_t entities_extracted;
        size_t relationships_extracted;
        size_t communities_created;
    };
    
    IngestResult ingest_document(
        const std::string& content,
        const std::string& title,
        const IngestOptions& options = {}
    );
    
    // Batch ingestion
    std::vector<IngestResult> ingest_documents(
        const std::vector<Document>& documents,
        const IngestOptions& options = {}
    );

    // Query pipeline
    struct QueryOptions {
        size_t max_results = 20;
        float similarity_threshold = 0.5f;
        
        // Search strategy
        bool use_semantic = true;
        bool use_keyword = true;
        bool use_graph = true;
        bool use_hyde = false;
        bool use_reranking = true;
        
        // Weights for RRF fusion
        float semantic_weight = 0.4f;
        float keyword_weight = 0.3f;
        float graph_weight = 0.3f;
        
        // Reranking
        int rerank_top_k = 10;
        std::string reranker_model = "cross-encoder";
    };
    
    struct QueryResult {
        struct Source {
            std::string chunk_id;
            std::string content;
            std::string document_title;
            float score;
            float vector_score;
            float bm25_score;
            float graph_score;
            std::vector<std::string> cited_entities;
        };
        
        std::vector<Source> sources;
        std::string generated_response;
        float response_confidence;
        std::vector<std::string> citations;
        
        // For debugging
        std::vector<std::string> queries_used;
        size_t chunks_evaluated;
    };
    
    QueryResult query(
        const std::string& question,
        const QueryOptions& options = {}
    );

    // Knowledge graph queries
    struct GraphQueryOptions {
        size_t max_hops = 2;
        size_t max_entities = 50;
        bool include_relationships = true;
        bool include_community_reports = true;
    };
    
    // Local queries (specific entities)
    std::vector<Entity> find_entities(const std::string& query);
    std::vector<Relationship> find_relationships(const std::string& entity_id);
    
    // Global queries (use community reports)
    std::vector<CommunityReport> global_query(const std::string& query);
    
    // Community browsing
    std::vector<Community> get_communities(size_t level = 0);
    CommunityReport get_community_report(const std::string& community_id);
    
    // Statistics
    struct Stats {
        size_t document_count;
        size_t chunk_count;
        size_t entity_count;
        size_t relationship_count;
        size_t community_count;
        size_t community_report_count;
    };
    
    Stats get_stats() const;
};

} // namespace
```

---

## Embedding Model Integration

### llama.cpp Integration (Production Ready)

```cpp
class EmbeddingModel {
public:
    struct Config {
        std::string model_path;         // GGUF file path
        int context_length = 8192;       // Nomic supports 8k
        int n_threads = 4;
        int n_gpu_layers = 32;          // For Vulkan/CUDA
        bool use_vulkan = true;          // AMD GPU support
    };
    
    bool load(const Config& config);
    bool is_loaded() const;
    
    // Single embedding
    EmbeddingVector embed(const std::string& text);
    
    // Batch embedding (more efficient)
    std::vector<EmbeddingVector> embed_batch(const std::vector<std::string>& texts);
    
    size_t dimensions() const { return dimensions_; }
    std::string model_name() const { return model_name_; }

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    size_t dimensions_ = 0;
    std::string model_name_;
};
```

### Recommended Models (GGUF Format)

| Model | Dimensions | Context | Size | Notes |
|-------|-----------|---------|------|-------|
| **nomic-embed-text-v1.5** | 768 | 8k | 274MB | Best overall, 8k context |
| **nomic-embed-text-v1.5-Q4_K_M** | 768 | 8k | 84MB | Quantized, great quality/size |
| **all-MiniLM-L6-v2** | 384 | 256 | 90MB | Fast, good for short text |
| **gte-Qwen2-1.5B-instruct** | 1536 | 8k | 3GB | High quality, larger |

### Model Download Instructions

```bash
mkdir -p models

# Nomic Embed Text v1.5 (recommended)
wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf \
    -O models/nomic-embed-text-v1.5.Q4_K_M.gguf

# Alternative: Full precision
wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.gguf \
    -O models/nomic-embed-text-v1.5.gguf
```

---

## Implementation Roadmap

### Phase 1: Foundation (This Session)
- [ ] Replace HashEmbedding with real llama.cpp EmbeddingModel
- [ ] Add configurable embedding model loading
- [ ] Verify real embeddings work

### Phase 2: Knowledge Graph (Next Session)
- [ ] Implement Entity extraction interface (LLM-based)
- [ ] Implement Relationship extraction
- [ ] Build graph storage (in-memory or SQLite)
- [ ] Add graph traversal queries

### Phase 3: Hybrid Search Enhancement
- [ ] Parallelize semantic + keyword + graph search
- [ ] Implement RRF fusion properly
- [ ] Add configurable weights

### Phase 4: Contextual Chunking
- [ ] Implement semantic chunking with overlap
- [ ] Add contextual enrichment (LLM call)
- [ ] Store both original and enriched

### Phase 5: Advanced Retrieval
- [ ] Add HyDE query enhancement
- [ ] Implement cross-encoder reranking
- [ ] Add query expansion (multi-query)

### Phase 6: Community Detection
- [ ] Implement Leiden/Louvain community detection
- [ ] Generate community summaries (LLM)
- [ ] Add global query capability

---

## File Structure

```
ApolloCore/
├── CMakeLists.txt                  # Top-level build (links libapollo_core.a)
├── README.md                       # Main docs
├── CodeMax.md                      # Planning + status doc
├── docs/
│   ├── design.md                   # This document
│   ├── BANNER_PROMPTS.md           # Prompts for LLM extraction
│   └── file-formats.md             # ApolloCore file formats
├── include/apollocore/
│   ├── apollo_persistence.hpp      # Main high-level API (ApolloCore)
│   ├── kb_persistence.hpp          # SQLite + FBIN + project registry
│   ├── entity_extractor.hpp        # LLM entity extraction (Qwen2.5)
│   ├── cross_encoder.hpp           # Cross-encoder reranking
│   ├── hyde.hpp                    # HyDE query enhancement
│   ├── contextual_retrieval.hpp    # Anthropic-style chunking
│   ├── embedding_model.hpp         # GGUF embeddings
│   ├── onnx_embedding.hpp          # ONNX embeddings (alt)
│   ├── persistence.hpp             # Schema + migrations
│   ├── fbin.hpp                    # Binary embedding storage
│   ├── apollo_hybrid_search.hpp    # Hybrid vector + BM25 + RRF
│   ├── apollo_ts_wrapper.hpp       # tree-sitter C++ RAII wrapper
│   └── apollo_code_extractor.hpp   # M3.B AST → entities/relationships
├── src/
│   ├── apollocore.cpp              # ApolloEngine (LLM driver)
│   ├── entity_extractor.cpp        # Entity extraction (uses llama.cpp)
│   ├── cross_encoder.cpp           # Cross-encoder reranking
│   ├── hyde.cpp                    # HyDE
│   ├── contextual_retrieval.cpp    # Contextual chunking
│   ├── embedding_model.cpp         # llama.cpp embeddings wrapper
│   ├── onnx_embedding.cpp          # ONNX embeddings
│   ├── persistence.cpp             # Schema + apply_project_migrations
│   ├── fbin.cpp                    # FBIN storage format
│   ├── kb_persistence.cpp          # SQLite + project registry
│   ├── apollo_persistence.cpp      # ApolloCore v2 + M3.C scoping
│   ├── apollo_hybrid_search.cpp    # Hybrid search impl
│   ├── apollo_ts_wrapper.cpp       # tree-sitter wrapper impl
│   └── apollo_code_extractor.cpp   # Code extraction impl
├── models/                         # GGUF models (user-provided, auto-downloaded)
│   └── README.md
├── examples/
│   └── full_demo.cpp               # End-to-end demo
└── tests/
    ├── test_v2.cpp                 # Hybrid search v2 tests
    ├── test_embeddings.cpp         # Embedding model tests
    ├── test_entity_extraction.cpp  # LLM extraction tests
    ├── test_cross_encoder.cpp      # Cross-encoder tests
    ├── test_persistence.cpp        # Schema/persistence tests
    ├── test_fbin.cpp               # FBIN format tests
    ├── test_kb_persistence.cpp     # KB persistence tests
    ├── test_apollo_persistence.cpp # ApolloCore integration tests
    ├── test_multi_project.cpp      # M3.C tests (15 tests)
    ├── test_code_extractor.cpp     # M3.B tests (12 tests)
    ├── test_hyde.cpp               # HyDE query enhancement tests
    ├── test_contextual_retrieval.cpp # Anthropic-style chunking tests
    └── test_hybrid_search.cpp      # M2.7+M2.8 wiring tests (6 tests)

**Active tests: 13 suites, all passing via `ctest`**

---

## Evaluation Metrics

### Retrieval Metrics (Standard)

| Metric | Description | Target |
|--------|-------------|--------|
| **Recall@K** | Relevant docs in top-K | >0.85 |
| **MRR** | Mean Reciprocal Rank | >0.70 |
| **NDCG@K** | Normalized DCG | >0.75 |

### RAG-Specific Metrics

| Metric | Description |
|--------|-------------|
| **Context Precision** | Retrieved chunks actually relevant |
| **Answer Faithfulness** | Answer grounded in context |
| **Answer Relevance** | Answer addresses the question |

### Evaluation Framework

```cpp
class RAGEvaluator {
public:
    struct TestCase {
        std::string question;
        std::vector<std::string> ground_truth_chunks;
        std::string expected_answer_type;
    };
    
    struct EvaluationResult {
        float recall;
        float precision;
        float mrr;
        float ndcg;
        float faithfulness;
        float relevance;
    };
    
    EvaluationResult evaluate(
        ApolloCoreV2& system,
        const std::vector<TestCase>& test_cases
    );
};
```

---

## References

1. **GraphRAG**: Microsoft Research - "From Local to Global: A GraphRAG Approach to Query-Focused Summarization" (arXiv:2404.16130)

2. **LightRAG**: "LightRAG: Simple and Fast Retrieval-Augmented Generation" (arXiv:2410.05779)

3. **RAPTOR**: "Recursive Abstractive Processing for Tree-Organized Retrieval" (ICLR 2024)

4. **Self-RAG**: "Self-RAG: Learning to Retrieve, Generate, and Critique" (arXiv:2310.11511)

5. **Anthropic Contextual Retrieval**: https://www.anthropic.com/engineering/contextual-retrieval

6. **Hybrid Search with RRF**: Azure AI Search, OpenSearch, Qdrant documentation

7. **ColBERT**: "ColBERT: Efficient and Effective Passage Search via Contextualized Late Interaction" (SIGIR 2020)

---

## Appendix: llama.cpp Embedding API

```cpp
// From llama.cpp include/llama.h

// Get embeddings from context
const float* llama_get_embeddings(const struct llama_context * ctx);

// Embed a single token sequence
struct llama_batch {
    llama_token * token;     // token IDs
    float * pos;             // positions
    int32_t * n_seq_id;      // number of sequences
    llama_seq_id * seq_id;   // sequence IDs
    int32_t  n_tokens;
    int32_t  n_seq_total;
};

// Simplified embedding extraction
float* embeddings = llama_get_embeddings(ctx);
std::vector<float> vec(embeddings, embeddings + n_embd);
```

---

*Document Version: 1.0*  
*Created: 2026-06-21*  
*Status: Design Complete - Implementation Pending*
