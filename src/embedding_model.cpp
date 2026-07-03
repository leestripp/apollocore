/**
 * ApolloCore - Embedding Model using llama.cpp
 * 
 * Uses llama.cpp for running embedding models with Vulkan GPU acceleration.
 */

#include "apollocore/apollocore.hpp"
#include "llama.h"

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>

namespace apollocore {

// ============================================================================
// EmbeddingModel - llama.cpp backed embedding
// ============================================================================

class EmbeddingModel {
public:
    struct Impl {
        llama_model* model = nullptr;
        llama_context* ctx = nullptr;
        int n_tokens = 0;
        int n_ctx = 2048;
        int n_threads = 4;
        int n_gpu_layers = 32;  // GPU layers for Vulkan
        bool has_encoder = false;  // True for BERT-style models
        
        ~Impl() {
            if (ctx) llama_free(ctx);
            if (model) llama_model_free(model);
        }
    };
    
    std::unique_ptr<Impl> pimpl;
    size_t dimensions_{384};
    std::string model_path_;
    bool use_gpu_ = true;
    
public:
    EmbeddingModel();
    explicit EmbeddingModel(const std::string& model_path, bool use_gpu = true);
    ~EmbeddingModel();
    
    // Load a GGUF model file
    bool load(const std::string& path, bool use_gpu = true);
    bool is_loaded() const { return pimpl && pimpl->model != nullptr; }
    size_t dimensions() const { return dimensions_; }
    std::string model_name() const;
    
    // Embed a single text
    EmbeddingResult embed(std::string_view text);
    
    // Embed multiple texts (batch processing)
    std::vector<EmbeddingResult> embed_batch(const std::vector<std::string>& texts);
    
    // Get last token count
    int last_token_count() const { return pimpl ? pimpl->n_tokens : 0; }
    
private:
    // Internal: run inference and extract embedding
    EmbeddingVector run_inference(const std::vector<llama_token>& tokens);
    
    // Tokenize text
    std::vector<llama_token> tokenize(std::string_view text);
    
    // Normalize embedding vector
    void normalize(EmbeddingVector& vec);
};

// ============================================================================
// Implementation
// ============================================================================

EmbeddingModel::EmbeddingModel() : pimpl(std::make_unique<Impl>()) {}

EmbeddingModel::EmbeddingModel(const std::string& model_path, bool use_gpu) 
    : pimpl(std::make_unique<Impl>()) {
    load(model_path, use_gpu);
}

EmbeddingModel::~EmbeddingModel() = default;

bool EmbeddingModel::load(const std::string& path, bool use_gpu) {
    model_path_ = path;
    use_gpu_ = use_gpu;
    
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = use_gpu ? 32 : 0;
    model_params.vocab_only = false;  // We need the model weights for embeddings
    
    pimpl->model = llama_model_load_from_file(path.c_str(), model_params);
    if (!pimpl->model) {
        fprintf(stderr, "Failed to load model from: %s\n", path.c_str());
        return false;
    }
    
    // Check if this is an encoder model (like BERT for embeddings)
    pimpl->has_encoder = llama_model_has_encoder(pimpl->model);
    
    // Get model dimensions
    dimensions_ = llama_model_n_embd(pimpl->model);
    if (dimensions_ == 0) {
        fprintf(stderr, "Model has no embedding dimension!\n");
        return false;
    }
    
    // Create inference context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = pimpl->n_ctx;
    ctx_params.n_threads = pimpl->n_threads;
    ctx_params.n_threads_batch = pimpl->n_threads;
    
    pimpl->ctx = llama_init_from_model(pimpl->model, ctx_params);
    if (!pimpl->ctx) {
        fprintf(stderr, "Failed to create context\n");
        return false;
    }
    
    // Enable embeddings extraction
    llama_set_embeddings(pimpl->ctx, true);
    
    // Phase C: stderr-only for stdio transport compatibility.
    fprintf(stderr, "[apollo_mcp_server] Loaded embedding model: %s\n", path.c_str());
    fprintf(stderr, "  Dimensions: %zu\n", dimensions_);
    fprintf(stderr, "  GPU enabled: %s\n", use_gpu ? "yes" : "no");
    fprintf(stderr, "  Has encoder: %s\n", pimpl->has_encoder ? "yes" : "no");
    
    return true;
}

std::string EmbeddingModel::model_name() const {
    if (!pimpl || !pimpl->model) return "none";
    // Return model path as name since llama_model_name doesn't exist
    return model_path_;
}

std::vector<llama_token> EmbeddingModel::tokenize(std::string_view text) {
    const auto vocab = llama_model_get_vocab(pimpl->model);
    std::vector<llama_token> tokens(512);
    int n_tokens = llama_tokenize(vocab, text.data(), (int)text.size(), 
                                  tokens.data(), (int)tokens.size(), true, true);
    if (n_tokens < 0) {
        // Need more space
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, text.data(), (int)text.size(),
                                  tokens.data(), (int)tokens.size(), true, true);
    }
    tokens.resize(std::max(0, n_tokens));
    return tokens;
}

EmbeddingVector EmbeddingModel::run_inference(const std::vector<llama_token>& tokens) {
    if (!pimpl || !pimpl->ctx || tokens.empty()) {
        return EmbeddingVector(dimensions_, 0.0f);
    }
    
    // For encoder models (BERT-style), use llama_encode
    // For decoder models (LLM-style), use llama_decode
    if (pimpl->has_encoder) {
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
        
        if (llama_encode(pimpl->ctx, batch)) {
            fprintf(stderr, "llama_encode failed\n");
            llama_batch_free(batch);
            return EmbeddingVector(dimensions_, 0.0f);
        }
        
        llama_batch_free(batch);
    } else {
        // Use llama_decode for decoder models
        auto batch = llama_batch_get_one(const_cast<llama_token*>(tokens.data()), (int)tokens.size());
        
        if (llama_decode(pimpl->ctx, batch)) {
            fprintf(stderr, "llama_decode failed\n");
            return EmbeddingVector(dimensions_, 0.0f);
        }
    }
    
    pimpl->n_tokens = (int)tokens.size();
    
    // Get embeddings - for pooled models, use sequence embeddings
    // Check pooling type: 0=NONE, 1=MEAN, 2=CLS, 3=LAST
    auto pooling = llama_pooling_type(pimpl->ctx);
    
    if (pooling != 0) {  // Has pooling enabled
        float* pooled = llama_get_embeddings_seq(pimpl->ctx, 0);
        if (pooled) {
            EmbeddingVector result(pooled, pooled + dimensions_);
            normalize(result);
            return result;
        }
    }
    
    // Fallback to regular embeddings
    const float* embeddings = llama_get_embeddings(pimpl->ctx);
    
    if (!embeddings) {
        fprintf(stderr, "Failed to get embeddings\n");
        return EmbeddingVector(dimensions_, 0.0f);
    }
    
    EmbeddingVector result(embeddings, embeddings + dimensions_);
    normalize(result);
    return result;
}

void EmbeddingModel::normalize(EmbeddingVector& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-10f) {
        for (float& v : vec) v /= norm;
    }
}

EmbeddingResult EmbeddingModel::embed(std::string_view text) {
    EmbeddingVector vec;

    if (!is_loaded()) {
        // Fallback: return zero vector
        fprintf(stderr, "Warning: Model not loaded, returning zero embedding\n");
        vec = EmbeddingVector(dimensions_, 0.0f);
    } else {
        auto tokens = tokenize(text);
        vec = run_inference(tokens);
    }

    return {std::move(vec), model_name(), dimensions_};
}

std::vector<EmbeddingResult> EmbeddingModel::embed_batch(const std::vector<std::string>& texts) {
    std::vector<EmbeddingResult> results;
    results.reserve(texts.size());
    
    for (const auto& text : texts) {
        results.push_back(embed(text));
    }
    
    return results;
}

// ============================================================================
// BM25 Index - Keyword search
// ============================================================================

class BM25Index {
public:
    struct DocInfo {
        std::string content;
        std::vector<std::string> terms;
        std::unordered_map<std::string, int> term_freqs;
        size_t doc_len = 0;
    };
    
    struct ScoredDoc {
        std::string doc_id;
        float score;
    };
    
    void add_document(const std::string& doc_id, const std::string& content);
    std::vector<ScoredDoc> search(const std::vector<std::string>& query_terms, size_t top_k = 10);
    void clear();
    
private:
    std::unordered_map<std::string, DocInfo> docs_;
    std::unordered_map<std::string, std::vector<std::string>> term_to_docs_;
    float avg_doc_len_ = 0.0f;
    size_t total_docs_ = 0;
    size_t total_terms_ = 0;
    
    std::vector<std::string> tokenize_text(const std::string& text);
    float idf(const std::string& term);
};

std::vector<std::string> BM25Index::tokenize_text(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current += std::tolower(static_cast<unsigned char>(c));
        } else if (!current.empty()) {
            // Skip stopwords
            if (current.size() > 2 && 
                current != "the" && current != "and" && current != "for" &&
                current != "are" && current != "but" && current != "not" &&
                current != "you" && current != "all" && current != "can") {
                tokens.push_back(current);
            }
            current.clear();
        }
    }
    if (!current.empty() && current.size() > 2) {
        tokens.push_back(current);
    }
    
    return tokens;
}

void BM25Index::add_document(const std::string& doc_id, const std::string& content) {
    DocInfo doc;
    doc.content = content;
    doc.terms = tokenize_text(content);
    doc.doc_len = doc.terms.size();
    
    for (const auto& term : doc.terms) {
        doc.term_freqs[term]++;
    }
    
    docs_[doc_id] = std::move(doc);
    
    for (const auto& term : docs_[doc_id].terms) {
        term_to_docs_[term].push_back(doc_id);
    }
    
    total_docs_++;
    total_terms_ += docs_[doc_id].doc_len;
    avg_doc_len_ = static_cast<float>(total_terms_) / total_docs_;
}

float BM25Index::idf(const std::string& term) {
    auto it = term_to_docs_.find(term);
    if (it == term_to_docs_.end()) return 0.0f;
    
    float df = static_cast<float>(it->second.size());
    return std::log((static_cast<float>(total_docs_) - df + 0.5f) / (df + 0.5f) + 1.0f);
}

std::vector<BM25Index::ScoredDoc> BM25Index::search(
    const std::vector<std::string>& query_terms, size_t top_k) {
    
    const float k1 = 1.5f;
    const float b = 0.75f;
    
    std::vector<ScoredDoc> scored_docs;
    
    for (const auto& [doc_id, doc] : docs_) {
        float score = 0.0f;
        
        for (const auto& query_term : query_terms) {
            auto it = doc.term_freqs.find(query_term);
            if (it == doc.term_freqs.end()) continue;
            
            int tf = it->second;
            float idf_score = idf(query_term);
            
            float numerator = tf * (k1 + 1.0f);
            float denominator = tf + k1 * (1.0f - b + b * static_cast<float>(doc.doc_len) / avg_doc_len_);
            
            score += idf_score * numerator / denominator;
        }
        
        if (score > 0.0f) {
            scored_docs.push_back({doc_id, score});
        }
    }
    
    std::sort(scored_docs.begin(), scored_docs.end(),
              [](const ScoredDoc& a, const ScoredDoc& b) { return a.score > b.score; });
    
    if (scored_docs.size() > top_k) {
        scored_docs.resize(top_k);
    }
    
    return scored_docs;
}

void BM25Index::clear() {
    docs_.clear();
    term_to_docs_.clear();
    avg_doc_len_ = 0.0f;
    total_docs_ = 0;
    total_terms_ = 0;
}

// ============================================================================
// Hybrid Search - RRF Fusion
// ============================================================================

std::vector<QueryResultItem> hybrid_search(
    const std::vector<VectorStore::SearchResult>& semantic_results,
    const std::vector<BM25Index::ScoredDoc>& keyword_results,
    float semantic_weight = 0.7f,
    size_t top_k = 10) {
    
    // Reciprocal Rank Fusion (RRF)
    const float k = 60.0f;  // RRF constant
    
    std::unordered_map<std::string, float> fused_scores;
    
    // Add semantic scores
    for (size_t i = 0; i < semantic_results.size(); ++i) {
        const auto& r = semantic_results[i];
        float rrf_score = 1.0f / (k + i + 1.0f);
        fused_scores[r.id] += semantic_weight * rrf_score;
    }
    
    // Add keyword scores
    for (size_t i = 0; i < keyword_results.size(); ++i) {
        const auto& r = keyword_results[i];
        float rrf_score = 1.0f / (k + i + 1.0f);
        float keyword_weight = 1.0f - semantic_weight;
        fused_scores[r.doc_id] += keyword_weight * rrf_score;
    }
    
    // Sort by fused score
    std::vector<std::pair<std::string, float>> sorted;
    for (const auto& [id, score] : fused_scores) {
        sorted.push_back({id, score});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Convert to QueryResults
    std::vector<QueryResultItem> results;
    for (size_t i = 0; i < std::min(sorted.size(), top_k); ++i) {
        QueryResultItem item;
        item.node_id = sorted[i].first;
        item.score = sorted[i].second;
        item.result_type = "hybrid";
        results.push_back(item);
    }
    
    return results;
}

} // namespace apollocore
