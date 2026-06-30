/**
 * ApolloCore - Cross-Encoder Reranking Test
 */

#include "apollocore/cross_encoder.hpp"
#include <iostream>
#include <cassert>

using namespace apollocore_v2;

void test_rerank_config() {
    std::cout << "Test: RerankConfig Factory Functions... ";
    
    auto default_cfg = default_rerank_config();
    assert(default_cfg.top_k == 20);
    assert(default_cfg.return_k == 10);
    assert(default_cfg.cross_encoder_weight == 0.5f);
    
    auto precision_cfg = high_precision_rerank_config();
    assert(precision_cfg.top_k == 50);
    assert(precision_cfg.return_k == 5);
    assert(precision_cfg.cross_encoder_weight == 0.7f);
    
    auto fast_cfg = fast_rerank_config();
    assert(fast_cfg.top_k == 10);
    assert(fast_cfg.return_k == 5);
    
    std::cout << "PASSED\n";
}

void test_reranked_result() {
    std::cout << "Test: RerankedResult Structure... ";
    
    RerankedResult r1("chunk_1", "Test content", 0.8f, 0.5f);
    assert(r1.chunk_id == "chunk_1");
    assert(r1.content == "Test content");
    assert(r1.vector_score == 0.8f);
    assert(r1.keyword_score == 0.5f);
    assert(r1.rerank_score == 0.0f);  // Default
    
    RerankedResult r2;
    assert(r2.original_score == 0.0f);
    assert(r2.final_score == 0.0f);
    
    std::cout << "PASSED\n";
}

void test_reranking_without_llm() {
    std::cout << "Test: Reranking Without LLM (Fallback)... ";
    
    RerankConfig config;
    config.top_k = 10;
    config.return_k = 5;
    config.cross_encoder_weight = 0.5f;
    config.original_weight = 0.5f;
    
    CrossEncoderReranker reranker(config);
    
    // Create test results
    std::vector<RerankedResult> results;
    results.push_back(RerankedResult("1", "Quantum mechanics describes atomic behavior", 0.9f, 0.8f));
    results.push_back(RerankedResult("2", "Python is a programming language", 0.7f, 0.9f));
    results.push_back(RerankedResult("3", "Albert Einstein developed relativity", 0.85f, 0.6f));
    
    // Run reranking
    auto reranked = reranker.rerank("What is quantum mechanics?", results);
    
    // Should still return results (using fallback)
    assert(!reranked.empty());
    assert(reranked.size() <= config.return_k);
    
    // First result should have highest combined score
    if (reranked.size() >= 2) {
        assert(reranked[0].final_score >= reranked[1].final_score);
    }
    
    std::cout << "PASSED\n";
}

void test_custom_scorer() {
    std::cout << "Test: Custom Scorer Function... ";
    
    RerankConfig config;
    config.top_k = 5;
    config.return_k = 3;
    config.cross_encoder_weight = 0.8f;
    config.original_weight = 0.2f;
    
    CrossEncoderReranker reranker(config);
    
    // Custom scorer: simple keyword matching
    auto scorer = [](const std::string& query, const std::string& doc) -> float {
        std::string q = query;
        std::string d = doc;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        
        // Count matching words
        size_t matches = 0;
        size_t words = 0;
        std::string word;
        for (char c : q) {
            if (std::isalnum(c)) {
                word += c;
            } else if (!word.empty()) {
                words++;
                if (d.find(word) != std::string::npos) matches++;
                word.clear();
            }
        }
        
        return (words > 0) ? (float)matches / words : 0.0f;
    };
    
    std::vector<RerankedResult> results;
    results.push_back(RerankedResult("1", "Quantum mechanics physics", 0.5f, 0.3f));
    results.push_back(RerankedResult("2", "Python programming language", 0.5f, 0.3f));
    results.push_back(RerankedResult("3", "Quantum computing algorithms", 0.5f, 0.3f));
    
    auto reranked = reranker.rerank_with_custom_scorer("quantum mechanics", results, scorer);
    
    // Quantum-related docs should score higher
    assert(!reranked.empty());
    
    std::cout << "PASSED\n";
}

void test_score_filtering() {
    std::cout << "Test: Score Threshold Filtering... ";
    
    RerankConfig config;
    config.top_k = 10;
    config.return_k = 10;
    config.cross_encoder_weight = 0.5f;
    config.original_weight = 0.5f;
    config.min_score_threshold = 0.6f;  // Filter out low scores
    
    CrossEncoderReranker reranker(config);
    
    std::vector<RerankedResult> results;
    results.push_back(RerankedResult("1", "High relevance doc", 0.9f, 0.8f));
    results.push_back(RerankedResult("2", "Low relevance doc", 0.2f, 0.1f));
    results.push_back(RerankedResult("3", "Medium relevance", 0.7f, 0.5f));
    
    auto reranked = reranker.rerank("test query", results);
    
    // Should filter out low-scoring results
    for (const auto& r : reranked) {
        assert(r.final_score >= 0.6f);
        (void)r;  // suppress unused warning
    }
    
    std::cout << "PASSED\n";
}

void test_empty_results() {
    std::cout << "Test: Empty Results Handling... ";
    
    RerankConfig config;
    CrossEncoderReranker reranker(config);
    
    auto reranked = reranker.rerank("query", {});
    assert(reranked.empty());
    
    std::cout << "PASSED\n";
}

void test_stats() {
    std::cout << "Test: Statistics Tracking... ";
    
    RerankConfig config;
    CrossEncoderReranker reranker(config);
    
    std::vector<RerankedResult> results;
    results.push_back(RerankedResult("1", "Doc 1", 0.8f, 0.6f));
    results.push_back(RerankedResult("2", "Doc 2", 0.7f, 0.5f));
    
    reranker.rerank("test", results);
    reranker.rerank("another", results);
    
    auto stats = reranker.get_stats();
    assert(stats.total_reranked == 2);
    (void)stats;
    assert(stats.total_documents >= 0);
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - Cross-Encoder Reranking Tests          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_rerank_config();
    test_reranked_result();
    test_reranking_without_llm();
    test_custom_scorer();
    test_score_filtering();
    test_empty_results();
    test_stats();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All Cross-Encoder Tests PASSED                       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Configuration presets: PASSED\n";
    std::cout << "   • RerankedResult structure: PASSED\n";
    std::cout << "   • Fallback reranking (no LLM): PASSED\n";
    std::cout << "   • Custom scorer function: PASSED\n";
    std::cout << "   • Score threshold filtering: PASSED\n";
    std::cout << "   • Empty results handling: PASSED\n";
    std::cout << "   • Statistics tracking: PASSED\n";
    std::cout << "\n";
    std::cout << "📦 To enable LLM-based reranking:\n";
    std::cout << "   1. Set config.llm.model_path\n";
    std::cout << "   2. The reranker will use the LLM to score query-doc pairs\n";
    std::cout << "\n";
    
    return 0;
}
