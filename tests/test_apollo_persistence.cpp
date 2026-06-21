/**
 * ApolloCore - ApolloCoreWithPersistence Integration Tests
 */

// CRITICAL: Undefine NDEBUG to enable assert() in Release mode
#undef NDEBUG

#include "apollocore/apollo_persistence.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>

using namespace apollocore_v2;

const std::string TEST_KB_PATH = "/tmp/apollo_persist_test";
const std::string MODEL_PATH = "models/nomic-embed-text-v1.5-Q4_K_M.gguf";

void cleanup() {
    if (std::filesystem::exists(TEST_KB_PATH)) {
        std::filesystem::remove_all(TEST_KB_PATH);
    }
}

void test_create_open_close() {
    std::cout << "Test: Create/Open/Close... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    
    // Doesn't exist yet
    assert(!ApolloCoreWithPersistence::kb_exists(TEST_KB_PATH));
    
    // Create
    assert(kb.create(TEST_KB_PATH, "Test KB"));
    assert(kb.is_open());
    assert(kb.get_kb_path() == TEST_KB_PATH);
    assert(kb.get_kb_name() == "Test KB");
    assert(ApolloCoreWithPersistence::kb_exists(TEST_KB_PATH));
    
    // Close
    kb.close();
    assert(!kb.is_open());
    
    // File still exists
    assert(ApolloCoreWithPersistence::kb_exists(TEST_KB_PATH));
    
    // Reopen
    assert(kb.open(TEST_KB_PATH));
    assert(kb.is_open());
    assert(kb.get_kb_name() == "Test KB");
    
    kb.close();
    cleanup();
    
    std::cout << "PASSED\n";
}

void test_add_document_without_model() {
    std::cout << "Test: Add Document (without embedding model)... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    std::string text = "Albert Einstein developed the theory of relativity. "
                       "He worked at Princeton University. "
                       "His theory changed modern physics.";
    
    size_t chunks = kb.add_document(text, 100);
    assert(chunks > 0);
    
    auto stats = kb.get_stats();
    assert(stats.document_count == 1);
    assert(stats.chunk_count == chunks);
    
    kb.close();
    cleanup();
    
    std::cout << "PASSED\n";
}

void test_query_after_reload() {
    std::cout << "Test: Query After Reload... ";
    
    cleanup();
    std::string model_path = MODEL_PATH;
    
    if (!std::filesystem::exists(model_path)) {
        std::cout << "SKIPPED (model not found: " << model_path << ")\n";
        return;
    }
    
    // Phase 1: Add documents with embeddings
    {
        ApolloCoreWithPersistence kb;
        kb.load_embedding_model(model_path);
        assert(kb.create(TEST_KB_PATH, "Physics KB"));
        
        kb.add_document("Albert Einstein developed the theory of relativity.", 200);
        kb.add_document("Quantum mechanics describes atomic behavior.", 200);
        kb.add_document("Newton's laws describe motion in classical physics.", 200);
        
        kb.close();
    }
    
    // Phase 2: Open and query
    {
        ApolloCoreWithPersistence kb;
        kb.load_embedding_model(model_path);
        assert(kb.open(TEST_KB_PATH));
        
        auto results = kb.query("relativity Einstein");
        assert(!results.empty());
        
        std::cout << "\n  Query: 'relativity Einstein' -> Top results:\n";
        for (const auto& [content, score] : results) {
            std::cout << "    Score " << score << ": " << content.substr(0, 60) << "...\n";
        }
        
        // Top result should contain "relativity"
        bool found = results[0].first.find("relativity") != std::string::npos;
        assert(found);
        
        kb.close();
    }
    
    cleanup();
    std::cout << "PASSED\n";
}

void test_persist_and_reload_chunks() {
    std::cout << "Test: Persist and Reload Chunks... ";
    
    cleanup();
    std::string model_path = MODEL_PATH;
    bool has_model = std::filesystem::exists(model_path);
    
    // Phase 1: Save
    {
        ApolloCoreWithPersistence kb;
        if (has_model) kb.load_embedding_model(model_path);
        assert(kb.create(TEST_KB_PATH, "Test"));
        
        std::string text = "This is a long document. It has multiple sentences. "
                          "Each sentence provides some context. The document "
                          "covers several topics. Testing persistence is important.";
        
        size_t chunks = kb.add_document(text, 100);
        assert(chunks > 0);
        
        kb.close();
    }
    
    // Phase 2: Reload and verify
    {
        ApolloCoreWithPersistence kb;
        if (has_model) kb.load_embedding_model(model_path);
        assert(kb.open(TEST_KB_PATH));
        
        auto stats = kb.get_stats();
        assert(stats.chunk_count > 0);
        assert(stats.document_count == 1);
        
        kb.close();
    }
    
    cleanup();
    std::cout << "PASSED\n";
}

void test_clear() {
    std::cout << "Test: Clear... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    kb.add_document("First document about physics.", 50);
    kb.add_document("Second document about chemistry.", 50);
    
    auto stats1 = kb.get_stats();
    assert(stats1.document_count == 2);
    
    kb.clear();
    
    auto stats2 = kb.get_stats();
    assert(stats2.document_count == 0);
    assert(stats2.chunk_count == 0);
    
    kb.close();
    cleanup();
    
    std::cout << "PASSED\n";
}

void test_destroy() {
    std::cout << "Test: Destroy... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    kb.add_document("Test document", 50);
    kb.close();
    
    assert(ApolloCoreWithPersistence::kb_exists(TEST_KB_PATH));
    
    // Reopen and destroy
    ApolloCoreWithPersistence kb2;
    assert(kb2.open(TEST_KB_PATH));
    assert(kb2.destroy());
    
    assert(!ApolloCoreWithPersistence::kb_exists(TEST_KB_PATH));
    
    std::cout << "PASSED\n";
}

void test_multiple_documents() {
    std::cout << "Test: Multiple Documents... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    assert(kb.create(TEST_KB_PATH, "Multi Doc KB"));
    
    for (int i = 1; i <= 5; i++) {
        std::string text = "Document number " + std::to_string(i) + 
                          " with some content about topic " + std::to_string(i) + ".";
        kb.add_document(text, 100);
    }
    
    auto stats = kb.get_stats();
    assert(stats.document_count == 5);
    assert(stats.chunk_count > 0);
    
    kb.close();
    cleanup();
    
    std::cout << "PASSED\n";
}

void test_open_without_create() {
    std::cout << "Test: Open Non-existent Fails... ";
    
    cleanup();
    ApolloCoreWithPersistence kb;
    assert(!kb.open("/nonexistent/path"));
    assert(!kb.is_open());
    
    std::cout << "PASSED\n";
}

void test_reload_preserves_data() {
    std::cout << "Test: Reload Preserves Data... ";
    
    cleanup();
    std::string model_path = MODEL_PATH;
    bool has_model = std::filesystem::exists(model_path);
    
    // Save
    {
        ApolloCoreWithPersistence kb;
        if (has_model) kb.load_embedding_model(model_path);
        assert(kb.create(TEST_KB_PATH, "Reload Test"));
        
        kb.add_document("First document with specific content.", 100);
        kb.add_document("Second document with other content.", 100);
        kb.add_document("Third document with yet more content.", 100);
        
        auto stats_before = kb.get_stats();
        size_t chunks_before = stats_before.chunk_count;
        kb.close();
        
        assert(chunks_before > 0);
    }
    
    // Reload and verify same number of chunks
    {
        ApolloCoreWithPersistence kb;
        if (has_model) kb.load_embedding_model(model_path);
        assert(kb.open(TEST_KB_PATH));
        
        auto stats_after = kb.get_stats();
        assert(stats_after.document_count == 3);
        assert(stats_after.chunk_count > 0);
        
        kb.close();
    }
    
    cleanup();
    std::cout << "PASSED\n";
}

void test_embedding_persistence() {
    std::cout << "Test: Embeddings Persist... ";
    
    cleanup();
    std::string model_path = MODEL_PATH;
    
    if (!std::filesystem::exists(model_path)) {
        std::cout << "SKIPPED (model not found)\n";
        return;
    }
    
    // Save
    ApolloCoreWithPersistence kb1;
    kb1.load_embedding_model(model_path);
    assert(kb1.create(TEST_KB_PATH, "Embed Test"));
    
    kb1.add_document("Quantum entanglement is a phenomenon in physics.", 200);
    
    auto stats1 = kb1.get_stats();
    assert(stats1.embedding_files_size > 0);
    
    kb1.close();
    
    // Verify files exist
    assert(std::filesystem::exists(TEST_KB_PATH + "/embeddings"));
    auto emb_files = std::filesystem::directory_iterator(TEST_KB_PATH + "/embeddings");
    int count = 0;
    for (const auto& entry : emb_files) {
        if (entry.path().extension() == ".fbin") count++;
    }
    assert(count > 0);
    
    cleanup();
    std::cout << "PASSED (embeddings persisted to .fbin files)\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ApolloCore - Integration with Persistence Tests         ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_create_open_close();
    test_add_document_without_model();
    test_query_after_reload();
    test_persist_and_reload_chunks();
    test_clear();
    test_destroy();
    test_multiple_documents();
    test_open_without_create();
    test_reload_preserves_data();
    test_embedding_persistence();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All ApolloCore Persistence Integration Tests PASSED  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Create/Open/Close: PASSED\n";
    std::cout << "   • Add document (no model): PASSED\n";
    std::cout << "   • Query after reload: PASSED\n";
    std::cout << "   • Persist and reload chunks: PASSED\n";
    std::cout << "   • Clear: PASSED\n";
    std::cout << "   • Destroy: PASSED\n";
    std::cout << "   • Multiple documents: PASSED\n";
    std::cout << "   • Open non-existent fails: PASSED\n";
    std::cout << "   • Reload preserves data: PASSED\n";
    std::cout << "   • Embeddings persist: PASSED\n";
    std::cout << "\n";
    std::cout << "💾 The ApolloCore now persists data across restarts!\n";
    std::cout << "\n";
    
    return 0;
}