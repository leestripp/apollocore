/**
 * ApolloCore - Persistence Integration Implementation
 */

#include "apollocore/apollo_persistence.hpp"
#include "apollocore/kb_persistence.hpp"
#include "apollocore/fbin.hpp"
#include "apollocore/persistence.hpp"
#include "llama.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

namespace apollocore_v2 {

// ============================================================================
// Simple Embedding Model (for persistence integration tests)
// ============================================================================

class Embedder {
public:
    Embedder() = default;
    
    bool load(const std::string& path) {
        llama_backend_init();
        
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;
        
        model_ = llama_model_load_from_file(path.c_str(), model_params);
        if (!model_) {
            std::cerr << "[Embedder] Failed to load model: " << path << "\n";
            return false;
        }
        
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;
        ctx_params.n_threads = 4;
        
        ctx_ = llama_init_from_model(model_, ctx_params);
        if (!ctx_) {
            llama_model_free(model_);
            model_ = nullptr;
            return false;
        }
        
        return true;
    }
    
    ~Embedder() {
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }
    
    bool is_loaded() const { return ctx_ != nullptr; }
    
    int dimensions() const {
        return model_ ? llama_model_n_embd(model_) : 0;
    }
    
    std::vector<float> embed(const std::string& text) {
        std::vector<float> result(dimensions(), 0.0f);
        if (!ctx_ || !model_) return result;
        
        const llama_vocab* vocab = llama_model_get_vocab(model_);
        
        std::vector<llama_token> tokens(2048);
        int n_tokens = llama_tokenize(
            vocab, text.c_str(), (int)text.size(),
            tokens.data(), (int)tokens.size(), true, true
        );
        if (n_tokens <= 0) return result;
        tokens.resize(n_tokens);
        
        for (int i = 0; i < n_tokens; i++) {
            llama_batch batch = llama_batch_get_one(&tokens[i], 1);
            if (llama_decode(ctx_, batch) != 0) {
                return result;
            }
        }
        
        const float* emb = llama_get_embeddings(ctx_);
        if (emb) {
            for (int i = 0; i < dimensions(); i++) {
                result[i] = emb[i];
            }
        }
        
        // Normalize
        float norm = 0.0f;
        for (float v : result) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (auto& v : result) v /= norm;
        }
        
        return result;
    }
    
private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
};

// Alias for the header's forward-declared class
// (header uses KnowledgeBasePersistence directly now)

// ============================================================================
// Constructor / Destructor
// ============================================================================

ApolloCoreWithPersistence::ApolloCoreWithPersistence() 
    : embedder_(std::make_unique<Embedder>())
    , kb_persistence_(std::make_unique<KnowledgeBasePersistence>()) {}

ApolloCoreWithPersistence::~ApolloCoreWithPersistence() {
    close();
}

// ============================================================================
// Model Management
// ============================================================================

bool ApolloCoreWithPersistence::load_embedding_model(
    const std::string& path, bool use_gpu) {
    return embedder_->load(path);
}

// ============================================================================
// Knowledge Base Management
// ============================================================================

bool ApolloCoreWithPersistence::kb_exists(const std::string& path) {
    return KnowledgeBasePersistence::exists(path);
}

bool ApolloCoreWithPersistence::create(
    const std::string& path, const std::string& name) {
    if (is_open_) close();
    
    if (!kb_persistence_->create(path, name)) {
        return false;
    }
    
    // Clear in-memory data
    documents_.clear();
    chunks_.clear();
    
    is_open_ = true;
    std::cout << "[ApolloCore] Created knowledge base: " << path << "\n";
    return true;
}

bool ApolloCoreWithPersistence::open(const std::string& path) {
    if (is_open_) close();
    
    if (!kb_persistence_->open(path)) {
        return false;
    }
    
    // Load all data into memory
    documents_.clear();
    chunks_.clear();
    
    auto loaded_chunks = kb_persistence_->load_all_chunks();
    std::cout << "[ApolloCore] Loaded " << loaded_chunks.size() << " chunks\n";
    
    // Group chunks by node (document)
    std::unordered_map<std::string, std::string> node_content;
    for (const auto& c : loaded_chunks) {
        node_content[c.node_id] += c.content + "\n";
        chunks_.push_back({c.id, c.embedding});
    }
    
    // Build documents from chunks
    for (const auto& [node_id, content] : node_content) {
        documents_.push_back({node_id, content});
    }
    
    std::cout << "[ApolloCore] Loaded " << documents_.size() << " documents\n";
    
    is_open_ = true;
    std::cout << "[ApolloCore] Opened knowledge base: " << path << "\n";
    return true;
}

bool ApolloCoreWithPersistence::save() {
    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open\n";
        return false;
    }
    return kb_persistence_->save();
}

void ApolloCoreWithPersistence::close() {
    if (is_open_) {
        kb_persistence_->close();
        is_open_ = false;
    }
}

void ApolloCoreWithPersistence::clear() {
    if (is_open_) {
        kb_persistence_->clear();
    }
    documents_.clear();
    chunks_.clear();
}

bool ApolloCoreWithPersistence::destroy() {
    bool was_open = is_open_;
    close();
    
    if (was_open) {
        return kb_persistence_->destroy();
    }
    return true;
}

std::string ApolloCoreWithPersistence::get_kb_path() const {
    return kb_persistence_->get_path();
}

std::string ApolloCoreWithPersistence::get_kb_name() const {
    return kb_persistence_->get_name();
}

// ============================================================================
// Data Operations
// ============================================================================

size_t ApolloCoreWithPersistence::add_document(
    const std::string& text, size_t chunk_size) {
    
    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open. Call create() or open() first.\n";
        return 0;
    }
    
    // Generate IDs
    std::string doc_id = "doc_" + std::to_string(documents_.size() + 1);
    std::string node_id = doc_id;
    
    // Save node
    kb_persistence_->save_node(
        node_id, doc_id, text.substr(0, 100), 
        "", "document", ""
    );
    
    // Simple chunking by sentences
    std::vector<std::string> chunk_texts;
    std::string current_chunk;
    
    std::istringstream iss(text);
    std::string sentence;
    while (std::getline(iss, sentence, '.')) {
        if (!sentence.empty()) sentence += ".";
        
        if (current_chunk.length() + sentence.length() > chunk_size) {
            if (!current_chunk.empty()) {
                chunk_texts.push_back(current_chunk);
            }
            current_chunk = sentence;
        } else {
            current_chunk += sentence;
        }
    }
    if (!current_chunk.empty()) {
        chunk_texts.push_back(current_chunk);
    }
    
    if (chunk_texts.empty()) {
        chunk_texts.push_back(text);
    }
    
    // Save chunks
    kb_persistence_->begin_save();
    
    size_t count = 0;
    for (size_t i = 0; i < chunk_texts.size(); i++) {
        std::string chunk_id = doc_id + "_chunk_" + std::to_string(i);
        
        // Embed
        std::vector<float> embedding;
        if (embedder_->is_loaded()) {
            embedding = embedder_->embed(chunk_texts[i]);
        } else {
            embedding.assign(768, 0.0f);
        }
        
        // Save to disk
        if (kb_persistence_->save_chunk(
            chunk_id, node_id, chunk_texts[i], i, embedding)) {
            
            // Add to in-memory store
            chunks_.push_back({chunk_id, embedding});
            count++;
        }
    }
    
    kb_persistence_->end_save();
    
    // Add document to in-memory
    documents_.push_back({doc_id, text});
    
    std::cout << "[ApolloCore] Added document '" << doc_id << "' with " 
              << count << " chunks\n";
    
    return count;
}

std::vector<std::pair<std::string, float>> 
ApolloCoreWithPersistence::query(const std::string& query_text, size_t top_k) {
    std::vector<std::pair<std::string, float>> results;
    
    if (chunks_.empty()) return results;
    
    // Embed query
    std::vector<float> query_emb;
    if (embedder_->is_loaded()) {
        query_emb = embedder_->embed(query_text);
    } else {
        return results;
    }
    
    // Get chunk content from DB for display
    auto all_chunks = kb_persistence_->load_all_chunks();
    
    // Cosine similarity with all chunks
    struct ScoredChunk {
        std::string content;
        float score;
    };
    std::vector<ScoredChunk> scored;
    
    for (size_t i = 0; i < chunks_.size(); i++) {
        const auto& emb = chunks_[i].second;
        if (emb.size() != query_emb.size()) continue;
        
        float dot = 0.0f;
        float norm_q = 0.0f;
        float norm_c = 0.0f;
        
        for (size_t j = 0; j < query_emb.size(); j++) {
            dot += query_emb[j] * emb[j];
            norm_q += query_emb[j] * query_emb[j];
            norm_c += emb[j] * emb[j];
        }
        
        float denom = std::sqrt(norm_q) * std::sqrt(norm_c);
        float sim = denom > 0.0f ? dot / denom : 0.0f;
        
        // Get content from DB
        std::string content = (i < all_chunks.size()) ? all_chunks[i].content : "";
        
        ScoredChunk sc;
        sc.content = content;
        sc.score = sim;
        scored.push_back(sc);
    }
    
    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
        [](const ScoredChunk& a, const ScoredChunk& b) {
            return a.score > b.score;
        });
    
    // Return top_k
    size_t n = std::min(top_k, scored.size());
    for (size_t i = 0; i < n; i++) {
        results.push_back({scored[i].content, scored[i].score});
    }
    
    return results;
}

// ============================================================================
// Statistics
// ============================================================================

ApolloCoreWithPersistence::Stats ApolloCoreWithPersistence::get_stats() {
    Stats stats;
    
    stats.document_count = documents_.size();
    stats.chunk_count = chunks_.size();
    
    if (is_open_) {
        auto disk_stats = kb_persistence_->get_disk_stats();
        stats.entity_count = disk_stats.total_entities;
        stats.relationship_count = disk_stats.total_relationships;
        stats.embedding_files_size = disk_stats.embedding_files_size;
    }
    
    return stats;
}

} // namespace apollocore_v2
