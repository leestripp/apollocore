/**
 * ApolloCore - Entity Extraction Implementation
 * 
 * LLM-based entity and relationship extraction using structured prompting.
 * Supports multiple LLM backends.
 */

#include "apollocore/entity_extractor.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <chrono>

// Include llama.cpp for LLM inference
#include "llama.h"

namespace apollocore_v2 {

// ============================================================================
// Llama Backend State (PIMPL)
// ============================================================================

struct LlamaCppBackend::LlamaState {
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
// LlamaCppBackend Implementation
// ============================================================================

bool LlamaCppBackend::initialize(const ExtractionConfig::LLMConfig& config) {
    if (config.model_path.empty()) {
        std::cerr << "[LlamaCppBackend] No model path specified\n";
        return false;
    }
    
    state_ = std::make_unique<LlamaState>();
    
    // Initialize backend (no special params in this API)
    llama_backend_init();
    
    // Load model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;  // CPU only for extraction
    
    state_->model = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (!state_->model) {
        std::cerr << "[LlamaCppBackend] Failed to load model: " << config.model_path << "\n";
        state_.reset();
        return false;
    }
    
    // Create context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config.n_ctx;
    ctx_params.n_threads = config.n_threads;
    
    state_->ctx = llama_init_from_model(state_->model, ctx_params);
    if (!state_->ctx) {
        std::cerr << "[LlamaCppBackend] Failed to create context\n";
        llama_model_free(state_->model);
        state_->model = nullptr;
        state_.reset();
        return false;
    }
    
    initialized_ = true;
    return true;
}

std::string LlamaCppBackend::generate(const std::string& prompt) {
    if (!initialized_ || !state_ || !state_->ctx) {
        return "";
    }
    
    // Get vocab from model
    const llama_vocab* vocab = llama_model_get_vocab(state_->model);
    (void)vocab;  // Available if needed for custom tokenization
    int n_ctx = llama_n_ctx(state_->ctx);
    
    // Tokenize the prompt
    std::vector<llama_token> tokens(n_ctx);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), (int)prompt.length(), 
                                  tokens.data(), (int)tokens.size(), true, true);
    if (n_tokens < 0) {
        std::cerr << "[LlamaCppBackend] Tokenization failed\n";
        return "";
    }
    tokens.resize(n_tokens);
    
    // Evaluate prompt tokens
    for (int i = 0; i < (int)tokens.size(); i++) {
        llama_batch batch = llama_batch_get_one(&tokens[i], 1);
        if (llama_decode(state_->ctx, batch) != 0) {
            std::cerr << "[LlamaCppBackend] Failed to decode token " << i << "\n";
            return "";
        }
    }
    
    // Create sampler
    llama_sampler* smpl = llama_sampler_init_greedy();
    
    // Generate response
    std::string response;
    llama_token new_token;
    int max_tokens = 2048;
    
    for (int i = 0; i < max_tokens; i++) {
        // Sample next token
        new_token = llama_sampler_sample(smpl, state_->ctx, -1);
        
        // Check for EOS
        if (llama_vocab_is_eog(vocab, new_token)) {
            break;
        }
        
        // Convert token to text and add to response
        char buf[256];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) {
            response.append(buf, n);
        }
        
        // Check for end of response marker
        if (response.find("<|im_end|>") != std::string::npos) {
            // Remove the marker
            size_t pos = response.find("<|im_end|>");
            response = response.substr(0, pos);
            break;
        }
        
        // Check for end of FULL JSON (not just entities array)
        // Look for "}}}" which would be: end of array + end of object
        // (e.g., "...}]}") - this means both entities AND relationships are done
        if (response.find("}]}") != std::string::npos) {
            break;
        }
        // Also stop on "}\n" if we have a complete-looking JSON
        if (response.find("}\n") != std::string::npos && 
            response.find("\"relationships\"") != std::string::npos) {
            break;
        }
        
        // Add token to context for next iteration
        llama_batch batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(state_->ctx, batch) != 0) {
            break;
        }
    }
    
    llama_sampler_free(smpl);
    
    return response;
}

// ============================================================================
// Configuration Factory Functions
// ============================================================================

ExtractionConfig default_extraction_config() {
    ExtractionConfig config;
    config.min_confidence = 0.5f;
    config.enable_coreference = true;
    config.max_text_length = 2048;
    config.batch_size = 1;
    return config;
}

ExtractionConfig scientific_extraction_config() {
    ExtractionConfig config = default_extraction_config();
    config.entity_types = {
        EntityType::PERSON,
        EntityType::ORGANIZATION,
        EntityType::CONCEPT,
        EntityType::SCIENTIFIC_TERM,
        EntityType::DATE,
        EntityType::PRODUCT,
        EntityType::EVENT
    };
    config.relation_types = {
        RelationType::DEVELOPED,
        RelationType::PUBLISHED,
        RelationType::WORKED_AT,
        RelationType::RELATED_TO,
        RelationType::CAUSED,
        RelationType::PART_OF,
        RelationType::USES,
        RelationType::DEPENDS_ON,
        RelationType::EVALUATES
    };
    config.min_confidence = 0.6f;  // Higher threshold for scientific accuracy
    return config;
}

ExtractionConfig news_extraction_config() {
    ExtractionConfig config = default_extraction_config();
    config.entity_types = {
        EntityType::PERSON,
        EntityType::ORGANIZATION,
        EntityType::LOCATION,
        EntityType::EVENT,
        EntityType::DATE
    };
    config.relation_types = {
        RelationType::FOUNDED,
        RelationType::WORKED_AT,
        RelationType::LOCATED_IN,
        RelationType::RELATED_TO,
        RelationType::CAUSED
    };
    config.min_confidence = 0.5f;
    return config;
}

ExtractionConfig medical_extraction_config() {
    ExtractionConfig config = default_extraction_config();
    config.entity_types = {
        EntityType::PERSON,
        EntityType::ORGANIZATION,
        EntityType::DISEASE,
        EntityType::DRUG,
        EntityType::MEDICAL_PROCEDURE,
        EntityType::DATE
    };
    config.relation_types = {
        RelationType::TREATS,
        RelationType::DIAGNOSES,
        RelationType::WORKED_AT,
        RelationType::RELATED_TO,
        RelationType::CAUSED
    };
    config.min_confidence = 0.7f;  // Higher for medical safety
    return config;
}

ExtractionConfig llama_extraction_config(const std::string& model_path) {
    ExtractionConfig config = default_extraction_config();
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
    config.llm.model_path = model_path;
    config.llm.n_ctx = 4096;
    config.llm.n_threads = 4;
    config.llm.temperature = 0.1f;
    config.llm.max_tokens = 2048;
    return config;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

EntityExtractor::EntityExtractor(const ExtractionConfig& config) : config_(config) {
    initialize_backend();
}

EntityExtractor::~EntityExtractor() = default;

EntityExtractor::EntityExtractor(EntityExtractor&& other) noexcept
    : config_(std::move(other.config_))
    , initialized_(other.initialized_)
    , stats_(other.stats_)
    , llm_backend_(std::move(other.llm_backend_)) {
    other.initialized_ = false;
}

EntityExtractor& EntityExtractor::operator=(EntityExtractor&& other) noexcept {
    if (this != &other) {
        config_ = std::move(other.config_);
        initialized_ = other.initialized_;
        stats_ = other.stats_;
        llm_backend_ = std::move(other.llm_backend_);
        other.initialized_ = false;
    }
    return *this;
}

void EntityExtractor::set_config(const ExtractionConfig& config) {
    config_ = config;
    initialize_backend();
}

// ============================================================================
// Backend Initialization
// ============================================================================

void EntityExtractor::initialize_backend() {
    llm_backend_ = nullptr;
    initialized_ = false;
    
    if (config_.llm.backend == ExtractionConfig::LLMConfig::Backend::NONE) {
        return;  // No LLM backend
    }
    
    // Create appropriate backend
    if (config_.llm.backend == ExtractionConfig::LLMConfig::Backend::LLAMA_CPP) {
        auto backend = std::make_unique<LlamaCppBackend>();
        if (backend->initialize(config_.llm)) {
            llm_backend_ = std::move(backend);
            initialized_ = true;
        }
    }
    
    // For now, we don't support other backends without additional setup
    if (!initialized_) {
        if (config_.llm.verbose) {
            std::cerr << "[EntityExtractor] No LLM backend initialized. ";
            std::cerr << "Set config.llm.backend to NONE for rule-based extraction.\n";
        }
    }
}

// ============================================================================
// Prompt Templates
// ============================================================================

std::string EntityExtractor::create_extraction_prompt(const std::string& text) {
    std::ostringstream prompt;
    
    // Use Qwen-compatible chat format
    prompt << R"(<|im_start|>system
You are an expert entity extraction system. Extract entities and relationships from text and return ONLY valid JSON. No explanations.
<|im_end|>
<|im_start|>user
Extract ALL entities and relationships from this text. Return ONLY this JSON format:

{"entities": [{"name": "text", "type": "PERSON|ORGANIZATION|LOCATION|CONCEPT|EVENT|PRODUCT|DATE", "description": "brief", "confidence": 0.0-1.0}], "relationships": [{"source": "name1", "target": "name2", "type": "FOUNDED|DEVELOPED|PUBLISHED|WORKED_AT|LOCATED_IN|RELATED_TO|CAUSED|PART_OF", "description": "brief"}]}

Example input: "Steve Jobs founded Apple Inc. in Cupertino."
Example output: {"entities": [{"name": "Steve Jobs", "type": "PERSON", "description": "Apple co-founder", "confidence": 0.99}, {"name": "Apple Inc.", "type": "ORGANIZATION", "description": "Tech company", "confidence": 0.99}, {"name": "Cupertino", "type": "LOCATION", "description": "City", "confidence": 0.98}], "relationships": [{"source": "Steve Jobs", "target": "Apple Inc.", "type": "FOUNDED", "description": "Founded company"}]}

Text: )" << text << R"(
<|im_end|>
<|im_start|>assistant
)";
    
    return prompt.str();
}

std::string EntityExtractor::create_relationship_prompt(
    const std::string& text, 
    const std::vector<Entity>& entities
) {
    if (entities.empty()) {
        return "";
    }
    
    std::ostringstream prompt;
    prompt << "<s>[INST] <<SYS>>\n";
    prompt << "Given these entities extracted from text, find ALL relationships between them.\n\n";
    prompt << "Entities:\n";
    for (const auto& e : entities) {
        prompt << "- " << e.name << " (" << entity_type_to_string(e.type) << ")\n";
    }
    prompt << "\nRelationship types: FOUNDED, DEVELOPED, PUBLISHED, WORKED_AT, LOCATED_IN, ";
    prompt << "RELATED_TO, CAUSED, PART_OF, USES, DEPENDS_ON\n\n";
    prompt << "Return JSON:\n";
    prompt << R"({"relationships": [{"source": "name1", "target": "name2", "type": "TYPE", "description": "..."}]})" << "\n";
    prompt << "<</SYS>>\n\n";
    prompt << "Text: \"" << text << "\"\nJSON: [/INST]";
    
    return prompt.str();
}

// ============================================================================
// JSON Parsing
// ============================================================================

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string extract_json_object(const std::string& text) {
    // Find the first { and last }
    size_t start = text.find('{');
    size_t end = text.find_last_of('}');
    
    if (start != std::string::npos && end != std::string::npos && end > start) {
        std::string json = text.substr(start, end - start + 1);
        return trim(json);
    }
    
    // Fallback: try to find JSON-like structure
    std::regex json_pattern(R"(\{[\s\S]*\})");
    std::smatch match;
    if (std::regex_search(text, match, json_pattern)) {
        return match.str();
    }
    
    return "{}";
}

static std::string get_json_string(const std::string& json, const std::string& key) {
    // Simple string-based extraction for JSON values
    // Look for "key": "value" pattern
    std::string search = "\"" + key + "\"";
    size_t key_pos = json.find(search);
    if (key_pos == std::string::npos) {
        // Try "key": value (without quotes on value)
        std::string alt_search = "\"" + key + "\":";
        size_t alt_pos = json.find(alt_search);
        if (alt_pos == std::string::npos) return "";
        
        // Find the start of the value
        size_t value_start = json.find(':', alt_pos) + 1;
        while (value_start < json.size() && (json[value_start] == ' ' || json[value_start] == '\"')) {
            value_start++;
        }
        
        // Find the end (comma, }, or ] in unquoted case)
        size_t value_end = value_start;
        while (value_end < json.size() && json[value_end] != ',' && json[value_end] != '}' && json[value_end] != ']') {
            value_end++;
        }
        
        std::string val = json.substr(value_start, value_end - value_start);
        // Remove trailing punctuation
        if (!val.empty() && (val.back() == ',' || val.back() == '.')) {
            val.pop_back();
        }
        // Trim whitespace
        while (!val.empty() && val.back() == ' ') val.pop_back();
        while (!val.empty() && val.front() == ' ') val = val.substr(1);
        return val;
    }
    
    // Find the colon after the key
    size_t colon_pos = json.find(':', key_pos);
    if (colon_pos == std::string::npos) return "";
    
    // Find the opening quote of the value
    size_t value_start = json.find('"', colon_pos);
    if (value_start == std::string::npos) return "";
    value_start++; // Skip the opening quote
    
    // Find the closing quote
    size_t value_end = json.find('"', value_start);
    if (value_end == std::string::npos) return "";
    
    return json.substr(value_start, value_end - value_start);
}

static float get_json_float(const std::string& json, const std::string& key) {
    std::string val = get_json_string(json, key);
    if (!val.empty()) {
        try {
            // Remove any non-numeric characters except dot
            std::string num_str;
            for (char c : val) {
                if (c == '.' || (c >= '0' && c <= '9')) {
                    num_str += c;
                }
            }
            if (!num_str.empty()) {
                return std::stof(num_str);
            }
        } catch (...) {
            return 0.5f;
        }
    }
    return 0.5f;
}

static std::vector<std::string> extract_json_arrays(const std::string& json, const std::string& array_name) {
    std::vector<std::string> result;
    
    // Find array bounds - find the array name and then the opening [
    std::string search = "\"" + array_name + "\"";
    size_t array_start = json.find(search);
    if (array_start == std::string::npos) return result;
    
    // Find the opening bracket after the array name
    size_t bracket_start = json.find('[', array_start);
    if (bracket_start == std::string::npos) return result;
    
    // Find the matching closing bracket
    int depth = 1;
    size_t bracket_end = bracket_start + 1;
    while (bracket_end < json.size() && depth > 0) {
        if (json[bracket_end] == '[') depth++;
        else if (json[bracket_end] == ']') depth--;
        else if (json[bracket_end] == '{') depth++;
        else if (json[bracket_end] == '}') depth--;
        bracket_end++;
    }
    
    std::string array_content = json.substr(bracket_start + 1, bracket_end - bracket_start - 2);
    
    // Extract individual objects - find each {...}
    size_t obj_start = 0;
    int brace_depth = 0;
    for (size_t i = 0; i <= array_content.size(); i++) {
        if (i == array_content.size() || (array_content[i] == ',' && brace_depth == 0)) {
            if (obj_start < i) {
                std::string obj = array_content.substr(obj_start, i - obj_start);
                // Trim whitespace
                while (!obj.empty() && obj.front() == ' ') obj = obj.substr(1);
                while (!obj.empty() && obj.back() == ' ') obj.pop_back();
                if (!obj.empty()) {
                    result.push_back(obj);
                }
            }
            obj_start = i + 1;
        } else if (array_content[i] == '{') {
            brace_depth++;
        } else if (array_content[i] == '}') {
            brace_depth--;
        }
    }
    
    return result;
}

ExtractionResult EntityExtractor::parse_extraction_response(const std::string& response, const std::string& chunk_id) {
    ExtractionResult result;
    
    std::string json = extract_json_object(response);
    
    if (json.empty() || json == "{}") {
        return result;
    }
    
    // Extract entities array
    auto entity_objects = extract_json_arrays(json, "entities");
    for (const auto& obj : entity_objects) {
        std::string name = get_json_string(obj, "name");
        std::string type_str = get_json_string(obj, "type");
        std::string description = get_json_string(obj, "description");
        float confidence = get_json_float(obj, "confidence");
        
        if (!name.empty()) {
            EntityType type = string_to_entity_type(type_str);
            
            // Accept all entity types in the static version (filtering can be done later)
            Entity entity(name, type, description, confidence);
            entity.id = result.entities.size();  // Use index as temp ID
            result.add_entity(entity, chunk_id);
        }
    }
    
    // Extract relationships array
    auto rel_objects = extract_json_arrays(json, "relationships");
    for (const auto& obj : rel_objects) {
        std::string source = get_json_string(obj, "source");
        std::string target = get_json_string(obj, "target");
        std::string type_str = get_json_string(obj, "type");
        std::string description = get_json_string(obj, "description");
        
        if (!source.empty() && !target.empty()) {
            RelationType type = string_to_relation_type(type_str);
            
            // Accept all relationship types in the static version
            Relationship rel(source, target, type, description, 1.0f);
            result.add_relationship(rel);
        }
    }
    
    return result;
}

// ============================================================================
// Text Preprocessing
// ============================================================================

std::string EntityExtractor::preprocess_text(const std::string& text) {
    std::string result = text;
    
    // Remove excessive whitespace
    std::regex ws(R"(\s+)");
    result = std::regex_replace(result, ws, " ");
    
    // Trim
    result = trim(result);
    
    // Remove null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
    
    return result;
}

std::vector<std::string> EntityExtractor::split_into_chunks(const std::string& text, size_t max_length) {
    std::vector<std::string> chunks;
    
    if (text.length() <= max_length) {
        chunks.push_back(text);
        return chunks;
    }
    
    // Split by sentences first, then by words if needed
    std::vector<std::string> sentences;
    std::string current;
    
    for (char c : text) {
        current += c;
        if (c == '.' || c == '!' || c == '?') {
            if (current.length() > 1) {
                sentences.push_back(trim(current));
            }
            current.clear();
        }
    }
    if (!current.empty()) {
        sentences.push_back(trim(current));
    }
    
    // Combine sentences into chunks
    std::string chunk;
    for (const auto& sentence : sentences) {
        if (chunk.length() + sentence.length() + 1 <= max_length) {
            if (!chunk.empty()) chunk += " ";
            chunk += sentence;
        } else {
            if (!chunk.empty()) {
                chunks.push_back(chunk);
            }
            // If single sentence is too long, split by words
            if (sentence.length() > max_length) {
                std::istringstream iss(sentence);
                std::string word;
                std::string subchunk;
                while (iss >> word) {
                    if (subchunk.length() + word.length() + 1 <= max_length) {
                        if (!subchunk.empty()) subchunk += " ";
                        subchunk += word;
                    } else {
                        if (!subchunk.empty()) {
                            chunks.push_back(subchunk);
                        }
                        subchunk = word;
                    }
                }
                if (!subchunk.empty()) {
                    chunk = subchunk;
                } else {
                    chunk.clear();
                }
            } else {
                chunk = sentence;
            }
        }
    }
    
    if (!chunk.empty()) {
        chunks.push_back(chunk);
    }
    
    return chunks;
}

// ============================================================================
// Entity Deduplication
// ============================================================================

float EntityExtractor::calculate_entity_similarity(const Entity& a, const Entity& b) {
    if (a.type != b.type) return 0.0f;
    
    // Simple case-insensitive string comparison
    std::string name_a = a.name;
    std::string name_b = b.name;
    std::transform(name_a.begin(), name_a.end(), name_a.begin(), ::tolower);
    std::transform(name_b.begin(), name_b.end(), name_b.begin(), ::tolower);
    
    if (name_a == name_b) return 1.0f;
    
    // Check if one contains the other
    if (name_a.find(name_b) != std::string::npos || name_b.find(name_a) != std::string::npos) {
        return 0.8f;
    }
    
    return 0.0f;
}

void EntityExtractor::merge_duplicate_entities(std::vector<Entity>& entities) {
    std::vector<Entity> merged;
    std::unordered_set<size_t> used;
    
    for (size_t i = 0; i < entities.size(); ++i) {
        if (used.count(i)) continue;
        
        Entity best = entities[i];
        for (size_t j = i + 1; j < entities.size(); ++j) {
            if (used.count(j)) continue;
            
            float sim = calculate_entity_similarity(best, entities[j]);
            if (sim > 0.7f) {
                // Merge - keep higher confidence
                if (entities[j].confidence > best.confidence) {
                    best = entities[j];
                }
                // Keep the longer/more complete description
                if (best.description.empty() && !entities[j].description.empty()) {
                    best.description = entities[j].description;
                }
                used.insert(j);
            }
        }
        
        merged.push_back(best);
        used.insert(i);
    }
    
    entities = merged;
}

// ============================================================================
// Main Extraction
// ============================================================================

std::string EntityExtractor::generate_entity_id(const std::string& name) {
    std::string id = name;
    std::transform(id.begin(), id.end(), id.begin(), ::tolower);
    std::replace(id.begin(), id.end(), ' ', '_');
    std::replace(id.begin(), id.end(), '.', '_');
    // Remove non-alphanumeric except underscore
    id.erase(std::remove_if(id.begin(), id.end(), 
        [](char c) { return !std::isalnum(c) && c != '_'; }), id.end());
    return "entity_" + id;
}

ExtractionResult EntityExtractor::extract(const std::string& text, const std::string& chunk_id) {
    auto start = std::chrono::high_resolution_clock::now();
    
    ExtractionResult result;
    
    // Preprocess
    std::string clean_text = preprocess_text(text);
    
    if (clean_text.empty()) {
        return result;
    }
    
    // If LLM is not initialized, return empty result
    if (!initialized_ || !llm_backend_) {
        if (config_.llm.verbose) {
            std::cerr << "[EntityExtractor] LLM not initialized. ";
            std::cerr << "Download an LLM model and set config.llm.model_path to enable extraction.\n";
        }
        return result;
    }
    
    // Check if text is too long
    if (clean_text.length() > config_.max_text_length * 4) {  // Rough char/token ratio
        // Split into chunks
        auto chunks = split_into_chunks(clean_text, config_.max_text_length * 4);
        for (const auto& c : chunks) {
            auto chunk_result = extract(c, chunk_id);
            result.entities.insert(result.entities.end(), chunk_result.entities.begin(), chunk_result.entities.end());
            result.relationships.insert(result.relationships.end(), chunk_result.relationships.begin(), chunk_result.relationships.end());
        }
        
        // Merge duplicates
        merge_duplicate_entities(result.entities);
        
        auto end = std::chrono::high_resolution_clock::now();
        stats_.total_time_ms += std::chrono::duration<double>(end - start).count();
        stats_.total_extractions++;
        
        return result;
    }
    
    // Create prompt
    std::string prompt = create_extraction_prompt(clean_text);
    
    if (config_.llm.verbose) {
        std::cerr << "[EntityExtractor] Processing text of length " << clean_text.length() << "\n";
    }
    
    // Generate with LLM
    std::string response = llm_backend_->generate(prompt);
    
    if (config_.llm.verbose) {
        std::cerr << "[EntityExtractor] Generated response of length " << response.length() << "\n";
    }
    
    // Parse response
    result = parse_extraction_response(response, chunk_id);
    
    // Merge duplicates
    merge_duplicate_entities(result.entities);
    
    // Filter by confidence
    result = result.filter_by_confidence(config_.min_confidence);
    
    auto end = std::chrono::high_resolution_clock::now();
    stats_.total_time_ms += std::chrono::duration<double>(end - start).count();
    stats_.total_extractions++;
    stats_.total_entities += result.entities.size();
    stats_.total_relationships += result.relationships.size();
    
    return result;
}

std::vector<ExtractionResult> EntityExtractor::extract_batch(
    const std::vector<std::string>& texts,
    const std::vector<std::string>& chunk_ids
) {
    std::vector<ExtractionResult> results;
    
    size_t i = 0;
    for (const auto& text : texts) {
        std::string chunk_id = (i < chunk_ids.size()) ? chunk_ids[i] : "";
        results.push_back(extract(text, chunk_id));
        i++;
    }
    
    return results;
}

} // namespace apollocore_v2
