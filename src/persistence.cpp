/**
 * ApolloCore - Persistence Module Implementation
 */

#include "apollocore/persistence.hpp"
#include <sqlite3.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <iostream>

namespace apollocore_v2 {

// ============================================================================
// SQLiteDB Implementation
// ============================================================================

SQLiteDB::SQLiteDB() = default;

SQLiteDB::~SQLiteDB() {
    close();
}

bool SQLiteDB::open(const std::string& path) {
    if (db_) {
        close();
    }
    
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[SQLiteDB] Failed to open: " << last_error() << "\n";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    
    // Enable foreign keys
    execute("PRAGMA foreign_keys = ON;");
    
    // Use WAL mode for better concurrency
    execute("PRAGMA journal_mode = WAL;");
    
    return true;
}

void SQLiteDB::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SQLiteDB::execute(const std::string& sql) {
    if (!db_) return false;
    
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "[SQLiteDB] SQL error: " << (error_msg ? error_msg : "unknown") << "\n";
        if (error_msg) {
            sqlite3_free(error_msg);
        }
        return false;
    }
    
    return true;
}

bool SQLiteDB::execute(const std::string& sql, const std::vector<std::string>& params) {
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) return false;
    
    for (size_t i = 0; i < params.size(); i++) {
        if (!bind_string(stmt, (int)(i + 1), params[i])) {
            finalize(stmt);
            return false;
        }
    }
    
    bool ok = step(stmt);
    finalize(stmt);
    return ok;
}

sqlite3_stmt* SQLiteDB::prepare(const std::string& sql) const {
    if (!db_) return nullptr;
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "[SQLiteDB] Prepare failed: " << last_error() << "\n";
        return nullptr;
    }
    
    return stmt;
}

bool SQLiteDB::bind_string(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (!stmt) return false;
    int rc = sqlite3_bind_text(stmt, index, value.c_str(), (int)value.size(), SQLITE_TRANSIENT);
    return rc == SQLITE_OK;
}

bool SQLiteDB::bind_int(sqlite3_stmt* stmt, int index, int value) {
    if (!stmt) return false;
    int rc = sqlite3_bind_int(stmt, index, value);
    return rc == SQLITE_OK;
}

bool SQLiteDB::bind_real(sqlite3_stmt* stmt, int index, double value) {
    if (!stmt) return false;
    int rc = sqlite3_bind_double(stmt, index, value);
    return rc == SQLITE_OK;
}

bool SQLiteDB::bind_null(sqlite3_stmt* stmt, int index) {
    if (!stmt) return false;
    int rc = sqlite3_bind_null(stmt, index);
    return rc == SQLITE_OK;
}

bool SQLiteDB::step(sqlite3_stmt* stmt) {
    if (!stmt) return false;
    int rc = sqlite3_step(stmt);
    // SQLITE_DONE = success (no more rows)
    // SQLITE_ROW = success (got a row)
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

bool SQLiteDB::reset(sqlite3_stmt* stmt) {
    if (!stmt) return false;
    int rc = sqlite3_reset(stmt);
    return rc == SQLITE_OK;
}

bool SQLiteDB::finalize(sqlite3_stmt* stmt) {
    if (!stmt) return false;
    int rc = sqlite3_finalize(stmt);
    return rc == SQLITE_OK;
}

std::string SQLiteDB::column_string(sqlite3_stmt* stmt, int col) const {
    if (!stmt) return "";
    const unsigned char* text = sqlite3_column_text(stmt, col);
    if (!text) return "";
    return std::string(reinterpret_cast<const char*>(text));
}

int SQLiteDB::column_int(sqlite3_stmt* stmt, int col) const {
    if (!stmt) return 0;
    return sqlite3_column_int(stmt, col);
}

double SQLiteDB::column_real(sqlite3_stmt* stmt, int col) const {
    if (!stmt) return 0.0;
    return sqlite3_column_double(stmt, col);
}

bool SQLiteDB::column_is_null(sqlite3_stmt* stmt, int col) const {
    if (!stmt) return true;
    return sqlite3_column_type(stmt, col) == SQLITE_NULL;
}

std::string SQLiteDB::last_error() const {
    if (!db_) return "no database";
    const char* err = sqlite3_errmsg(db_);
    return err ? err : "unknown error";
}

bool SQLiteDB::begin_transaction() {
    return execute("BEGIN TRANSACTION;");
}

bool SQLiteDB::commit_transaction() {
    return execute("COMMIT;");
}

bool SQLiteDB::rollback_transaction() {
    return execute("ROLLBACK;");
}

long long SQLiteDB::last_insert_rowid() const {
    if (!db_) return 0;
    return sqlite3_last_insert_rowid(db_);
}

// ============================================================================
// Schema Initialization
// ============================================================================

bool initialize_schema(SQLiteDB& db) {
    if (!db.is_open()) {
        std::cerr << "[Schema] Database not open\n";
        return false;
    }
    
    std::string schema_sql = R"(
        -- Knowledge base configuration
        CREATE TABLE IF NOT EXISTS config (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            embedding_dim INTEGER DEFAULT 768,
            embedding_model TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        -- Documents (nodes in knowledge tree)
        CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            content TEXT,
            parent_id TEXT,
            document_type TEXT,
            metadata TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_nodes_parent ON nodes(parent_id);

        -- Chunks (indexed content units)
        CREATE TABLE IF NOT EXISTS chunks (
            id TEXT PRIMARY KEY,
            node_id TEXT NOT NULL,
            content TEXT NOT NULL,
            position INTEGER,
            embedding_file TEXT,
            content_hash TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_chunks_node ON chunks(node_id);
        CREATE INDEX IF NOT EXISTS idx_chunks_hash ON chunks(content_hash);

        -- Entities (extracted knowledge)
        CREATE TABLE IF NOT EXISTS entities (
            id TEXT PRIMARY KEY,
            chunk_id TEXT,
            name TEXT NOT NULL,
            type TEXT NOT NULL,
            description TEXT,
            confidence REAL DEFAULT 0.5,
            metadata TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_entities_chunk ON entities(chunk_id);
        CREATE INDEX IF NOT EXISTS idx_entities_type ON entities(type);
        CREATE INDEX IF NOT EXISTS idx_entities_name ON entities(name);

        -- Relationships (entity connections)
        CREATE TABLE IF NOT EXISTS relationships (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            target_id TEXT NOT NULL,
            type TEXT NOT NULL,
            description TEXT,
            confidence REAL DEFAULT 0.5,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
        CREATE INDEX IF NOT EXISTS idx_rels_source ON relationships(source_id);
        CREATE INDEX IF NOT EXISTS idx_rels_target ON relationships(target_id);
        CREATE INDEX IF NOT EXISTS idx_rels_type ON relationships(type);
    )";
    
    if (!db.execute(schema_sql)) {
        std::cerr << "[Schema] Failed to create tables\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string escape_sql_string(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);
    for (char c : input) {
        if (c == '\'') {
            output += "''";
        } else {
            output += c;
        }
    }
    return output;
}

} // namespace apollocore_v2
