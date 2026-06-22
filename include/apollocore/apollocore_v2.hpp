#pragma once

/**
 * ApolloCore v2 - State-of-the-Art Knowledge Graph RAG System
 * 
 * Features:
 * - Real embeddings via llama.cpp (GGUF models)
 * - Hybrid search (Vector + BM25 + RRF)
 * - Knowledge graph extraction (planned)
 * - Cross-encoder reranking (planned)
 * - Query enhancement HyDE (planned)
 */

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <chrono>
#include <optional>
#include <algorithm>
#include <queue>
#include <random>
#include <numeric>
#include <cmath>
#include <fstream>
#include <sstream>

#include "llama.h"

namespace apollocore_v2 {

// ============================================================================
// Core Types
// ============================================================================

using ChunkID = std::string;
using NodeID = std::string;
using EntityID = std::string;
using EmbeddingVector = std::vector<float>;

struct Metadata : std::unordered_map<std::string, std::string> {
    using std::unordered_map<std::string, std::string>::unordered_map;
};

struct EmbeddingResult {
    EmbeddingVector vector;
    std::string model_name;
    size_t dimensions{0};
};

// ============================================================================
// Embedding Model - Real llama.cpp Integration
// ============================================================================

class EmbeddingModel {
public:
    struct Config {
        std::string model_path;
        int context_length{2048};
        int n_threads{4};
        int n_gpu_layers{32};
        bool use_gpu{true};
    };
    
    EmbeddingModel() = default;
    explicit EmbeddingModel(const Config& config) { load(config); }
    ~EmbeddingModel();
    
    // Load GGUF model
    bool load(const Config& config);
    bool load(const std::string& path, bool use_gpu = true) {
        return load({path, 2048, 4, 32, use_gpu});
    }
    
    bool is_loaded() const { return ctx_ != nullptr; }
    size_t dimensions() const { return n_embd_; }
    std::string model_name() const { return model_path_; }
    
    // Embed text
    EmbeddingResult embed(std::string_view text);
    std::vector<EmbeddingResult> embed_batch(const std::vector<std::string>& texts);
    
private:
    llama_model* model_{nullptr};
    llama_context* ctx_{nullptr};
    int n_ctx_{2048};
    int n_threads_{4};
    size_t n_embd_{0};
    std::string model_path_;
    bool has_encoder_{false};  // True for BERT-style embedding models
    
    std::vector<llama_token> tokenize(const std::string& text);
    EmbeddingVector run_inference(const std::vector<llama_token>& tokens);
    void normalize(EmbeddingVector& vec);
};

// ============================================================================
// Vector Store
// ============================================================================

class VectorStore {
public:
    bool add_vector(const ChunkID& id, const EmbeddingVector& vec, const Metadata& meta = {}) {
        if (vec.empty()) return false;
        if (dims_ == 0) dims_ = vec.size();
        vectors_[id] = vec;
        metadata_[id] = meta;
        return true;
    }
    
    std::optional<EmbeddingVector> get_vector(const ChunkID& id) const {
        auto it = vectors_.find(id);
        return it != vectors_.end() ? std::optional(it->second) : std::nullopt;
    }
    
    struct SearchResult {
        ChunkID id;
        float similarity;
        Metadata metadata;
    };
    
    std::vector<SearchResult> search(const EmbeddingVector& query, size_t top_k, float min_sim = 0.0f) const {
        if (query.empty() || vectors_.empty()) return {};
        
        std::vector<SearchResult> results;
        for (auto& [id, vec] : vectors_) {
            float sim = cosine_similarity(query, vec);
            if (sim >= min_sim) {
                auto mit = metadata_.find(id);
                results.push_back({id, sim, mit != metadata_.end() ? mit->second : Metadata{}});
            }
        }
        
        std::sort(results.begin(), results.end(), 
                  [](const auto& a, const auto& b) { return a.similarity > b.similarity; });
        if (results.size() > top_k) results.resize(top_k);
        return results;
    }
    
    size_t size() const { return vectors_.size(); }
    size_t dimensions() const { return dims_; }
    void clear() { vectors_.clear(); metadata_.clear(); dims_ = 0; }
    bool remove_vector(const ChunkID& id) {
        auto it = vectors_.find(id);
        if (it == vectors_.end()) return false;
        vectors_.erase(it);
        metadata_.erase(id);
        return true;
    }
    
private:
    float cosine_similarity(const EmbeddingVector& a, const EmbeddingVector& b) const {
        float dot = 0, na = 0, nb = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            dot += a[i] * b[i];
            na += a[i] * a[i];
            nb += b[i] * b[i];
        }
        return dot / (std::sqrt(na) * std::sqrt(nb) + 1e-10f);
    }
    
    std::unordered_map<ChunkID, EmbeddingVector> vectors_;
    std::unordered_map<ChunkID, Metadata> metadata_;
    size_t dims_{0};
};

// ============================================================================
// BM25 Index
// ============================================================================

class BM25Index {
public:
    struct DocInfo {
        std::string content;
        std::vector<std::string> terms;
        std::unordered_map<std::string, int> term_freqs;
        size_t doc_len{0};
    };
    
    struct ScoredDoc {
        ChunkID doc_id;
        float score;
    };
    
    void add_document(const ChunkID& doc_id, const std::string& content);
    std::vector<ScoredDoc> search(const std::vector<std::string>& query_terms, size_t top_k = 10);
    void clear();
    
private:
    static constexpr float k1_ = 1.5f;
    static constexpr float b_ = 0.75f;
    
    std::vector<std::string> tokenize(const std::string& text);
    float idf(const std::string& term);
    
    std::unordered_map<ChunkID, DocInfo> docs_;
    std::unordered_map<std::string, std::vector<ChunkID>> term_to_docs_;
    float avg_doc_len_{0};
    size_t total_docs_{0};
    size_t total_terms_{0};
    
    static const std::unordered_set<std::string> stopwords_;
};

const std::unordered_set<std::string> BM25Index::stopwords_{
    "the", "and", "for", "are", "but", "not", "you", "all", "can", "had", "her",
    "was", "one", "our", "out", "has", "have", "been", "were", "they", "this",
    "that", "with", "from", "your", "what", "when", "where", "which", "their",
    "will", "would", "there", "could", "other", "into", "just", "about"
};

// ============================================================================
// Smart Chunking
// ============================================================================

struct ChunkConfig {
    size_t target_size{512};       // characters
    size_t overlap{128};           // characters
    bool sentence_aware{true};
    bool paragraph_aware{true};
};

struct Chunk {
    ChunkID id;
    std::string content;
    std::string enriched_content;  // With context (future)
    NodeID source_node_id;
    size_t start_pos{0};
    size_t end_pos{0};
    size_t chunk_index{0};
    bool is_continuation{false};
    EmbeddingVector embedding;
    std::chrono::system_clock::time_point created_at;
    
    Chunk() : created_at(std::chrono::system_clock::now()) {}
};

class SmartChunker {
public:
    explicit SmartChunker(const ChunkConfig& config = {}) : config_(config) {}
    
    std::vector<Chunk> chunk(const std::string& text, const NodeID& source_id);
    
private:
    ChunkConfig config_;
    
    std::vector<std::string> split_sentences(const std::string& text);
    std::vector<std::string> split_paragraphs(const std::string& text);
};

// ============================================================================
// Knowledge Node (Tree Structure)
// ============================================================================

using NodeHandle = std::shared_ptr<class KnowledgeNode>;

class KnowledgeNode {
public:
    NodeID id;
    std::string title;
    std::string content;
    Metadata metadata;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::weak_ptr<KnowledgeNode> parent;
    std::vector<NodeHandle> children;
    
    KnowledgeNode() : created_at(std::chrono::system_clock::now()), updated_at(created_at) {}
    KnowledgeNode(NodeID id_, std::string_view title_, std::string_view content_) 
        : id(std::move(id_)), title(title_), content(content_),
          created_at(std::chrono::system_clock::now()), updated_at(created_at) {}
    
    bool is_root() const { return parent.expired(); }
};

class KnowledgeTree {
public:
    KnowledgeTree();
    NodeHandle create_node(std::string_view title, std::string_view content = {}, NodeHandle parent = nullptr);
    bool delete_node(NodeHandle node);
    NodeHandle get_node(const NodeID& id) const;
    NodeHandle get_root() const;
    bool move_node(NodeHandle node, NodeHandle new_parent);
    std::vector<NodeHandle> get_children(NodeHandle node) const;
    std::vector<NodeHandle> get_ancestors(NodeHandle node) const;
    std::vector<NodeHandle> get_descendants(NodeHandle node) const;
    std::string_view get_content(NodeHandle node) const;
    bool set_content(NodeHandle node, std::string_view content);
    void traverse_bfs(std::function<void(NodeHandle)> callback) const;
    size_t node_count() const;
    void clear();
    
private:
    NodeHandle root_;
    std::unordered_map<NodeID, NodeHandle> nodes_;
    size_t node_counter_{0};
    NodeID generate_id();
    void collect_descendants(NodeHandle node, std::vector<NodeHandle>& result) const;
};

// ============================================================================
// Knowledge Graph (Entity Extraction - Interface)
// ============================================================================

struct Entity {
    EntityID id;
    std::string name;
    std::string type;          // PERSON, ORG, LOC, CONCEPT, EVENT
    std::string description;
    float confidence{0.0f};
    std::vector<std::string> aliases;
    std::string community_id;
    ChunkID source_chunk_id;
    EmbeddingVector embedding;
};

struct Relationship {
    std::string id;
    EntityID source_id;
    EntityID target_id;
    std::string type;          // founded, works_for, located_in, etc.
    std::string description;
    float confidence{0.0f};
    ChunkID source_chunk_id;
};

struct Community {
    std::string id;
    size_t level{0};
    std::string summary;
    std::vector<EntityID> entity_ids;
    std::string parent_id;
    std::vector<std::string> child_ids;
    EmbeddingVector summary_embedding;
};

class KnowledgeGraph {
public:
    // Entity operations
    EntityID add_entity(const Entity& entity);
    std::optional<Entity> get_entity(const EntityID& id) const;
    std::vector<Entity> find_entities(std::string_view query);
    std::vector<Entity> get_entities_by_type(const std::string& type);
    
    // Relationship operations
    std::string add_relationship(const Relationship& rel);
    std::vector<Relationship> get_relationships(const EntityID& entity_id);
    std::vector<Relationship> get_all_relationships() const;
    
    // Community operations
    std::string add_community(const Community& community);
    std::optional<Community> get_community(const std::string& id) const;
    std::vector<Community> get_communities_at_level(size_t level) const;
    
    // Graph traversal
    std::vector<Entity> expand_from_entity(const EntityID& seed, size_t max_hops = 2);
    
    // Statistics
    struct Stats {
        size_t entity_count{0};
        size_t relationship_count{0};
        size_t community_count{0};
    };
    Stats get_stats() const;
    
    void clear();
    
private:
    std::unordered_map<EntityID, Entity> entities_;
    std::unordered_map<std::string, Relationship> relationships_;
    std::unordered_map<std::string, Community> communities_;
    std::unordered_map<std::string, std::vector<std::string>> entity_to_communities_;
    
    EntityID generate_entity_id();
};

// ============================================================================
// Query Types
// ============================================================================

struct QueryOptions {
    size_t max_results{10};
    float similarity_threshold{0.5f};
    
    // Search strategy
    bool use_semantic{true};
    bool use_keyword{true};
    bool use_graph{false};
    
    // RRF weights
    float semantic_weight{0.5f};
    float keyword_weight{0.5f};
    
    // Number of results to fetch from each retriever before fusion
    size_t initial_top_k{50};
    
    // Reranking (future)
    bool use_reranking{false};
};

struct QueryResultItem {
    ChunkID chunk_id;
    NodeID node_id;
    std::string title;
    std::string content;
    float score{0.0f};
    float vector_score{0.0f};
    float bm25_score{0.0f};
    float graph_score{0.0f};
    float semantic_weight{0.5f};
    float keyword_weight{0.5f};
    std::string result_type{"hybrid"};
    Metadata metadata;
};

struct QueryResults {
    std::vector<QueryResultItem> items;
    size_t total_hits{0};
    std::vector<std::string> queries_used;
};

// ============================================================================
// RRF Fusion
// ============================================================================

class HybridSearchEngine {
public:
    struct Config {
        float semantic_weight = 0.5f;
        float keyword_weight = 0.5f;
        float graph_weight = 0.0f;
        float rrf_k = 60.0f;           // RRF constant
        size_t initial_top_k = 50;      // Results from each retriever
    };
    
    HybridSearchEngine() : config_(Config{}) {}
    explicit HybridSearchEngine(const Config& config) : config_(config) {}
    
    void set_vector_store(VectorStore* store) { vector_store_ = store; }
    void set_bm25_index(BM25Index* index) { bm25_index_ = index; }
    void set_embedding_model(EmbeddingModel* model) { embedding_model_ = model; }
    void set_knowledge_graph(KnowledgeGraph* graph) { knowledge_graph_ = graph; }
    
    QueryResults search(std::string_view query, const QueryOptions& options);
    
private:
    Config config_;
    VectorStore* vector_store_{nullptr};
    BM25Index* bm25_index_{nullptr};
    EmbeddingModel* embedding_model_{nullptr};
    KnowledgeGraph* knowledge_graph_{nullptr};
    
    std::vector<std::string> parse_query(std::string_view query);
    std::vector<VectorStore::SearchResult> semantic_search(std::string_view query, size_t top_k);
    std::vector<BM25Index::ScoredDoc> keyword_search(const std::vector<std::string>& terms, size_t top_k);
    std::vector<QueryResultItem> fuse_results(
        const std::vector<VectorStore::SearchResult>& semantic,
        const std::vector<BM25Index::ScoredDoc>& keyword,
        size_t top_k);
};

// ============================================================================
// ApolloCore V2 - Main API
// ============================================================================

// Forward declaration
class Chunk;
using ChunkHandle = std::shared_ptr<Chunk>;

/**
 * ApolloEngine - The core search engine (advanced use only).
 *
 * This is the original full-featured ApolloCore class with template-heavy
 * handles (NodeHandle, ChunkHandle), knowledge graph integration, hybrid
 * search (vector + BM25 + RRF), and chunk enrichment.
 *
 * For most users, the simpler `ApolloCore` class in `apollo_persistence.hpp`
 * is recommended — it has all the same capabilities with a cleaner API.
 */
class ApolloEngine {
public:
    ApolloEngine();
    ~ApolloEngine();
    
    // Model management
    bool load_embedding_model(const std::string& path, bool use_gpu = true);
    bool is_model_loaded() const { return embedding_model_ && embedding_model_->is_loaded(); }
    
    // Node operations
    NodeHandle create_node(std::string_view title, std::string_view content = {});
    NodeHandle create_node(std::string_view title, std::string_view content, NodeHandle parent);
    bool delete_node(NodeHandle node, bool delete_children = true);
    NodeHandle get_node(const NodeID& id) const;
    NodeHandle get_root() const;
    std::vector<NodeHandle> get_children(NodeHandle node) const;
    std::vector<NodeHandle> get_ancestors(NodeHandle node) const;
    std::vector<NodeHandle> get_descendants(NodeHandle node) const;
    std::string_view get_content(NodeHandle node) const;
    bool set_content(NodeHandle node, std::string_view content);
    
    // Chunk operations
    ChunkHandle add_chunk(std::string_view content, NodeHandle parent_node);
    std::vector<ChunkHandle> add_document(std::string_view text, NodeHandle parent_node, 
                                           const ChunkConfig& config = {});
    
    // Search
    std::vector<ChunkHandle> search_chunks(std::string_view query, size_t top_k = 10);
    QueryResults query(std::string_view query_text, const QueryOptions& options = {});
    
    // Statistics
    struct Stats {
        size_t node_count{0};
        size_t chunk_count{0};
        size_t entity_count{0};
        size_t relationship_count{0};
    };
    Stats get_stats() const;
    
    void clear();
    
private:
    ChunkID gen_chunk_id();
    std::vector<std::string> split_sentences(std::string_view text) const;
    
    std::unique_ptr<EmbeddingModel> embedding_model_;
    KnowledgeTree tree_;
    VectorStore vector_store_;
    BM25Index bm25_index_;
    KnowledgeGraph knowledge_graph_;
    HybridSearchEngine hybrid_search_;
    SmartChunker chunker_;
    
    std::unordered_map<ChunkID, std::shared_ptr<Chunk>> chunks_;
    std::unordered_map<NodeID, std::vector<ChunkID>> node_to_chunks_;
    size_t chunk_counter_{0};
};

// ============================================================================
// Inline Implementations
// ============================================================================

// --- EmbeddingModel ---

inline EmbeddingModel::~EmbeddingModel() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

inline bool EmbeddingModel::load(const Config& config) {
    model_path_ = config.model_path;
    n_ctx_ = config.context_length;
    n_threads_ = config.n_threads;
    
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = config.use_gpu ? config.n_gpu_layers : 0;
    mparams.vocab_only = false;
    
    model_ = llama_model_load_from_file(config.model_path.c_str(), mparams);
    if (!model_) {
        fprintf(stderr, "[EmbeddingModel] Failed to load: %s\n", config.model_path.c_str());
        return false;
    }
    
    // Check if this is an encoder model (like BERT for embeddings)
    has_encoder_ = llama_model_has_encoder(model_);
    
    n_embd_ = llama_model_n_embd(model_);
    if (n_embd_ == 0) {
        fprintf(stderr, "[EmbeddingModel] Model has no embedding dimension!\n");
        return false;
    }
    
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = n_ctx_;
    cparams.n_threads = n_threads_;
    cparams.n_threads_batch = n_threads_;
    
    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        fprintf(stderr, "[EmbeddingModel] Failed to create context\n");
        return false;
    }
    
    // Enable embeddings extraction
    llama_set_embeddings(ctx_, true);
    
    printf("[EmbeddingModel] Loaded: %s\n", config.model_path.c_str());
    printf("[EmbeddingModel]   Dimensions: %zu\n", n_embd_);
    printf("[EmbeddingModel]   GPU: %s\n", config.use_gpu ? "yes" : "no");
    printf("[EmbeddingModel]   Has encoder: %s\n", has_encoder_ ? "yes" : "no");
    
    return true;
}

inline std::vector<llama_token> EmbeddingModel::tokenize(const std::string& text) {
    const auto vocab = llama_model_get_vocab(model_);
    std::vector<llama_token> tokens(1024);
    int n_tokens = llama_tokenize(vocab, text.data(), (int)text.size(),
                                   tokens.data(), (int)tokens.size(), true, true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, text.data(), (int)text.size(),
                                   tokens.data(), (int)tokens.size(), true, true);
    }
    tokens.resize(std::max(0, n_tokens));
    return tokens;
}

inline EmbeddingVector EmbeddingModel::run_inference(const std::vector<llama_token>& tokens) {
    if (!ctx_ || tokens.empty()) return EmbeddingVector(n_embd_, 0.0f);
    
    // For encoder models (BERT-style), use llama_encode
    // For decoder models (LLM-style), use llama_decode
    if (has_encoder_) {
        // Use llama_encode for encoder models like nomic-bert
        llama_batch batch = llama_batch_init((int32_t)tokens.size(), 0, 1);
        
        for (int i = 0; i < (int)tokens.size(); i++) {
            batch.token[i] = tokens[i];
            batch.pos[i] = i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = 1;  // We want embeddings for all tokens
        }
        batch.n_tokens = (int)tokens.size();
        
        if (llama_encode(ctx_, batch)) {
            fprintf(stderr, "[EmbeddingModel] llama_encode failed\n");
            llama_batch_free(batch);
            return EmbeddingVector(n_embd_, 0.0f);
        }
        
        llama_batch_free(batch);
    } else {
        // Use llama_decode for decoder models
        auto batch = llama_batch_get_one(const_cast<llama_token*>(tokens.data()), (int)tokens.size());
        if (llama_decode(ctx_, batch)) {
            fprintf(stderr, "[EmbeddingModel] llama_decode failed\n");
            return EmbeddingVector(n_embd_, 0.0f);
        }
    }
    
    // Get embeddings - for pooled models, use sequence embeddings
    // Check pooling type: 0=NONE, 1=MEAN, 2=CLS, 3=LAST
    auto pooling = llama_pooling_type(ctx_);
    
    if (pooling != 0) {  // Has pooling enabled
        float* pooled = llama_get_embeddings_seq(ctx_, 0);
        if (pooled) {
            EmbeddingVector result(pooled, pooled + n_embd_);
            normalize(result);
            return result;
        }
    }
    
    // Fallback to regular embeddings
    const float* embeddings = llama_get_embeddings(ctx_);
    if (!embeddings) {
        fprintf(stderr, "[EmbeddingModel] Failed to get embeddings\n");
        return EmbeddingVector(n_embd_, 0.0f);
    }
    
    EmbeddingVector result(embeddings, embeddings + n_embd_);
    normalize(result);
    return result;
}

inline void EmbeddingModel::normalize(EmbeddingVector& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-10f) for (float& v : vec) v /= norm;
}

inline EmbeddingResult EmbeddingModel::embed(std::string_view text) {
    if (!is_loaded()) {
        return {EmbeddingVector(n_embd_, 0.0f), "none", n_embd_};
    }
    auto vec = run_inference(tokenize(std::string(text)));
    return {std::move(vec), model_path_, n_embd_};
}

inline std::vector<EmbeddingResult> EmbeddingModel::embed_batch(const std::vector<std::string>& texts) {
    std::vector<EmbeddingResult> results;
    results.reserve(texts.size());
    for (const auto& t : texts) results.push_back(embed(t));
    return results;
}

// --- BM25Index ---

inline void BM25Index::add_document(const ChunkID& doc_id, const std::string& content) {
    DocInfo doc;
    doc.content = content;
    doc.terms = tokenize(content);
    doc.doc_len = doc.terms.size();
    for (const auto& t : doc.terms) doc.term_freqs[t]++;
    
    docs_[doc_id] = std::move(doc);
    for (const auto& t : docs_[doc_id].terms) term_to_docs_[t].push_back(doc_id);
    
    total_docs_++;
    total_terms_ += docs_[doc_id].doc_len;
    avg_doc_len_ = static_cast<float>(total_terms_) / total_docs_;
}

inline std::vector<std::string> BM25Index::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current += std::tolower(static_cast<unsigned char>(c));
        } else if (!current.empty()) {
            if (current.size() > 2 && !stopwords_.count(current)) tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty() && current.size() > 2 && !stopwords_.count(current)) tokens.push_back(current);
    return tokens;
}

inline float BM25Index::idf(const std::string& term) {
    auto it = term_to_docs_.find(term);
    if (it == term_to_docs_.end()) return 0.0f;
    float df = static_cast<float>(it->second.size());
    return std::log((static_cast<float>(total_docs_) - df + 0.5f) / (df + 0.5f) + 1.0f);
}

inline std::vector<BM25Index::ScoredDoc> BM25Index::search(
    const std::vector<std::string>& query_terms, size_t top_k) {
    std::vector<ScoredDoc> scored;
    for (auto& [doc_id, doc] : docs_) {
        float score = 0.0f;
        for (auto& qt : query_terms) {
            auto it = doc.term_freqs.find(qt);
            if (it == doc.term_freqs.end()) continue;
            int tf = it->second;
            float idf_score = idf(qt);
            float denom = tf + k1_ * (1.0f - b_ + b_ * static_cast<float>(doc.doc_len) / avg_doc_len_);
            score += idf_score * (tf * (k1_ + 1.0f)) / denom;
        }
        if (score > 0.0f) scored.push_back({doc_id, score});
    }
    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.score > b.score; });
    if (scored.size() > top_k) scored.resize(top_k);
    return scored;
}

inline void BM25Index::clear() {
    docs_.clear(); term_to_docs_.clear();
    avg_doc_len_ = 0; total_docs_ = 0; total_terms_ = 0;
}

// --- SmartChunker ---

inline std::vector<Chunk> SmartChunker::chunk(const std::string& text, const NodeID& source_id) {
    std::vector<Chunk> result;
    size_t pos = 0;
    size_t idx = 0;
    
    auto paragraphs = split_paragraphs(text);
    std::string current;
    
    for (size_t p = 0; p < paragraphs.size(); p++) {
        auto& para = paragraphs[p];
        if (para.empty()) continue;
        
        if (current.size() + para.size() <= config_.target_size) {
            if (!current.empty()) current += "\n\n";
            current += para;
        } else {
            if (!current.empty()) {
                result.push_back({});
                auto& c = result.back();
                c.id = "chunk_" + std::to_string(idx++);
                c.content = current;
                c.source_node_id = source_id;
                c.start_pos = pos;
                c.end_pos = pos + current.size();
                c.chunk_index = result.size() - 1;
                pos += current.size() + 2;
            }
            
            // Handle paragraph larger than target
            if (para.size() > config_.target_size) {
                auto sentences = split_sentences(para);
                current.clear();
                for (auto& s : sentences) {
                    if (current.size() + s.size() > config_.target_size) {
                        if (!current.empty()) {
                            result.push_back({});
                            auto& c = result.back();
                            c.id = "chunk_" + std::to_string(idx++);
                            c.content = current;
                            c.source_node_id = source_id;
                            c.chunk_index = result.size() - 1;
                        }
                        current = s;
                    } else {
                        if (!current.empty()) current += " ";
                        current += s;
                    }
                }
            } else {
                current = para;
            }
        }
    }
    
    if (!current.empty()) {
        result.push_back({});
        auto& c = result.back();
        c.id = "chunk_" + std::to_string(idx++);
        c.content = current;
        c.source_node_id = source_id;
        c.chunk_index = result.size() - 1;
    }
    
    return result;
}

inline std::vector<std::string> SmartChunker::split_sentences(const std::string& text) {
    std::vector<std::string> sents;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        cur += c;
        if ((c == '.' || c == '!' || c == '?') && (i + 1 >= text.size() || 
            std::isspace(static_cast<unsigned char>(text[i + 1])))) {
            if (!cur.empty()) { sents.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) sents.push_back(cur);
    return sents;
}

inline std::vector<std::string> SmartChunker::split_paragraphs(const std::string& text) {
    std::vector<std::string> paras;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty()) { paras.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) paras.push_back(cur);
    return paras;
}

// --- KnowledgeTree ---

inline KnowledgeTree::KnowledgeTree() {
    root_ = std::make_shared<KnowledgeNode>("root", "Root", "Root of knowledge tree");
    nodes_["root"] = root_;
}

inline NodeID KnowledgeTree::generate_id() { return "node_" + std::to_string(++node_counter_); }

inline NodeHandle KnowledgeTree::create_node(std::string_view title, std::string_view content, NodeHandle parent) {
    auto node = std::make_shared<KnowledgeNode>(generate_id(), title, content);
    if (parent) { node->parent = parent; parent->children.push_back(node); }
    nodes_[node->id] = node;
    return node;
}

inline bool KnowledgeTree::delete_node(NodeHandle node) {
    if (!node || node == root_) return false;
    for (auto& child : node->children) delete_node(child);
    if (auto p = node->parent.lock()) {
        p->children.erase(std::remove(p->children.begin(), p->children.end(), node), p->children.end());
    }
    nodes_.erase(node->id);
    return true;
}

inline NodeHandle KnowledgeTree::get_node(const NodeID& id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? it->second : nullptr;
}

inline NodeHandle KnowledgeTree::get_root() const { return root_; }

inline bool KnowledgeTree::move_node(NodeHandle node, NodeHandle new_parent) {
    if (!node || !new_parent || node == root_) return false;
    if (auto p = node->parent.lock()) {
        p->children.erase(std::remove(p->children.begin(), p->children.end(), node), p->children.end());
    }
    node->parent = new_parent;
    new_parent->children.push_back(node);
    return true;
}

inline std::vector<NodeHandle> KnowledgeTree::get_children(NodeHandle node) const {
    return node ? node->children : std::vector<NodeHandle>{};
}

inline std::vector<NodeHandle> KnowledgeTree::get_ancestors(NodeHandle node) const {
    std::vector<NodeHandle> result;
    while (auto p = node->parent.lock()) { result.push_back(p); node = p; }
    std::reverse(result.begin(), result.end());
    return result;
}

inline void KnowledgeTree::collect_descendants(NodeHandle node, std::vector<NodeHandle>& result) const {
    for (auto& c : node->children) { result.push_back(c); collect_descendants(c, result); }
}

inline std::vector<NodeHandle> KnowledgeTree::get_descendants(NodeHandle node) const {
    std::vector<NodeHandle> result;
    if (node) collect_descendants(node, result);
    return result;
}

inline std::string_view KnowledgeTree::get_content(NodeHandle node) const {
    return node ? std::string_view(node->content) : std::string_view{};
}

inline bool KnowledgeTree::set_content(NodeHandle node, std::string_view content) {
    if (!node) return false;
    node->content = content;
    node->updated_at = std::chrono::system_clock::now();
    return true;
}

inline void KnowledgeTree::traverse_bfs(std::function<void(NodeHandle)> callback) const {
    if (!callback) return;
    std::queue<NodeHandle> q;
    q.push(root_);
    while (!q.empty()) { auto n = q.front(); q.pop(); callback(n); for (auto& c : n->children) q.push(c); }
}

inline size_t KnowledgeTree::node_count() const { return nodes_.size(); }

inline void KnowledgeTree::clear() {
    nodes_.clear(); node_counter_ = 0;
    root_ = std::make_shared<KnowledgeNode>("root", "Root", "Root of knowledge tree");
    nodes_["root"] = root_;
}

// --- KnowledgeGraph ---

inline EntityID KnowledgeGraph::generate_entity_id() {
    static size_t counter = 0;
    return "entity_" + std::to_string(++counter);
}

inline EntityID KnowledgeGraph::add_entity(const Entity& entity) {
    Entity e = entity;
    if (e.id.empty()) e.id = generate_entity_id();
    entities_[e.id] = e;
    return e.id;
}

inline std::optional<Entity> KnowledgeGraph::get_entity(const EntityID& id) const {
    auto it = entities_.find(id);
    return it != entities_.end() ? std::optional(it->second) : std::nullopt;
}

inline std::vector<Entity> KnowledgeGraph::find_entities(std::string_view query) {
    // Simple text match - in production, use embedding similarity
    std::vector<Entity> result;
    std::string q(query);
    for (auto& [id, e] : entities_) {
        if (e.name.find(q) != std::string::npos || e.description.find(q) != std::string::npos) {
            result.push_back(e);
        }
    }
    return result;
}

inline std::vector<Entity> KnowledgeGraph::get_entities_by_type(const std::string& type) {
    std::vector<Entity> result;
    for (auto& [id, e] : entities_) {
        if (e.type == type) result.push_back(e);
    }
    return result;
}

inline std::string KnowledgeGraph::add_relationship(const Relationship& rel) {
    std::string rid = rel.id.empty() ? "rel_" + std::to_string(relationships_.size() + 1) : rel.id;
    Relationship r = rel;
    r.id = rid;
    relationships_[rid] = r;
    return rid;
}

inline std::vector<Relationship> KnowledgeGraph::get_relationships(const EntityID& entity_id) {
    std::vector<Relationship> result;
    for (auto& [id, r] : relationships_) {
        if (r.source_id == entity_id || r.target_id == entity_id) result.push_back(r);
    }
    return result;
}

inline std::vector<Relationship> KnowledgeGraph::get_all_relationships() const {
    std::vector<Relationship> result;
    for (auto& [id, r] : relationships_) result.push_back(r);
    return result;
}

inline std::string KnowledgeGraph::add_community(const Community& community) {
    std::string cid = community.id.empty() ? "comm_" + std::to_string(communities_.size() + 1) : community.id;
    Community c = community;
    c.id = cid;
    communities_[cid] = c;
    for (auto& eid : c.entity_ids) entity_to_communities_[eid].push_back(cid);
    return cid;
}

inline std::optional<Community> KnowledgeGraph::get_community(const std::string& id) const {
    auto it = communities_.find(id);
    return it != communities_.end() ? std::optional(it->second) : std::nullopt;
}

inline std::vector<Community> KnowledgeGraph::get_communities_at_level(size_t level) const {
    std::vector<Community> result;
    for (auto& [id, c] : communities_) {
        if (c.level == level) result.push_back(c);
    }
    return result;
}

inline std::vector<Entity> KnowledgeGraph::expand_from_entity(const EntityID& seed, size_t max_hops) {
    std::vector<Entity> result;
    std::unordered_set<EntityID> visited;
    std::queue<std::pair<EntityID, size_t>> queue;
    
    if (auto e = get_entity(seed)) {
        result.push_back(*e);
        visited.insert(seed);
        queue.push({seed, 0});
    }
    
    while (!queue.empty()) {
        auto [current, depth] = queue.front();
        queue.pop();
        if (depth >= max_hops) continue;
        
        for (auto& rel : get_relationships(current)) {
            if (rel.source_id == current && !visited.count(rel.target_id)) {
                if (auto e = get_entity(rel.target_id)) {
                    result.push_back(*e);
                    visited.insert(rel.target_id);
                    queue.push({rel.target_id, depth + 1});
                }
            }
            if (rel.target_id == current && !visited.count(rel.source_id)) {
                if (auto e = get_entity(rel.source_id)) {
                    result.push_back(*e);
                    visited.insert(rel.source_id);
                    queue.push({rel.source_id, depth + 1});
                }
            }
        }
    }
    
    return result;
}

inline KnowledgeGraph::Stats KnowledgeGraph::get_stats() const {
    return {entities_.size(), relationships_.size(), communities_.size()};
}

inline void KnowledgeGraph::clear() {
    entities_.clear();
    relationships_.clear();
    communities_.clear();
    entity_to_communities_.clear();
}

// --- HybridSearchEngine ---

inline std::vector<std::string> HybridSearchEngine::parse_query(std::string_view query) {
    std::vector<std::string> terms;
    std::string cur;
    for (char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += std::tolower(static_cast<unsigned char>(c));
        } else if (!cur.empty()) {
            terms.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) terms.push_back(cur);
    return terms;
}

inline std::vector<VectorStore::SearchResult> HybridSearchEngine::semantic_search(
    std::string_view query, size_t top_k) {
    if (!embedding_model_ || !vector_store_) return {};
    auto qemb = embedding_model_->embed(query);
    return vector_store_->search(qemb.vector, top_k);
}

inline std::vector<BM25Index::ScoredDoc> HybridSearchEngine::keyword_search(
    const std::vector<std::string>& terms, size_t top_k) {
    if (!bm25_index_) return {};
    return bm25_index_->search(terms, top_k);
}

inline std::vector<QueryResultItem> HybridSearchEngine::fuse_results(
    const std::vector<VectorStore::SearchResult>& semantic,
    const std::vector<BM25Index::ScoredDoc>& keyword,
    size_t top_k) {
    
    const float k = config_.rrf_k;
    std::unordered_map<std::string, QueryResultItem> fused;
    
    // Add semantic results
    for (size_t i = 0; i < semantic.size(); ++i) {
        auto& r = semantic[i];
        QueryResultItem item;
        item.chunk_id = r.id;
        item.vector_score = r.similarity;
        item.semantic_weight = config_.semantic_weight;
        fused[r.id] = item;
    }
    
    // Add keyword scores
    for (size_t i = 0; i < keyword.size(); ++i) {
        auto& r = keyword[i];
        auto& item = fused[r.doc_id];
        item.chunk_id = r.doc_id;
        item.bm25_score = r.score;
    }
    
    // Calculate RRF scores
    std::vector<std::pair<std::string, float>> scores;
    for (auto& [id, item] : fused) {
        float semantic_rrf = 0.0f;
        float keyword_rrf = 0.0f;
        
        for (size_t i = 0; i < semantic.size(); ++i) {
            if (semantic[i].id == id) {
                semantic_rrf = 1.0f / (k + i + 1.0f);
                break;
            }
        }
        for (size_t i = 0; i < keyword.size(); ++i) {
            if (keyword[i].doc_id == id) {
                keyword_rrf = 1.0f / (k + i + 1.0f);
                break;
            }
        }
        
        item.score = config_.semantic_weight * semantic_rrf + config_.keyword_weight * keyword_rrf;
        scores.push_back({id, item.score});
    }
    
    // Sort by fused score
    std::sort(scores.begin(), scores.end(), [](auto& a, auto& b) { return a.second > b.second; });
    
    std::vector<QueryResultItem> result;
    for (size_t i = 0; i < std::min(scores.size(), top_k); ++i) {
        auto item = fused[scores[i].first];
        item.result_type = "hybrid";
        result.push_back(item);
    }
    
    return result;
}

inline QueryResults HybridSearchEngine::search(std::string_view query, const QueryOptions& options) {
    QueryResults results;
    results.queries_used.push_back(std::string(query));
    
    size_t initial_k = options.initial_top_k > 0 ? options.initial_top_k : config_.initial_top_k;
    
    std::vector<VectorStore::SearchResult> semantic;
    std::vector<BM25Index::ScoredDoc> keyword;
    
    if (options.use_semantic) {
        semantic = semantic_search(query, initial_k);
    }
    
    if (options.use_keyword) {
        auto terms = parse_query(query);
        keyword = keyword_search(terms, initial_k);
    }
    
    results.items = fuse_results(semantic, keyword, options.max_results);
    results.total_hits = results.items.size();
    
    return results;
}

// --- ApolloEngine ---

using ChunkHandle = std::shared_ptr<Chunk>;

inline ApolloEngine::ApolloEngine() 
    : embedding_model_(std::make_unique<EmbeddingModel>()),
      hybrid_search_(HybridSearchEngine::Config{0.5f, 0.5f, 0.0f, 60.0f, 50}) {
    hybrid_search_.set_vector_store(&vector_store_);
    hybrid_search_.set_bm25_index(&bm25_index_);
    hybrid_search_.set_embedding_model(embedding_model_.get());
    hybrid_search_.set_knowledge_graph(&knowledge_graph_);
}

inline ApolloEngine::~ApolloEngine() = default;

inline bool ApolloEngine::load_embedding_model(const std::string& path, bool use_gpu) {
    return embedding_model_->load(path, use_gpu);
}

inline ChunkID ApolloEngine::gen_chunk_id() { return "chunk_" + std::to_string(++chunk_counter_); }

inline std::vector<std::string> ApolloEngine::split_sentences(std::string_view text) const {
    std::vector<std::string> sents;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        cur += c;
        if ((c == '.' || c == '!' || c == '?') && (i + 1 >= text.size() || 
            std::isspace(static_cast<unsigned char>(text[i + 1])))) {
            if (!cur.empty()) { sents.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) sents.push_back(cur);
    return sents;
}

inline NodeHandle ApolloEngine::create_node(std::string_view title, std::string_view content) {
    return tree_.create_node(title, content, tree_.get_root());
}

inline NodeHandle ApolloEngine::create_node(std::string_view title, std::string_view content, NodeHandle parent) {
    return tree_.create_node(title, content, parent ? parent : tree_.get_root());
}

inline bool ApolloEngine::delete_node(NodeHandle node, bool delete_children) {
    if (!node) return false;
    if (delete_children) {
        for (auto& d : tree_.get_descendants(node)) {
            for (auto& cid : node_to_chunks_[d->id]) {
                vector_store_.remove_vector(cid);
                bm25_index_.clear();  // Would need better management
                chunks_.erase(cid);
            }
        }
    }
    return tree_.delete_node(node);
}

inline NodeHandle ApolloEngine::get_node(const NodeID& id) const { return tree_.get_node(id); }
inline NodeHandle ApolloEngine::get_root() const { return tree_.get_root(); }
inline std::vector<NodeHandle> ApolloEngine::get_children(NodeHandle node) const { return tree_.get_children(node); }
inline std::vector<NodeHandle> ApolloEngine::get_ancestors(NodeHandle node) const { return tree_.get_ancestors(node); }
inline std::vector<NodeHandle> ApolloEngine::get_descendants(NodeHandle node) const { return tree_.get_descendants(node); }
inline std::string_view ApolloEngine::get_content(NodeHandle node) const { return tree_.get_content(node); }

inline bool ApolloEngine::set_content(NodeHandle node, std::string_view content) {
    return tree_.set_content(node, content);
}

inline ChunkHandle ApolloEngine::add_chunk(std::string_view content, NodeHandle parent_node) {
    NodeID pid = parent_node ? parent_node->id : "";
    auto chunk = std::make_shared<Chunk>();
    chunk->id = gen_chunk_id();
    chunk->content = content;
    chunk->source_node_id = pid;
    
    // Get real embedding
    if (is_model_loaded()) {
        auto emb = embedding_model_->embed(std::string(content));
        chunk->embedding = emb.vector;
        vector_store_.add_vector(chunk->id, emb.vector);
    }
    
    bm25_index_.add_document(chunk->id, std::string(content));
    chunks_[chunk->id] = chunk;
    if (!pid.empty()) node_to_chunks_[pid].push_back(chunk->id);
    
    return chunk;
}

inline std::vector<ChunkHandle> ApolloEngine::add_document(std::string_view text, NodeHandle parent_node, 
                                                          const ChunkConfig& config) {
    NodeID pid = parent_node ? parent_node->id : "";
    SmartChunker chunker(config);
    auto new_chunks = chunker.chunk(std::string(text), pid);
    
    std::vector<ChunkHandle> result;
    for (auto& c : new_chunks) {
        c.id = gen_chunk_id();
        c.chunk_index = result.size();
        
        if (is_model_loaded()) {
            auto emb = embedding_model_->embed(c.content);
            c.embedding = emb.vector;
            vector_store_.add_vector(c.id, emb.vector);
        }
        
        bm25_index_.add_document(c.id, c.content);
        auto handle = std::make_shared<Chunk>(std::move(c));
        chunks_[handle->id] = handle;
        if (!pid.empty()) node_to_chunks_[pid].push_back(handle->id);
        result.push_back(handle);
    }
    
    return result;
}

inline std::vector<ChunkHandle> ApolloEngine::search_chunks(std::string_view query, size_t top_k) {
    QueryResults results = hybrid_search_.search(query, QueryOptions{top_k, 0.0f, true, true, false});
    std::vector<ChunkHandle> found;
    for (auto& item : results.items) {
        auto it = chunks_.find(item.chunk_id);
        if (it != chunks_.end()) found.push_back(it->second);
    }
    return found;
}

inline QueryResults ApolloEngine::query(std::string_view query_text, const QueryOptions& options) {
    QueryResults results = hybrid_search_.search(query_text, options);
    
    for (auto& item : results.items) {
        if (auto it = chunks_.find(item.chunk_id); it != chunks_.end()) {
            item.content = it->second->content;
            item.node_id = it->second->source_node_id;
            if (auto node = tree_.get_node(item.node_id)) {
                item.title = node->title;
            }
        }
    }
    
    return results;
}

inline ApolloEngine::Stats ApolloEngine::get_stats() const {
    auto kg_stats = knowledge_graph_.get_stats();
    return {tree_.node_count(), chunks_.size(), kg_stats.entity_count, kg_stats.relationship_count};
}

inline void ApolloEngine::clear() {
    tree_.clear();
    chunks_.clear();
    node_to_chunks_.clear();
    vector_store_.clear();
    bm25_index_.clear();
    knowledge_graph_.clear();
    chunk_counter_ = 0;
}

} // namespace apollocore_v2
