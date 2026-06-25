/**
 * ApolloCore REST API (Phase R1)
 *
 * HTTP API for ApolloCore, powered by OmniNet.
 * Exposes KB lifecycle, document add, search, and entity access.
 */

#include "OmniNet.h"
#include "apollocore/apollo_persistence.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;
using json = nlohmann::json;

namespace apollocore_v2::rest {

// ============================================================================
// Globals (single KB per server, matches ApolloCore design)
// ============================================================================

std::unique_ptr<ApolloCore> g_kb;
static std::mutex g_kb_mutex;  // protects g_kb during transitions

// ============================================================================
// JSON Helpers (R1.10)
// ============================================================================

namespace {

void write_json(onh::Response& resp, int status, const json& body) {
    resp.setStatus(static_cast<onh::StatusCode>(status));
    resp.setContentType("application/json");
    resp.setBody(body.dump());
}

void write_error(onh::Response& resp, int status, const std::string& msg) {
    write_json(resp, status, json{{"error", msg}, {"code", status}});
}

json parse_body(const onh::Request& req) {
    auto body = req.bodyAsString();
    if (body.empty()) return json::object();
    try {
        return json::parse(body);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Invalid JSON: ") + e.what());
    }
}

// Path safety: reject absolute paths and ".." components
bool is_safe_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/') return false;
    if (path.find("..") != std::string::npos) return false;
    return true;
}

} // namespace

// ============================================================================
// Route Handlers (R1.4–R1.8)
// ============================================================================

void handle_health(const onh::Request&, onh::Response& resp) {
    write_json(resp, 200, json{
        {"status", "ok"},
        {"service", "apollocore"},
        {"version", "1.0.0"},
        {"transport", "omninet-rest"}
    });
}

void handle_kb_create(const onh::Request& req, onh::Response& resp) {
    json body;
    try {
        body = parse_body(req);
    } catch (const std::exception& e) {
        write_error(resp, 400, e.what());
        return;
    }

    std::string path = body.value("path", "");
    std::string name = body.value("name", "Untitled");

    if (!is_safe_path(path)) {
        write_error(resp, 400, "Invalid path: must be relative and not contain '..'");
        return;
    }

    std::lock_guard<std::mutex> lock(g_kb_mutex);

    if (!g_kb) g_kb = std::make_unique<ApolloCore>();
    if (!g_kb->create(path, name)) {
        write_error(resp, 500, "Failed to create knowledge base");
        return;
    }

    write_json(resp, 200, json{{"success", true}, {"path", path}, {"name", name}});
}

void handle_kb_open(const onh::Request& req, onh::Response& resp) {
    json body;
    try {
        body = parse_body(req);
    } catch (const std::exception& e) {
        write_error(resp, 400, e.what());
        return;
    }

    std::string path = body.value("path", "");
    if (!is_safe_path(path)) {
        write_error(resp, 400, "Invalid path");
        return;
    }

    std::lock_guard<std::mutex> lock(g_kb_mutex);

    if (!g_kb) g_kb = std::make_unique<ApolloCore>();
    if (!g_kb->open(path)) {
        write_error(resp, 404, "Knowledge base not found at: " + path);
        return;
    }

    auto stats = g_kb->get_stats();
    write_json(resp, 200, json{
        {"success", true},
        {"path", path},
        {"name", g_kb->get_kb_name()},
        {"stats", {
            {"documents", stats.document_count},
            {"chunks", stats.chunk_count},
            {"entities", stats.entity_count}
        }}
    });
}

void handle_kb_close(const onh::Request&, onh::Response& resp) {
    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (g_kb) g_kb->close();
    write_json(resp, 200, json{{"success", true}});
}

void handle_kb_destroy(const onh::Request&, onh::Response& resp) {
    std::lock_guard<std::mutex> lock(g_kb_mutex);
    bool ok = g_kb && g_kb->destroy();
    g_kb.reset();
    write_json(resp, 200, json{{"success", ok}});
}

void handle_kb_stats(const onh::Request&, onh::Response& resp) {
    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open");
        return;
    }

    auto s = g_kb->get_stats();
    write_json(resp, 200, json{
        {"documents", s.document_count},
        {"chunks", s.chunk_count},
        {"entities", s.entity_count},
        {"relationships", s.relationship_count},
        {"chunks_with_entities", s.chunks_with_entities}
    });
}

void handle_add_document(const onh::Request& req, onh::Response& resp) {
    json body;
    try {
        body = parse_body(req);
    } catch (const std::exception& e) {
        write_error(resp, 400, e.what());
        return;
    }

    std::string text = body.value("text", "");
    if (text.empty()) {
        write_error(resp, 400, "Missing 'text' field");
        return;
    }

    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open. POST /api/kb/create or /api/kb/open first");
        return;
    }

    size_t chunks = g_kb->add_document(text);
    auto stats = g_kb->get_stats();
    write_json(resp, 200, json{
        {"chunk_count", chunks},
        {"doc_id", "doc_" + std::to_string(stats.document_count)},
        {"total_chunks", stats.chunk_count}
    });
}

void handle_search(const onh::Request& req, onh::Response& resp) {
    json body;
    try {
        body = parse_body(req);
    } catch (const std::exception& e) {
        write_error(resp, 400, e.what());
        return;
    }

    std::string query_text = body.value("query", "");
    size_t top_k = body.value("top_k", 5);

    if (query_text.empty()) {
        write_error(resp, 400, "Missing 'query' field");
        return;
    }

    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open");
        return;
    }

    auto results = g_kb->query(query_text, top_k);
    json arr = json::array();
    for (const auto& [chunk_id, score] : results) {
        arr.push_back({{"chunk_id", chunk_id}, {"score", score}});
    }
    write_json(resp, 200, json{{"results", arr}, {"count", arr.size()}});
}

void handle_entities(const onh::Request& req, onh::Response& resp) {
    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open");
        return;
    }

    // Parse query params (limit, chunk_id)
    size_t limit = 100;
    std::string chunk_id;
    const auto& query = req.query();
    if (auto lim = query.get("limit")) {
        try { limit = std::stoul(std::string(*lim)); } catch (...) {}
    }
    if (auto cid = query.get("chunk_id")) {
        chunk_id = std::string(*cid);
    }

    std::vector<Entity> entities;
    if (!chunk_id.empty()) {
        entities = g_kb->get_entities_for_chunk(chunk_id);
    } else {
        entities = g_kb->get_entities(limit);
    }

    json arr = json::array();
    for (const auto& e : entities) {
        arr.push_back({
            {"name", e.name},
            {"type", entity_type_to_string(e.type)},
            {"description", e.description},
            {"confidence", e.confidence},
            {"id", e.id}
        });
    }
    write_json(resp, 200, json{{"entities", arr}, {"count", arr.size()}});
}

void handle_relationships(const onh::Request& req, onh::Response& resp) {
    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open");
        return;
    }

    size_t limit = 100;
    const auto& query = req.query();
    if (auto lim = query.get("limit")) {
        try { limit = std::stoul(std::string(*lim)); } catch (...) {}
    }

    auto rels = g_kb->get_relationships(limit);
    json arr = json::array();
    for (const auto& r : rels) {
        arr.push_back({
            {"source", r.source},
            {"target", r.target},
            {"type", relation_type_to_string(r.type)},
            {"description", r.description},
            {"weight", r.weight}
        });
    }
    write_json(resp, 200, json{{"relationships", arr}, {"count", arr.size()}});
}

void handle_extract(const onh::Request& req, onh::Response& resp) {
    json body;
    try {
        body = parse_body(req);
    } catch (const std::exception& e) {
        write_error(resp, 400, e.what());
        return;
    }

    float min_confidence = body.value("min_confidence", 0.5f);
    bool skip_extracted = body.value("skip_already_extracted", true);

    std::lock_guard<std::mutex> lock(g_kb_mutex);
    if (!g_kb || !g_kb->is_open()) {
        write_error(resp, 404, "No knowledge base open");
        return;
    }
    if (!g_kb->is_llm_loaded()) {
        write_error(resp, 503, "LLM not loaded. Restart server with --llm-model");
        return;
    }

    size_t count = g_kb->extract_entities(min_confidence, skip_extracted);
    auto stats = g_kb->get_stats();
    write_json(resp, 200, json{
        {"entities_extracted", count},
        {"total_entities", stats.entity_count},
        {"total_relationships", stats.relationship_count},
        {"chunks_with_entities", stats.chunks_with_entities}
    });
}

// ============================================================================
// Router Setup
// ============================================================================

void register_routes(onh::Server& server) {
    // Health
    server.get("/api/health", handle_health);

    // KB lifecycle
    server.post("/api/kb/create",   handle_kb_create);
    server.post("/api/kb/open",     handle_kb_open);
    server.post("/api/kb/close",    handle_kb_close);
    server.delete_("/api/kb",         handle_kb_destroy);
    server.get ("/api/kb/stats",    handle_kb_stats);

    // Documents & search
    server.post("/api/documents",   handle_add_document);
    server.post("/api/search",      handle_search);

    // Graph access
    server.get ("/api/entities",        handle_entities);
    server.get ("/api/relationships",   handle_relationships);
    server.post("/api/extract",         handle_extract);
}

} // namespace apollocore_v2::rest