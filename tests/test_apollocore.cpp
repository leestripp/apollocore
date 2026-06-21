#include "apollocore/apollocore.hpp"
#include <iostream>
#include <cassert>

using namespace apollocore;

int main() {
    std::cout << "ApolloCore Basic Tests\n";
    std::cout << "====================\n\n";

    // Test 1: Create ApolloCore
    {
        ApolloCore kb;
        std::cout << "Test 1: Create ApolloCore - PASSED\n";
    }

    // Test 2: Create nodes
    {
        ApolloCore kb;
        auto root = kb.get_root();
        assert(root != nullptr);
        
        auto node1 = kb.create_node("Node 1", "Content 1");
        assert(node1 != nullptr);
        assert(kb.get_title(node1) == "Node 1");
        
        auto node2 = kb.create_node("Node 2", "Content 2", node1);
        assert(node2 != nullptr);
        
        auto children = kb.get_children(node1);
        assert(children.size() == 1);
        
        std::cout << "Test 2: Create nodes - PASSED\n";
    }

    // Test 3: Add chunks
    {
        ApolloCore kb;
        auto node = kb.create_node("Test", "Test content");
        
        auto chunk = kb.add_chunk("This is a test chunk", node);
        assert(chunk != nullptr);
        assert(chunk->has_embedding());
        
        auto chunks = kb.search_chunks("test");
        assert(chunks.size() >= 1);
        
        std::cout << "Test 3: Add and search chunks - PASSED\n";
    }

    // Test 4: Query
    {
        ApolloCore kb;
        auto node = kb.create_node("Science", "Scientific topics");
        kb.add_document("Quantum mechanics describes atomic behavior.", node);
        kb.add_document("Classical mechanics covers macroscopic objects.", node);
        
        auto results = kb.query("quantum");
        assert(results.total_hits >= 1);
        
        std::cout << "Test 4: Query - PASSED\n";
    }

    // Test 5: Stats
    {
        ApolloCore kb;
        kb.create_node("Node 1");
        kb.create_node("Node 2");
        
        auto stats = kb.get_stats();
        assert(stats.node_count >= 3); // +1 for root
        
        std::cout << "Test 5: Stats - PASSED\n";
    }

    std::cout << "\n====================\n";
    std::cout << "All tests passed!\n";
    return 0;
}
