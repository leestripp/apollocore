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
 */
#pragma once

#include "apollocore/kb_persistence.hpp"
#include "apollocore/fbin.hpp"
#include "apollocore/entity_extractor.hpp"
#include "apollocore/apollo_hybrid_search.hpp"  // M2.7: vector + BM25 + graph
#include "apollocore/cross_encoder.hpp"        // M2.8: LLM-based reranking
#include <nlohmann/json.hpp>
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
     * Add a document with a custom ID (e.g., file path for codebases).
     * If custom_doc_id is empty, generates an auto ID like doc_N.
     *
     * @param text The document text
     * @param custom_doc_id The ID to use (empty = auto-generate)
     * @param chunk_size Target chunk size in characters
     * @return Number of chunks created
     */
    size_t add_document(const std::string& text, const std::string& custom_doc_id,
                        size_t chunk_size = 512);

    /**
     * Delete a document by its ID. Removes the document node, all its chunks,
     * all entities extracted from those chunks, and all relationships involving
     * those entities. Also removes .fbin embedding files for the chunks.
     *
     * @param document_id The document/node ID returned by add_document()
     * @return true if deleted, false if not found or KB not open
     */
    bool delete_document(const std::string& document_id);

    /**
     * Manually add an entity to the KB. Used for populating code-aware
     * entities (functions, classes) when the LLM extractor isn't sufficient.
     *
     * @param name Entity name (e.g. "authenticate", "Session")
     * @param type_str Entity type as string (e.g. "FUNCTION", "CLASS")
     * @param chunk_id Source chunk ID (where this entity was found)
     * @param description Brief description (optional)
     * @param confidence 0-1 (default 1.0 for manually added)
     * @return entity ID, or empty string on failure
     */
    std::string add_entity(
        const std::string& name,
        const std::string& type_str,
        const std::string& chunk_id,
        const std::string& description = "",
        float confidence = 1.0f);

    /**
     * Manually add a relationship between two entities.
     *
     * @param source Source entity name (must exist as entity)
     * @param target Target entity name (must exist as entity)
     * @param type_str Relationship type as string (e.g. "CALLS", "INHERITS")
     * @param description Brief description (optional)
     * @param confidence 0-1 (default 1.0)
     * @return relationship ID, or empty string on failure
     */
    std::string add_relationship(
        const std::string& source,
        const std::string& target,
        const std::string& type_str,
        const std::string& description = "",
        float confidence = 1.0f);

    /**
     * Query the knowledge base
     */
    std::vector<std::pair<std::string, float>> query(
        const std::string& query_text,
        size_t top_k = 5
    );

    // ========================================================================
    // Hybrid Search (vector + BM25 + graph) — Phase M2.7
    // ========================================================================

    /**
     * Hybrid search: combines vector similarity + BM25 keyword match + graph
     * expansion (entities matching query terms) using weighted-sum fusion.
     *
     * Unlike `query()` which only does vector similarity, this searches all
     * three sources simultaneously and combines the scores.
     *
     * @param query_text Natural language query
     * @param top_k Number of results to return
     * @param alpha Weight for vector similarity (default 0.5)
     * @param beta  Weight for BM25 keyword score (default 0.3)
     * @param gamma Weight for graph/entity boost (default 0.2)
     * @return Ranked list of hybrid results with per-source scores
     */
    std::vector<search::HybridResult> query_hybrid(
        const std::string& query_text,
        size_t top_k = 5,
        double alpha = 0.5,
        double beta = 0.3,
        double gamma = 0.2);

    // ========================================================================
    // Cross-Encoder Reranking (vector + LLM scoring) — Phase M2.8
    // ========================================================================

    /**
     * Query with cross-encoder reranking: does vector search then re-scores
     * top results with the LLM (cross-encoder style).
     *
     * Requires an LLM model to be loaded via `load_llm_model()`.
     * Without an LLM, this falls back to plain vector search.
     *
     * @param query_text Natural language query
     * @param top_k Number of results to return after reranking
     * @param candidate_k Number of candidates to retrieve before reranking
     *                     (default: top_k * 4 for good recall)
     * @return Reranked results with original + rerank + final scores
     */
    std::vector<RerankedResult> query_with_rerank(
        const std::string& query_text,
        size_t top_k = 5,
        size_t candidate_k = 0  // 0 = auto (4 * top_k)
    );

    /**
     * Full pipeline: hybrid search → cross-encoder rerank.
     * Best quality, slowest (requires LLM for rerank step).
     */
    std::vector<RerankedResult> query_full(
        const std::string& query_text,
        size_t top_k = 5);

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
        bool skip_already_extracted = true,
        size_t limit = 0           // 0 = no limit; >0 = stop after this many chunks
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
     * Get all chunks in the current KB as (id, content) pairs.
     * Used by hybrid search to build BM25 index.
     */
    std::vector<std::pair<std::string, std::string>> get_all_chunks();

    /**
     * Get all documents (nodes) in the current KB as (id, content) pairs.
     */
    std::vector<std::pair<std::string, std::string>> get_all_documents();

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

    // ========================================================================
    // Project Scoping (Phase M3.C — multi-agent support)
    // ========================================================================
    // Each ApolloCore instance operates in a "project context". Writes and
    // deletes are scoped to this project. Reads can optionally include
    // data from projects marked as "shared" by this project.
    //
    // Default project name is "default" (backwards-compatible with v1.x).

    /**
     * Set the current project context. Must be called after open() or create().
     * @param project_name Name of the project (must exist or be created via register_project)
     * @return true if project exists and was set
     */
    bool set_current_project(const std::string& project_name);

    /**
     * Get the current project name.
     */
    std::string current_project() const { return current_project_; }

    /**
     * Register a new project. Called automatically by create() with name=default.
     * @param name Unique project name (alphanumeric + _ -)
     * @param description Optional human description
     * @param is_shared Whether other projects can read this project's data
     * @param owner Optional owner name (agent name, user, etc.)
     * @return true on success
     */
    bool register_project(const std::string& name,
                          const std::string& description = "",
                          bool is_shared = false,
                          const std::string& owner = "");

    /**
     * Mark a project as shared (other projects can read it).
     */
    bool set_project_shared(const std::string& name, bool is_shared);

    /**
     * Mark another project as a read source for the current project.
     * (Other projects' shared data becomes readable from current project.)
     */
    bool add_shared_read_source(const std::string& project_name);

    /**
     * Remove a shared read source.
     */
    bool remove_shared_read_source(const std::string& project_name);

    /**
     * List all registered projects (in current KB).
     * @return JSON array: [{name, description, path, is_shared, owner, created_at}]
     */
    nlohmann::json list_projects() const;

    /**
     * Get info about a specific project.
     * @return JSON object or null if not found
     */
    nlohmann::json get_project_info(const std::string& name) const;

    /**
     * Delete a project (removes all its data). Cannot delete current project.
     * Use set_current_project() to switch first, then delete.
     * @param name Project name
     * @param confirm Must be true (safety check)
     * @return true on success
     */
    bool delete_project(const std::string& name, bool confirm);

    /**
     * Get the set of project names visible to the current project (own + shared sources).
     */
    std::vector<std::string> visible_projects() const;

private:
    std::unique_ptr<class Embedder> embedder_;
    std::unique_ptr<EntityExtractor> entity_extractor_;  // For entity extraction
    std::unique_ptr<KnowledgeBasePersistence> kb_persistence_;
    std::vector<std::pair<std::string, std::string>> documents_;  // id, content
    // Cached chunks for vector search: (chunk_id, embedding, project_id)
    struct CachedChunk {
        std::string id;
        std::vector<float> embedding;
        std::string project_id;
    };
    std::vector<CachedChunk> chunks_;
    std::string llm_model_path_;  // Path to loaded LLM model
    bool is_open_ = false;

    // Project scoping (Phase M3.C)
    std::string current_project_ = "default";
    std::vector<std::string> shared_read_sources_;  // project names
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
