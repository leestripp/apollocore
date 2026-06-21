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
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
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
 * This class provides persistence methods to ApolloCore.
 * It can be used in two ways:
 * 1. Composition: via ApolloCoreWithPersistence wrapper
 * 2. Direct: by adding methods to ApolloCore (modifies header)
 * 
 * We use composition for clean separation.
 */
class ApolloCoreWithPersistence {
public:
    ApolloCoreWithPersistence();
    ~ApolloCoreWithPersistence();
    
    // Non-copyable
    ApolloCoreWithPersistence(const ApolloCoreWithPersistence&) = delete;
    ApolloCoreWithPersistence& operator=(const ApolloCoreWithPersistence&) = delete;
    
    // ========================================================================
    // Model management
    // ========================================================================
    bool load_embedding_model(const std::string& path, bool use_gpu = false);
    
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
    // Statistics
    // ========================================================================
    
    struct Stats {
        size_t document_count = 0;
        size_t chunk_count = 0;
        size_t entity_count = 0;
        size_t relationship_count = 0;
        size_t embedding_files_size = 0;
    };
    
    Stats get_stats();
    
private:
    std::unique_ptr<class Embedder> embedder_;
    std::unique_ptr<KnowledgeBasePersistence> kb_persistence_;
    std::vector<std::pair<std::string, std::string>> documents_;  // id, content
    std::vector<std::pair<std::string, std::vector<float>>> chunks_;  // id, embedding
    bool is_open_ = false;
};

} // namespace apollocore_v2
