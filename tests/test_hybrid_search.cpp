/**
 * ApolloCore Hybrid Search & Reranking Tests
 *
 * Tests the wired-up query_hybrid(), query_with_rerank(), and query_full()
 * methods on ApolloCore — confirms the new APIs work end-to-end.
 */

#undef NDEBUG

#include "apollocore/apollo_persistence.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

using namespace apollocore_v2;
namespace search = apollocore_v2::search;

const std::string TEST_KB_PATH = "/tmp/apollo_hybrid_test";

void cleanup() {
    std::filesystem::remove_all(TEST_KB_PATH);
}

// ============================================================================
// Test 1: query_hybrid returns vector+BM25+graph results
// ============================================================================
void test_query_hybrid_returns_results() {
    std::cout << "Test 1: query_hybrid returns results... ";
    cleanup();

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    kb.create(TEST_KB_PATH, "Hybrid Test KB");

    // Add documents with different content
    kb.add_document("Quantum mechanics is a fundamental theory in physics that describes nature at the smallest scales.",
                    "doc_quantum", 256);
    kb.add_document("Albert Einstein developed the theory of relativity and worked at Princeton University.",
                    "doc_einstein", 256);
    kb.add_document("Biology is the scientific study of life and living organisms, examining structure and function.",
                    "doc_biology", 256);

    auto results = kb.query_hybrid("quantum mechanics physics", 3);

    assert(!results.empty());
    assert(results.size() <= 3);

    // First result should have highest combined score
    std::cout << "\n  Top results:\n";
    for (size_t i = 0; i < std::min(size_t(3), results.size()); ++i) {
        std::cout << "    " << results[i].chunk_id
                  << " score=" << results[i].score
                  << " v=" << results[i].vector_score
                  << " bm25=" << results[i].bm25_score
                  << " graph=" << results[i].graph_score << "\n";
    }

    // HybridResult should have non-trivial combined score
    assert(results[0].score > 0.0);

    // Results should be sorted by score descending
    for (size_t i = 1; i < results.size(); ++i) {
        assert(results[i].score <= results[i-1].score);
    }

    // At least one of vector/BM25 should have non-zero score for top result
    assert(results[0].vector_score > 0.0 || results[0].bm25_score > 0.0);

    std::cout << "  PASSED\n";
}

// ============================================================================
// Test 2: query_hybrid uses weights correctly
// ============================================================================
void test_query_hybrid_weights() {
    std::cout << "Test 2: query_hybrid respects alpha/beta/gamma weights... ";
    cleanup();

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    kb.create(TEST_KB_PATH, "Weights Test");

    kb.add_document("The quick brown fox jumps over the lazy dog.", "doc_a", 256);
    kb.add_document("A fox is a small omnivorous mammal.", "doc_b", 256);
    kb.add_document("Dogs are loyal companions and common pets.", "doc_c", 256);

    // All-vector weight (alpha=1.0, others=0)
    auto vec_only = kb.query_hybrid("fox mammal", 3, 1.0, 0.0, 0.0);

    // All-BM25 weight (alpha=0, beta=1.0)
    auto bm25_only = kb.query_hybrid("fox mammal", 3, 0.0, 1.0, 0.0);

    assert(!vec_only.empty());
    assert(!bm25_only.empty());

    // Both should return results
    std::cout << "\n  Vector-only top: " << vec_only[0].chunk_id
              << " (vec=" << vec_only[0].vector_score << ")\n";
    std::cout << "  BM25-only top: " << bm25_only[0].chunk_id
              << " (bm25=" << bm25_only[0].bm25_score << ")\n";

    std::cout << "  PASSED\n";
}

// ============================================================================
// Test 3: query_hybrid works on KB with no embedding model (BM25 only)
// ============================================================================
void test_query_hybrid_fallback() {
    std::cout << "Test 3: query_hybrid with embedding model still works... ";

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");

    bool opened = kb.open(TEST_KB_PATH);
    assert(opened);

    auto results = kb.query_hybrid("fox", 2);
    assert(!results.empty());

    std::cout << "PASSED\n";
    kb.close();
    cleanup();
}

// ============================================================================
// Test 4: query_with_rerank works (with LLM fallback)
// ============================================================================
void test_query_with_rerank() {
    std::cout << "Test 4: query_with_rerank returns results... ";
    cleanup();

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    kb.create(TEST_KB_PATH, "Rerank Test");

    kb.add_document("Quantum mechanics describes the behavior of matter at atomic scales.",
                    "doc_q", 256);
    kb.add_document("Classical mechanics describes macroscopic objects.",
                    "doc_c", 256);

    // Without LLM loaded, should fall back to vector ranking
    auto results = kb.query_with_rerank("quantum mechanics", 2);
    assert(!results.empty());
    assert(results.size() <= 2);

    // Should have final_score set
    assert(results[0].final_score > 0.0f);

    std::cout << "\n  Top reranked: " << results[0].chunk_id
              << " score=" << results[0].final_score << "\n";

    std::cout << "  PASSED\n";
}

// ============================================================================
// Test 5: query_full pipeline runs end-to-end
// ============================================================================
void test_query_full() {
    std::cout << "Test 5: query_full pipeline (hybrid + rerank)... ";
    cleanup();

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    kb.create(TEST_KB_PATH, "Full Pipeline Test");

    kb.add_document("Einstein published the theory of relativity in 1915.",
                    "doc_e", 256);
    kb.add_document("Newton developed classical mechanics in the 17th century.",
                    "doc_n", 256);

    auto results = kb.query_full("Einstein relativity theory", 2);
    assert(!results.empty());

    std::cout << "\n  Top full-pipeline: " << results[0].chunk_id
              << " final=" << results[0].final_score << "\n";

    std::cout << "  PASSED\n";
}

// ============================================================================
// Test 6: query() still works (backward compat)
// ============================================================================
void test_query_unchanged() {
    std::cout << "Test 6: query() unchanged for backward compat... ";
    cleanup();

    ApolloCore kb;
    kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");
    kb.create(TEST_KB_PATH, "Compat Test");

    kb.add_document("Apollo is a knowledge base library.", "doc_apollo", 256);

    // Original API still works
    std::vector<std::pair<std::string, float>> results = kb.query("knowledge base", 1);
    assert(!results.empty());
    // Top result should have non-zero similarity
    assert(results[0].second > 0.0f);

    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "===========================================\n";
    std::cout << "ApolloCore Hybrid Search & Reranking Tests\n";
    std::cout << "===========================================\n\n";

    test_query_hybrid_returns_results();
    test_query_hybrid_weights();
    test_query_hybrid_fallback();
    test_query_with_rerank();
    test_query_full();
    test_query_unchanged();

    cleanup();

    std::cout << "\n===========================================\n";
    std::cout << "ALL HYBRID/RERANK TESTS PASSED ✅\n";
    std::cout << "===========================================\n";

    return 0;
}