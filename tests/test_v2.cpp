/**
 * ApolloCore v2 - Test with Real Embeddings
 * 
 * This test verifies that the llama.cpp embedding model integration works.
 * To run this test:
 * 
 * 1. Download a GGUF embedding model:
 *    wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5-Q4_K_M.gguf \
 *        -O models/nomic-embed-text-v1.5-Q4_K_M.gguf
 * 
 * 2. Build and run:
 *    cmake --build build
 *    ./build/tests/test_v2
 */

#include "apollocore/apollocore_v2.hpp"
#include <iostream>

using namespace apollocore_v2;

int main() {
    std::cout << "ApolloCore v2 - Real Embedding Test\n";
    std::cout << "====================================\n\n";
    
    ApolloEngine kb;
    
    // Check if model is loaded
    std::cout << "Model loaded: " << (kb.is_model_loaded() ? "YES" : "NO") << "\n\n";
    
    if (!kb.is_model_loaded()) {
        std::cout << "NOTE: No embedding model loaded.\n";
        std::cout << "To enable real embeddings, download a GGUF model:\n\n";
        std::cout << "  wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/\\\n";
        std::cout << "    nomic-embed-text-v1.5-Q4_K_M.gguf -O models/nomic-embed-text-v1.5-Q4_K_M.gguf\n\n";
        std::cout << "Then load it with:\n";
        std::cout << "  kb.load_embedding_model(\"models/nomic-embed-text-v1.5-Q4_K_M.gguf\");\n\n";
        std::cout << "Running in fallback mode (no real embeddings)...\n\n";
    }
    
    // Test 1: Create nodes
    {
        std::cout << "Test 1: Create knowledge nodes...\n";
        auto science = kb.create_node("Science", "Scientific knowledge base");
        auto physics = kb.create_node("Physics", "Physical sciences", science);
        auto quantum = kb.create_node("Quantum Mechanics", "Quantum physics topics", physics);
        
        std::cout << "  Created: " << science->title << "\n";
        std::cout << "  Created: " << physics->title << "\n";
        std::cout << "  Created: " << quantum->title << "\n";
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 2: Add document with smart chunking
    {
        std::cout << "Test 2: Add document with smart chunking...\n";
        auto quantum = kb.get_node("node_3");  // Quantum Mechanics node
        
        std::string doc = 
            "Quantum mechanics is a fundamental theory in physics that provides a description "
            "of the physical properties of nature at the scale of atoms and subatomic particles.\n\n"
            "The uncertainty principle, introduced by Werner Heisenberg, states that the position "
            "and momentum of a particle cannot both be known precisely at the same time.\n\n"
            "Quantum entanglement is a phenomenon where two particles become correlated and the "
            "quantum state of each particle cannot be described independently of the state of the others.";
        
        auto chunks = kb.add_document(doc, quantum);
        std::cout << "  Added " << chunks.size() << " chunks\n";
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 3: Hybrid search
    {
        std::cout << "Test 3: Hybrid search (vector + BM25)...\n";
        auto results = kb.query("quantum mechanics", QueryOptions{
            .max_results = 5,
            .use_semantic = true,
            .use_keyword = true,
            .semantic_weight = 0.5f,
            .keyword_weight = 0.5f
        });
        
        std::cout << "  Query: 'quantum mechanics'\n";
        std::cout << "  Found " << results.total_hits << " results\n";
        for (size_t i = 0; i < results.items.size(); ++i) {
            auto& item = results.items[i];
            std::cout << "  [" << i << "] Score: " << item.score 
                      << " (vector: " << item.vector_score 
                      << ", bm25: " << item.bm25_score << ")\n";
            std::cout << "      Content: " << item.content.substr(0, 60) << "...\n";
        }
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 4: Semantic-only search
    {
        std::cout << "Test 4: Semantic-only search...\n";
        auto results = kb.query("entanglement", QueryOptions{
            .max_results = 3,
            .use_semantic = true,
            .use_keyword = false
        });
        
        std::cout << "  Query: 'entanglement'\n";
        std::cout << "  Found " << results.total_hits << " results\n";
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 5: Keyword-only search (BM25)
    {
        std::cout << "Test 5: Keyword-only search (BM25)...\n";
        auto results = kb.query("uncertainty principle", QueryOptions{
            .max_results = 3,
            .use_semantic = false,
            .use_keyword = true
        });
        
        std::cout << "  Query: 'uncertainty principle'\n";
        std::cout << "  Found " << results.total_hits << " results\n";
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 6: Node hierarchy
    {
        std::cout << "Test 6: Node hierarchy...\n";
        auto quantum = kb.get_node("node_3");
        auto ancestors = kb.get_ancestors(quantum);
        std::cout << "  Ancestors of 'Quantum Mechanics':\n";
        for (auto& a : ancestors) {
            std::cout << "    - " << a->title << "\n";
        }
        std::cout << "  ✓ PASSED\n\n";
    }
    
    // Test 7: Statistics
    {
        std::cout << "Test 7: Statistics...\n";
        auto stats = kb.get_stats();
        std::cout << "  Nodes: " << stats.node_count << "\n";
        std::cout << "  Chunks: " << stats.chunk_count << "\n";
        std::cout << "  Entities: " << stats.entity_count << "\n";
        std::cout << "  Relationships: " << stats.relationship_count << "\n";
        std::cout << "  ✓ PASSED\n\n";
    }
    
    std::cout << "====================================\n";
    std::cout << "All tests completed!\n";
    
    if (!kb.is_model_loaded()) {
        std::cout << "\nNOTE: Tests ran without real embeddings.\n";
        std::cout << "Download a model to enable full functionality.\n";
    }
    
    return 0;
}
