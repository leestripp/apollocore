/**
 * ApolloCore - HyDE Query Enhancement Test
 */

#include "apollocore/hyde.hpp"
#include <iostream>
#include <cassert>

using namespace apollocore_v2;

void test_hyde_config() {
    std::cout << "Test: HyDE Config Factory Functions... ";
    
    auto default_cfg = default_hyde_config();
    assert(default_cfg.num_generations == 3);
    assert(default_cfg.temperature == 0.7f);
    assert(default_cfg.weight_hyde == 0.7f);
    assert(default_cfg.weight_query == 0.3f);
    
    auto diverse_cfg = diverse_hyde_config();
    assert(diverse_cfg.num_generations == 5);
    assert(diverse_cfg.temperature == 1.0f);
    
    auto fast_cfg = fast_hyde_config();
    assert(fast_cfg.num_generations == 2);
    assert(fast_cfg.temperature == 0.5f);
    
    std::cout << "PASSED\n";
}

void test_hypothetical_document() {
    std::cout << "Test: HypotheticalDocument Structure... ";
    
    HypotheticalDocument doc1;
    assert(doc1.quality_score == 0.0f);
    assert(doc1.content.empty());
    
    HypotheticalDocument doc2("Test content", {1.0f, 2.0f, 3.0f}, 0.8f);
    assert(doc2.content == "Test content");
    assert(doc2.embedding.size() == 3);
    assert(doc2.quality_score == 0.8f);
    
    std::cout << "PASSED\n";
}

void test_hyde_result() {
    std::cout << "Test: HydeResult Structure... ";
    
    HydeResult result;
    assert(result.num_docs() == 0);
    assert(result.original_query.empty());
    
    result.original_query = "What is quantum mechanics?";
    result.hypothetical_docs.push_back(HypotheticalDocument("Doc 1", {}, 0.9f));
    result.hypothetical_docs.push_back(HypotheticalDocument("Doc 2", {}, 0.8f));
    assert(result.num_docs() == 2);
    
    std::cout << "PASSED\n";
}

void test_hyde_enhancer_without_llm() {
    std::cout << "Test: HyDE Enhancer Without LLM... ";
    
    HydeConfig config;
    config.num_generations = 2;
    config.verbose = false;
    
    HydeEnhancer enhancer(config);
    
    // Should not be initialized without LLM
    assert(!enhancer.is_initialized());
    
    // But should still handle calls gracefully
    auto result = enhancer.enhance("What is quantum mechanics?");
    
    // Returns original query
    assert(result.original_query == "What is quantum mechanics?");
    
    std::cout << "PASSED\n";
}

void test_batch_enhancement() {
    std::cout << "Test: Batch Enhancement... ";
    
    HydeConfig config;
    config.num_generations = 1;
    config.verbose = false;
    
    HydeEnhancer enhancer(config);
    
    std::vector<std::string> queries = {
        "What is quantum mechanics?",
        "How does photosynthesis work?",
        "What caused World War II?"
    };
    
    auto results = enhancer.enhance_batch(queries);
    
    assert(results.size() == 3);
    assert(results[0].original_query == queries[0]);
    assert(results[1].original_query == queries[1]);
    assert(results[2].original_query == queries[2]);
    
    std::cout << "PASSED\n";
}

void test_stats_tracking() {
    std::cout << "Test: Statistics Tracking... ";
    
    HydeConfig config;
    config.num_generations = 3;
    config.verbose = false;
    
    HydeEnhancer enhancer(config);
    
    enhancer.enhance("Query 1");
    enhancer.enhance("Query 2");
    enhancer.enhance("Query 3");
    
    auto stats = enhancer.get_stats();
    assert(stats.total_enhanced == 3);
    assert(stats.total_docs_generated == 9);  // 3 queries * 3 docs each
    
    std::cout << "PASSED\n";
}

void test_config_setters() {
    std::cout << "Test: Config Getter... ";
    
    HydeConfig config;
    config.num_generations = 5;
    config.embedding.dimensions = 1024;
    
    HydeEnhancer enhancer(config);
    
    auto retrieved_config = enhancer.get_config();
    assert(retrieved_config.num_generations == 5);
    assert(retrieved_config.embedding.dimensions == 1024);
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - HyDE Query Enhancement Tests          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_hyde_config();
    test_hypothetical_document();
    test_hyde_result();
    test_hyde_enhancer_without_llm();
    test_batch_enhancement();
    test_stats_tracking();
    test_config_setters();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All HyDE Tests PASSED                              ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Configuration presets: PASSED\n";
    std::cout << "   • HypotheticalDocument structure: PASSED\n";
    std::cout << "   • HydeResult structure: PASSED\n";
    std::cout << "   • Fallback operation (no LLM): PASSED\n";
    std::cout << "   • Batch enhancement: PASSED\n";
    std::cout << "   • Statistics tracking: PASSED\n";
    std::cout << "\n";
    std::cout << "📦 To enable full HyDE with LLM:\n";
    std::cout << "   1. Set config.llm.model_path to an instruct model\n";
    std::cout << "   2. Optionally set config.embedding.model_path\n";
    std::cout << "   3. HyDE will generate hypothetical documents\n";
    std::cout << "\n";
    
    return 0;
}
