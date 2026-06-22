/**
 * ApolloCore - Persistence Integration
 * 
 * Adds save/load capabilities to the main ApolloCore class.
 * 
 * This file extends the ApolloCore class with persistence methods.
 * 
 * Usage:
 *   ApolloCore kb;
 *   kb.load_embedding_model("models/embeddings.gguf");
 *   
 *   // Option 1: Create new knowledge base
 *   kb.create("data/my_kb", "My Knowledge Base");
 *   kb.add_document("...", kb.get_root());
 *   // Auto-persists on add_document
 *   
 *   // Option 2: Open existing
 *   kb.open("data/my_kb");
 *   // All chunks/embeddings/entities/relationships are loaded
 *   
 *   // Query (works on loaded data)
 *   auto results = kb.query("search");
 *   
 *   // Close
 *   kb.close();
 *   kb.destroy();  // delete the KB
 */

#include "apollocore/kb_persistence.hpp"
#include "apollocore/fbin.hpp"
#include "apollocore/entity_extractor.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace apollocore_v2 {

// ============================================================================
// ApolloCore Persistence Extension
// ============================================================================

// Forward declarations for internal classes
class SimpleEmbedder;
struct StoredChunk;
struct StoredDocument;

/**
 * This class is the unified ApolloCore entry point.
 * It can be used in two ways:
 * 1. Composition: via ApolloCoreWithPersistence wrapper
 * 2. Direct: by adding methods to ApolloCore (modifies header)
 *
 * We use composition for clean separation.
 */
class ApolloCore {
public:
    ApolloCore();
    ~ApolloCore();
    
    // Non-copyable
    ApolloCore(const ApolloCore&) = delete;
    ApolloCore& operator=(const ApolloCore&) = delete;
    
    // ========================================================================
    // Model management
    // ========================================================================
    bool load_embedding_model(const std::string& path, bool use_gpu = false);
    
    /**
     * Load the LLM model for entity extraction (e.g. Qwen2.5).
     * Required for extract_entities().
     * @param path Path to the GGUF model file
     * @param use_gpu Whether to use GPU layers
     * @return true on success
     */
    bool load_llm_model(const std::string& path, bool use_gpu = false);
    
    /**
     * Check if the LLM model is loaded
     */
    bool is_llm_loaded() const;
    
    // ========================================================================
    // Knowledge base management
    // ========================================================================
    
    /**
     * Check if a knowledge base exists at path
     */
    static bool kb_exists(const std::string& path);
    
    /**
     * Create a new knowledge base
     */
    bool create(const std::string& path, const std::string& name = "Untitled");
    
    /**
     * Open an existing knowledge base
     */
    bool open(const std::string& path);
    
    /**
     * Save all in-memory changes to disk
     */
    bool save();
    
    /**
     * Close the current knowledge base
     */
    void close();
    
    /**
     * Clear all data (memory + disk)
     */
    void clear();
    
    /**
     * Delete the entire knowledge base
     */
    bool destroy();
    
    /**
     * Get current knowledge base path
     */
    std::string get_kb_path() const;
    
    /**
     * Get knowledge base name
     */
    std::string get_kb_name() const;
    
    /**
     * Check if a knowledge base is open
     */
    bool is_open() const { return is_open_; }
    
    // ========================================================================
    // Data operations (auto-persist when KB is open)
    // ========================================================================
    
    /**
     * Add a document - automatically saves to disk if KB is open
     * @param text The document text
     * @param chunk_size Target chunk size in characters
     * @return Number of chunks created
     */
    size_t add_document(const std::string& text, size_t chunk_size = 512);
    
    /**
     * Query the knowledge base
     */
    std::vector<std::pair<std::string, float>> query(
        const std::string& query_text, 
        size_t top_k = 5
    );
    
    // ========================================================================
    // Entity extraction (requires LLM model)
    // ========================================================================
    
    /**
     * Extract entities and relationships from all chunks using the LLM.
     * Uses the loaded LLM model (Qwen2.5 or similar).
     * 
     * This is explicit (not automatic on add_document) because LLM calls
     * are slow. Call once after adding a batch of documents.
     * 
     * @param min_confidence Minimum confidence threshold (0-1)
     * @param skip_already_extracted If true, skip chunks already processed
     * @return Total number of entities extracted
     */
    size_t extract_entities(
        float min_confidence = 0.5f,
        bool skip_already_extracted = true
    );
    
    /**
     * Extract entities from a single chunk by ID.
     * @param chunk_id The chunk ID to process
     * @return Number of entities extracted from this chunk
     */
    size_t extract_entities_for_chunk(
        const std::string& chunk_id,
        float min_confidence = 0.5f
    );
    
    /**
     * Get all entities from the knowledge base.
     * @param limit Max entities to return (0 = all)
     */
    std::vector<Entity> get_entities(size_t limit = 0);
    
    /**
     * Get entities extracted from a specific chunk.
     */
    std::vector<Entity> get_entities_for_chunk(const std::string& chunk_id);
    
    /**
     * Get all relationships from the knowledge base.
     * @param limit Max relationships to return (0 = all)
     */
    std::vector<Relationship> get_relationships(size_t limit = 0);
    
    /**
     * Get the underlying EntityExtractor (advanced use).
     */
    EntityExtractor* get_entity_extractor();
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct Stats {
        size_t document_count = 0;
        size_t chunk_count = 0;
        size_t entity_count = 0;
        size_t relationship_count = 0;
        size_t embedding_files_size = 0;
        size_t chunks_with_entities = 0;  // How many chunks have been entity-extracted
    };
    
    Stats get_stats();
    
private:
    std::unique_ptr<class Embedder> embedder_;
    std::unique_ptr<EntityExtractor> entity_extractor_;  // For entity extraction
    std::unique_ptr<KnowledgeBasePersistence> kb_persistence_;
    std::vector<std::pair<std::string, std::string>> documents_;  // id, content
    std::vector<std::pair<std::string, std::vector<float>>> chunks_;  // id, embedding
    std::string llm_model_path_;  // Path to loaded LLM model
    bool is_open_ = false;
};

// ============================================================================
// Backward Compatibility Alias
// ============================================================================

/**
 * @deprecated Use ApolloCore directly. This alias is kept for
 * backward compatibility with code that used the old name.
 */
using ApolloCoreWithPersistence = ApolloCore;

} // namespace apollocore_v2
