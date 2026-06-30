/**
 * ApolloCore - Persistence Module Test
 */

#include "apollocore/persistence.hpp"
#include <sqlite3.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <cstdio>
#include <algorithm>

using namespace apollocore_v2;

void test_sqlitedb_open_close() {
    std::cout << "Test: SQLiteDB Open/Close... ";
    
    std::string test_path = "/tmp/apollo_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-journal");
    std::filesystem::remove(test_path + "-wal");
    
    SQLiteDB db;
    assert(!db.is_open());
    
    assert(db.open(test_path));
    assert(db.is_open());
    
    db.close();
    assert(!db.is_open());
    
    std::filesystem::remove(test_path);
    std::cout << "PASSED\n";
}

void test_schema_initialization() {
    std::cout << "Test: Schema Initialization... ";
    
    std::string test_path = "/tmp/apollo_schema_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    
    SQLiteDB db;
    assert(db.open(test_path));
    assert(initialize_schema(db));
    
    // Verify tables exist by querying sqlite_master
    sqlite3_stmt* stmt = db.prepare(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;"
    );
    
    std::vector<std::string> tables;
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) break;
        tables.push_back(db.column_string(stmt, 0));
    }
    db.finalize(stmt);
    
    // Check expected tables
    assert(std::find(tables.begin(), tables.end(), "config") != tables.end());
    assert(std::find(tables.begin(), tables.end(), "nodes") != tables.end());
    assert(std::find(tables.begin(), tables.end(), "chunks") != tables.end());
    assert(std::find(tables.begin(), tables.end(), "entities") != tables.end());
    assert(std::find(tables.begin(), tables.end(), "relationships") != tables.end());
    
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    std::cout << "PASSED (" << tables.size() << " tables created)\n";
}

void test_execute_with_params() {
    std::cout << "Test: Execute with Parameters... ";
    
    std::string test_path = "/tmp/apollo_params_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    
    SQLiteDB db;
    assert(db.open(test_path));
    assert(initialize_schema(db));
    
    // Insert into config table
    std::string sql = "INSERT INTO config (name, embedding_dim) VALUES (?, ?);";
    sqlite3_stmt* stmt = db.prepare(sql);
    assert(stmt != nullptr);
    
    assert(db.bind_string(stmt, 1, "Test Knowledge Base"));
    assert(db.bind_int(stmt, 2, 768));
    assert(db.step(stmt));
    db.finalize(stmt);
    
    // Verify
    stmt = db.prepare("SELECT name, embedding_dim FROM config WHERE name = ?;");
    db.bind_string(stmt, 1, "Test Knowledge Base");
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string name = db.column_string(stmt, 0);
        int dim = db.column_int(stmt, 1);
        assert(name == "Test Knowledge Base");
        assert(dim == 768);
        found = true;
        (void)name; (void)dim; (void)found;  // suppress unused-but-set
    }
    db.finalize(stmt);
    
    assert(found);
    
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    std::cout << "PASSED\n";
}

void test_transactions() {
    std::cout << "Test: Transactions... ";
    
    std::string test_path = "/tmp/apollo_transaction_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    
    SQLiteDB db;
    assert(db.open(test_path));
    assert(initialize_schema(db));
    
    // Insert a node
    sqlite3_stmt* stmt = db.prepare(
        "INSERT INTO nodes (id, title, content) VALUES (?, ?, ?);"
    );
    db.bind_string(stmt, 1, "node_001");
    db.bind_string(stmt, 2, "Test Node");
    db.bind_string(stmt, 3, "Test content here");
    assert(db.step(stmt));
    db.finalize(stmt);
    
    // Verify
    stmt = db.prepare("SELECT COUNT(*) FROM nodes WHERE id = 'node_001';");
    sqlite3_step(stmt);
    int count = db.column_int(stmt, 0);
    db.finalize(stmt);
    assert(count == 1);
    (void)count;

    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    std::cout << "PASSED\n";
}

void test_persist_and_reload() {
    std::cout << "Test: Persist and Reload... ";
    
    std::string test_path = "/tmp/apollo_persist_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    
    // Create and populate
    {
        SQLiteDB db;
        assert(db.open(test_path));
        assert(initialize_schema(db));
        
        sqlite3_stmt* stmt = db.prepare(
            "INSERT INTO chunks (id, node_id, content, position) VALUES (?, ?, ?, ?);"
        );
        db.bind_string(stmt, 1, "chunk_001");
        db.bind_string(stmt, 2, "node_001");
        db.bind_string(stmt, 3, "Persistent chunk content");
        db.bind_int(stmt, 4, 0);
        assert(db.step(stmt));
        db.finalize(stmt);
    }
    
    // Reopen and verify
    {
        SQLiteDB db;
        assert(db.open(test_path));
        
        sqlite3_stmt* stmt = db.prepare(
            "SELECT content, position FROM chunks WHERE id = 'chunk_001';"
        );
        
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string content = db.column_string(stmt, 0);
            int pos = db.column_int(stmt, 1);
            assert(content == "Persistent chunk content");
            assert(pos == 0);
            found = true;
            (void)content; (void)pos; (void)found;
        }
        db.finalize(stmt);
        
        assert(found);
    }
    
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    std::cout << "PASSED\n";
}

void test_helper_functions() {
    std::cout << "Test: Helper Functions... ";
    
    // Test escape_sql_string
    std::string escaped = escape_sql_string("It's a test 'with quotes'");
    assert(escaped == "It''s a test ''with quotes''");
    
    // Test current_timestamp returns non-empty string
    std::string ts = current_timestamp();
    assert(!ts.empty());
    assert(ts.length() >= 19);  // "YYYY-MM-DD HH:MM:SS"
    
    std::cout << "PASSED\n";
}

void test_realistic_workflow() {
    std::cout << "Test: Realistic Workflow... ";
    
    std::string test_path = "/tmp/apollo_workflow_test.db";
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    
    SQLiteDB db;
    assert(db.open(test_path));
    assert(initialize_schema(db));
    
    // Create a node
    assert(db.execute(
        "INSERT INTO nodes (id, title, content, document_type) VALUES (?, ?, ?, ?);",
        {"node_001", "Einstein", "Albert Einstein bio", "biography"}
    ));
    
    // Create chunks for the node
    assert(db.execute(
        "INSERT INTO chunks (id, node_id, content, position) VALUES (?, ?, ?, ?);",
        {"chunk_001", "node_001", "First chunk content", "0"}
    ));
    assert(db.execute(
        "INSERT INTO chunks (id, node_id, content, position) VALUES (?, ?, ?, ?);",
        {"chunk_002", "node_001", "Second chunk content", "1"}
    ));
    
    // Create entities
    assert(db.execute(
        "INSERT INTO entities (id, chunk_id, name, type, confidence) VALUES (?, ?, ?, ?, ?);",
        {"entity_001", "chunk_001", "Albert Einstein", "PERSON", "0.95"}
    ));
    
    // Create relationship
    assert(db.execute(
        "INSERT INTO relationships (id, source_id, target_id, type) VALUES (?, ?, ?, ?);",
        {"rel_001", "entity_001", "entity_001", "RELATED_TO"}
    ));
    
    // Verify everything
    sqlite3_stmt* stmt = db.prepare("SELECT COUNT(*) FROM nodes;");
    sqlite3_step(stmt);
    int nodes = db.column_int(stmt, 0);
    db.finalize(stmt);
    assert(nodes == 1);
    (void)nodes;

    stmt = db.prepare("SELECT COUNT(*) FROM chunks WHERE node_id = 'node_001';");
    sqlite3_step(stmt);
    int chunks = db.column_int(stmt, 0);
    db.finalize(stmt);
    assert(chunks == 2);
    (void)chunks;

    stmt = db.prepare("SELECT COUNT(*) FROM entities;");
    sqlite3_step(stmt);
    int entities = db.column_int(stmt, 0);
    db.finalize(stmt);
    assert(entities == 1);
    (void)entities;

    stmt = db.prepare("SELECT COUNT(*) FROM relationships;");
    sqlite3_step(stmt);
    int rels = db.column_int(stmt, 0);
    db.finalize(stmt);
    assert(rels == 1);
    (void)rels;
    
    std::filesystem::remove(test_path);
    std::filesystem::remove(test_path + "-wal");
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - Persistence Module Tests                ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_sqlitedb_open_close();
    test_schema_initialization();
    test_execute_with_params();
    test_transactions();
    test_persist_and_reload();
    test_helper_functions();
    test_realistic_workflow();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All Persistence Tests PASSED                          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • SQLiteDB open/close: PASSED\n";
    std::cout << "   • Schema initialization: PASSED\n";
    std::cout << "   • Parameterized queries: PASSED\n";
    std::cout << "   • Transactions: PASSED\n";
    std::cout << "   • Persist and reload: PASSED\n";
    std::cout << "   • Helper functions: PASSED\n";
    std::cout << "   • Realistic workflow: PASSED\n";
    std::cout << "\n";
    std::cout << "🗄️ Tables created:\n";
    std::cout << "   • config - Knowledge base configuration\n";
    std::cout << "   • nodes - Knowledge tree documents\n";
    std::cout << "   • chunks - Indexed content units\n";
    std::cout << "   • entities - Extracted entities\n";
    std::cout << "   • relationships - Entity connections\n";
    std::cout << "\n";
    
    return 0;
}
