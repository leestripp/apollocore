/**
 * ApolloCore MCP HTTP Transport — Implementation (Phase M1.16)
 *
 * JSON-RPC 2.0 over HTTP. Thin layer over ApolloCore API.
 * See apollo_mcp_http.hpp for the public surface.
 */

#include "apollocore/apollo_mcp_http.hpp"
#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_rest_api.hpp"  // reuse g_kb

#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;
using json = nlohmann::json;

namespace apollocore_v2::mcp {

// ============================================================================
// JSON-RPC Helpers
// ============================================================================

namespace {

inline void write_json(onh::Response& resp, int status, const json& body) {
    resp.setStatus(static_cast<onh::StatusCode>(status));
    resp.setContentType("application/json");
    resp.setBody(body.dump());
}

inline json parse_body(const onh::Request& req) {
    auto body = req.bodyAsString();
    if (body.empty()) return json::object();
    try {
        return json::parse(body);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Invalid JSON: ") + e.what());
    }
}

// ============================================================================
// Tool Definitions (8 tools — mirrors REST API)
// ============================================================================

json get_tools_list() {
    return json::array({
        {
            {"name", "search_kb"},
            {"description", "Semantic search over the knowledge base. Returns ranked chunks matching the query."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Natural language query"}}},
                    {"top_k", {{"type", "integer"}, {"description", "Number of results (default 5)"}, {"default", 5}}}
                }},
                {"required", json::array({"query"})}
            }}
        },
        {
            {"name", "add_document"},
            {"description", "Add a document to the knowledge base. Automatically embedded and chunked."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"text", {{"type", "string"}, {"description", "Document text to add"}}}
                }},
                {"required", json::array({"text"})}
            }}
        },
        {
            {"name", "extract_entities"},
            {"description", "Extract entities and relationships from all chunks using the LLM. Requires LLM model to be loaded."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"min_confidence", {{"type", "number"}, {"description", "Minimum confidence threshold (0-1, default 0.5)"}, {"default", 0.5}}},
                    {"skip_already_extracted", {{"type", "boolean"}, {"description", "Skip chunks already processed (default true)"}, {"default", true}}}
                }}
            }}
        },
        {
            {"name", "get_entities"},
            {"description", "List entities in the knowledge base. Optionally filter by chunk_id."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"chunk_id", {{"type", "string"}, {"description", "Filter to this chunk only (optional)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Max entities to return (default 100)"}, {"default", 100}}}
                }}
            }}
        },
        {
            {"name", "get_relationships"},
            {"description", "List relationships (edges) in the knowledge base."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"limit", {{"type", "integer"}, {"description", "Max relationships (default 100)"}, {"default", 100}}}
                }}
            }}
        },
        {
            {"name", "create_kb"},
            {"description", "Create a new knowledge base at the given path."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path for new KB (relative, no ..)"}}},
                    {"name", {{"type", "string"}, {"description", "KB name (default Untitled)"}, {"default", "Untitled"}}}
                }},
                {"required", json::array({"path"})}
            }}
        },
        {
            {"name", "open_kb"},
            {"description", "Open an existing knowledge base from disk."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to existing KB"}}}
                }},
                {"required", json::array({"path"})}
            }}
        },
        {
            {"name", "get_stats"},
            {"description", "Get statistics for the currently open knowledge base."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}
        }
    });
}

// ============================================================================
// Resource Definitions (3 resources)
// ============================================================================

json get_resources_list() {
    return json::array({
        {
            {"uri", "apollocore://current_kb"},
            {"name", "Current Knowledge Base"},
            {"description", "Metadata about the currently open KB (path, name, stats)"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "apollocore://entities"},
            {"name", "All Entities"},
            {"description", "All entities in the currently open KB"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "apollocore://stats"},
            {"name", "Real-time Stats"},
            {"description", "Real-time KB statistics"},
            {"mimeType", "application/json"}
        }
    });
}

// ============================================================================
// Tool Dispatch — calls ApolloCore directly (same process, no HTTP hop)
// ============================================================================

json call_tool(const std::string& name, const json& args) {
    if (!rest::g_kb) {
        throw std::runtime_error("No KB initialized");
    }

    if (name == "search_kb") {
        std::string query_text = args.value("query", "");
        size_t top_k = args.value("top_k", 5);
        if (query_text.empty()) throw std::runtime_error("Missing 'query'");
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");

        auto results = rest::g_kb->query(query_text, top_k);
        json arr = json::array();
        for (const auto& [chunk_id, score] : results) {
            arr.push_back({{"chunk_id", chunk_id}, {"score", score}});
        }
        return {{"results", arr}, {"count", arr.size()}};
    }

    if (name == "add_document") {
        std::string text = args.value("text", "");
        if (text.empty()) throw std::runtime_error("Missing 'text'");
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");

        size_t chunks = rest::g_kb->add_document(text);
        auto stats = rest::g_kb->get_stats();
        return {
            {"chunk_count", chunks},
            {"doc_id", "doc_" + std::to_string(stats.document_count)},
            {"total_chunks", stats.chunk_count}
        };
    }

    if (name == "extract_entities") {
        float min_conf = args.value("min_confidence", 0.5f);
        bool skip_done = args.value("skip_already_extracted", true);
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        if (!rest::g_kb->is_llm_loaded()) throw std::runtime_error("LLM not loaded");

        size_t count = rest::g_kb->extract_entities(min_conf, skip_done);
        auto stats = rest::g_kb->get_stats();
        return {
            {"entities_extracted", count},
            {"total_entities", stats.entity_count},
            {"total_relationships", stats.relationship_count}
        };
    }

    if (name == "get_entities") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        std::string chunk_id = args.value("chunk_id", std::string{});
        size_t limit = args.value("limit", 100);

        std::vector<Entity> entities;
        if (!chunk_id.empty()) {
            entities = rest::g_kb->get_entities_for_chunk(chunk_id);
        } else {
            entities = rest::g_kb->get_entities(limit);
        }
        json arr = json::array();
        for (const auto& e : entities) {
            arr.push_back({
                {"name", e.name}, {"type", entity_type_to_string(e.type)},
                {"description", e.description}, {"confidence", e.confidence}
            });
        }
        return {{"entities", arr}, {"count", arr.size()}};
    }

    if (name == "get_relationships") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        size_t limit = args.value("limit", 100);
        auto rels = rest::g_kb->get_relationships(limit);
        json arr = json::array();
        for (const auto& r : rels) {
            arr.push_back({
                {"source", r.source}, {"target", r.target},
                {"type", relation_type_to_string(r.type)},
                {"weight", r.weight}
            });
        }
        return {{"relationships", arr}, {"count", arr.size()}};
    }

    if (name == "create_kb") {
        std::string path = args.value("path", "");
        std::string name = args.value("name", "Untitled");
        if (path.empty()) throw std::runtime_error("Missing 'path'");
        if (!rest::g_kb->create(path, name)) throw std::runtime_error("Create failed");
        return {{"success", true}, {"path", path}, {"name", name}};
    }

    if (name == "open_kb") {
        std::string path = args.value("path", "");
        if (path.empty()) throw std::runtime_error("Missing 'path'");
        if (!rest::g_kb->open(path)) throw std::runtime_error("Open failed");
        return {{"success", true}, {"path", path}, {"name", rest::g_kb->get_kb_name()}};
    }

    if (name == "get_stats") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        auto s = rest::g_kb->get_stats();
        return {
            {"documents", s.document_count},
            {"chunks", s.chunk_count},
            {"entities", s.entity_count},
            {"relationships", s.relationship_count},
            {"chunks_with_entities", s.chunks_with_entities}
        };
    }

    throw std::runtime_error("Unknown tool: " + name);
}

// ============================================================================
// Resource Read
// ============================================================================

json read_resource(const std::string& uri) {
    if (!rest::g_kb) {
        throw std::runtime_error("No KB initialized");
    }

    if (uri == "apollocore://current_kb") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        return {
            {"path", rest::g_kb->get_kb_path()},
            {"name", rest::g_kb->get_kb_name()},
            {"open", rest::g_kb->is_open()}
        };
    }

    if (uri == "apollocore://entities") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        auto entities = rest::g_kb->get_entities(1000);
        json arr = json::array();
        for (const auto& e : entities) {
            arr.push_back({{"name", e.name}, {"type", entity_type_to_string(e.type)}});
        }
        return {{"entities", arr}, {"count", arr.size()}};
    }

    if (uri == "apollocore://stats") {
        if (!rest::g_kb->is_open()) throw std::runtime_error("No KB open");
        auto s = rest::g_kb->get_stats();
        return {
            {"documents", s.document_count},
            {"chunks", s.chunk_count},
            {"entities", s.entity_count},
            {"relationships", s.relationship_count}
        };
    }

    throw std::runtime_error("Unknown resource URI: " + uri);
}

} // namespace

// ============================================================================
// JSON-RPC Request Handler
// ============================================================================

void handle_jsonrpc(const onh::Request& req, onh::Response& resp) {
    json rpc;
    try {
        rpc = parse_body(req);
    } catch (const std::exception& e) {
        write_json(resp, 400, {
            {"jsonrpc", "2.0"},
            {"id", nullptr},
            {"error", {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}}}
        });
        return;
    }

    json id = rpc.value("id", json(nullptr));
    std::string method = rpc.value("method", "");
    json params = rpc.value("params", json::object());

    // Build response envelope
    json envelope = {{"jsonrpc", "2.0"}, {"id", id}};

    try {
        json result;

        if (method == "initialize") {
            result = {
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {
                    {"name", "apollocore-mcp"},
                    {"version", "1.0.0"}
                }},
                {"capabilities", {
                    {"tools", json::object()},
                    {"resources", json::object()}
                }}
            };
        }
        else if (method == "tools/list") {
            result = {{"tools", get_tools_list()}};
        }
        else if (method == "tools/call") {
            std::string tool_name = params.value("name", "");
            json args = params.value("arguments", json::object());
            json tool_result = call_tool(tool_name, args);
            result = {
                {"content", json::array({
                    {
                        {"type", "text"},
                        {"text", tool_result.dump()}
                    }
                })},
                {"isError", false}
            };
        }
        else if (method == "resources/list") {
            result = {{"resources", get_resources_list()}};
        }
        else if (method == "resources/read") {
            std::string uri = params.value("uri", "");
            json content = read_resource(uri);
            result = {
                {"contents", json::array({
                    {
                        {"uri", uri},
                        {"mimeType", "application/json"},
                        {"text", content.dump()}
                    }
                })}
            };
        }
        else if (method == "notifications/initialized") {
            // No-op: client tells us init is done. Just acknowledge.
            envelope["result"] = json::object();
            write_json(resp, 200, envelope);
            return;
        }
        else if (method == "ping") {
            result = json::object();
        }
        else {
            // Method not found
            write_json(resp, 200, {
                {"jsonrpc", "2.0"}, {"id", id},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
            });
            return;
        }

        envelope["result"] = result;
        write_json(resp, 200, envelope);
    }
    catch (const std::exception& e) {
        write_json(resp, 200, {
            {"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}
        });
    }
}

// SSE placeholder (for future streaming)
void handle_sse(const onh::Request&, onh::Response& resp) {
    resp.setStatus(onh::StatusCode::OK);
    resp.setContentType("text/event-stream");
    resp.setHeader(std::string_view("Cache-Control"), std::string_view("no-cache"));
    resp.setBody("event: ready\ndata: {\"transport\":\"sse\"}\n\n");
}

void register_routes(onh::Server& server) {
    server.post("/mcp",     handle_jsonrpc);
    server.get ("/mcp/sse", handle_sse);
}

} // namespace apollocore_v2::mcp