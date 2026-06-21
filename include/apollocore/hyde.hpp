#pragma once

/**
 * ApolloCore - HyDE Query Enhancement Module
 * 
 * HyDE (Hypothetical Document Embeddings) improves query-document matching by:
 * 1. LLM generates a hypothetical document that answers the query
 * 2. Embed the hypothetical document
 * 3. Use the embedding to find similar real documents
 * 
 * Reference: Gao, L., Ma, X., Lin, J., & Callan, J. (2022). 
 * "Precise Zero-Shot Dense Retrieval without Relevance Labels."
 * https://arxiv.org/abs/2212.10496
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace apollocore_v2 {

// ============================================================================
// HyDE Configuration
// ============================================================================

/**
 * Configuration for HyDE query enhancement
 */
struct HydeConfig {
    // Number of hypothetical documents to generate
    size_t num_generations = 3;
    
    // Temperature for LLM generation (higher = more diverse)
    float temperature = 0.7f;
    
    // Maximum tokens per generated document
    int max_tokens = 200;
    
    // Combine HyDE embedding with original query embedding
    // weight_hyde * hyde_embedding + weight_query * query_embedding
    float weight_hyde = 0.7f;
    float weight_query = 0.3f;
    
    // Enable verbose logging
    bool verbose = false;
    
    // LLM settings for generating hypothetical documents
    struct LLMConfig {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        bool verbose = false;
    } llm;
    
    // Embedding model settings (reuse from EmbeddingModel)
    struct EmbeddingConfig {
        std::string model_path;  // Same as embedding model
        int dimensions = 768;
    } embedding;
};

// ============================================================================
// Generated Document
// ============================================================================

/**
 * A hypothetical document generated for HyDE
 */
struct HypotheticalDocument {
    std::string content;      // The generated text
    std::vector<float> embedding;  // Embedding of the content
    float quality_score;     // Self-assessed quality (0-1)
    
    HypotheticalDocument() : quality_score(0.0f) {}
    HypotheticalDocument(const std::string& c, const std::vector<float>& e, float q = 0.5f)
        : content(c), embedding(e), quality_score(q) {}
};

/**
 * Result from HyDE query enhancement
 */
struct HydeResult {
    std::vector<HypotheticalDocument> hypothetical_docs;
    std::vector<float> combined_embedding;
    std::string original_query;
    
    size_t num_docs() const { return hypothetical_docs.size(); }
};

// ============================================================================
// HyDE Query Enhancer
// ============================================================================

/**
 * HyDE Query Enhancer
 * 
 * Generates hypothetical documents from queries and combines their embeddings
 * with the original query embedding for improved retrieval.
 * 
 * Usage:
 *   HydeEnhancer enhancer(config);
 *   auto result = enhancer.enhance("What is quantum mechanics?");
 *   // result.combined_embedding can be used for similarity search
 */
class HydeEnhancer {
public:
    /**
     * Create a HyDE enhancer
     * @param config HyDE configuration
     */
    explicit HydeEnhancer(const HydeConfig& config);
    
    /**
     * Destructor
     */
    ~HydeEnhancer();
    
    // Non-copyable
    HydeEnhancer(const HydeEnhancer&) = delete;
    HydeEnhancer& operator=(const HydeEnhancer&) = delete;
    
    /**
     * Check if enhancer is initialized
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * Enhance a query using HyDE
     * @param query The original search query
     * @return HyDE result with hypothetical docs and combined embedding
     */
    HydeResult enhance(const std::string& query);
    
    /**
     * Enhance multiple queries (batch processing)
     * @param queries Vector of queries
     * @return Vector of HyDE results
     */
    std::vector<HydeResult> enhance_batch(const std::vector<std::string>& queries);
    
    /**
     * Get configuration
     */
    const HydeConfig& get_config() const { return config_; }
    
    /**
     * Get statistics
     */
    struct Stats {
        size_t total_enhanced = 0;
        size_t total_docs_generated = 0;
        double total_time_ms = 0;
    };
    const Stats& get_stats() const { return stats_; }

private:
    HydeConfig config_;
    bool initialized_ = false;
    Stats stats_;
    
    // LLM state for generating hypothetical documents
    struct LlamaState;
    std::unique_ptr<LlamaState> llm_state_;
    
    void initialize();
    std::string generate_hypothetical_doc(const std::string& query);
    float assess_quality(const std::string& query, const std::string& doc);
    
    std::vector<HypotheticalDocument> generate_docs(
        const std::string& query, 
        size_t count
    );
    
    std::vector<float> combine_embeddings(
        const std::vector<HypotheticalDocument>& docs,
        const std::vector<float>& query_embedding
    );
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Create default HyDE configuration
 */
HydeConfig default_hyde_config();

/**
 * Create configuration for diverse generations
 * (More variations, good for complex queries)
 */
HydeConfig diverse_hyde_config();

/**
 * Create configuration for fast HyDE
 * (Fewer generations, quicker response)
 */
HydeConfig fast_hyde_config();

} // namespace apollocore_v2
