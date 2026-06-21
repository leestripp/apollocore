#pragma once

/**
 * ApolloCore - Cross-Encoder Reranking Module
 * 
 * Re-ranks search results using a cross-encoder model for improved precision.
 * 
 * How it works:
 * 1. Initial retrieval: Fast bi-encoder vector search (already done)
 * 2. Reranking: Cross-encoder scores query + document pairs
 * 3. Final ranking: Combine vector + cross-encoder scores
 * 
 * Cross-encoder advantages:
 * - Computes attention between query and document jointly
 * - More accurate relevance scoring
 * - Captures query-document interactions
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace apollocore_v2 {

// ============================================================================
// Reranking Result
// ============================================================================

/**
 * Represents a reranked search result
 */
struct RerankedResult {
    std::string chunk_id;       // Source chunk ID
    std::string content;         // Document content
    float original_score;         // Score from initial retrieval
    float rerank_score;           // Score from cross-encoder
    float final_score;            // Combined score
    float vector_score;           // Semantic similarity score
    float keyword_score;          // BM25 keyword score
    
    RerankedResult() 
        : original_score(0.0f), rerank_score(0.0f), final_score(0.0f),
          vector_score(0.0f), keyword_score(0.0f) {}
    
    RerankedResult(const std::string& id, const std::string& text, float vec = 0.0f, float kw = 0.0f)
        : chunk_id(id), content(text), original_score(0.0f), 
          rerank_score(0.0f), final_score(0.0f), 
          vector_score(vec), keyword_score(kw) {}
};

/**
 * Configuration for cross-encoder reranking
 */
struct RerankConfig {
    // Number of top results to rerank (from initial retrieval)
    size_t top_k = 20;
    
    // How many results to return after reranking
    size_t return_k = 10;
    
    // Weight for cross-encoder score in final ranking
    float cross_encoder_weight = 0.5f;
    
    // Weight for original retrieval score
    float original_weight = 0.5f;
    
    // Minimum score threshold (0-1)
    float min_score_threshold = 0.0f;
    
    // Enable verbose logging
    bool verbose = false;
    
    // LLM settings for scoring (reuse entity extractor LLM if available)
    struct LLMConfig {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        bool verbose = false;
    } llm;
};

// ============================================================================
// Cross-Encoder Interface
// ============================================================================

/**
 * Interface for cross-encoder models
 */
class ICrossEncoder {
public:
    virtual ~ICrossEncoder() = default;
    
    /**
     * Score a single query-document pair
     * @param query The search query
     * @param document The document/chunk to score
     * @return Relevance score (0-1)
     */
    virtual float score(const std::string& query, const std::string& document) = 0;
    
    /**
     * Score multiple query-document pairs
     * @param query The search query
     * @param documents Vector of documents to score
     * @return Vector of scores (0-1) in same order as documents
     */
    virtual std::vector<float> score_batch(
        const std::string& query, 
        const std::vector<std::string>& documents
    ) = 0;
    
    /**
     * Check if initialized
     */
    virtual bool is_initialized() const = 0;
};

// ============================================================================
// Cross-Encoder Reranker
// ============================================================================

/**
 * Cross-encoder based reranker
 * 
 * Re-ranks search results using an LLM to score query-document relevance.
 * 
 * Usage:
 *   CrossEncoderReranker reranker(config);
 *   auto results = reranker.rerank(query, initial_results);
 */
class CrossEncoderReranker {
public:
    /**
     * Create a reranker
     * @param config Reranking configuration
     */
    explicit CrossEncoderReranker(const RerankConfig& config);
    
    /**
     * Destructor
     */
    ~CrossEncoderReranker();
    
    // Non-copyable
    CrossEncoderReranker(const CrossEncoderReranker&) = delete;
    CrossEncoderReranker& operator=(const CrossEncoderReranker&) = delete;
    
    /**
     * Check if reranker is initialized
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * Rerank search results
     * @param query The original search query
     * @param results Results from initial retrieval (vector search)
     * @return Reranked results sorted by final_score descending
     */
    std::vector<RerankedResult> rerank(
        const std::string& query,
        const std::vector<RerankedResult>& results
    );
    
    /**
     * Rerank with custom scoring function
     * @param query The original search query
     * @param results Results from initial retrieval
     * @param scorer Custom scoring function (query, doc) -> score
     * @return Reranked results
     */
    std::vector<RerankedResult> rerank_with_custom_scorer(
        const std::string& query,
        const std::vector<RerankedResult>& results,
        std::function<float(const std::string&, const std::string&)> scorer
    );
    
    /**
     * Get configuration
     */
    const RerankConfig& get_config() const { return config_; }
    
    /**
     * Get statistics
     */
    struct Stats {
        size_t total_reranked = 0;
        size_t total_documents = 0;
        double total_time_ms = 0;
    };
    const Stats& get_stats() const { return stats_; }

private:
    RerankConfig config_;
    bool initialized_ = false;
    Stats stats_;
    
    // LLM backend for scoring (optional - can use custom scorer)
    struct LlamaState;
    std::unique_ptr<LlamaState> llama_state_;
    
    void initialize_llm();
    std::string create_scoring_prompt(const std::string& query, const std::string& document);
    float parse_score_response(const std::string& response);
    std::vector<float> score_batch_llm(const std::string& query, const std::vector<std::string>& docs);
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Create default reranking configuration
 */
RerankConfig default_rerank_config();

/**
 * Create configuration for high-precision reranking
 * (More weight on cross-encoder, fewer results)
 */
RerankConfig high_precision_rerank_config();

/**
 * Create configuration for fast reranking
 * (Less documents to re-rank)
 */
RerankConfig fast_rerank_config();

} // namespace apollocore_v2
