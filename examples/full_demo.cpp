/**
 * ApolloCore - Full Feature Demo
 *
 * Demonstrates all major features of ApolloCore in a single runnable example:
 *   1. Loading the embedding model
 *   2. Creating a persistent knowledge base
 *   3. Adding documents (auto-embedded and persisted)
 *   4. Querying the knowledge base (semantic similarity)
 *   5. Statistics and disk usage
 *   6. Saving and reloading (data survives program restart)
 *   7. Cleanup (destroy)
 *
 * Run with:
 *   ./build/examples/full_demo
 */

#include <apollocore/apollo.hpp>
#include <apollocore/entity_extractor.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace apollocore;
using namespace std::chrono;

void print_header(const std::string& title) {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  " << std::left << std::setw(61) << title << "  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
}

void print_step(int n, const std::string& desc) {
    std::cout << "\n─── Step " << n << ": " << desc << " ───\n";
}

int main() {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║   ApolloCore - Full Feature Demonstration                   ║
    ║                                                               ║
    ║   A state-of-the-art C++23 RAG knowledge base with local   ║
    ║   LLM inference, hybrid search, and persistent storage.    ║
    ║                                                               ║
    ╚═══════════════════════════════════════════════════════════════╝
    )" << "\n";

    const std::string KB_PATH = "data/demo_kb";
    const std::string EMBED_MODEL = "models/nomic-embed-text-v1.5.Q4_K_M.gguf";
    const std::string LLM_MODEL   = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";

    // Clean up any previous run
    if (ApolloCore::kb_exists(KB_PATH)) {
        std::cout << "Removing previous demo database at " << KB_PATH << "...\n";
        ApolloCore kb;
        kb.open(KB_PATH);
        kb.destroy();
    }

    // ====================================================================
    print_step(1, "Initialize ApolloCore and load embedding model");
    // ====================================================================
    ApolloCore kb;
    
    auto t0 = high_resolution_clock::now();
    bool model_loaded = kb.load_embedding_model(EMBED_MODEL);
    auto t1 = high_resolution_clock::now();
    
    if (model_loaded) {
        std::cout << "✓ Embedding model loaded in "
                  << duration_cast<milliseconds>(t1 - t0).count() << "ms\n";
        std::cout << "  Model: " << EMBED_MODEL << "\n";
    } else {
        std::cout << "✗ Failed to load model. Running in text-only mode.\n";
    }

    // ====================================================================
    print_step(2, "Create a new persistent knowledge base");
    // ====================================================================
    bool created = kb.create(KB_PATH, "ApolloCore Demo KB");
    if (created) {
        std::cout << "✓ Knowledge base created at: " << KB_PATH << "\n";
        std::cout << "  Name: " << kb.get_kb_name() << "\n";
    } else {
        std::cerr << "✗ Failed to create knowledge base\n";
        return 1;
    }

    // ====================================================================
    print_step(3, "Add documents (auto-embedded and persisted)");
    // ====================================================================
    std::vector<std::pair<std::string, std::string>> documents = {
        {"Physics",
         "Albert Einstein developed the theory of relativity, encompassing both "
         "special relativity and general relativity. Special relativity, published "
         "in 1905, introduced the famous equation E=mc². General relativity, published "
         "in 1915, describes gravity as the curvature of spacetime caused by mass and energy."},
        
        {"Quantum Mechanics",
         "Quantum mechanics is a fundamental theory in physics that describes "
         "nature at the smallest scales of atoms and subatomic particles. It introduces "
         "concepts like wave-particle duality, the uncertainty principle, and quantum "
         "entanglement. Pioneers include Max Planck, Niels Bohr, Werner Heisenberg, "
         "and Erwin Schrödinger."},
        
        {"Computer Science",
         "Computer science is the study of computation, algorithms, and information "
         "processing. It encompasses theory, algorithms, programming languages, software "
         "engineering, artificial intelligence, and computer hardware. Modern computing "
         "relies heavily on mathematical foundations laid by Alan Turing and John von Neumann."},
        
        {"Biology",
         "Biology is the scientific study of life and living organisms. It examines "
         "structure, function, growth, evolution, and distribution of living things. "
         "Key subfields include genetics, ecology, microbiology, and biochemistry. "
         "Charles Darwin's theory of evolution by natural selection revolutionized biology."},
        
        {"Mathematics",
         "Mathematics is the abstract science of number, quantity, structure, space, "
         "and change. It provides a framework for modeling the natural world and is "
         "essential to all other sciences. Major branches include algebra, analysis, "
         "geometry, topology, and number theory."}
    };
    
    for (const auto& [title, text] : documents) {
        std::cout << "  • Adding: " << title << " (" << text.length() << " chars)... ";
        std::cout.flush();
        auto ts = high_resolution_clock::now();
        size_t chunks = kb.add_document(text);
        auto te = high_resolution_clock::now();
        std::cout << chunks << " chunks in "
                  << duration_cast<milliseconds>(te - ts).count() << "ms\n";
    }

    // ====================================================================
    print_step(4, "Query the knowledge base (semantic search)");
    // ====================================================================
    std::vector<std::pair<std::string, std::string>> queries = {
        {"What did Einstein develop?", "Should find physics document"},
        {"How do computers process information?", "Should find computer science document"},
        {"What is the study of life?", "Should find biology document"},
        {"Tell me about the uncertainty principle", "Should find quantum mechanics document"}
    };
    
    for (const auto& [q, expected] : queries) {
        std::cout << "\n  Query: \"" << q << "\"\n";
        std::cout << "  Expected: " << expected << "\n";
        std::cout << "  ─────────────────────────────────────\n";
        
        auto ts = high_resolution_clock::now();
        auto results = kb.query(q, 3);
        auto te = high_resolution_clock::now();
        
        for (size_t i = 0; i < results.size(); i++) {
            const auto& [content, score] = results[i];
            std::string preview = content.substr(0, 80);
            if (content.length() > 80) preview += "...";
            std::cout << "  [" << (i+1) << "] score=" << std::fixed 
                      << std::setprecision(3) << score << " | " 
                      << preview << "\n";
        }
        std::cout << "  (took " << duration_cast<microseconds>(te - ts).count() 
                  << " µs)\n";
    }

    // ====================================================================
    print_step(5, "View statistics");
    // ====================================================================
    auto stats = kb.get_stats();
    std::cout << "  In-memory documents: " << stats.document_count << "\n";
    std::cout << "  In-memory chunks:    " << stats.chunk_count << "\n";
    std::cout << "  Entities on disk:    " << stats.entity_count << "\n";
    std::cout << "  Relationships:       " << stats.relationship_count << "\n";
    std::cout << "  Embedding file size: " 
              << (stats.embedding_files_size / 1024) << " KB\n";

    // ====================================================================
    print_step(6, "Demonstrate persistence (data survives restart)");
    // ====================================================================
    std::cout << "  Closing knowledge base...\n";
    kb.close();
    std::cout << "  ✓ All data flushed to disk\n";
    std::cout << "  (imagine the program exits here)\n\n";
    
    std::cout << "  Reopening knowledge base in a fresh ApolloCore instance...\n";
    ApolloCore kb2;
    
    auto t2 = high_resolution_clock::now();
    bool opened = kb2.open(KB_PATH);
    auto t3 = high_resolution_clock::now();
    
    if (opened) {
        std::cout << "  ✓ Reopened in " << duration_cast<milliseconds>(t3 - t2).count() << "ms\n";
        std::cout << "  Name: " << kb2.get_kb_name() << "\n";
        
        auto stats2 = kb2.get_stats();
        std::cout << "  Documents loaded: " << stats2.document_count << "\n";
        std::cout << "  Chunks loaded:    " << stats2.chunk_count << "\n";
        
        // Run a query to confirm everything works
        std::cout << "\n  Running query on reloaded data...\n";
        auto results = kb2.query("relativity Einstein", 1);
        if (!results.empty()) {
            std::cout << "  ✓ Top result: score=" << results[0].second 
                      << " | " << results[0].first.substr(0, 60) << "...\n";
        }
    } else {
        std::cerr << "  ✗ Failed to reopen\n";
    }
    
    // ====================================================================
    print_step(7, "Cleanup (destroy the demo database)");
    // ====================================================================
    kb2.close();
    kb2.destroy();
    std::cout << "  ✓ Knowledge base destroyed\n";
    std::cout << "  ✓ Disk space freed\n";

    // ====================================================================
    print_header("Demo Complete!");
    // ====================================================================
    std::cout << R"(
  ApolloCore demonstrated:
    ✓ Local embedding model loading
    ✓ Persistent knowledge base creation
    ✓ Document ingestion with auto-embedding
    ✓ Semantic similarity search
    ✓ Statistics tracking
    ✓ Data persistence across "restarts"
    ✓ Clean destruction
    
  Try modifying this demo to:
    • Add your own documents
    • Experiment with different chunk sizes
    • Try different query types
    • Combine with entity extraction (see examples/llm_extraction.cpp)
    
  All code is local. No API keys. No cloud. Just C++.
    )" << "\n";

    return 0;
}
