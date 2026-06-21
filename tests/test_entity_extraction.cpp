/**
 * ApolloCore - Entity Extraction Tests
 * 
 * Tests the entity extraction framework without requiring an LLM model.
 * Full extraction tests require downloading a GGUF model.
 */

#include "apollocore/entity_extractor.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace apollocore_v2;

void test_entity_types() {
    std::cout << "Test: Entity Types Conversion... ";
    
    assert(std::string(entity_type_to_string(EntityType::PERSON)) == "PERSON");
    assert(std::string(entity_type_to_string(EntityType::ORGANIZATION)) == "ORGANIZATION");
    assert(std::string(entity_type_to_string(EntityType::LOCATION)) == "LOCATION");
    assert(std::string(entity_type_to_string(EntityType::CONCEPT)) == "CONCEPT");
    
    assert(string_to_entity_type("PERSON") == EntityType::PERSON);
    assert(string_to_entity_type("ORGANIZATION") == EntityType::ORGANIZATION);
    
    std::cout << "PASSED\n";
}

void test_relation_types() {
    std::cout << "Test: Relation Types Conversion... ";
    
    assert(std::string(relation_type_to_string(RelationType::FOUNDED)) == "FOUNDED");
    assert(std::string(relation_type_to_string(RelationType::WORKED_AT)) == "WORKED_AT");
    assert(std::string(relation_type_to_string(RelationType::DEVELOPED)) == "DEVELOPED");
    
    assert(string_to_relation_type("FOUNDED") == RelationType::FOUNDED);
    assert(string_to_relation_type("WORKED_AT") == RelationType::WORKED_AT);
    
    std::cout << "PASSED\n";
}

void test_entity_structure() {
    std::cout << "Test: Entity Structure... ";
    
    Entity entity("Albert Einstein", EntityType::PERSON, "Physicist", 0.99f);
    
    assert(entity.name == "Albert Einstein");
    assert(entity.type == EntityType::PERSON);
    assert(entity.description == "Physicist");
    assert(std::abs(entity.confidence - 0.99f) < 0.001f);
    
    std::cout << "PASSED\n";
}

void test_relationship_structure() {
    std::cout << "Test: Relationship Structure... ";
    
    Relationship rel("Einstein", "Relativity", RelationType::DEVELOPED, "Created theory", 0.95f);
    
    assert(rel.source == "Einstein");
    assert(rel.target == "Relativity");
    assert(rel.type == RelationType::DEVELOPED);
    assert(rel.description == "Created theory");
    assert(std::abs(rel.weight - 0.95f) < 0.001f);
    
    std::cout << "PASSED\n";
}

void test_extraction_result() {
    std::cout << "Test: Extraction Result... ";
    
    ExtractionResult result;
    
    Entity e1("Apple Inc.", EntityType::ORGANIZATION, "Tech company", 0.99f);
    Entity e2("Steve Jobs", EntityType::PERSON, "Co-founder", 0.99f);
    
    result.add_entity(e1, "chunk_1");
    result.add_entity(e2, "chunk_1");
    result.add_relationship(Relationship("Steve Jobs", "Apple Inc.", RelationType::FOUNDED));
    
    assert(result.entity_count() == 2);
    assert(result.relationship_count() == 1);
    
    std::cout << "PASSED\n";
}

void test_extraction_result_filter() {
    std::cout << "Test: Confidence Filtering... ";
    
    ExtractionResult result;
    
    result.add_entity(Entity("High", EntityType::PERSON, "", 0.9f));
    result.add_entity(Entity("Medium", EntityType::PERSON, "", 0.6f));
    result.add_entity(Entity("Low", EntityType::PERSON, "", 0.3f));
    result.add_relationship(Relationship("A", "B", RelationType::RELATED_TO, "", 0.8f));
    result.add_relationship(Relationship("C", "D", RelationType::RELATED_TO, "", 0.4f));
    
    auto filtered = result.filter_by_confidence(0.5f);
    
    assert(filtered.entity_count() == 2);  // High and Medium
    assert(filtered.relationship_count() == 1);  // Only the 0.8 one
    
    std::cout << "PASSED\n";
}

void test_config_defaults() {
    std::cout << "Test: Default Configurations... ";
    
    auto default_cfg = default_extraction_config();
    assert(default_cfg.min_confidence == 0.5f);
    assert(default_cfg.entity_types.count(EntityType::PERSON) > 0);
    
    auto scientific_cfg = scientific_extraction_config();
    assert(scientific_cfg.min_confidence == 0.6f);
    assert(scientific_cfg.entity_types.count(EntityType::SCIENTIFIC_TERM) > 0);
    
    auto medical_cfg = medical_extraction_config();
    assert(medical_cfg.min_confidence == 0.7f);
    assert(medical_cfg.entity_types.count(EntityType::DISEASE) > 0);
    assert(medical_cfg.entity_types.count(EntityType::DRUG) > 0);
    
    auto llama_cfg = llama_extraction_config("/path/to/model.gguf");
    assert(llama_cfg.llm.backend == ExtractionConfig::LLMConfig::Backend::LLAMA_CPP);
    assert(llama_cfg.llm.model_path == "/path/to/model.gguf");
    
    std::cout << "PASSED\n";
}

void test_entity_id_generation() {
    std::cout << "Test: Entity ID Generation... ";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::NONE;
    EntityExtractor extractor(config);
    
    std::string id1 = extractor.generate_entity_id("Albert Einstein");
    std::string id2 = extractor.generate_entity_id("albert einstein");  // Same name, lowercase
    
    assert(id1 == id2);  // Should be same ID for same name
    
    std::string id3 = extractor.generate_entity_id("Apple Inc.");
    assert(id3.find("entity_") == 0);
    assert(id3.find("apple") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_extractor_without_llm() {
    std::cout << "Test: Extractor Without LLM... ";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::NONE;
    EntityExtractor extractor(config);
    
    // Should not be initialized
    assert(!extractor.is_initialized());
    
    // Extract should return empty result without crashing
    auto result = extractor.extract("This is a test text about Albert Einstein.");
    assert(result.entity_count() == 0);
    assert(result.relationship_count() == 0);
    
    std::cout << "PASSED\n";
}

void test_parse_extraction_response() {
    std::cout << "Test: Parse JSON Response... ";
    
    std::string mock_response = R"({
  "entities": [
    {"name": "Steve Jobs", "type": "PERSON", "description": "Apple co-founder", "confidence": 0.99},
    {"name": "Apple Inc.", "type": "ORGANIZATION", "description": "Tech company", "confidence": 0.98}
  ],
  "relationships": [
    {"source": "Steve Jobs", "target": "Apple Inc.", "type": "FOUNDED", "description": "Co-founded"}
  ]
})";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::NONE;
    EntityExtractor extractor(config);
    
    auto result = EntityExtractor::parse_extraction_response(mock_response, "chunk_1");
    
    assert(result.entity_count() == 2);
    assert(result.relationship_count() == 1);
    
    // Check first entity
    assert(result.entities[0].name == "Steve Jobs");
    assert(result.entities[0].type == EntityType::PERSON);
    
    // Check relationship
    assert(result.relationships[0].source == "Steve Jobs");
    assert(result.relationships[0].target == "Apple Inc.");
    assert(result.relationships[0].type == RelationType::FOUNDED);
    
    std::cout << "PASSED\n";
}

void test_stats() {
    std::cout << "Test: Extraction Statistics... ";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::NONE;
    EntityExtractor extractor(config);
    
    extractor.extract("Test text");
    extractor.extract("Another test");
    
    auto stats = extractor.get_stats();
    assert(stats.total_extractions == 2);
    
    std::cout << "PASSED\n";
}

void test_scientific_config() {
    std::cout << "Test: Scientific Configuration... ";
    
    auto config = scientific_extraction_config();
    
    // Check scientific entity types
    assert(config.entity_types.count(EntityType::SCIENTIFIC_TERM) > 0);
    assert(config.entity_types.count(EntityType::CONCEPT) > 0);
    assert(config.entity_types.count(EntityType::DATE) > 0);
    
    // Check scientific relation types
    assert(config.relation_types.count(RelationType::DEVELOPED) > 0);
    assert(config.relation_types.count(RelationType::PUBLISHED) > 0);
    assert(config.relation_types.count(RelationType::EVALUATES) > 0);
    
    std::cout << "PASSED\n";
}

void test_llm_config() {
    std::cout << "Test: LLM Configuration... ";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
    config.llm.model_path = "models/llama-3.2-3b-q4_K_M.gguf";
    config.llm.n_ctx = 8192;
    config.llm.temperature = 0.05f;
    config.llm.max_tokens = 4096;
    config.llm.verbose = true;
    
    assert(config.llm.backend == ExtractionConfig::LLMConfig::Backend::LLAMA_CPP);
    assert(config.llm.n_ctx == 8192);
    assert(config.llm.temperature == 0.05f);
    
    std::cout << "PASSED\n";
}

void test_prompt_generation() {
    std::cout << "Test: Prompt Generation... ";
    
    std::string prompt = EntityExtractor::create_extraction_prompt("Albert Einstein was born in Ulm.");
    
    // Check that prompt contains the text
    assert(prompt.find("Albert Einstein was born in Ulm.") != std::string::npos);
    
    // Check that prompt contains key elements
    assert(prompt.find("PERSON") != std::string::npos);
    assert(prompt.find("ORGANIZATION") != std::string::npos);
    assert(prompt.find("LOCATION") != std::string::npos);
    assert(prompt.find("entities") != std::string::npos);
    assert(prompt.find("relationships") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_empty_response_parsing() {
    std::cout << "Test: Empty Response Parsing... ";
    
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::NONE;
    EntityExtractor extractor(config);
    
    auto result1 = EntityExtractor::parse_extraction_response("{}", "chunk_1");
    assert(result1.entity_count() == 0);
    
    auto result2 = EntityExtractor::parse_extraction_response("", "chunk_1");
    assert(result2.entity_count() == 0);
    
    auto result3 = EntityExtractor::parse_extraction_response("not json at all", "chunk_1");
    assert(result3.entity_count() == 0);
    
    std::cout << "PASSED\n";
}

void print_separator() {
    std::cout << "\n" << std::string(50, '-') << "\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - Entity Extraction Unit Tests              ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    print_separator();
    
    // Basic structure tests
    test_entity_types();
    test_relation_types();
    test_entity_structure();
    test_relationship_structure();
    
    print_separator();
    
    // Extraction result tests
    test_extraction_result();
    test_extraction_result_filter();
    
    print_separator();
    
    // Configuration tests
    test_config_defaults();
    test_entity_id_generation();
    test_scientific_config();
    test_llm_config();
    
    print_separator();
    
    // Parser tests
    test_parse_extraction_response();
    test_prompt_generation();
    test_empty_response_parsing();
    
    print_separator();
    
    // Integration tests (without LLM)
    test_extractor_without_llm();
    test_stats();
    
    print_separator();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All Entity Extraction Tests PASSED                      ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Entity types and conversions: PASSED\n";
    std::cout << "   • Relationship types and conversions: PASSED\n";
    std::cout << "   • Extraction result structures: PASSED\n";
    std::cout << "   • JSON parsing: PASSED\n";
    std::cout << "   • Configuration factory functions: PASSED\n";
    std::cout << "   • Prompt generation: PASSED\n";
    std::cout << "\n";
    std::cout << "📦 To enable full LLM extraction:\n";
    std::cout << "   1. Download llama-3.2-3B GGUF model\n";
    std::cout << "   2. Set config.llm.backend = Backend::LLAMA_CPP\n";
    std::cout << "   3. Set config.llm.model_path = \"models/llama-3.2-3b-q4_K_M.gguf\"\n";
    std::cout << "\n";
    
    return 0;
}
