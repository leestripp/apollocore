/**
 * ApolloCore - Simple Usage Example
 * 
 * Demonstrates the v2 API with hybrid search capabilities.
 */

#include <apollocore/apollocore_v2.hpp>
#include <iostream>

int main() {
    using namespace apollocore_v2;

    std::cout << "ApolloCore Simple Usage Example\n";
    std::cout << "===============================\n\n";
    
    ApolloEngine kb;
    
    // Load embedding model
    std::cout << "0. Loading embedding model...\n";
    std::string model_path = "models/nomic-embed-text-v1.5.Q4_K_M.gguf";
    if (!kb.load_embedding_model(model_path, false)) {
        std::cout << "   WARNING: Could not load embedding model from " << model_path << "\n";
        std::cout << "   Semantic search will not work.\n\n";
    } else {
        std::cout << "   Loaded: " << model_path << "\n\n";
    }
    
    // Create knowledge hierarchy
    std::cout << "1. Creating knowledge hierarchy...\n";
    auto science = kb.create_node("Science", "Scientific knowledge base");
    auto physics = kb.create_node("Physics", "Physical sciences", science);
    auto quantum = kb.create_node("Quantum Mechanics", "Quantum physics topics", physics);
    auto relativity = kb.create_node("Relativity", "Einstein's theories", physics);
    
    std::cout << "   Created: " << science->title << "\n";
    std::cout << "   Created: " << physics->title << "\n";
    std::cout << "   Created: " << quantum->title << "\n";
    std::cout << "   Created: " << relativity->title << "\n\n";
    
    // Add documents
    std::cout << "2. Adding documents...\n";
    auto quantum_chunks = kb.add_document(
        "Quantum mechanics describes the behavior of matter and energy at atomic "
        "and subatomic scales. The theory is characterized by wave-particle duality, "
        "the uncertainty principle, and quantum entanglement. "
        "The uncertainty principle states that certain pairs of physical properties, "
        "like position and momentum, cannot both be known to arbitrary precision."
        "Quantum entanglement is a phenomenon where particles become connected and "
        "the quantum state of one instantly influences the other, regardless of distance.",
        quantum
    );
    std::cout << "   Added " << quantum_chunks.size() << " chunks to Quantum Mechanics\n";
    
    auto relativity_chunks = kb.add_document(
        "Albert Einstein developed the theory of relativity, which encompasses "
        "both special relativity and general relativity. "
        "Special relativity introduced the concept that the speed of light is "
        "constant for all observers, regardless of their relative motion. "
        "General relativity describes gravity as the curvature of spacetime "
        "caused by mass and energy.",
        relativity
    );
    std::cout << "   Added " << relativity_chunks.size() << " chunks to Relativity\n\n";
    
    // Hybrid search
    std::cout << "3. Hybrid search (vector + BM25)...\n";
    auto results = kb.query("quantum mechanics", QueryOptions{
        .max_results = 5,
        .use_semantic = true,
        .use_keyword = true,
        .semantic_weight = 0.5f,
        .keyword_weight = 0.5f
    });
    std::cout << "   Query: 'quantum mechanics'\n";
    std::cout << "   Found " << results.total_hits << " results\n";
    for (size_t i = 0; i < results.items.size() && i < 3; ++i) {
        auto& r = results.items[i];
        std::cout << "   [" << i << "] Score: " << r.score 
                  << " (vec: " << r.vector_score << ", bm25: " << r.bm25_score << ")\n";
    }
    std::cout << "\n";
    
    // Semantic-only search
    std::cout << "4. Semantic-only search...\n";
    auto semantic_results = kb.query("entanglement", QueryOptions{
        .max_results = 3,
        .use_semantic = true,
        .use_keyword = false
    });
    std::cout << "   Query: 'entanglement'\n";
    std::cout << "   Found " << semantic_results.total_hits << " results\n";
    for (size_t i = 0; i < semantic_results.items.size() && i < 2; ++i) {
        auto& r = semantic_results.items[i];
        std::cout << "   [" << i << "] Content: " << r.content.substr(0, 50) << "...\n";
    }
    std::cout << "\n";
    
    // Keyword-only search (BM25)
    std::cout << "5. Keyword-only search (BM25)...\n";
    auto keyword_results = kb.query("Einstein relativity", QueryOptions{
        .max_results = 3,
        .use_semantic = false,
        .use_keyword = true
    });
    std::cout << "   Query: 'Einstein relativity'\n";
    std::cout << "   Found " << keyword_results.total_hits << " results\n";
    for (size_t i = 0; i < keyword_results.items.size() && i < 2; ++i) {
        auto& r = keyword_results.items[i];
        std::cout << "   [" << i << "] Content: " << r.content.substr(0, 50) << "...\n";
    }
    std::cout << "\n";
    
    // Node hierarchy
    std::cout << "6. Node hierarchy...\n";
    auto ancestors = kb.get_ancestors(quantum);
    std::cout << "   Ancestors of 'Quantum Mechanics':\n";
    for (auto& a : ancestors) {
        std::cout << "   - " << a->title << "\n";
    }
    std::cout << "\n";
    
    // Statistics
    std::cout << "7. Statistics...\n";
    auto stats = kb.get_stats();
    std::cout << "   Nodes: " << stats.node_count << "\n";
    std::cout << "   Chunks: " << stats.chunk_count << "\n";
    std::cout << "   Entities: " << stats.entity_count << "\n";
    std::cout << "   Relationships: " << stats.relationship_count << "\n";
    
    std::cout << "\n===============================\n";
    std::cout << "Example completed successfully!\n";
    
    if (!kb.is_model_loaded()) {
        std::cout << "\nNOTE: No embedding model loaded.\n";
        std::cout << "For real embeddings, download:\n";
        std::cout << "  wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/\n";
        std::cout << "    nomic-embed-text-v1.5-Q4_K_M.gguf -O models/\n";
    }
    
    return 0;
}
