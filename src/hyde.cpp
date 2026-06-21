/**
 * ApolloCore - HyDE Query Enhancement Implementation
 */

#include "apollocore/hyde.hpp"
#include "llama.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>

namespace apollocore_v2 {

// ============================================================================
// Llama State (PIMPL)
// ============================================================================

struct HydeEnhancer::LlamaState {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    
    ~LlamaState() {
        if (ctx) {
            llama_free(ctx);
            ctx = nullptr;
        }
        if (model) {
            llama_model_free(model);
            model = nullptr;
        }
    }
};

// ============================================================================
// Configuration Factory Functions
// ============================================================================

HydeConfig default_hyde_config() {
    HydeConfig config;
    config.num_generations = 3;
    config.temperature = 0.7f;
    config.max_tokens = 200;
    config.weight_hyde = 0.7f;
    config.weight_query = 0.3f;
    config.verbose = false;
    return config;
}

HydeConfig diverse_hyde_config() {
    HydeConfig config = default_hyde_config();
    config.num_generations = 5;
    config.temperature = 1.0f;
    config.max_tokens = 300;
    return config;
}

HydeConfig fast_hyde_config() {
    HydeConfig config = default_hyde_config();
    config.num_generations = 2;
    config.temperature = 0.5f;
    config.max_tokens = 150;
    return config;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

HydeEnhancer::HydeEnhancer(const HydeConfig& config) 
    : config_(config) {
    if (!config_.llm.model_path.empty()) {
        initialize();
    }
}

HydeEnhancer::~HydeEnhancer() = default;

// ============================================================================
// Initialization
// ============================================================================

void HydeEnhancer::initialize() {
    llama_backend_init();
    
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;
    
    llm_state_ = std::make_unique<LlamaState>();
    llm_state_->model = llama_model_load_from_file(
        config_.llm.model_path.c_str(), 
        model_params
    );
    
    if (!llm_state_->model) {
        if (config_.verbose) {
            std::cerr << "[HyDE] Failed to load model: " << config_.llm.model_path << "\n";
        }
        llm_state_.reset();
        return;
    }
    
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.llm.n_ctx;
    ctx_params.n_threads = config_.llm.n_threads;
    
    llm_state_->ctx = llama_init_from_model(llm_state_->model, ctx_params);
    
    if (!llm_state_->ctx) {
        if (config_.verbose) {
            std::cerr << "[HyDE] Failed to create context\n";
        }
        llama_model_free(llm_state_->model);
        llm_state_.reset();
        return;
    }
    
    initialized_ = true;
    
    if (config_.verbose) {
        std::cout << "[HyDE] Initialized with model: " << config_.llm.model_path << "\n";
    }
}

// ============================================================================
// Prompt Generation
// ============================================================================

std::string create_hyde_prompt(const std::string& query) {
    std::ostringstream prompt;
    prompt << R"(<|im_start|>system
You are a helpful assistant that writes short, informative text passages.
Write a brief paragraph (2-3 sentences) that directly answers the user's question.
The passage should be factual and informative, as if it were from a Wikipedia article or textbook.
<|im_end|>
<|im_start|>user
)" << query << R"(
<|im_end|>
<|im_start|>assistant
)";
    return prompt.str();
}

std::string HydeEnhancer::generate_hypothetical_doc(const std::string& query) {
    if (!initialized_ || !llm_state_) {
        // Return a placeholder if not initialized
        return "This document is about " + query + ".";
    }
    
    std::string prompt = create_hyde_prompt(query);
    
    const llama_vocab* vocab = llama_model_get_vocab(llm_state_->model);
    
    // Tokenize
    std::vector<llama_token> tokens(llama_n_ctx(llm_state_->ctx));
    int n_tokens = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.length(),
        tokens.data(), (int)tokens.size(), true, true
    );
    
    if (n_tokens < 0) {
        return "Document about " + query;
    }
    tokens.resize(n_tokens);
    
    // Evaluate prompt
    for (int i = 0; i < n_tokens; i++) {
        llama_batch batch = llama_batch_get_one(&tokens[i], 1);
        llama_decode(llm_state_->ctx, batch);
    }
    
    // Generate response
    std::string response;
    llama_sampler* smpl = llama_sampler_init_greedy();
    
    for (int i = 0; i < config_.max_tokens; i++) {
        llama_token new_token = llama_sampler_sample(smpl, llm_state_->ctx, -1);
        
        if (llama_vocab_is_eog(vocab, new_token)) {
            break;
        }
        
        char buf[64];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) {
            response.append(buf, n);
        }
        
        // Stop at paragraph end
        if (response.find("\n\n") != std::string::npos) {
            size_t pos = response.find("\n\n");
            response = response.substr(0, pos);
            break;
        }
        
        llama_batch batch = llama_batch_get_one(&new_token, 1);
        llama_decode(llm_state_->ctx, batch);
    }
    
    llama_sampler_free(smpl);
    
    if (response.empty()) {
        return "Document about " + query;
    }
    
    return response;
}

// ============================================================================
// Quality Assessment
// ============================================================================

float HydeEnhancer::assess_quality(const std::string& query, const std::string& doc) {
    // Simple heuristic: longer docs with query-related terms score higher
    float score = 0.5f;
    
    // Length factor (longer is generally better)
    if (doc.length() > 100) score += 0.1f;
    if (doc.length() > 200) score += 0.1f;
    
    // Query term presence
    std::string lower_doc = doc;
    std::string lower_query = query;
    std::transform(lower_doc.begin(), lower_doc.end(), lower_doc.begin(), ::tolower);
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
    
    // Check if key words from query appear in doc
    std::istringstream iss(lower_query);
    std::string word;
    int matches = 0;
    int total = 0;
    while (iss >> word) {
        if (word.length() > 3) {  // Skip short words
            total++;
            if (lower_doc.find(word) != std::string::npos) {
                matches++;
            }
        }
    }
    
    if (total > 0) {
        score += 0.2f * (float)matches / total;
    }
    
    return std::min(1.0f, std::max(0.0f, score));
}

// ============================================================================
// Generate Multiple Hypothetical Documents
// ============================================================================

std::vector<HypotheticalDocument> HydeEnhancer::generate_docs(
    const std::string& query, 
    size_t count
) {
    std::vector<HypotheticalDocument> docs;
    
    for (size_t i = 0; i < count; i++) {
        std::string content = generate_hypothetical_doc(query);
        float quality = assess_quality(query, content);
        
        docs.emplace_back(content, std::vector<float>(), quality);
        
        if (config_.verbose) {
            std::cout << "[HyDE] Generated doc " << (i + 1) << "/" << count 
                      << " (quality: " << quality << ")\n";
        }
    }
    
    return docs;
}

// ============================================================================
// Combine Embeddings
// ============================================================================

std::vector<float> HydeEnhancer::combine_embeddings(
    const std::vector<HypotheticalDocument>& docs,
    const std::vector<float>& query_embedding
) {
    if (docs.empty()) {
        return query_embedding;  // Return original query embedding
    }
    
    // Calculate weighted average of hypothetical document embeddings
    // Note: In real implementation, you would embed each doc using EmbeddingModel
    // For now, we use a simple average
    
    size_t dim = config_.embedding.dimensions;
    if (query_embedding.size() > 0) {
        dim = query_embedding.size();
    }
    
    std::vector<float> combined(dim, 0.0f);
    
    // Sum all embeddings
    for (const auto& doc : docs) {
        if (doc.embedding.size() == dim) {
            for (size_t i = 0; i < dim; i++) {
                combined[i] += doc.embedding[i] * doc.quality_score;
            }
        }
    }
    
    // Normalize by sum of quality scores
    float total_quality = 0.0f;
    for (const auto& doc : docs) {
        total_quality += doc.quality_score;
    }
    
    if (total_quality > 0.0f) {
        for (size_t i = 0; i < dim; i++) {
            combined[i] /= total_quality;
        }
    }
    
    // If we have a query embedding, blend it
    if (!query_embedding.empty() && query_embedding.size() == dim) {
        for (size_t i = 0; i < dim; i++) {
            combined[i] = config_.weight_hyde * combined[i] + 
                         config_.weight_query * query_embedding[i];
        }
    }
    
    // Normalize final embedding
    float norm = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        norm += combined[i] * combined[i];
    }
    norm = std::sqrt(norm);
    
    if (norm > 0.0f) {
        for (size_t i = 0; i < dim; i++) {
            combined[i] /= norm;
        }
    }
    
    return combined;
}

// ============================================================================
// Main HyDE Enhancement
// ============================================================================

HydeResult HydeEnhancer::enhance(const std::string& query) {
    auto start = std::chrono::high_resolution_clock::now();
    
    HydeResult result;
    result.original_query = query;
    
    if (!initialized_) {
        if (config_.verbose) {
            std::cout << "[HyDE] Not initialized, returning original query\n";
        }
        // Still return an empty result
        result.combined_embedding = std::vector<float>(config_.embedding.dimensions, 0.0f);
        return result;
    }
    
    // Generate hypothetical documents
    result.hypothetical_docs = generate_docs(query, config_.num_generations);
    
    // Note: In full implementation, you would embed each hypothetical doc
    // using the EmbeddingModel. For now, we skip this step.
    
    // Calculate combined embedding
    result.combined_embedding = combine_embeddings(result.hypothetical_docs, {});
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    stats_.total_enhanced++;
    stats_.total_docs_generated += result.num_docs();
    stats_.total_time_ms += duration;
    
    if (config_.verbose) {
        std::cout << "[HyDE] Enhancement completed in " << duration << "ms\n";
    }
    
    return result;
}

std::vector<HydeResult> HydeEnhancer::enhance_batch(
    const std::vector<std::string>& queries
) {
    std::vector<HydeResult> results;
    
    for (const auto& query : queries) {
        results.push_back(enhance(query));
    }
    
    return results;
}

} // namespace apollocore_v2
