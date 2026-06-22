/**
 * ApolloCore - Test Real Embeddings with Nomic Model
 */

#include <apollocore/apollocore_v2.hpp>
#include <iostream>

int main() {
    using namespace apollocore_v2;
    
    std::cout << "ApolloCore - Real Embedding Test with Nomic\n";
    std::cout << "=============================================\n\n";
    
    ApolloEngine kb;
    
    // Load the embedding model
    const std::string model_path = "models/nomic-embed-text-v1.5-Q4_K_M.gguf";
    std::cout << "Loading embedding model: " << model_path << "\n";
    
    if (!kb.load_embedding_model(model_path, false)) {  // false = CPU only (no GPU in container)
        std::cerr << "ERROR: Failed to load embedding model!\n";
        return 1;
    }
    
    std::cout << "Model loaded successfully!\n\n";
    
    // Create knowledge hierarchy
    std::cout << "Creating knowledge base...\n";
    auto science = kb.create_node("Science", "Scientific knowledge base");
    auto physics = kb.create_node("Physics", "Physical sciences", science);
    auto quantum = kb.create_node("Quantum Mechanics", "Quantum physics topics", physics);
    
    std::cout << "  - Created: " << science->title << "\n";
    std::cout << "  - Created: " << physics->title << "\n";
    std::cout << "  - Created: " << quantum->title << "\n\n";
    
    // Add documents
    std::cout << "Adding documents...\n";
    auto chunks1 = kb.add_document(
        "Quantum mechanics describes the behavior of matter and energy at atomic "
        "and subatomic scales. The uncertainty principle states that certain pairs "
        "of physical properties cannot both be known to arbitrary precision.",
        quantum
    );
    std::cout << "  - Added " << chunks1.size() << " chunks to Quantum Mechanics\n";
    
    auto chunks2 = kb.add_document(
        "Albert Einstein developed the theory of relativity. "
        "E equals mc squared is the most famous equation from special relativity.",
        physics
    );
    std::cout << "  - Added " << chunks2.size() << " chunks to Physics\n\n";
    
    // Test embedding dimensions
    std::cout << "Testing embedding...\n";
    EmbeddingModel emb;
    if (emb.load(model_path, false)) {
        auto result = emb.embed("This is a test sentence for embedding.");
        std::cout << "  - Embedding dimensions: " << result.dimensions << "\n";
        std::cout << "  - Model name: " << result.model_name << "\n";
        if (!result.vector.empty()) {
            std::cout << "  - First 5 values: ";
            for (int i = 0; i < 5 && i < (int)result.vector.size(); i++) {
                std::cout << result.vector[i] << " ";
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
    
    // Hybrid search test
    std::cout << "Testing hybrid search...\n";
    auto results = kb.query("quantum mechanics uncertainty principle", QueryOptions{
        .max_results = 5,
        .use_semantic = true,
        .use_keyword = true,
        .semantic_weight = 0.5f,
        .keyword_weight = 0.5f
    });
    
    std::cout << "  Query: 'quantum mechanics uncertainty principle'\n";
    std::cout << "  Results: " << results.total_hits << "\n";
    for (size_t i = 0; i < results.items.size(); ++i) {
        auto& r = results.items[i];
        std::cout << "  [" << i << "] Score: " << r.score 
                  << " (vec: " << r.vector_score << ", bm25: " << r.bm25_score << ")\n";
        std::cout << "      Content: " << r.content.substr(0, 60) << "...\n";
    }
    std::cout << "\n";
    
    // Semantic-only search
    std::cout << "Testing semantic search (vector only)...\n";
    auto semantic = kb.query("atomic particles energy", QueryOptions{
        .use_semantic = true,
        .use_keyword = false
    });
    std::cout << "  Query: 'atomic particles energy'\n";
    std::cout << "  Results: " << semantic.total_hits << "\n";
    for (size_t i = 0; i < semantic.items.size(); ++i) {
        auto& r = semantic.items[i];
        std::cout << "  [" << i << "] Score: " << r.vector_score << "\n";
    }
    std::cout << "\n";
    
    // Statistics
    auto stats = kb.get_stats();
    std::cout << "Statistics:\n";
    std::cout << "  - Nodes: " << stats.node_count << "\n";
    std::cout << "  - Chunks: " << stats.chunk_count << "\n";
    std::cout << "  - Entities: " << stats.entity_count << "\n";
    std::cout << "  - Relationships: " << stats.relationship_count << "\n";
    
    std::cout << "\n=============================================\n";
    std::cout << "Test completed successfully!\n";
    
    return 0;
}
