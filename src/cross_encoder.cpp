/**
 * ApolloCore - Cross-Encoder Reranking Implementation
 */

#include "apollocore/cross_encoder.hpp"
#include "llama.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cctype>

namespace apollocore_v2 {

// ============================================================================
// Llama State (PIMPL)
// ============================================================================

struct CrossEncoderReranker::LlamaState {
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

RerankConfig default_rerank_config() {
    RerankConfig config;
    config.top_k = 20;
    config.return_k = 10;
    config.cross_encoder_weight = 0.5f;
    config.original_weight = 0.5f;
    config.min_score_threshold = 0.0f;
    config.verbose = false;
    return config;
}

RerankConfig high_precision_rerank_config() {
    RerankConfig config = default_rerank_config();
    config.top_k = 50;
    config.return_k = 5;
    config.cross_encoder_weight = 0.7f;
    config.original_weight = 0.3f;
    return config;
}

RerankConfig fast_rerank_config() {
    RerankConfig config = default_rerank_config();
    config.top_k = 10;
    config.return_k = 5;
    config.cross_encoder_weight = 0.4f;
    config.original_weight = 0.6f;
    return config;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

CrossEncoderReranker::CrossEncoderReranker(const RerankConfig& config) 
    : config_(config) {
    if (!config_.llm.model_path.empty()) {
        initialize_llm();
    }
}

CrossEncoderReranker::~CrossEncoderReranker() = default;

// ============================================================================
// LLM Initialization
// ============================================================================

void CrossEncoderReranker::initialize_llm() {
    llama_backend_init();
    
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;
    
    llama_state_ = std::make_unique<LlamaState>();
    llama_state_->model = llama_model_load_from_file(
        config_.llm.model_path.c_str(), 
        model_params
    );
    
    if (!llama_state_->model) {
        if (config_.verbose) {
            std::cerr << "[CrossEncoder] Failed to load model: " << config_.llm.model_path << "\n";
        }
        llama_state_.reset();
        return;
    }
    
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.llm.n_ctx;
    ctx_params.n_threads = config_.llm.n_threads;
    
    llama_state_->ctx = llama_init_from_model(llama_state_->model, ctx_params);
    
    if (!llama_state_->ctx) {
        if (config_.verbose) {
            std::cerr << "[CrossEncoder] Failed to create context\n";
        }
        llama_model_free(llama_state_->model);
        llama_state_.reset();
        return;
    }
    
    initialized_ = true;
    
    if (config_.verbose) {
        std::cout << "[CrossEncoder] Initialized with model: " << config_.llm.model_path << "\n";
    }
}

// ============================================================================
// Prompt Generation
// ============================================================================

std::string CrossEncoderReranker::create_scoring_prompt(
    const std::string& query, 
    const std::string& document
) {
    std::ostringstream prompt;
    
    // Truncate document if too long
    std::string doc = document;
    if (doc.length() > 500) {
        doc = doc.substr(0, 500) + "...";
    }
    
    prompt << R"(<|im_start|>system
You are a relevance scoring system. Score how relevant a document is to a query.
Return ONLY a number between 0.0 and 1.0, where:
- 1.0 = Highly relevant, directly answers the query
- 0.5 = Somewhat relevant, contains related information
- 0.0 = Not relevant, doesn't help answer the query
<|im_end|>
<|im_start|>user
Query: )" << query << R"(
Document: )" << doc << R"(
<|im_end|>
<|im_start|>assistant
)";
    
    return prompt.str();
}

float CrossEncoderReranker::parse_score_response(const std::string& response) {
    // Try to extract a number from the response
    std::string cleaned;
    for (char c : response) {
        if (c == '.' || c == '-' || (c >= '0' && c <= '9')) {
            cleaned += c;
        }
    }
    
    // Find the first valid number
    float score = 0.5f;  // Default
    char* end;
    float parsed = std::strtof(cleaned.c_str(), &end);
    
    if (end != cleaned.c_str() && parsed >= 0.0f && parsed <= 1.0f) {
        score = parsed;
    }
    
    return score;
}

// ============================================================================
// LLM Scoring
// ============================================================================

std::vector<float> CrossEncoderReranker::score_batch_llm(
    const std::string& query,
    const std::vector<std::string>& docs
) {
    std::vector<float> scores;
    
    if (!initialized_ || !llama_state_) {
        // Return neutral scores if not initialized
        return std::vector<float>(docs.size(), 0.5f);
    }
    
    const llama_vocab* vocab = llama_model_get_vocab(llama_state_->model);
    
    for (const auto& doc : docs) {
        std::string prompt = create_scoring_prompt(query, doc);
        
        // Tokenize
        std::vector<llama_token> tokens(llama_n_ctx(llama_state_->ctx));
        int n_tokens = llama_tokenize(
            vocab, prompt.c_str(), (int)prompt.length(),
            tokens.data(), (int)tokens.size(), true, true
        );
        if (n_tokens < 0) {
            scores.push_back(0.5f);
            continue;
        }
        tokens.resize(n_tokens);
        
        // Evaluate prompt
        for (int i = 0; i < n_tokens; i++) {
            llama_batch batch = llama_batch_get_one(&tokens[i], 1);
            llama_decode(llama_state_->ctx, batch);
        }
        
        // Generate response
        std::string response;
        llama_sampler* smpl = llama_sampler_init_greedy();
        
        for (int i = 0; i < 20; i++) {
            llama_token new_token = llama_sampler_sample(smpl, llama_state_->ctx, -1);
            
            if (llama_vocab_is_eog(vocab, new_token)) {
                break;
            }
            
            char buf[64];
            int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
            if (n > 0) {
                response.append(buf, n);
            }
            
            if (response.find('\n') != std::string::npos) {
                break;
            }
            
            llama_batch batch = llama_batch_get_one(&new_token, 1);
            llama_decode(llama_state_->ctx, batch);
        }
        
        llama_sampler_free(smpl);
        
        scores.push_back(parse_score_response(response));
    }
    
    return scores;
}

// ============================================================================
// Main Reranking
// ============================================================================

std::vector<RerankedResult> CrossEncoderReranker::rerank(
    const std::string& query,
    const std::vector<RerankedResult>& results
) {
    auto start = std::chrono::high_resolution_clock::now();
    
    if (results.empty()) {
        return {};
    }
    
    // Take top-k from initial results
    size_t to_rerank = std::min(config_.top_k, results.size());
    std::vector<RerankedResult> candidates(results.begin(), results.begin() + to_rerank);
    
    if (config_.verbose) {
        std::cout << "[CrossEncoder] Reranking " << candidates.size() << " candidates\n";
    }
    
    // Score with cross-encoder (LLM)
    std::vector<std::string> docs;
    for (const auto& r : candidates) {
        docs.push_back(r.content);
    }
    
    std::vector<float> cross_scores;
    if (initialized_) {
        cross_scores = score_batch_llm(query, docs);
    } else {
        // Fallback: use original scores
        cross_scores.resize(candidates.size(), 0.5f);
    }
    
    // Compute final scores
    for (size_t i = 0; i < candidates.size(); i++) {
        candidates[i].rerank_score = cross_scores[i];
        
        // Combine original and cross-encoder scores
        float combined = (config_.cross_encoder_weight * cross_scores[i]) +
                        (config_.original_weight * candidates[i].original_score);
        candidates[i].final_score = combined;
    }
    
    // Sort by final score descending
    std::sort(candidates.begin(), candidates.end(),
        [](const RerankedResult& a, const RerankedResult& b) {
            return a.final_score > b.final_score;
        });
    
    // Return top-k
    size_t return_count = std::min(config_.return_k, candidates.size());
    std::vector<RerankedResult> final_results;
    for (size_t i = 0; i < return_count; i++) {
        if (candidates[i].final_score >= config_.min_score_threshold) {
            final_results.push_back(candidates[i]);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    stats_.total_reranked++;
    stats_.total_documents += final_results.size();
    stats_.total_time_ms += duration;
    
    if (config_.verbose) {
        std::cout << "[CrossEncoder] Reranking completed in " << duration << "ms\n";
    }
    
    return final_results;
}

std::vector<RerankedResult> CrossEncoderReranker::rerank_with_custom_scorer(
    const std::string& query,
    const std::vector<RerankedResult>& results,
    std::function<float(const std::string&, const std::string&)> scorer
) {
    auto start = std::chrono::high_resolution_clock::now();
    
    if (results.empty()) {
        return {};
    }
    
    // Take top-k
    size_t to_rerank = std::min(config_.top_k, results.size());
    std::vector<RerankedResult> candidates(results.begin(), results.begin() + to_rerank);
    
    // Score with custom scorer
    for (auto& r : candidates) {
        r.rerank_score = scorer(query, r.content);
        r.final_score = (config_.cross_encoder_weight * r.rerank_score) +
                        (config_.original_weight * r.original_score);
    }
    
    // Sort by final score
    std::sort(candidates.begin(), candidates.end(),
        [](const RerankedResult& a, const RerankedResult& b) {
            return a.final_score > b.final_score;
        });
    
    // Return top-k
    size_t return_count = std::min(config_.return_k, candidates.size());
    std::vector<RerankedResult> final_results;
    for (size_t i = 0; i < return_count; i++) {
        if (candidates[i].final_score >= config_.min_score_threshold) {
            final_results.push_back(candidates[i]);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    stats_.total_reranked++;
    stats_.total_documents += final_results.size();
    stats_.total_time_ms += duration;
    
    return final_results;
}

} // namespace apollocore_v2
