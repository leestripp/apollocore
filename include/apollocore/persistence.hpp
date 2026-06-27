#pragma once

/**
 * ApolloCore - Persistence Module
 * 
 * Provides SQLite-backed persistence for ApolloCore.
 * 
 * Storage layout:
 *   <path>/
 *   ├── apollo.db         # SQLite database (metadata)
 *   ├── embeddings/       # Binary embedding files
 *   └── config.json       # Configuration
 * 
 * The SQLite database stores:
 *   - config: Knowledge base settings
 *   - nodes: Knowledge tree (documents)
 *   - chunks: Indexed content units
 *   - entities: Extracted entities
 *   - relationships: Entity connections
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <unordered_map>

// Forward declare sqlite3 to avoid exposing C header
struct sqlite3;
struct sqlite3_stmt;

namespace apollocore_v2 {

// ============================================================================
// SQLite Wrapper
// ============================================================================

/**
 * Thin RAII wrapper around sqlite3
 */
class SQLiteDB {
public:
    SQLiteDB();
    ~SQLiteDB();
    
    // Non-copyable
    SQLiteDB(const SQLiteDB&) = delete;
    SQLiteDB& operator=(const SQLiteDB&) = delete;
    
    /**
     * Open or create a database file
     */
    bool open(const std::string& path);
    
    /**
     * Close the database
     */
    void close();
    
    /**
     * Check if database is open
     */
    bool is_open() const { return db_ != nullptr; }
    
    /**
     * Execute SQL (no results)
     */
    bool execute(const std::string& sql);
    
    /**
     * Execute SQL with parameters (no results)
     */
    bool execute(const std::string& sql, const std::vector<std::string>& params);
    
    /**
     * Prepare a statement
     */
    sqlite3_stmt* prepare(const std::string& sql) const;
    
    /**
     * Bind a string parameter
     */
    bool bind_string(sqlite3_stmt* stmt, int index, const std::string& value);
    
    /**
     * Bind an integer parameter
     */
    bool bind_int(sqlite3_stmt* stmt, int index, int value);
    
    /**
     * Bind a real (double) parameter
     */
    bool bind_real(sqlite3_stmt* stmt, int index, double value);
    
    /**
     * Bind NULL
     */
    bool bind_null(sqlite3_stmt* stmt, int index);
    
    /**
     * Execute prepared statement
     */
    bool step(sqlite3_stmt* stmt);
    
    /**
     * Reset prepared statement
     */
    bool reset(sqlite3_stmt* stmt);
    
    /**
     * Finalize prepared statement
     */
    bool finalize(sqlite3_stmt* stmt);
    
    /**
     * Get column value as string
     */
    std::string column_string(sqlite3_stmt* stmt, int col) const;

    /**
     * Get column value as int
     */
    int column_int(sqlite3_stmt* stmt, int col) const;

    /**
     * Get column value as real
     */
    double column_real(sqlite3_stmt* stmt, int col) const;

    /**
     * Get column is NULL
     */
    bool column_is_null(sqlite3_stmt* stmt, int col) const;
    
    /**
     * Get last error message
     */
    std::string last_error() const;
    
    /**
     * Begin transaction
     */
    bool begin_transaction();
    
    /**
     * Commit transaction
     */
    bool commit_transaction();
    
    /**
     * Rollback transaction
     */
    bool rollback_transaction();
    
    /**
     * Get last inserted row ID
     */
    long long last_insert_rowid() const;

private:
    sqlite3* db_ = nullptr;
};

// ============================================================================
// Schema Initialization
// ============================================================================

/**
 * Initialize the ApolloCore schema in an SQLite database
 * @param db SQLite database (must be open)
 * @return true on success
 */
bool initialize_schema(SQLiteDB& db);

/**
 * Apply project_id migration (M3.C — multi-agent + project scoping).
 *
 * Adds `project_id` column to nodes/chunks/entities/relationships
 * (defaulting to 'default' for existing data) and creates the
 * `projects` registry table.
 *
 * Backwards-compatible: existing DBs are migrated in place.
 * Uses PRAGMA user_version for migration tracking.
 *
 * @param db SQLite database (must be open)
 * @return true on success
 */
bool apply_project_migrations(SQLiteDB& db);

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Get current timestamp as ISO string
 */
std::string current_timestamp();

/**
 * Escape a string for SQL
 */
std::string escape_sql_string(const std::string& input);

} // namespace apollocore_v2
