/**
 * ApolloCore - Knowledge Base Persistence Tests
 */

// CRITICAL: Undefine NDEBUG to enable assert() in Release mode
#undef NDEBUG

#include "apollocore/kb_persistence.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace apollocore_v2;

const std::string TEST_KB_PATH = "/tmp/apollo_test_kb";

void cleanup() {
    if (std::filesystem::exists(TEST_KB_PATH)) {
        std::filesystem::remove_all(TEST_KB_PATH);
    }
}

void test_create() {
    std::cout << "Test: Create Knowledge Base... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test KB"));
    assert(kb.is_open());
    assert(kb.get_path() == TEST_KB_PATH);
    assert(kb.get_name() == "Test KB");
    
    // Verify directory structure
    assert(std::filesystem::exists(TEST_KB_PATH + "/apollo.db"));
    assert(std::filesystem::exists(TEST_KB_PATH + "/embeddings"));
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_create_already_exists() {
    std::cout << "Test: Create Already Exists Fails... ";
    
    cleanup();
    KnowledgeBasePersistence kb1;
    assert(kb1.create(TEST_KB_PATH, "Test KB"));
    kb1.close();
    
    KnowledgeBasePersistence kb2;
    assert(!kb2.create(TEST_KB_PATH, "Another KB"));
    
    std::cout << "PASSED\n";
}

void test_exists() {
    std::cout << "Test: Existence Check... ";
    
    cleanup();
    
    // Non-existent
    assert(!KnowledgeBasePersistence::exists(TEST_KB_PATH));
    
    // Create
    KnowledgeBasePersistence kb;
    kb.create(TEST_KB_PATH, "Test");
    
    // Now exists
    assert(KnowledgeBasePersistence::exists(TEST_KB_PATH));
    
    // Non-existent path
    assert(!KnowledgeBasePersistence::exists("/nonexistent/path"));
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_open_existing() {
    std::cout << "Test: Open Existing KB... ";
    
    cleanup();
    {
        KnowledgeBasePersistence kb;
        assert(kb.create(TEST_KB_PATH, "Test KB"));
        kb.close();
    }
    
    KnowledgeBasePersistence kb;
    assert(kb.open(TEST_KB_PATH));
    assert(kb.is_open());
    assert(kb.get_name() == "Test KB");
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_save_node() {
    std::cout << "Test: Save Node... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(kb.save_node(
        "node_001", "Einstein", "Albert Einstein bio",
        "", "biography", "{\"author\": \"Smith\"}"
    ));
    
    auto nodes = kb.load_all_nodes();
    assert(nodes.size() == 1);
    assert(nodes[0].id == "node_001");
    assert(nodes[0].title == "Einstein");
    assert(nodes[0].document_type == "biography");
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_save_chunk_with_embedding() {
    std::cout << "Test: Save Chunk with Embedding... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    // Create a 768-dim embedding
    std::vector<float> embedding(768);
    for (size_t i = 0; i < 768; i++) {
        embedding[i] = static_cast<float>(i) * 0.01f;
    }
    
    assert(kb.save_node("node_001", "Doc", "", "", "", ""));
    assert(kb.save_chunk(
        "chunk_001", "node_001", "Chunk content",
        0, embedding
    ));
    
    // Verify embedding file exists
    std::string emb_path = TEST_KB_PATH + "/embeddings/chunk_001.fbin";
    assert(std::filesystem::exists(emb_path));
    
    // Verify SQLite record
    auto chunks = kb.load_all_chunks();
    assert(chunks.size() == 1);
    assert(chunks[0].id == "chunk_001");
    assert(chunks[0].position == 0);
    
    // Verify embedding was saved correctly
    assert(!chunks[0].embedding.empty());
    assert(chunks[0].embedding.size() == 768);
    for (size_t i = 0; i < 768; i++) {
        assert(std::abs(chunks[0].embedding[i] - embedding[i]) < 1e-5f);
    }
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_save_entities() {
    std::cout << "Test: Save Entities... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(kb.save_node("node_001", "Doc", "", "", "", ""));
    std::vector<float> dummy_emb(64, 0.1f);
    assert(kb.save_chunk("chunk_001", "node_001", "Content", 0, dummy_emb));

    assert(kb.save_entity(
        "entity_001", "chunk_001", "Albert Einstein",
        "PERSON", "Famous physicist", 0.95f
    ));
    assert(kb.save_entity(
        "entity_002", "chunk_001", "Princeton",
        "ORGANIZATION", "University", 0.85f
    ));
    
    auto entities = kb.load_all_entities();
    assert(entities.size() == 2);
    
    bool found_einstein = false;
    for (const auto& e : entities) {
        if (e.name == "Albert Einstein") {
            found_einstein = true;
            assert(e.type == "PERSON");
            assert(std::abs(e.confidence - 0.95f) < 1e-6f);
        }
    }
    assert(found_einstein);
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_save_relationships() {
    std::cout << "Test: Save Relationships... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(kb.save_node("node_001", "Doc", "", "", "", ""));
    std::vector<float> emb2(64, 0.1f);
    assert(kb.save_chunk("chunk_001", "node_001", "Content", 0, emb2));
    
    assert(kb.save_entity("e1", "chunk_001", "Einstein", "PERSON", "", 0.9f));
    assert(kb.save_entity("e2", "chunk_001", "Princeton", "ORG", "", 0.9f));
    
    assert(kb.save_relationship(
        "rel_001", "e1", "e2", "WORKED_AT",
        "Einstein worked at Princeton", 0.95f
    ));
    
    auto rels = kb.load_all_relationships();
    assert(rels.size() == 1);
    assert(rels[0].type == "WORKED_AT");
    assert(rels[0].source_id == "e1");
    assert(rels[0].target_id == "e2");
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_bulk_transaction() {
    std::cout << "Test: Bulk Transaction... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(kb.begin_save());
    
    for (int i = 0; i < 100; i++) {
        assert(kb.save_node(
            "node_" + std::to_string(i),
            "Node " + std::to_string(i),
            "Content",
            "", "", ""
        ));
    }
    
    assert(kb.end_save());
    
    auto nodes = kb.load_all_nodes();
    assert(nodes.size() == 100);
    
    kb.close();
    std::cout << "PASSED (100 nodes in transaction)\n";
}

void test_persist_and_reload() {
    std::cout << "Test: Persist and Reload... ";
    
    cleanup();
    
    // Phase 1: Create and populate
    {
        KnowledgeBasePersistence kb;
        assert(kb.create(TEST_KB_PATH, "Test KB"));
        
        assert(kb.save_node("node_001", "Einstein", "Bio", "", "biography", ""));
        
        std::vector<float> emb(384, 0.5f);
        assert(kb.save_chunk("chunk_001", "node_001", "Theory of relativity", 0, emb));
        assert(kb.save_entity("e1", "chunk_001", "Albert Einstein", "PERSON", "", 0.95f));
        assert(kb.save_relationship("r1", "e1", "e1", "RELATED_TO", "", 0.9f));
        
        // Explicit save
        assert(kb.save());
        kb.close();
    }
    
    // Phase 2: Open and verify
    {
        KnowledgeBasePersistence kb;
        assert(kb.open(TEST_KB_PATH));
        
        auto nodes = kb.load_all_nodes();
        assert(nodes.size() == 1);
        assert(nodes[0].title == "Einstein");
        
        auto chunks = kb.load_all_chunks();
        assert(chunks.size() == 1);
        assert(chunks[0].embedding.size() == 384);
        
        auto entities = kb.load_all_entities();
        assert(entities.size() == 1);
        
        auto rels = kb.load_all_relationships();
        assert(rels.size() == 1);
        
        kb.close();
    }
    
    std::cout << "PASSED\n";
}

void test_clear() {
    std::cout << "Test: Clear... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    // Add some data
    std::vector<float> emb(128, 0.1f);
    assert(kb.save_node("n1", "T", "", "", "", ""));
    assert(kb.save_chunk("c1", "n1", "Content", 0, emb));
    
    assert(std::filesystem::exists(TEST_KB_PATH + "/embeddings/c1.fbin"));
    
    // Clear
    kb.clear();
    
    auto nodes = kb.load_all_nodes();
    assert(nodes.empty());
    
    auto chunks = kb.load_all_chunks();
    assert(chunks.empty());
    
    assert(!std::filesystem::exists(TEST_KB_PATH + "/embeddings/c1.fbin"));
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_destroy() {
    std::cout << "Test: Destroy... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(std::filesystem::exists(TEST_KB_PATH));
    
    assert(kb.destroy());
    
    assert(!std::filesystem::exists(TEST_KB_PATH));
    assert(kb.get_path().empty());
    
    std::cout << "PASSED\n";
}

void test_disk_stats() {
    std::cout << "Test: Disk Statistics... ";
    
    cleanup();
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Test"));
    
    assert(kb.save_node("n1", "T", "", "", "", ""));
    
    std::vector<float> emb(768, 0.5f);
    assert(kb.save_chunk("c1", "n1", "Content", 0, emb));
    assert(kb.save_entity("e1", "c1", "Name", "PERSON", "", 0.5f));
    assert(kb.save_relationship("r1", "e1", "e1", "T", "", 0.5f));
    
    auto stats = kb.get_disk_stats();
    assert(stats.total_nodes == 1);
    assert(stats.total_chunks == 1);
    assert(stats.total_entities == 1);
    assert(stats.total_relationships == 1);
    assert(stats.embedding_files_size == 3088);  // 16 + 768*4
    
    kb.close();
    std::cout << "PASSED\n";
}

void test_complete_workflow() {
    std::cout << "Test: Complete Workflow... ";
    
    cleanup();
    
    // Create
    KnowledgeBasePersistence kb;
    assert(kb.create(TEST_KB_PATH, "Einstein KB"));
    
    // Save documents
    assert(kb.save_node(
        "doc_einstein", "Albert Einstein",
        "Albert Einstein was a theoretical physicist.",
        "", "biography", "{\"year\": 1879}"
    ));
    
    assert(kb.save_node(
        "doc_princeton", "Princeton University",
        "Princeton is a private research university.",
        "", "organization", ""
    ));
    
    // Save chunks with embeddings
    std::vector<float> emb_einstein(768);
    std::vector<float> emb_princeton(768);
    for (size_t i = 0; i < 768; i++) {
        emb_einstein[i] = static_cast<float>(i) / 1000.0f;
        emb_princeton[i] = static_cast<float>(768 - i) / 1000.0f;
    }
    
    assert(kb.save_chunk(
        "chunk_e1", "doc_einstein",
        "Einstein developed the theory of relativity.",
        0, emb_einstein
    ));
    assert(kb.save_chunk(
        "chunk_p1", "doc_princeton",
        "Princeton is located in New Jersey.",
        0, emb_princeton
    ));
    
    // Save entities
    assert(kb.save_entity(
        "e_albert", "chunk_e1", "Albert Einstein", "PERSON",
        "Theoretical physicist", 0.99f
    ));
    assert(kb.save_entity(
        "e_princeton", "chunk_p1", "Princeton University", "ORGANIZATION",
        "Research university", 0.99f
    ));
    
    // Save relationship
    assert(kb.save_relationship(
        "r_worked", "e_albert", "e_princeton", "WORKED_AT",
        "Einstein worked at Princeton", 0.95f
    ));
    
    // Get stats
    auto stats = kb.get_disk_stats();
    assert(stats.total_nodes == 2);
    assert(stats.total_chunks == 2);
    assert(stats.total_entities == 2);
    assert(stats.total_relationships == 1);
    
    // Close
    kb.close();
    
    // Reopen
    KnowledgeBasePersistence kb2;
    assert(kb2.open(TEST_KB_PATH));
    
    auto chunks = kb2.load_all_chunks();
    assert(chunks.size() == 2);
    assert(chunks[0].embedding.size() == 768);
    assert(chunks[1].embedding.size() == 768);
    
    kb2.close();
    
    std::cout << "PASSED (full workflow)\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   ApolloCore - Knowledge Base Persistence Tests          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_create();
    test_create_already_exists();
    test_exists();
    test_open_existing();
    test_save_node();
    test_save_chunk_with_embedding();
    test_save_entities();
    test_save_relationships();
    test_bulk_transaction();
    test_persist_and_reload();
    test_clear();
    test_destroy();
    test_disk_stats();
    test_complete_workflow();
    
    cleanup();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All KB Persistence Tests PASSED                      ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Create KB: PASSED\n";
    std::cout << "   • Create fails if exists: PASSED\n";
    std::cout << "   • Existence check: PASSED\n";
    std::cout << "   • Open existing: PASSED\n";
    std::cout << "   • Save node: PASSED\n";
    std::cout << "   • Save chunk with embedding: PASSED\n";
    std::cout << "   • Save entities: PASSED\n";
    std::cout << "   • Save relationships: PASSED\n";
    std::cout << "   • Bulk transactions: PASSED\n";
    std::cout << "   • Persist and reload: PASSED\n";
    std::cout << "   • Clear all data: PASSED\n";
    std::cout << "   • Destroy (delete all): PASSED\n";
    std::cout << "   • Disk statistics: PASSED\n";
    std::cout << "   • Complete workflow: PASSED\n";
    std::cout << "\n";
    std::cout << "💾 Storage Layout:\n";
    std::cout << "   /tmp/apollo_test_kb/\n";
    std::cout << "   ├── apollo.db         # SQLite metadata\n";
    std::cout << "   ├── apollo.db-wal     # Write-Ahead Log\n";
    std::cout << "   └── embeddings/\n";
    std::cout << "       └── *.fbin         # Binary embeddings\n";
    std::cout << "\n";
    
    return 0;
}
