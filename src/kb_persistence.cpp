/**
 * ApolloCore - Knowledge Base Persistence Implementation
 */

#include "apollocore/kb_persistence.hpp"
#include "apollocore/persistence.hpp"
#include "apollocore/fbin.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace apollocore_v2 {

// ============================================================================
// Constructor / Destructor
// ============================================================================

KnowledgeBasePersistence::KnowledgeBasePersistence() = default;

KnowledgeBasePersistence::~KnowledgeBasePersistence() {
    close();
}

// ============================================================================
// Path Helpers
// ============================================================================

std::string KnowledgeBasePersistence::get_db_path() const {
    return path_ + "/apollo.db";
}

std::string KnowledgeBasePersistence::get_embeddings_dir() const {
    return path_ + "/embeddings";
}

std::string KnowledgeBasePersistence::get_chunk_embedding_path(
    const std::string& chunk_id) const {
    // Sanitize: chunk_id may contain slashes (e.g. "src/auth.cpp_chunk_0")
    // which would create subdirectories. Replace with underscores for the
    // filesystem path. The SQLite chunk_id keeps the slashes for navigability.
    std::string safe_id = chunk_id;
    for (auto& c : safe_id) {
        if (c == '/' || c == '\\') c = '_';
    }
    return get_embeddings_dir() + "/" + safe_id + ".fbin";
}

bool KnowledgeBasePersistence::create_directory_structure() {
    try {
        fs::create_directories(path_);
        fs::create_directories(get_embeddings_dir());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[KB Persistence] Failed to create directories: " 
                  << e.what() << "\n";
        return false;
    }
}

// ============================================================================
// Existence Check
// ============================================================================

bool KnowledgeBasePersistence::exists(const std::string& path) {
    if (!fs::exists(path)) return false;
    if (!fs::is_directory(path)) return false;
    return fs::exists(path + "/apollo.db");
}

// ============================================================================
// Create
// ============================================================================

bool KnowledgeBasePersistence::create(const std::string& path, 
                                      const std::string& name) {
    if (exists(path)) {
        std::cerr << "[KB Persistence] Already exists: " << path << "\n";
        return false;
    }
    
    path_ = path;
    
    if (!create_directory_structure()) {
        return false;
    }
    
    if (!db_.open(get_db_path())) {
        return false;
    }
    
    if (!initialize_schema(db_)) {
        db_.close();
        return false;
    }
    
    // Insert config
    db_.execute(
        "INSERT INTO config (name, embedding_dim) VALUES (?, ?);",
        {name, "768"}
    );
    
    std::cerr << "[KB Persistence] Created: " << path << " (" << name << ")\n";
    return true;
}

// ============================================================================
// Open
// ============================================================================

bool KnowledgeBasePersistence::open(const std::string& path) {
    if (!exists(path)) {
        std::cerr << "[KB Persistence] Does not exist: " << path << "\n";
        return false;
    }
    
    path_ = path;
    
    if (!db_.open(get_db_path())) {
        return false;
    }
    
    if (!initialize_schema(db_)) {
        db_.close();
        return false;
    }
    
    std::cerr << "[KB Persistence] Opened: " << path << "\n";
    return true;
}

// ============================================================================
// Save
// ============================================================================

bool KnowledgeBasePersistence::save() {
    if (!db_.is_open()) return false;
    return true;  // Auto-commit is on; data is saved on insert
}

// ============================================================================
// Close
// ============================================================================

void KnowledgeBasePersistence::close() {
    if (in_transaction_) {
        db_.rollback_transaction();
        in_transaction_ = false;
    }
    db_.close();
}

// ============================================================================
// Clear / Destroy
// ============================================================================

void KnowledgeBasePersistence::clear() {
    if (db_.is_open()) {
        // Delete from all tables
        db_.execute("DELETE FROM relationships;");
        db_.execute("DELETE FROM entities;");
        db_.execute("DELETE FROM chunks;");
        db_.execute("DELETE FROM nodes;");
    }
    
    // Delete embedding files
    if (fs::exists(get_embeddings_dir())) {
        for (const auto& entry : fs::directory_iterator(get_embeddings_dir())) {
            fs::remove(entry.path());
        }
    }
}

void KnowledgeBasePersistence::clear_disk() {
    // Remove embedding files
    if (fs::exists(get_embeddings_dir())) {
        for (const auto& entry : fs::directory_iterator(get_embeddings_dir())) {
            fs::remove(entry.path());
        }
    }
}

bool KnowledgeBasePersistence::destroy() {
    close();
    
    if (fs::exists(path_)) {
        try {
            fs::remove_all(path_);
            std::cerr << "[KB Persistence] Destroyed: " << path_ << "\n";
            path_.clear();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[KB Persistence] Failed to destroy: " << e.what() << "\n";
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Config
// ============================================================================

std::string KnowledgeBasePersistence::get_name() {
    if (!db_.is_open()) return "";
    
    sqlite3_stmt* stmt = db_.prepare("SELECT name FROM config LIMIT 1;");
    if (!stmt) return "";
    
    std::string name;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        name = db_.column_string(stmt, 0);
    }
    db_.finalize(stmt);
    
    return name;
}

bool KnowledgeBasePersistence::load_config() {
    if (!db_.is_open()) return false;
    
    sqlite3_stmt* stmt = db_.prepare("SELECT COUNT(*) FROM config;");
    if (!stmt) return false;
    
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ok = db_.column_int(stmt, 0) > 0;
    }
    db_.finalize(stmt);
    
    return ok;
}

// ============================================================================
// Data Operations
// ============================================================================

bool KnowledgeBasePersistence::save_node(
    const std::string& id,
    const std::string& title,
    const std::string& content,
    const std::string& parent_id,
    const std::string& document_type,
    const std::string& metadata_json,
    const std::string& project_id)
{
    if (!db_.is_open()) return false;

    // Default project_id to "default" if not provided (backwards-compat)
    std::string pid = project_id.empty() ? std::string("default") : project_id;

    return db_.execute(
        "INSERT OR REPLACE INTO nodes "
        "(id, title, content, parent_id, document_type, metadata, project_id, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        {id, title, content, parent_id, document_type, metadata_json, pid,
         current_timestamp()}
    );
}

bool KnowledgeBasePersistence::save_chunk(
    const std::string& id,
    const std::string& node_id,
    const std::string& content,
    size_t position,
    const std::vector<float>& embedding,
    const std::string& project_id)
{
    if (!db_.is_open()) return false;
    
    // Save embedding to .fbin file
    std::string emb_path = get_chunk_embedding_path(id);
    if (!save_fbin(emb_path, embedding)) {
        std::cerr << "[KB Persistence] Failed to save embedding for: " << id << "\n";
        return false;
    }
    
    // Save chunk metadata to SQLite
    return db_.execute(
        "INSERT OR REPLACE INTO chunks "
        "(id, node_id, content, position, embedding_file, project_id) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        {id, node_id, content, std::to_string(position), emb_path,
         project_id.empty() ? std::string("default") : project_id}
    );
}

bool KnowledgeBasePersistence::save_entity(
    const std::string& id,
    const std::string& chunk_id,
    const std::string& name,
    const std::string& type,
    const std::string& description,
    float confidence,
    const std::string& project_id)
{
    if (!db_.is_open()) return false;
    
    return db_.execute(
        "INSERT OR REPLACE INTO entities "
        "(id, chunk_id, name, type, description, confidence, project_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        {id, chunk_id, name, type, description, std::to_string(confidence),
         project_id.empty() ? std::string("default") : project_id}
    );
}

bool KnowledgeBasePersistence::save_relationship(
    const std::string& id,
    const std::string& source_id,
    const std::string& target_id,
    const std::string& type,
    const std::string& description,
    float confidence,
    const std::string& project_id)
{
    if (!db_.is_open()) return false;
    
    return db_.execute(
        "INSERT OR REPLACE INTO relationships "
        "(id, source_id, target_id, type, description, confidence, project_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        {id, source_id, target_id, type, description, std::to_string(confidence),
         project_id.empty() ? std::string("default") : project_id}
    );
}

// ============================================================================
// Bulk Operations
// ============================================================================

bool KnowledgeBasePersistence::begin_save() {
    if (!db_.is_open()) return false;
    if (in_transaction_) return true;
    
    if (db_.begin_transaction()) {
        in_transaction_ = true;
        return true;
    }
    return false;
}

bool KnowledgeBasePersistence::end_save() {
    if (!db_.is_open()) return false;
    if (!in_transaction_) return true;
    
    bool ok = db_.commit_transaction();
    in_transaction_ = false;
    return ok;
}

// ============================================================================
// Load Operations
// ============================================================================

std::vector<KnowledgeBasePersistence::LoadedNode> 
KnowledgeBasePersistence::load_all_nodes() {
    std::vector<LoadedNode> result;
    if (!db_.is_open()) return result;
    
    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, title, content, parent_id, document_type, metadata, project_id "
        "FROM nodes ORDER BY id;"
    );
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedNode n;
        n.id = db_.column_string(stmt, 0);
        n.title = db_.column_string(stmt, 1);
        n.content = db_.column_string(stmt, 2);
        n.parent_id = db_.column_string(stmt, 3);
        n.document_type = db_.column_string(stmt, 4);
        n.metadata = db_.column_string(stmt, 5);
        n.project_id = db_.column_string(stmt, 6);
        result.push_back(n);
    }
    db_.finalize(stmt);
    
    return result;
}

std::vector<KnowledgeBasePersistence::LoadedChunk>
KnowledgeBasePersistence::load_all_chunks() {
    std::vector<LoadedChunk> result;
    if (!db_.is_open()) return result;
    
    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, node_id, content, position, embedding_file, project_id "
        "FROM chunks ORDER BY node_id, position;"
    );
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedChunk c;
        c.id = db_.column_string(stmt, 0);
        c.node_id = db_.column_string(stmt, 1);
        c.content = db_.column_string(stmt, 2);
        c.position = static_cast<size_t>(db_.column_int(stmt, 3));

        // Load embedding
        std::string emb_path = db_.column_string(stmt, 4);
        if (!emb_path.empty() && fs::exists(emb_path)) {
            c.embedding = load_fbin(emb_path);
        }

        c.project_id = db_.column_string(stmt, 5);

        result.push_back(c);
    }
    db_.finalize(stmt);
    
    return result;
}

std::vector<KnowledgeBasePersistence::LoadedEntity>
KnowledgeBasePersistence::load_all_entities() {
    std::vector<LoadedEntity> result;
    if (!db_.is_open()) return result;
    
    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, chunk_id, name, type, description, confidence, project_id "
        "FROM entities ORDER BY id;"
    );
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedEntity e;
        e.id = db_.column_string(stmt, 0);
        e.chunk_id = db_.column_string(stmt, 1);
        e.name = db_.column_string(stmt, 2);
        e.type = db_.column_string(stmt, 3);
        e.description = db_.column_string(stmt, 4);
        e.confidence = static_cast<float>(db_.column_real(stmt, 5));
        e.project_id = db_.column_string(stmt, 6);
        result.push_back(e);
    }
    db_.finalize(stmt);
    
    return result;
}

std::vector<KnowledgeBasePersistence::LoadedRelationship>
KnowledgeBasePersistence::load_all_relationships() {
    std::vector<LoadedRelationship> result;
    if (!db_.is_open()) return result;
    
    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, source_id, target_id, type, description, confidence, project_id "
        "FROM relationships ORDER BY id;"
    );
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedRelationship r;
        r.id = db_.column_string(stmt, 0);
        r.source_id = db_.column_string(stmt, 1);
        r.target_id = db_.column_string(stmt, 2);
        r.type = db_.column_string(stmt, 3);
        r.description = db_.column_string(stmt, 4);
        r.confidence = static_cast<float>(db_.column_real(stmt, 5));
        r.project_id = db_.column_string(stmt, 6);
        result.push_back(r);
    }
    db_.finalize(stmt);
    
    return result;
}

std::vector<KnowledgeBasePersistence::LoadedEntity>
KnowledgeBasePersistence::load_entities_for_projects(
    const std::vector<std::string>& project_ids, size_t limit)
{
    std::vector<LoadedEntity> result;
    if (!db_.is_open()) return result;

    std::string sql = "SELECT id, chunk_id, name, type, description, confidence, "
                      "metadata, project_id, created_at FROM entities";

    if (!project_ids.empty()) {
        // Build "?, ?, ?" placeholder
        std::string placeholders;
        for (size_t i = 0; i < project_ids.size(); ++i) {
            if (i > 0) placeholders += ", ";
            placeholders += "?";
        }
        sql += " WHERE project_id IN (" + placeholders + ")";
    }
    sql += " ORDER BY created_at";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = db_.prepare(sql);
    if (!stmt) return result;

    if (!project_ids.empty()) {
        for (size_t i = 0; i < project_ids.size(); ++i) {
            db_.bind_string(stmt, static_cast<int>(i + 1), project_ids[i]);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedEntity e;
        e.id = db_.column_string(stmt, 0);
        e.chunk_id = db_.column_string(stmt, 1);
        e.name = db_.column_string(stmt, 2);
        e.type = db_.column_string(stmt, 3);
        e.description = db_.column_string(stmt, 4);
        e.confidence = static_cast<float>(db_.column_real(stmt, 5));
        e.metadata = db_.column_string(stmt, 6);
        e.project_id = db_.column_string(stmt, 7);
        e.created_at = db_.column_string(stmt, 8);
        result.push_back(e);
    }
    db_.finalize(stmt);
    return result;
}

std::vector<KnowledgeBasePersistence::LoadedRelationship>
KnowledgeBasePersistence::load_relationships_for_projects(
    const std::vector<std::string>& project_ids, size_t limit)
{
    std::vector<LoadedRelationship> result;
    if (!db_.is_open()) return result;

    std::string sql = "SELECT id, source_id, target_id, type, description, "
                      "confidence, project_id, created_at FROM relationships";

    if (!project_ids.empty()) {
        std::string placeholders;
        for (size_t i = 0; i < project_ids.size(); ++i) {
            if (i > 0) placeholders += ", ";
            placeholders += "?";
        }
        sql += " WHERE project_id IN (" + placeholders + ")";
    }
    sql += " ORDER BY created_at";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = db_.prepare(sql);
    if (!stmt) return result;

    if (!project_ids.empty()) {
        for (size_t i = 0; i < project_ids.size(); ++i) {
            db_.bind_string(stmt, static_cast<int>(i + 1), project_ids[i]);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LoadedRelationship r;
        r.id = db_.column_string(stmt, 0);
        r.source_id = db_.column_string(stmt, 1);
        r.target_id = db_.column_string(stmt, 2);
        r.type = db_.column_string(stmt, 3);
        r.description = db_.column_string(stmt, 4);
        r.confidence = static_cast<float>(db_.column_real(stmt, 5));
        r.project_id = db_.column_string(stmt, 6);
        r.created_at = db_.column_string(stmt, 7);
        result.push_back(r);
    }
    db_.finalize(stmt);
    return result;
}

std::vector<float> KnowledgeBasePersistence::load_chunk_embedding(
    const std::string& chunk_id) {
    return load_fbin(get_chunk_embedding_path(chunk_id));
}

// ============================================================================
// Statistics
// ============================================================================

KnowledgeBasePersistence::DiskStats 
KnowledgeBasePersistence::get_disk_stats() {
    DiskStats stats;
    if (!db_.is_open()) return stats;
    
    auto count_query = [&](const std::string& sql) -> size_t {
        sqlite3_stmt* stmt = db_.prepare(sql);
        if (!stmt) return 0;
        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<size_t>(db_.column_int(stmt, 0));
        }
        db_.finalize(stmt);
        return count;
    };
    
    stats.total_nodes = count_query("SELECT COUNT(*) FROM nodes;");
    stats.total_chunks = count_query("SELECT COUNT(*) FROM chunks;");
    stats.total_entities = count_query("SELECT COUNT(*) FROM entities;");
    stats.total_relationships = count_query("SELECT COUNT(*) FROM relationships;");
    
    // Sum embedding file sizes
    if (fs::exists(get_embeddings_dir())) {
        for (const auto& entry : fs::directory_iterator(get_embeddings_dir())) {
            if (fs::is_regular_file(entry.path())) {
                stats.embedding_files_size += fs::file_size(entry.path());
            }
        }
    }
    
    return stats;
}

bool KnowledgeBasePersistence::delete_document(const std::string& document_id) {
    std::cerr << "[KBP::delete_document] enter doc=" << document_id
              << " db_open=" << db_.is_open() << std::endl;
    std::cerr.flush();
    if (!db_.is_open()) return false;

    // 1. Find chunk IDs for this document
    std::vector<std::string> chunk_ids;
    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id FROM chunks WHERE node_id = ?;"
    );
    if (!stmt) return false;
    db_.bind_string(stmt, 1, document_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        chunk_ids.push_back(db_.column_string(stmt, 0));
    }
    db_.finalize(stmt);

    // 2. Delete .fbin embedding files for these chunks
    for (const auto& cid : chunk_ids) {
        std::string emb_path = get_chunk_embedding_path(cid);
        std::error_code ec;
        std::filesystem::remove(emb_path, ec);
        // Ignore errors — file may not exist
    }

    // 3. Find entity IDs for these chunks (query one at a time)
    std::vector<std::string> entity_ids;
    for (const auto& cid : chunk_ids) {
        sqlite3_stmt* estmt = db_.prepare(
            "SELECT id FROM entities WHERE chunk_id = ?;"
        );
        if (!estmt) continue;
        db_.bind_string(estmt, 1, cid);
        while (sqlite3_step(estmt) == SQLITE_ROW) {
            entity_ids.push_back(db_.column_string(estmt, 0));
        }
        db_.finalize(estmt);
    }

    // 4. Delete relationships involving these entities
    for (const auto& eid : entity_ids) {
        db_.execute(
            "DELETE FROM relationships WHERE source_id = ? OR target_id = ?;",
            {eid, eid}
        );
    }

    // 5. Delete entities (those that came from these chunks)
    for (const auto& cid : chunk_ids) {
        db_.execute("DELETE FROM entities WHERE chunk_id = ?;", {cid});
    }

    // 6. Delete chunks for this document
    db_.execute("DELETE FROM chunks WHERE node_id = ?;", {document_id});

    // 7. Delete the document node itself
    db_.execute("DELETE FROM nodes WHERE id = ?;", {document_id});

    return true;
}

// ============================================================================
// Project Management (Phase M3.C — multi-agent support)
// ============================================================================

bool KnowledgeBasePersistence::save_project(const ProjectInfo& info) {
    if (!db_.is_open()) return false;
    return db_.execute(
        "INSERT OR REPLACE INTO projects "
        "(id, name, description, path, is_shared, owner, metadata, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);",
        {
            info.id.empty() ? info.name : info.id,
            info.name,
            info.description,
            info.path,
            std::to_string(info.is_shared ? 1 : 0),
            info.owner,
            info.metadata
        }
    );
}

std::vector<KnowledgeBasePersistence::ProjectInfo>
KnowledgeBasePersistence::load_all_projects() {
    std::vector<ProjectInfo> result;
    if (!db_.is_open()) return result;

    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, name, description, path, is_shared, owner, metadata, "
        "       created_at, updated_at FROM projects ORDER BY name;"
    );
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ProjectInfo p;
        p.id = db_.column_string(stmt, 0);
        p.name = db_.column_string(stmt, 1);
        p.description = db_.column_string(stmt, 2);
        p.path = db_.column_string(stmt, 3);
        p.is_shared = db_.column_int(stmt, 4) != 0;
        p.owner = db_.column_string(stmt, 5);
        p.metadata = db_.column_string(stmt, 6);
        p.created_at = db_.column_string(stmt, 7);
        p.updated_at = db_.column_string(stmt, 8);
        result.push_back(p);
    }
    db_.finalize(stmt);
    return result;
}

KnowledgeBasePersistence::ProjectInfo
KnowledgeBasePersistence::load_project(const std::string& name) {
    ProjectInfo p;
    if (!db_.is_open()) return p;

    sqlite3_stmt* stmt = db_.prepare(
        "SELECT id, name, description, path, is_shared, owner, metadata, "
        "       created_at, updated_at FROM projects WHERE name = ?;"
    );
    if (!stmt) return p;

    db_.bind_string(stmt, 1, name);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p.id = db_.column_string(stmt, 0);
        p.name = db_.column_string(stmt, 1);
        p.description = db_.column_string(stmt, 2);
        p.path = db_.column_string(stmt, 3);
        p.is_shared = db_.column_int(stmt, 4) != 0;
        p.owner = db_.column_string(stmt, 5);
        p.metadata = db_.column_string(stmt, 6);
        p.created_at = db_.column_string(stmt, 7);
        p.updated_at = db_.column_string(stmt, 8);
    }
    db_.finalize(stmt);
    return p;
}

bool KnowledgeBasePersistence::set_project_shared(
    const std::string& name, bool is_shared) {
    if (!db_.is_open()) return false;
    return db_.execute(
        "UPDATE projects SET is_shared = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE name = ?;",
        {std::to_string(is_shared ? 1 : 0), name}
    );
}

bool KnowledgeBasePersistence::delete_project_data(const std::string& name) {
    if (!db_.is_open()) return false;

    // Delete all rows for this project across all tables
    db_.execute("DELETE FROM nodes WHERE project_id = ?;", {name});
    db_.execute("DELETE FROM chunks WHERE project_id = ?;", {name});
    db_.execute("DELETE FROM entities WHERE project_id = ?;", {name});
    db_.execute("DELETE FROM relationships WHERE project_id = ?;", {name});
    db_.execute("DELETE FROM projects WHERE name = ?;", {name});

    return true;
}

} // namespace apollocore_v2
