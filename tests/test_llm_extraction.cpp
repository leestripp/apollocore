/**
 * ApolloCore - LLM Entity Extraction Integration Test
 * 
 * Tests the full entity extraction pipeline with a real LLM model.
 * Requires: qwen2.5-0.5b-instruct-q4_k_m.gguf or similar
 */

#include "apollocore/entity_extractor.hpp"
#include <iostream>
#include <thread>

using namespace apollocore_v2;

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     ApolloCore - LLM Entity Extraction Test                  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    // Find model path
    std::string model_path = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";
    if (argc > 1) {
        model_path = argv[1];
    }
    
    std::cout << "Model: " << model_path << "\n";
    
    // Create extractor config
    ExtractionConfig config;
    config.llm.backend = ExtractionConfig::LLMConfig::Backend::LLAMA_CPP;
    config.llm.model_path = model_path;
    config.llm.n_ctx = 2048;
    config.llm.n_threads = 4;
    config.llm.verbose = true;
    
    // Create extractor
    std::cout << "\nInitializing extractor...\n";
    EntityExtractor extractor(config);
    
    if (!extractor.is_initialized()) {
        std::cout << "❌ Failed to initialize extractor\n";
        std::cout << "Make sure the model file exists: " << model_path << "\n";
        return 1;
    }
    
    std::cout << "✅ Extractor initialized successfully!\n\n";
    
    // Test text
    std::string test_text = R"(
        Albert Einstein published his theory of relativity in 1915 while working at the 
        University of Zurich. He later joined Princeton University where he developed 
        quantum mechanics concepts with Neils Bohr. The theory was validated by Arthur 
        Eddington's 1919 solar eclipse experiment. Einstein received the Nobel Prize 
        in Physics in 1921.
    )";
    
    std::cout << "Test text:\n" << test_text << "\n\n";
    
    // Run extraction
    std::cout << "Running entity extraction...\n";
    auto start = std::chrono::high_resolution_clock::now();
    auto result = extractor.extract(test_text, "chunk_001");
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "                    EXTRACTION RESULTS\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "Time: " << duration << "ms\n";
    std::cout << "\n";
    
    // Print entities
    std::cout << "ENTITIES (" << result.entity_count() << "):\n";
    std::cout << "───────────────────────────────────────────────────────────────\n";
    for (const auto& entity : result.entities) {
        std::cout << "  • " << entity.name << " (" << entity_type_to_string(entity.type) << ")\n";
        std::cout << "    Description: " << entity.description << "\n";
        std::cout << "    Confidence: " << entity.confidence << "\n";
    }
    
    std::cout << "\n";
    
    // Print relationships
    std::cout << "RELATIONSHIPS (" << result.relationship_count() << "):\n";
    std::cout << "───────────────────────────────────────────────────────────────\n";
    for (const auto& rel : result.relationships) {
        std::cout << "  • " << rel.source 
                  << " --[" << relation_type_to_string(rel.type) << "]--> " 
                  << rel.target << "\n";
        std::cout << "    " << rel.description << "\n";
    }
    
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    
    // Statistics
    auto stats = extractor.get_stats();
    std::cout << "Statistics:\n";
    std::cout << "  Total extractions: " << stats.total_extractions << "\n";
    std::cout << "  Total entities: " << stats.total_entities << "\n";
    std::cout << "  Total relationships: " << stats.total_relationships << "\n";
    std::cout << "  Total time: " << stats.total_time_ms << "ms\n";
    
    std::cout << "\n";
    
    if (result.entity_count() > 0) {
        std::cout << "✅ Entity extraction working!\n";
        return 0;
    } else {
        std::cout << "⚠️  No entities extracted - model may need different prompts\n";
        return 1;
    }
}
