#pragma once

/**
 * ApolloCore - Entity Extraction Module
 * 
 * LLM-based entity and relationship extraction for Knowledge Graph population.
 * 
 * Architecture:
 * - EntityExtractor: Main class for extraction
 * - Uses structured prompting for entity/relationship extraction
 * - Ready for integration with any LLM backend (llama.cpp, OpenAI API, etc.)
 * 
 * Usage:
 *   ExtractionConfig config;
 *   config.llm.model_path = "models/llama-3.2-3b-q4_K_M.gguf";
 *   EntityExtractor extractor(config);
 *   
 *   auto result = extractor.extract("Albert Einstein worked at Princeton...");
 *   // result.entities, result.relationships
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <memory>
#include <functional>

namespace apollocore_v2 {

// ============================================================================
// Entity Types
// ============================================================================

enum class EntityType {
    PERSON,               // Named individuals
    ORGANIZATION,         // Companies, universities, governments, teams
    LOCATION,             // Cities, countries, buildings, geographic features
    EVENT,                // Conferences, wars, festivals, accidents
    CONCEPT,              // Theories, ideas, technologies, methodologies
    PRODUCT,              // Software, hardware, products, services
    DATE,                 // Specific dates, time periods
    NUMBER,               // Quantities, measurements, statistics
    DISEASE,              // Medical conditions
    DRUG,                 // Medications
    MEDICAL_PROCEDURE,     // Medical treatments
    SCIENTIFIC_TERM,       // Domain-specific scientific concepts
    UNKNOWN               // Unclassified entities
};

// Convert entity type to string
inline const char* entity_type_to_string(EntityType type) {
    switch (type) {
        case EntityType::PERSON: return "PERSON";
        case EntityType::ORGANIZATION: return "ORGANIZATION";
        case EntityType::LOCATION: return "LOCATION";
        case EntityType::EVENT: return "EVENT";
        case EntityType::CONCEPT: return "CONCEPT";
        case EntityType::PRODUCT: return "PRODUCT";
        case EntityType::DATE: return "DATE";
        case EntityType::NUMBER: return "NUMBER";
        case EntityType::DISEASE: return "DISEASE";
        case EntityType::DRUG: return "DRUG";
        case EntityType::MEDICAL_PROCEDURE: return "MEDICAL_PROCEDURE";
        case EntityType::SCIENTIFIC_TERM: return "SCIENTIFIC_TERM";
        default: return "UNKNOWN";
    }
}

// Parse entity type from string
inline EntityType string_to_entity_type(const std::string& str) {
    if (str == "PERSON") return EntityType::PERSON;
    if (str == "ORGANIZATION") return EntityType::ORGANIZATION;
    if (str == "LOCATION") return EntityType::LOCATION;
    if (str == "EVENT") return EntityType::EVENT;
    if (str == "CONCEPT") return EntityType::CONCEPT;
    if (str == "PRODUCT") return EntityType::PRODUCT;
    if (str == "DATE") return EntityType::DATE;
    if (str == "NUMBER") return EntityType::NUMBER;
    if (str == "DISEASE") return EntityType::DISEASE;
    if (str == "DRUG") return EntityType::DRUG;
    if (str == "MEDICAL_PROCEDURE") return EntityType::MEDICAL_PROCEDURE;
    if (str == "SCIENTIFIC_TERM") return EntityType::SCIENTIFIC_TERM;
    return EntityType::UNKNOWN;
}

// ============================================================================
// Relationship Types
// ============================================================================

enum class RelationType {
    FOUNDED,              // Person/Org establishment
    DEVELOPED,            // Created a product/theory
    PUBLISHED,            // Authored papers/books
    WORKED_AT,           // Employment relationship
    LOCATED_IN,          // Geographic containment
    RELATED_TO,          // General association
    CAUSED,              // Event causation
    PART_OF,             // Compositional relationship
    TREATS,              // Medical (drug→disease)
    DIAGNOSES,           // Medical (procedure→disease)
    EVALUATES,           // Research methodology
    USES,                // Technology adoption
    IMPLEMENTS,          // Software patterns
    DEPENDS_ON,          // Dependencies
    CAUSED_BY,           // Reverse causation
    SUBTYPE_OF,          // Taxonomic relationship
    SUPERTYPE_OF,        // Taxonomic relationship
    UNKNOWN              // Unclassified
};

// Convert relation type to string
inline const char* relation_type_to_string(RelationType type) {
    switch (type) {
        case RelationType::FOUNDED: return "FOUNDED";
        case RelationType::DEVELOPED: return "DEVELOPED";
        case RelationType::PUBLISHED: return "PUBLISHED";
        case RelationType::WORKED_AT: return "WORKED_AT";
        case RelationType::LOCATED_IN: return "LOCATED_IN";
        case RelationType::RELATED_TO: return "RELATED_TO";
        case RelationType::CAUSED: return "CAUSED";
        case RelationType::PART_OF: return "PART_OF";
        case RelationType::TREATS: return "TREATS";
        case RelationType::DIAGNOSES: return "DIAGNOSES";
        case RelationType::EVALUATES: return "EVALUATES";
        case RelationType::USES: return "USES";
        case RelationType::IMPLEMENTS: return "IMPLEMENTS";
        case RelationType::DEPENDS_ON: return "DEPENDS_ON";
        case RelationType::CAUSED_BY: return "CAUSED_BY";
        case RelationType::SUBTYPE_OF: return "SUBTYPE_OF";
        case RelationType::SUPERTYPE_OF: return "SUPERTYPE_OF";
        default: return "UNKNOWN";
    }
}

// Parse relation type from string
inline RelationType string_to_relation_type(const std::string& str) {
    if (str == "FOUNDED") return RelationType::FOUNDED;
    if (str == "DEVELOPED") return RelationType::DEVELOPED;
    if (str == "PUBLISHED") return RelationType::PUBLISHED;
    if (str == "WORKED_AT") return RelationType::WORKED_AT;
    if (str == "LOCATED_IN") return RelationType::LOCATED_IN;
    if (str == "RELATED_TO") return RelationType::RELATED_TO;
    if (str == "CAUSED") return RelationType::CAUSED;
    if (str == "PART_OF") return RelationType::PART_OF;
    if (str == "TREATS") return RelationType::TREATS;
    if (str == "DIAGNOSES") return RelationType::DIAGNOSES;
    if (str == "EVALUATES") return RelationType::EVALUATES;
    if (str == "USES") return RelationType::USES;
    if (str == "IMPLEMENTS") return RelationType::IMPLEMENTS;
    if (str == "DEPENDS_ON") return RelationType::DEPENDS_ON;
    if (str == "CAUSED_BY") return RelationType::CAUSED_BY;
    if (str == "SUBTYPE_OF") return RelationType::SUBTYPE_OF;
    if (str == "SUPERTYPE_OF") return RelationType::SUPERTYPE_OF;
    return RelationType::UNKNOWN;
}

// ============================================================================
// Core Data Structures
// ============================================================================

/**
 * Represents an extracted entity from text
 */
struct Entity {
    std::string id;               // Unique identifier
    std::string name;             // Entity name (original text)
    EntityType type;              // Entity type
    std::string description;      // Brief description
    float confidence;             // Extraction confidence (0-1)
    std::vector<std::string> sources;  // Source chunk IDs
    
    Entity() : type(EntityType::UNKNOWN), confidence(0.0f) {}
    
    Entity(const std::string& n, EntityType t, const std::string& desc = "", float conf = 1.0f)
        : name(n), type(t), description(desc), confidence(conf) {}
    
    std::string to_string() const {
        return "Entity{" + name + ", " + std::string(entity_type_to_string(type)) + 
               ", conf=" + std::to_string(confidence) + "}";
    }
};

/**
 * Represents a relationship between two entities
 */
struct Relationship {
    std::string source;           // Source entity name
    std::string target;           // Target entity name
    RelationType type;            // Relationship type
    std::string description;     // Relationship description
    float weight;                 // Confidence/strength (0-1)
    
    Relationship() : type(RelationType::UNKNOWN), weight(0.0f) {}
    
    Relationship(const std::string& src, const std::string& tgt, RelationType t, 
                const std::string& desc = "", float w = 1.0f)
        : source(src), target(tgt), type(t), description(desc), weight(w) {}
    
    std::string to_string() const {
        return "Relationship{" + source + " --[" + std::string(relation_type_to_string(type)) + 
               "]--> " + target + " (w=" + std::to_string(weight) + ")}";
    }
};

// ============================================================================
// Extraction Result
// ============================================================================

/**
 * Result of entity extraction from a single text
 */
struct ExtractionResult {
    std::vector<Entity> entities;
    std::vector<Relationship> relationships;
    std::unordered_map<std::string, std::vector<std::string>> entity_sources; // entity_name -> chunk_ids
    
    void add_entity(const Entity& entity, const std::string& chunk_id = "") {
        entities.push_back(entity);
        if (!chunk_id.empty()) {
            entity_sources[entity.name].push_back(chunk_id);
        }
    }
    
    void add_relationship(const Relationship& rel) {
        relationships.push_back(rel);
    }
    
    size_t entity_count() const { return entities.size(); }
    size_t relationship_count() const { return relationships.size(); }
    
    // Filter by minimum confidence
    ExtractionResult filter_by_confidence(float min_confidence) const {
        ExtractionResult filtered;
        for (const auto& e : entities) {
            if (e.confidence >= min_confidence) {
                filtered.add_entity(e);
            }
        }
        for (const auto& r : relationships) {
            if (r.weight >= min_confidence) {
                filtered.add_relationship(r);
            }
        }
        return filtered;
    }
    
    std::string to_string() const {
        std::string result = "ExtractionResult{entities=" + std::to_string(entities.size()) +
                            ", relationships=" + std::to_string(relationships.size()) + "}\n";
        for (const auto& e : entities) {
            result += "  " + e.to_string() + "\n";
        }
        for (const auto& r : relationships) {
            result += "  " + r.to_string() + "\n";
        }
        return result;
    }
};

// ============================================================================
// Entity Extractor Configuration
// ============================================================================

struct ExtractionConfig {
    // Which entity types to extract
    std::unordered_set<EntityType> entity_types = {
        EntityType::PERSON,
        EntityType::ORGANIZATION,
        EntityType::LOCATION,
        EntityType::CONCEPT,
        EntityType::EVENT,
        EntityType::PRODUCT,
        EntityType::DATE,
        EntityType::SCIENTIFIC_TERM
    };
    
    // Which relation types to extract
    std::unordered_set<RelationType> relation_types = {
        RelationType::FOUNDED,
        RelationType::DEVELOPED,
        RelationType::PUBLISHED,
        RelationType::WORKED_AT,
        RelationType::LOCATED_IN,
        RelationType::RELATED_TO,
        RelationType::CAUSED,
        RelationType::PART_OF,
        RelationType::USES,
        RelationType::DEPENDS_ON
    };
    
    // Minimum confidence threshold
    float min_confidence = 0.5f;
    
    // Enable coreference resolution
    bool enable_coreference = true;
    
    // Maximum text length (tokens) - will chunk longer texts
    size_t max_text_length = 2048;
    
    // Batch size for processing
    size_t batch_size = 1;
    
    // LLM backend settings
    struct LLMConfig {
        enum class Backend {
            NONE,           // No LLM - use rule-based fallback
            LLAMA_CPP,      // Local llama.cpp
            OPENAI,         // OpenAI API
            ANTHROPIC       // Anthropic API
        };
        
        Backend backend = Backend::NONE;
        std::string model_path;   // For llama.cpp
        std::string api_key;      // For OpenAI/Anthropic
        std::string base_url;     // For API endpoints
        int n_ctx = 4096;
        int n_threads = 4;
        float temperature = 0.1f;  // Low temp for consistent extraction
        int max_tokens = 2048;
        bool verbose = false;
    } llm;
    
    // Callback for progress reporting
    std::function<void(const std::string&, float)> progress_callback;
};

// ============================================================================
// LLM Backend Interface
// ============================================================================

/**
 * Abstract interface for LLM backends
 * Implement this to add new LLM providers
 */
class ILLMBackend {
public:
    virtual ~ILLMBackend() = default;
    virtual bool initialize(const ExtractionConfig::LLMConfig& config) = 0;
    virtual std::string generate(const std::string& prompt) = 0;
    virtual bool is_initialized() const = 0;
};

// ============================================================================
// Entity Extractor Class
// ============================================================================

/**
 * LLM-based entity and relationship extractor
 * 
 * Uses few-shot prompting to extract structured entities and relationships
 * from unstructured text. Supports multiple LLM backends.
 * 
 * Usage:
 *   ExtractionConfig config;
 *   config.llm.backend = ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
 *   config.llm.model_path = "models/llama-3.2-3b-q4_K_M.gguf";
 *   EntityExtractor extractor(config);
 *   
 *   auto result = extractor.extract("Albert Einstein worked at Princeton...");
 */
class EntityExtractor {
public:
    /**
     * Create an entity extractor
     * @param config Configuration for extraction
     */
    explicit EntityExtractor(const ExtractionConfig& config);
    
    /**
     * Destructor
     */
    ~EntityExtractor();
    
    // Disable copying (has unique_ptr)
    EntityExtractor(const EntityExtractor&) = delete;
    EntityExtractor& operator=(const EntityExtractor&) = delete;
    
    // Allow moving
    EntityExtractor(EntityExtractor&&) noexcept;
    EntityExtractor& operator=(EntityExtractor&&) noexcept;
    
    /**
     * Check if extractor is initialized
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * Extract entities and relationships from text
     * @param text Input text to extract from
     * @param chunk_id Optional chunk ID for source tracking
     * @return Extraction result with entities and relationships
     */
    ExtractionResult extract(const std::string& text, const std::string& chunk_id = "");
    
    /**
     * Extract from multiple texts (batch processing)
     * @param texts Vector of texts to process
     * @param chunk_ids Optional chunk IDs
     * @return Vector of extraction results
     */
    std::vector<ExtractionResult> extract_batch(
        const std::vector<std::string>& texts,
        const std::vector<std::string>& chunk_ids = {}
    );
    
    /**
     * Update configuration
     */
    void set_config(const ExtractionConfig& config);
    
    /**
     * Get current configuration
     */
    const ExtractionConfig& get_config() const { return config_; }
    
    /**
     * Get extraction statistics
     */
    struct Stats {
        size_t total_extractions = 0;
        size_t total_entities = 0;
        size_t total_relationships = 0;
        double total_time_ms = 0;
    };
    const Stats& get_stats() const { return stats_; }
    
    /**
     * Generate unique entity ID
     */
    std::string generate_entity_id(const std::string& name);
    
    // =========================================================================
    // Public helper methods (for testing without LLM)
    // =========================================================================
    
    /**
     * Create extraction prompt (public for testing)
     */
    static std::string create_extraction_prompt(const std::string& text);
    
    /**
     * Parse extraction response (public for testing)
     * Note: This version accepts all entity types for testing purposes
     */
    static ExtractionResult parse_extraction_response(const std::string& json_response, const std::string& chunk_id = "");
    
private:
    ExtractionConfig config_;
    bool initialized_ = false;
    Stats stats_;
    
    // LLM backend
    std::unique_ptr<ILLMBackend> llm_backend_;
    
    // Private methods
    void initialize_backend();
    std::string create_relationship_prompt(const std::string& text, const std::vector<Entity>& entities);
    
    // Entity deduplication and merging
    void merge_duplicate_entities(std::vector<Entity>& entities);
    float calculate_entity_similarity(const Entity& a, const Entity& b);
    
    // Text preprocessing
    std::vector<std::string> split_into_chunks(const std::string& text, size_t max_length);
    std::string preprocess_text(const std::string& text);
};

// ============================================================================
// Llama.cpp Backend Implementation
// ============================================================================

/**
 * Llama.cpp backend for entity extraction
 * Uses local GGUF models via llama.cpp
 */
class LlamaCppBackend : public ILLMBackend {
public:
    bool initialize(const ExtractionConfig::LLMConfig& config) override;
    std::string generate(const std::string& prompt) override;
    bool is_initialized() const override { return initialized_; }
    
private:
    bool initialized_ = false;
    struct LlamaState;
    std::unique_ptr<LlamaState> state_;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Create default extraction configuration
 */
ExtractionConfig default_extraction_config();

/**
 * Create configuration optimized for scientific/technical texts
 */
ExtractionConfig scientific_extraction_config();

/**
 * Create configuration optimized for news articles
 */
ExtractionConfig news_extraction_config();

/**
 * Create configuration optimized for medical texts
 */
ExtractionConfig medical_extraction_config();

/**
 * Create configuration for local llama.cpp model
 */
ExtractionConfig llama_extraction_config(const std::string& model_path);

} // namespace apollocore_v2
