#pragma once

/**
 * ApolloCore - Contextual Retrieval Module
 * 
 * Improves retrieval quality by adding contextual information to chunks.
 * 
 * The Problem:
 * - Small chunks lack context (e.g., "The theory was published" - theory of what?)
 * - Embeddings may not capture the full meaning without surrounding context
 * 
 * The Solution:
 * - Add contextual information from surrounding chunks
 * - Include document/section metadata
 * - Use LLM to generate contextual summaries
 * 
 * Reference: "Contextual Retrieval" from Anthropic's research on RAG improvement.
 */

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace apollocore_v2 {

// ============================================================================
// Chunk Types
// ============================================================================

/**
 * Represents a chunk with contextual information
 */
struct Chunk {
    std::string id;              // Unique chunk identifier
    std::string content;         // Original chunk text
    std::string context;         // Added contextual information
    std::string contextualized;  // content + context combined
    std::vector<float> embedding;    // Embedding of contextualized content
    std::vector<float> embedding_raw; // Embedding of raw content (optional)
    
    // Metadata
    std::string document_id;     // Parent document ID
    size_t position = 0;         // Position in document
    size_t start_token = 0;      // Token offset in document
    size_t end_token = 0;       // End token offset
    
    Chunk() = default;
    Chunk(const std::string& i, const std::string& c) : id(i), content(c), contextualized(c) {}
};

/**
 * Document with metadata for contextual retrieval
 */
struct Document {
    std::string id;
    std::string title;           // Document title
    std::string content;         // Full document text
    std::string source;          // Source/URL if applicable
    std::string document_type;   // Article, paper, book, etc.
    std::unordered_map<std::string, std::string> metadata;  // Custom metadata
    
    std::vector<Chunk> chunks;
    
    Document() = default;
    Document(const std::string& i, const std::string& t, const std::string& c) 
        : id(i), title(t), content(c) {}
};

// ============================================================================
// Contextual Retrieval Configuration
// ============================================================================

/**
 * Configuration for contextual retrieval
 */
struct ContextualRetrievalConfig {
    // Context window settings
    size_t context_before = 1;   // Number of chunks to include before
    size_t context_after = 1;     // Number of chunks to include after
    
    // Context generation settings
    bool use_llm_context = false;    // Generate context with LLM
    size_t max_context_tokens = 200; // Max tokens for LLM-generated context
    
    // Chunking settings
    size_t chunk_size = 512;         // Target chunk size in tokens
    size_t chunk_overlap = 50;       // Overlap between chunks
    
    // Verbose logging
    bool verbose = false;
    
    // LLM settings for context generation
    struct LLMConfig {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        bool verbose = false;
    } llm;
};

// ============================================================================
// Context Generator
// ============================================================================

/**
 * Generates contextual information for chunks
 */
class ContextGenerator {
public:
    /**
     * Create a context generator
     */
    explicit ContextGenerator(const ContextualRetrievalConfig& config);
    
    /**
     * Destructor
     */
    ~ContextGenerator();
    
    // Non-copyable
    ContextGenerator(const ContextGenerator&) = delete;
    ContextGenerator& operator=(const ContextGenerator&) = delete;
    
    /**
     * Check if initialized
     */
    bool is_initialized() const { return initialized_; }
    
    // ========================================================================
    // Chunk Processing
    // ========================================================================
    
    /**
     * Process a document and create contextualized chunks
     * @param doc Document to process
     * @return Vector of chunks with context
     */
    std::vector<Chunk> process_document(const Document& doc);
    
    /**
     * Add context to a single chunk using surrounding chunks
     * @param chunk The chunk to contextualize
     * @param before Vector of chunks before this one
     * @param after Vector of chunks after this one
     * @return Chunk with context added
     */
    Chunk contextualize_chunk(const Chunk& chunk, 
                              const std::vector<Chunk>& before,
                              const std::vector<Chunk>& after);
    
    /**
     * Generate LLM-based context for a chunk
     * @param chunk The chunk to generate context for
     * @param before_chunks Context before
     * @param after_chunks Context after
     * @return Generated context string
     */
    std::string generate_llm_context(const Chunk& chunk,
                                   const std::vector<Chunk>& before_chunks,
                                   const std::vector<Chunk>& after_chunks);
    
    // ========================================================================
    // Utility Methods
    // ========================================================================
    
    /**
     * Split document into raw chunks (without context)
     * @param doc Document to split
     * @return Vector of raw chunks
     */
    std::vector<Chunk> split_into_chunks(const Document& doc);
    
    /**
     * Add document-level context to all chunks
     * @param chunks Chunks to add document context to
     * @param doc Parent document
     */
    void add_document_context(std::vector<Chunk>& chunks, const Document& doc);
    
    /**
     * Get configuration
     */
    const ContextualRetrievalConfig& get_config() const { return config_; }

private:
    ContextualRetrievalConfig config_;
    bool initialized_ = false;
    
    struct LlamaState;
    std::unique_ptr<LlamaState> llm_state_;
    
    void initialize();
    std::string create_contextualization_prompt(
        const std::string& chunk,
        const std::string& before_context,
        const std::string& after_context,
        const std::string& document_title
    );
};

// ============================================================================
// Contextual Retrieval Result
// ============================================================================

/**
 * Result from contextual retrieval
 */
struct ContextualRetrievalResult {
    std::vector<Chunk> chunks;           // Retrieved chunks with context
    std::vector<std::string> query_contexts;  // Query with added context
    double retrieval_time_ms = 0;
    double context_gen_time_ms = 0;
    
    size_t total_chunks() const { return chunks.size(); }
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Create default contextual retrieval configuration
 */
ContextualRetrievalConfig default_contextual_config();

/**
 * Create configuration for high-quality context
 * (More context chunks, LLM-generated context)
 */
ContextualRetrievalConfig high_quality_contextual_config();

/**
 * Create configuration for fast processing
 * (Less context, no LLM)
 */
ContextualRetrievalConfig fast_contextual_config();

/**
 * Estimate token count (rough approximation)
 */
size_t estimate_tokens(const std::string& text);

/**
 * Truncate text to fit within token limit
 */
std::string truncate_to_tokens(const std::string& text, size_t max_tokens);

} // namespace apollocore_v2
