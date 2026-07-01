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

        // CRITICAL: Enable embeddings extraction
        llama_set_embeddings(ctx_, true);

        // Cache encoder flag and pooling type for embed()
        has_encoder_ = llama_model_has_encoder(model_);

        std::cerr << "[Embedder] Loaded: " << path
                  << "  n_ctx=" << n_ctx() << "  n_embd=" << dimensions() << "\n";
        return true;
    }

    ~Embedder() {
        if (ctx_) llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    bool is_loaded() const { return ctx_ != nullptr; }

    int n_ctx() const { return 2048; }   // mirrors ctx_params.n_ctx above

    int dimensions() const {
        return model_ ? llama_model_n_embd(model_) : 0;
    }

    std::vector<float> embed(const std::string& text) {
        std::vector<float> result(dimensions(), 0.0f);
        if (!ctx_ || !model_) return result;

        const llama_vocab* vocab = llama_model_get_vocab(model_);

        // Reserve a large initial buffer; resize up if tokenize needs more.
        std::vector<llama_token> tokens(4096);
        int n_tokens = llama_tokenize(
            vocab, text.c_str(), (int)text.size(),
            tokens.data(), (int)tokens.size(), true, true
        );
        if (n_tokens < 0) {
            // Need more space. Resize and retry once.
            tokens.resize(-n_tokens);
            n_tokens = llama_tokenize(
                vocab, text.c_str(), (int)text.size(),
                tokens.data(), (int)tokens.size(), true, true
            );
        }
        if (n_tokens <= 0) {
            // Tokenization failed even after resize — return zero vector
            // rather than crashing.
            std::cerr << "[Embedder] tokenize failed (rc=" << n_tokens << ")\n";
            return result;
        }

        // === Defensive cap ===
        // llama_context::encode asserts if batch.n_tokens > n_ctx. Leave
        // a small safety margin for special tokens (BOS / EOS / pad).
        const int safety = 16;
        const int max_tokens = n_ctx() - safety;
        if (n_tokens > max_tokens) {
            std::cerr << "[Embedder] WARNING: text produced " << n_tokens
                      << " tokens, truncating to " << max_tokens
                      << " (n_ctx=" << n_ctx() << "). "
                      << "Pre-chunk the input so chunks fit n_ctx.\n";
            n_tokens = max_tokens;
        }
        tokens.resize(n_tokens);

        // Use proper batch setup with logits enabled for all tokens
        llama_batch batch = llama_batch_init((int32_t)n_tokens, 0, 1);
        for (int i = 0; i < n_tokens; i++) {
            batch.token[i] = tokens[i];
            batch.pos[i] = i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = 1;  // Required to get embeddings
        }
        batch.n_tokens = n_tokens;

        int rc;
        // Use cached encoder flag. Also treat pooled models as encoders.
        bool is_encoder = has_encoder_ ||
                         (llama_pooling_type(ctx_) != LLAMA_POOLING_TYPE_NONE);
        if (is_encoder) {
            // Encoder model (nomic-bert) - use llama_encode
            rc = llama_encode(ctx_, batch);
        } else {
            // Decoder model - use llama_decode
            rc = llama_decode(ctx_, batch);
        }
        llama_batch_free(batch);
        
        if (rc != 0) {
            std::cerr << "[Embedder] inference failed (rc=" << rc << ")\n";
            return result;
        }
        
        // For pooled models (nomic-bert), use embeddings_seq
        // For non-pooled, use embeddings
        const float* emb = nullptr;
        auto pool = llama_pooling_type(ctx_);
        if (pool != LLAMA_POOLING_TYPE_NONE) {
            emb = llama_get_embeddings_seq(ctx_, 0);
        } else {
            emb = llama_get_embeddings(ctx_);
        }
        
        if (emb) {
            for (int i = 0; i < dimensions(); i++) {
                result[i] = emb[i];
            }
        }
        
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
    bool has_encoder_ = false;
};

// Alias for the header's forward-declared class
// (header uses KnowledgeBasePersistence directly now)

// ============================================================================
// Constructor / Destructor
// ============================================================================

ApolloCore::ApolloCore() 
    : embedder_(std::make_unique<Embedder>())
    , entity_extractor_(nullptr)  // Created on demand when LLM is loaded
    , kb_persistence_(std::make_unique<KnowledgeBasePersistence>()) {}

ApolloCore::~ApolloCore() {
    close();
}

// ============================================================================
// Model Management
// ============================================================================

bool ApolloCore::load_embedding_model(
    const std::string& path, bool /*use_gpu*/) {
    return embedder_->load(path);
}

bool ApolloCore::load_llm_model(
    const std::string& path, bool /*use_gpu*/) {
    // Build extraction config using the loaded model
    ExtractionConfig config = llama_extraction_config(path);
    
    try {
        entity_extractor_ = std::make_unique<EntityExtractor>(config);
        llm_model_path_ = path;
        
        if (entity_extractor_->is_initialized()) {
            std::cout << "[ApolloCore] LLM model loaded for entity extraction: " 
                      << path << "\n";
            return true;
        } else {
            std::cerr << "[ApolloCore] LLM model loaded but extractor not initialized: "
                      << path << "\n";
            entity_extractor_.reset();
            llm_model_path_.clear();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ApolloCore] Failed to load LLM model: " << e.what() << "\n";
        entity_extractor_.reset();
        llm_model_path_.clear();
        return false;
    }
}

bool ApolloCore::is_llm_loaded() const {
    return entity_extractor_ && entity_extractor_->is_initialized();
}

// ============================================================================
// Knowledge Base Management
// ============================================================================

bool ApolloCore::kb_exists(const std::string& path) {
    return KnowledgeBasePersistence::exists(path);
}

bool ApolloCore::create(
    const std::string& path, const std::string& name) {
    if (is_open_) close();

    if (!kb_persistence_->create(path, name)) {
        return false;
    }

    // Clear in-memory data
    documents_.clear();
    chunks_.clear();

    // M3.C: Register "default" project for new KB
    // (User can later switch to a different project via set_current_project)
    current_project_ = "default";
    shared_read_sources_.clear();
    register_project("default", "Default project (auto-created)", false, "");

    is_open_ = true;
    std::cout << "[ApolloCore] Created knowledge base: " << path << "\n";
    return true;
}

bool ApolloCore::open(const std::string& path) {
    if (is_open_) close();

    if (!kb_persistence_->open(path)) {
        return false;
    }

    // Load all data into memory
    documents_.clear();
    chunks_.clear();

    // M3.C: Default to "default" project on open
    // (User can call set_current_project() to switch)
    current_project_ = "default";
    shared_read_sources_.clear();

    // Ensure "default" project exists (for backwards-compat with
    // pre-M3.C databases that may not have it registered)
    {
        auto info = get_project_info("default");
        if (info.is_null()) {
            register_project("default", "Default project", false, "system");
        }
    }

    auto loaded_chunks = kb_persistence_->load_all_chunks();
    std::cout << "[ApolloCore] Loaded " << loaded_chunks.size() << " chunks\n";
    
    // Group chunks by node (document)
    std::unordered_map<std::string, std::string> node_content;
    for (const auto& c : loaded_chunks) {
        node_content[c.node_id] += c.content + "\n";
        chunks_.push_back({c.id, c.embedding, c.project_id});
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

bool ApolloCore::save() {
    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open\n";
        return false;
    }
    return kb_persistence_->save();
}

void ApolloCore::close() {
    if (is_open_) {
        kb_persistence_->close();
        is_open_ = false;
    }
}

void ApolloCore::clear() {
    if (is_open_) {
        kb_persistence_->clear();
    }
    documents_.clear();
    chunks_.clear();
}

bool ApolloCore::destroy() {
    bool was_open = is_open_;
    close();
    
    if (was_open) {
        return kb_persistence_->destroy();
    }
    return true;
}

std::string ApolloCore::get_kb_path() const {
    return kb_persistence_->get_path();
}

std::string ApolloCore::get_kb_name() const {
    return kb_persistence_->get_name();
}

// ============================================================================
// Data Operations
// ============================================================================

size_t ApolloCore::add_document(
    const std::string& text, size_t chunk_size) {

    // Default: generate doc_N ID
    return add_document(text, "", chunk_size);
}

size_t ApolloCore::add_document(
    const std::string& text, const std::string& custom_doc_id, size_t chunk_size) {

    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open. Call create() or open() first.\n";
        return 0;
    }

    // Use custom doc_id if provided, otherwise generate
    std::string doc_id;
    if (!custom_doc_id.empty()) {
        doc_id = custom_doc_id;
    } else {
        doc_id = "doc_" + std::to_string(documents_.size() + 1);
    }
    std::string node_id = doc_id;
    
    // Save node
    kb_persistence_->save_node(
        node_id, doc_id, text.substr(0, 100),
        "", "document", "", current_project_
    );
    
    // Simple chunking by sentences
    std::vector<std::string> chunk_texts;
    std::string current_chunk;

    std::istringstream iss(text);
    std::string sentence;
    while (std::getline(iss, sentence, '.')) {
        if (!sentence.empty()) sentence += ".";

        // Sub-split sentences longer than chunk_size. Without this,
        // a file containing one huge "sentence" (e.g. a minified JS
        // blob or a long base64-encoded blob) bypasses the chunker
        // entirely and ships to the embedder as a single chunk that
        // can exceed n_ctx — crashing the process.
        while (sentence.size() > chunk_size) {
            std::string piece = sentence.substr(0, chunk_size);
            sentence = sentence.substr(chunk_size);
            if (!current_chunk.empty()) {
                chunk_texts.push_back(current_chunk);
                current_chunk.clear();
            }
            chunk_texts.push_back(std::move(piece));
        }

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
            chunk_id, node_id, chunk_texts[i], i, embedding, current_project_)) {

            // Add to in-memory store
            chunks_.push_back({chunk_id, embedding, current_project_});
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

bool ApolloCore::delete_document(const std::string& document_id) {
    if (!is_open_) {
        std::cerr << "[ApolloCore] delete_document: KB not open\n";
        return false;
    }
    if (!kb_persistence_) {
        std::cerr << "[ApolloCore] delete_document: kb_persistence_ null\n";
        return false;
    }

    // Delegate to persistence layer (handles all cascading deletes)
    bool ok = kb_persistence_->delete_document(document_id);
    if (!ok) return false;

    // Also remove from in-memory caches
    documents_.erase(
        std::remove_if(documents_.begin(), documents_.end(),
            [&](const std::pair<std::string, std::string>& p) {
                return p.first == document_id;
            }),
        documents_.end()
    );

    std::cout << "[ApolloCore] Deleted document '" << document_id << "'\n";
    return true;
}

std::string ApolloCore::add_entity(
    const std::string& name,
    const std::string& type_str,
    const std::string& chunk_id,
    const std::string& description,
    float confidence)
{
    if (!is_open_ || !kb_persistence_) return "";
    if (name.empty() || type_str.empty()) return "";

    // Generate a stable ID based on name + chunk
    std::string eid;
    if (!chunk_id.empty()) {
        eid = chunk_id + "_entity_" + name;
        // Replace slashes (if any) to keep ID safe
        for (auto& c : eid) if (c == '/' || c == '\\') c = '_';
    } else {
        eid = "entity_" + name;
    }

    bool ok = kb_persistence_->save_entity(
        eid, chunk_id, name, type_str, description, confidence,
        current_project_);
    if (!ok) return "";

    return eid;
}

std::string ApolloCore::add_relationship(
    const std::string& source,
    const std::string& target,
    const std::string& type_str,
    const std::string& description,
    float confidence)
{
    if (!is_open_ || !kb_persistence_) return "";
    if (source.empty() || target.empty() || type_str.empty()) return "";

    // Look up the actual entity IDs from names (entities may have been
    // added with auto-generated IDs like "<chunk>_entity_<name>").
    auto entities = kb_persistence_->load_all_entities();
    std::string source_id, target_id;
    for (const auto& e : entities) {
        if (e.name == source) source_id = e.id;
        if (e.name == target) target_id = e.id;
    }

    // Fallback: use names directly (in case entities haven't been added yet)
    if (source_id.empty()) source_id = source;
    if (target_id.empty()) target_id = target;

    // Generate a stable ID for the relationship
    std::string rid = source_id + "_" + type_str + "_" + target_id;
    for (auto& c : rid) if (c == '/' || c == '\\' || c == ' ') c = '_';

    bool ok = kb_persistence_->save_relationship(
        rid, source_id, target_id, type_str, description, confidence,
        current_project_);
    if (!ok) return "";

    return rid;
}

std::vector<std::pair<std::string, float>> 
ApolloCore::query(const std::string& query_text, size_t top_k) {
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
    
    // M3.C: Pre-compute visible projects for filtering
    auto visible = visible_projects();
    std::unordered_set<std::string> visible_set(visible.begin(), visible.end());

    // Cosine similarity with chunks (filtered by visible projects)
    struct ScoredChunk {
        std::string content;
        float score;
    };
    std::vector<ScoredChunk> scored;

    for (size_t i = 0; i < chunks_.size(); i++) {
        const auto& emb = chunks_[i].embedding;

        // M3.C: Skip chunks not in visible projects
        if (visible_set.count(chunks_[i].project_id) == 0) continue;

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
// Hybrid Search (M2.7) — vector + BM25 + graph
// ============================================================================

std::vector<search::HybridResult> ApolloCore::query_hybrid(
    const std::string& query_text,
    size_t top_k,
    double alpha,
    double beta,
    double gamma)
{
    if (!is_open_) return {};
    if (!embedder_->is_loaded()) {
        std::cerr << "[ApolloCore] query_hybrid: embedding model not loaded, "
                     "falling back to BM25 only\n";
    }

    search::HybridSearchParams params;
    params.query = query_text;
    params.top_k = top_k;
    params.alpha = alpha;
    params.beta = beta;
    params.gamma = gamma;
    params.candidate_multiplier = 5;  // get 5x more candidates per source
    params.graph_max_hops = 2;

    return search::hybrid_search(*this, params);
}

// ============================================================================
// Cross-Encoder Reranking (M2.8) — vector + LLM scoring
// ============================================================================

namespace {

// Helper: convert ApolloCore::query result to RerankedResult
std::vector<RerankedResult> to_reranked(
    const std::vector<std::pair<std::string, float>>& raw,
    ApolloCore& kb)
{
    std::vector<RerankedResult> results;
    auto all_chunks = kb.get_all_chunks();
    std::unordered_map<std::string, std::string> content_map(
        all_chunks.begin(), all_chunks.end());

    for (const auto& [chunk_id, score] : raw) {
        std::string content;
        auto it = content_map.find(chunk_id);
        if (it != content_map.end()) content = it->second;
        RerankedResult r(chunk_id, content, score, 0.0f);
        r.final_score = score;  // No rerank yet
        results.push_back(r);
    }
    return results;
}

}  // namespace

std::vector<RerankedResult> ApolloCore::query_with_rerank(
    const std::string& query_text,
    size_t top_k,
    size_t candidate_k)
{
    if (!is_open_) return {};

    if (candidate_k == 0) candidate_k = std::max(top_k * 4, size_t(20));

    // 1. Vector search to get candidates
    auto candidates = query(query_text, candidate_k);
    if (candidates.empty()) return {};

    // 2. Convert to RerankedResult format
    auto results = to_reranked(candidates, *this);

    // 3. If no LLM, fall back to plain vector ranking
    if (!entity_extractor_ || !is_llm_loaded()) {
        if (results.size() > top_k) results.resize(top_k);
        return results;
    }

    // 4. Cross-encoder rerank using the loaded LLM
    RerankConfig config;
    config.llm.model_path = llm_model_path_;
    config.top_k = candidate_k;
    config.return_k = top_k;

    CrossEncoderReranker reranker(config);
    if (reranker.is_initialized()) {
        results = reranker.rerank(query_text, results);
    }

    if (results.size() > top_k) results.resize(top_k);
    return results;
}

std::vector<RerankedResult> ApolloCore::query_full(
    const std::string& query_text,
    size_t top_k)
{
    if (!is_open_) return {};

    // 1. Hybrid search (vector + BM25 + graph)
    auto hybrid = query_hybrid(query_text, top_k);
    if (hybrid.empty()) return {};

    // 2. Convert HybridResult to RerankedResult
    std::vector<RerankedResult> results;
    results.reserve(hybrid.size());
    auto all_chunks = get_all_chunks();
    std::unordered_map<std::string, std::string> content_map(
        all_chunks.begin(), all_chunks.end());

    for (const auto& h : hybrid) {
        std::string content;
        auto it = content_map.find(h.chunk_id);
        if (it != content_map.end()) content = it->second;
        RerankedResult r(h.chunk_id, content,
                         static_cast<float>(h.vector_score),
                         static_cast<float>(h.bm25_score));
        r.final_score = static_cast<float>(h.score);
        results.push_back(r);
    }

    // 3. Cross-encoder rerank (if LLM is loaded)
    if (entity_extractor_ && is_llm_loaded()) {
        RerankConfig config;
        config.llm.model_path = llm_model_path_;
        config.top_k = static_cast<size_t>(hybrid.size());
        config.return_k = top_k;

        CrossEncoderReranker reranker(config);
        if (reranker.is_initialized()) {
            results = reranker.rerank(query_text, results);
        }
    }

    if (results.size() > top_k) results.resize(top_k);
    return results;
}

// ============================================================================
// Entity Extraction (G1.3, G1.4, G1.5, G1.6)
// ============================================================================

size_t ApolloCore::extract_entities_for_chunk(
    const std::string& chunk_id,
    float min_confidence)
{
    if (!is_llm_loaded()) {
        std::cerr << "[ApolloCore] LLM not loaded. Call load_llm_model() first.\n";
        return 0;
    }
    
    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open\n";
        return 0;
    }
    
    // Get chunk content from DB
    auto all_chunks = kb_persistence_->load_all_chunks();
    std::string chunk_content;
    for (const auto& c : all_chunks) {
        if (c.id == chunk_id) {
            chunk_content = c.content;
            break;
        }
    }
    
    if (chunk_content.empty()) {
        std::cerr << "[ApolloCore] Chunk not found: " << chunk_id << "\n";
        return 0;
    }
    
    // Skip if already extracted (G1.5: tracking)
    // We mark this by checking if any entities exist for this chunk
    auto existing_entities = kb_persistence_->load_all_entities();
    for (const auto& e : existing_entities) {
        if (e.chunk_id == chunk_id) {
            // Already extracted - skip
            return 0;
        }
    }
    
    // Run extraction (G1.3, G1.6: error handling)
    std::cerr << "[ApolloCore] Extracting entities from chunk: " << chunk_id << "...\n";
    
    ExtractionResult result;
    try {
        result = entity_extractor_->extract(chunk_content, chunk_id);
    } catch (const std::exception& e) {
        std::cerr << "[ApolloCore] Extraction failed for chunk " << chunk_id 
                  << ": " << e.what() << "\n";
        return 0;
    }
    
    // Apply confidence filter
    auto filtered = result.filter_by_confidence(min_confidence);
    
    // Save entities to DB (G1.4: wire to persistence)
    size_t saved_entities = 0;
    for (const auto& entity : filtered.entities) {
        std::string entity_id = chunk_id + "_entity_" + std::to_string(saved_entities);
        bool ok = kb_persistence_->save_entity(
            entity_id, chunk_id, entity.name,
            entity_type_to_string(entity.type),
            entity.description,
            entity.confidence,
            current_project_
        );
        if (ok) saved_entities++;
    }
    
    // Save relationships to DB
    size_t saved_rels = 0;
    for (const auto& rel : filtered.relationships) {
        // Find entity IDs by name (they were just created above)
        std::string source_id, target_id;
        for (size_t i = 0; i < filtered.entities.size(); i++) {
            if (filtered.entities[i].name == rel.source) {
                source_id = chunk_id + "_entity_" + std::to_string(i);
            }
            if (filtered.entities[i].name == rel.target) {
                target_id = chunk_id + "_entity_" + std::to_string(i);
            }
        }
        
        if (!source_id.empty() && !target_id.empty()) {
            std::string rel_id = chunk_id + "_rel_" + std::to_string(saved_rels);
            bool ok = kb_persistence_->save_relationship(
                rel_id, source_id, target_id,
                relation_type_to_string(rel.type),
                rel.description,
                rel.weight,
                current_project_
            );
            if (ok) saved_rels++;
        }
    }
    
    std::cerr << "[ApolloCore]   Extracted: " << saved_entities 
              << " entities, " << saved_rels << " relationships\n";
    
    return saved_entities;
}

size_t ApolloCore::extract_entities(
    float min_confidence,
    bool skip_already_extracted)
{
    if (!is_llm_loaded()) {
        std::cerr << "[ApolloCore] LLM not loaded. Call load_llm_model() first.\n";
        return 0;
    }
    
    if (!is_open_) {
        std::cerr << "[ApolloCore] No knowledge base open\n";
        return 0;
    }
    
    std::cerr << "[ApolloCore] Starting entity extraction for all chunks...\n";
    
    // Get all chunks from DB
    auto all_chunks = kb_persistence_->load_all_chunks();
    
    if (all_chunks.empty()) {
        std::cerr << "[ApolloCore] No chunks to process\n";
        return 0;
    }
    
    // Track which chunks have entities (for skip_already_extracted)
    std::unordered_set<std::string> chunks_with_entities;
    if (skip_already_extracted) {
        auto existing_entities = kb_persistence_->load_all_entities();
        for (const auto& e : existing_entities) {
            chunks_with_entities.insert(e.chunk_id);
        }
    }
    
    size_t total_entities = 0;
    size_t chunks_processed = 0;
    
    for (const auto& chunk : all_chunks) {
        if (skip_already_extracted && 
            chunks_with_entities.count(chunk.id)) {
            // Skip already-processed chunk
            continue;
        }
        
        size_t count = extract_entities_for_chunk(chunk.id, min_confidence);
        if (count > 0) {
            total_entities += count;
            chunks_processed++;
        }
    }
    
    std::cerr << "[ApolloCore] Entity extraction complete.\n";
    std::cerr << "  Total entities extracted: " << total_entities << "\n";
    std::cerr << "  Chunks processed: " << chunks_processed << "\n";
    
    return total_entities;
}

std::vector<std::pair<std::string, std::string>> ApolloCore::get_all_chunks() {
    std::vector<std::pair<std::string, std::string>> result;
    if (!is_open_ || !kb_persistence_) return result;

    auto chunks = kb_persistence_->load_all_chunks();

    // M3.C: Filter by visible projects
    auto visible = visible_projects();
    std::unordered_set<std::string> visible_set(visible.begin(), visible.end());

    result.reserve(chunks.size());
    for (const auto& c : chunks) {
        if (visible_set.count(c.project_id) == 0) continue;
        result.emplace_back(c.id, c.content);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> ApolloCore::get_all_documents() {
    std::vector<std::pair<std::string, std::string>> result;
    if (!is_open_ || !kb_persistence_) return result;

    auto nodes = kb_persistence_->load_all_nodes();

    // M3.C: Filter by visible projects
    auto visible = visible_projects();
    std::unordered_set<std::string> visible_set(visible.begin(), visible.end());

    result.reserve(nodes.size());
    for (const auto& n : nodes) {
        if (visible_set.count(n.project_id) == 0) continue;

        // Concatenate title and content for the prompt context
        std::string body = n.content;
        if (!n.title.empty()) {
            body = "[" + n.title + "]\n" + n.content;
        }
        result.emplace_back(n.id, body);
    }
    return result;
}

std::vector<Entity> ApolloCore::get_entities(size_t limit) {
    std::vector<Entity> result;
    if (!is_open_) return result;

    // M3.C: Filter by current project + any shared read sources
    auto visible = visible_projects();
    auto loaded = kb_persistence_->load_entities_for_projects(visible, limit);

    result.reserve(loaded.size());

    for (const auto& le : loaded) {
        Entity e;
        e.name = le.name;
        e.description = le.description;
        e.confidence = le.confidence;
        e.type = string_to_entity_type(le.type.c_str());
        e.id = le.id;
        e.sources.push_back(le.chunk_id);
        result.push_back(e);

        if (limit > 0 && result.size() >= limit) break;
    }

    return result;
}

std::vector<Entity> ApolloCore::get_entities_for_chunk(
    const std::string& chunk_id) {
    std::vector<Entity> result;
    if (!is_open_) return result;

    // M3.C: Filter by visible projects
    auto visible = visible_projects();
    auto loaded = kb_persistence_->load_entities_for_projects(visible);
    for (const auto& le : loaded) {
        if (le.chunk_id != chunk_id) continue;

        Entity e;
        e.name = le.name;
        e.description = le.description;
        e.confidence = le.confidence;
        e.type = string_to_entity_type(le.type.c_str());
        e.id = le.id;
        e.sources.push_back(le.chunk_id);
        result.push_back(e);
    }
    
    return result;
}

std::vector<Relationship> ApolloCore::get_relationships(size_t limit) {
    std::vector<Relationship> result;
    if (!is_open_) return result;

    // M3.C: Filter by current project + any shared read sources
    auto visible = visible_projects();
    auto loaded = kb_persistence_->load_relationships_for_projects(visible, limit);
    result.reserve(loaded.size());

    for (const auto& lr : loaded) {
        // Find the entity names from IDs (use visible entities too)
        auto entities = kb_persistence_->load_entities_for_projects(visible);
        std::string source_name, target_name;
        for (const auto& e : entities) {
            if (e.id == lr.source_id) source_name = e.name;
            if (e.id == lr.target_id) target_name = e.name;
        }

        Relationship r;
        r.source = source_name;
        r.target = target_name;
        r.type = string_to_relation_type(lr.type.c_str());
        r.description = lr.description;
        r.weight = lr.confidence;
        result.push_back(r);

        if (limit > 0 && result.size() >= limit) break;
    }

    return result;
}

EntityExtractor* ApolloCore::get_entity_extractor() {
    return entity_extractor_.get();
}

// ============================================================================
// Statistics
// ============================================================================

ApolloCore::Stats ApolloCore::get_stats() {
    Stats stats;

    // M3.C: Filter by visible projects
    auto visible = visible_projects();
    std::unordered_set<std::string> visible_set(visible.begin(), visible.end());

    // Count documents visible to current project (real count below)
    if (is_open_) {
        // Filtered counts from DB
        auto nodes = kb_persistence_->load_all_nodes();
        std::unordered_set<std::string> visible_doc_ids;
        for (const auto& n : nodes) {
            if (visible_set.count(n.project_id)) {
                visible_doc_ids.insert(n.id);
            }
        }
        stats.document_count = visible_doc_ids.size();

        // Filtered chunks
        stats.chunk_count = 0;
        for (const auto& c : chunks_) {
            if (visible_set.count(c.project_id)) {
                stats.chunk_count++;
            }
        }

        // Filtered entities
        auto entities = kb_persistence_->load_entities_for_projects(visible);
        stats.entity_count = entities.size();

        // Filtered relationships
        auto rels = kb_persistence_->load_relationships_for_projects(visible);
        stats.relationship_count = rels.size();

        auto disk_stats = kb_persistence_->get_disk_stats();
        stats.embedding_files_size = disk_stats.embedding_files_size;

        // Count chunks with entities (filtered)
        std::unordered_set<std::string> chunk_ids;
        for (const auto& e : entities) {
            chunk_ids.insert(e.chunk_id);
        }
        stats.chunks_with_entities = chunk_ids.size();
    }

    return stats;
}

// ============================================================================
// Project Scoping (Phase M3.C — multi-agent support)
// ============================================================================

bool ApolloCore::set_current_project(const std::string& project_name) {
    if (!is_open_ || !kb_persistence_) return false;
    if (project_name.empty()) return false;

    // Verify project exists
    auto info = kb_persistence_->load_project(project_name);
    if (info.name.empty()) {
        std::cerr << "[ApolloCore] Project not found: " << project_name << "\n";
        return false;
    }

    current_project_ = project_name;
    std::cout << "[ApolloCore] Current project set to: " << project_name << "\n";
    return true;
}

bool ApolloCore::register_project(const std::string& name,
                                  const std::string& description,
                                  bool is_shared,
                                  const std::string& owner) {
    if (!is_open_ || !kb_persistence_) return false;
    if (name.empty()) return false;

    KnowledgeBasePersistence::ProjectInfo info;
    info.id = name;
    info.name = name;
    info.description = description;
    info.path = "";  // KB-level project, not file path
    info.is_shared = is_shared;
    info.owner = owner;
    info.metadata = "{}";

    bool ok = kb_persistence_->save_project(info);
    if (ok) {
        std::cout << "[ApolloCore] Registered project: " << name
                  << (is_shared ? " (shared)" : "") << "\n";
    }
    return ok;
}

bool ApolloCore::set_project_shared(const std::string& name, bool is_shared) {
    if (!is_open_ || !kb_persistence_) return false;
    return kb_persistence_->set_project_shared(name, is_shared);
}

bool ApolloCore::add_shared_read_source(const std::string& project_name) {
    if (project_name == current_project_) return true;  // own project
    if (std::find(shared_read_sources_.begin(), shared_read_sources_.end(),
                  project_name) != shared_read_sources_.end()) {
        return true;  // already added
    }
    shared_read_sources_.push_back(project_name);
    std::cout << "[ApolloCore] Added shared read source: " << project_name << "\n";
    return true;
}

bool ApolloCore::remove_shared_read_source(const std::string& project_name) {
    auto it = std::find(shared_read_sources_.begin(),
                        shared_read_sources_.end(), project_name);
    if (it != shared_read_sources_.end()) {
        shared_read_sources_.erase(it);
        return true;
    }
    return false;
}

nlohmann::json ApolloCore::list_projects() const {
    nlohmann::json arr = nlohmann::json::array();
    if (!is_open_ || !kb_persistence_) return arr;

    auto projects = kb_persistence_->load_all_projects();
    for (const auto& p : projects) {
        arr.push_back({
            {"name", p.name},
            {"description", p.description},
            {"is_shared", p.is_shared},
            {"owner", p.owner},
            {"created_at", p.created_at},
            {"is_current", p.name == current_project_}
        });
    }
    return arr;
}

nlohmann::json ApolloCore::get_project_info(const std::string& name) const {
    if (!is_open_ || !kb_persistence_) return nullptr;

    auto p = kb_persistence_->load_project(name);
    if (p.name.empty()) return nullptr;

    nlohmann::json info = {
        {"name", p.name},
        {"description", p.description},
        {"is_shared", p.is_shared},
        {"owner", p.owner},
        {"metadata", p.metadata},
        {"created_at", p.created_at},
        {"updated_at", p.updated_at},
        {"is_current", p.name == current_project_}
    };
    return info;
}

bool ApolloCore::delete_project(const std::string& name, bool confirm) {
    if (!is_open_ || !kb_persistence_) return false;
    if (!confirm) {
        std::cerr << "[ApolloCore] delete_project requires confirm=true\n";
        return false;
    }
    if (name == current_project_) {
        std::cerr << "[ApolloCore] Cannot delete current project. "
                     "Switch projects first.\n";
        return false;
    }
    if (name == "default") {
        std::cerr << "[ApolloCore] Cannot delete 'default' project. "
                     "Use delete_project with a different name.\n";
        return false;
    }
    return kb_persistence_->delete_project_data(name);
}

std::vector<std::string> ApolloCore::visible_projects() const {
    std::vector<std::string> result;
    result.push_back(current_project_);
    for (const auto& src : shared_read_sources_) {
        result.push_back(src);
    }
    return result;
}

} // namespace apollocore_v2
