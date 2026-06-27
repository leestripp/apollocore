#pragma once

/**
 * ApolloCore - Persistence API
 * 
 * High-level save/load interface for ApolloCore.
 * 
 * Combines SQLite (metadata) + FBIN (embeddings) into a complete
 * persistence layer.
 * 
 * Usage:
 *   ApolloCore kb;
 *   kb.load_embedding_model("models/embeddings.gguf");
 *   
 *   // Build knowledge base
 *   auto doc = kb.add_document("...", root);
 *   kb.query("search query");
 *   
 *   // Persist
 *   kb.save("data/my_kb");
 *   
 *   // Or open existing
 *   ApolloCore kb2;
 *   kb2.load_embedding_model("models/embeddings.gguf");
 *   kb2.open("data/my_kb");
 */

#include "apollocore/persistence.hpp"
#include "apollocore/fbin.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace apollocore_v2 {

// ============================================================================
// Knowledge Base Persistence
// ============================================================================

class KnowledgeBasePersistence {
public:
    KnowledgeBasePersistence();
    ~KnowledgeBasePersistence();
    
    // Non-copyable
    KnowledgeBasePersistence(const KnowledgeBasePersistence&) = delete;
    KnowledgeBasePersistence& operator=(const KnowledgeBasePersistence&) = delete;
    
    /**
     * Check if a knowledge base exists at the given path
     */
    static bool exists(const std::string& path);
    
    /**
     * Create a new knowledge base (fails if exists)
     * @param path Directory to create KB in
     * @param name Display name
     * @return true on success
     */
    bool create(const std::string& path, const std::string& name);
    
    /**
     * Open an existing knowledge base
     * @param path Directory containing the KB
     * @return true on success
     */
    bool open(const std::string& path);
    
    /**
     * Save the current state to disk
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
     * Clear disk data only (keeps in-memory)
     */
    void clear_disk();
    
    /**
     * Delete the entire knowledge base (memory + disk)
     */
    bool destroy();
    
    /**
     * Get current path
     */
    const std::string& get_path() const { return path_; }
    
    /**
     * Get knowledge base name
     */
    std::string get_name();
    
    /**
     * Check if open
     */
    bool is_open() const { return db_.is_open(); }
    
    // ========================================================================
    // Data Operations (called by ApolloCore)
    // ========================================================================
    
    /**
     * Save a node
     */
    bool save_node(const std::string& id, 
                   const std::string& title,
                   const std::string& content,
                   const std::string& parent_id,
                   const std::string& document_type = "",
                   const std::string& metadata_json = "");
    
    /**
     * Save a chunk
     */
    bool save_chunk(const std::string& id,
                    const std::string& node_id,
                    const std::string& content,
                    size_t position,
                    const std::vector<float>& embedding);
    
    /**
     * Save an entity
     */
    bool save_entity(const std::string& id,
                     const std::string& chunk_id,
                     const std::string& name,
                     const std::string& type,
                     const std::string& description,
                     float confidence);
    
    /**
     * Save a relationship
     */
    bool save_relationship(const std::string& id,
                          const std::string& source_id,
                          const std::string& target_id,
                          const std::string& type,
                          const std::string& description,
                          float confidence);
    
    // ========================================================================
    // Bulk Operations (for efficiency)
    // ========================================================================
    
    /**
     * Begin a bulk save transaction
     */
    bool begin_save();
    
    /**
     * End a bulk save transaction (commit)
     */
    bool end_save();
    
    // ========================================================================
    // Load Operations
    // ========================================================================
    
    struct LoadedNode {
        std::string id;
        std::string title;
        std::string content;
        std::string parent_id;
        std::string document_type;
        std::string metadata;
    };
    
    struct LoadedChunk {
        std::string id;
        std::string node_id;
        std::string content;
        size_t position;
        std::vector<float> embedding;
    };
    
    struct LoadedEntity {
        std::string id;
        std::string chunk_id;
        std::string name;
        std::string type;
        std::string description;
        float confidence;
    };
    
    struct LoadedRelationship {
        std::string id;
        std::string source_id;
        std::string target_id;
        std::string type;
        std::string description;
        float confidence;
    };
    
    /**
     * Load all nodes
     */
    std::vector<LoadedNode> load_all_nodes();
    
    /**
     * Load all chunks
     */
    std::vector<LoadedChunk> load_all_chunks();
    
    /**
     * Load all entities
     */
    std::vector<LoadedEntity> load_all_entities();
    
    /**
     * Load all relationships
     */
    std::vector<LoadedRelationship> load_all_relationships();
    
    /**
     * Load chunk embedding
     */
    std::vector<float> load_chunk_embedding(const std::string& chunk_id);

    /**
     * Delete a document and all associated data (chunks, entities, relationships).
     * Removes .fbin embedding files for the chunks.
     *
     * @param document_id The document/node ID to delete
     * @return true if deleted, false on error or not found
     */
    bool delete_document(const std::string& document_id);

    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct DiskStats {
        size_t total_nodes = 0;
        size_t total_chunks = 0;
        size_t total_entities = 0;
        size_t total_relationships = 0;
        size_t embedding_files_size = 0;
    };
    
    DiskStats get_disk_stats();
    
private:
    std::string path_;
    SQLiteDB db_;
    bool in_transaction_ = false;
    
    // Helper functions
    std::string get_db_path() const;
    std::string get_embeddings_dir() const;
    std::string get_chunk_embedding_path(const std::string& chunk_id) const;
    bool create_directory_structure();
    bool load_config();
};

} // namespace apollocore_v2
