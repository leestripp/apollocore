/**
 * ApolloCore MCP HTTP Transport — Phase M1.16 (refactored)
 *
 * JSON-RPC 2.0 over HTTP. Uses the new Tool Registry (Phase M2.3)
 * to dispatch tool calls.
 *
 * MCP Methods:
 *   initialize, tools/list, tools/call, resources/list, resources/read,
 *   ping, notifications/initialized
 */

#include "apollocore/apollo_mcp_http.hpp"
#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_rest_api.hpp"  // for g_kb
#include "apollocore/apollo_mcp_tools.hpp"  // ToolRegistry
#include "apollocore/apollo_mcp_prompts.hpp"  // PromptRegistry
#include "apollocore/apollo_mcp_completion.hpp"  // provide_completion
#include "apollocore/apollo_mcp_errors.hpp"

#include <nlohmann/json.hpp>

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;
using json = nlohmann::json;

namespace apollocore_v2::mcp {

// Forward declaration from apollo_mcp_tools.cpp
void register_all_tools(ToolRegistry& registry, ApolloCore& kb);

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

// Global tool registry (initialized once on first use)
ToolRegistry& get_registry() {
    static ToolRegistry registry;
    static bool initialized = false;
    if (!initialized && rest::g_kb) {
        register_all_tools(registry, *rest::g_kb);
        initialized = true;
    }
    return registry;
}

// Global prompt registry (initialized once on first use)
PromptRegistry& get_prompt_registry() {
    static PromptRegistry registry;
    static bool initialized = false;
    if (!initialized && rest::g_kb) {
        register_all_prompts(registry, *rest::g_kb);
        initialized = true;
    }
    return registry;
}

// Minimal resources (URI scheme — full spec in M2.B)
json get_resources_list() {
    return json::array({
        {{"uri", "apollocore://current_kb"},
         {"name", "Current Knowledge Base"},
         {"mimeType", "application/json"}},
        {{"uri", "apollocore://stats"},
         {"name", "Real-time Stats"},
         {"mimeType", "application/json"}},
        {{"uri", "apollocore://tools"},
         {"name", "Tool Catalog"},
         {"mimeType", "application/json"}}
    });
}

} // namespace

void handle_jsonrpc(const onh::Request& req, onh::Response& resp) {
    json rpc;
    try {
        rpc = parse_body(req);
    } catch (const std::exception& e) {
        write_json(resp, 400, make_error(nullptr, ErrorCode::ParseError, e.what()));
        return;
    }

    json id = rpc.value("id", json(nullptr));
    std::string method = rpc.value("method", "");
    json params = rpc.value("params", json::object());

    json envelope = {{"jsonrpc", "2.0"}, {"id", id}};

    try {
        json result;

        if (method == "initialize") {
            result = {
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {
                    {"name", "apollocore-mcp"},
                    {"version", "2.0.0-m2"}  // Phase M2
                }},
                {"capabilities", {
                    {"tools", json::object()},
                    {"resources", json::object()},
                    {"prompts", json::object()},
                    {"completions", json::object()}
                }}
            };
        }
        else if (method == "tools/list") {
            result = get_registry().list_tools();
        }
        else if (method == "tools/call") {
            std::string tool_name = params.value("name", "");
            json args = params.value("arguments", json::object());
            json tool_result = get_registry().call(tool_name, args);
            result = {
                {"content", json::array({
                    {{"type", "text"}, {"text", tool_result.dump()}}
                })},
                {"isError", false}
            };
        }
        else if (method == "prompts/list") {
            result = get_prompt_registry().list_prompts();
        }
        else if (method == "prompts/get") {
            std::string prompt_name = params.value("name", "");
            json args = params.value("arguments", json::object());
            result = get_prompt_registry().get_prompt(prompt_name, args);
        }
        else if (method == "completion/complete") {
            // params: {ref: {type, name}, argument: {name, value}, context: {arguments}}
            json ref = params.value("ref", json::object());
            json argument = params.value("argument", json::object());
            json context = params.value("context", json::object());

            std::string ref_type = ref.value("type", "ref/tool");
            std::string ref_name = ref.value("name", "");
            std::string arg_name = argument.value("name", "");
            std::string partial = argument.value("value", "");
            json ctx_args = context.value("arguments", json::object());

            if (!rest::g_kb) {
                throw McpError(ErrorCode::KbNotFound, "No KB open");
            }
            result = provide_completion(*rest::g_kb, ref_type, ref_name,
                                       arg_name, partial, ctx_args);
        }
        else if (method == "resources/list") {
            result = {{"resources", get_resources_list()}};
        }
        else if (method == "resources/read") {
            std::string uri = params.value("uri", "");
            json content;
            if (uri == "apollocore://current_kb") {
                if (!rest::g_kb || !rest::g_kb->is_open()) {
                    throw McpError(ErrorCode::KbNotFound, "No KB open");
                }
                content = {
                    {"path", rest::g_kb->get_kb_path()},
                    {"name", rest::g_kb->get_kb_name()}
                };
            } else if (uri == "apollocore://stats") {
                if (!rest::g_kb || !rest::g_kb->is_open()) {
                    throw McpError(ErrorCode::KbNotFound, "No KB open");
                }
                auto s = rest::g_kb->get_stats();
                content = {
                    {"documents", s.document_count},
                    {"chunks", s.chunk_count},
                    {"entities", s.entity_count}
                };
            } else if (uri == "apollocore://tools") {
                content = get_registry().list_tools();
            } else {
                throw McpError(ErrorCode::ResourceNotFound,
                    "Unknown URI: " + uri);
            }
            result = {
                {"contents", json::array({
                    {{"uri", uri}, {"mimeType", "application/json"},
                     {"text", content.dump()}}
                })}
            };
        }
        else if (method == "notifications/initialized" || method == "ping") {
            result = json::object();
        }
        else {
            write_json(resp, 200, make_error(id, ErrorCode::MethodNotFound,
                "Unknown method: " + method));
            return;
        }

        envelope["result"] = result;
        write_json(resp, 200, envelope);
    }
    catch (const McpError& e) {
        write_json(resp, 200, make_error(id, e.code, e.what(), e.context));
    }
    catch (const std::exception& e) {
        write_json(resp, 200, make_error(id, ErrorCode::InternalError, e.what()));
    }
}

void handle_sse(const onh::Request&, onh::Response& resp) {
    resp.setStatus(onh::StatusCode::OK);
    resp.setContentType("text/event-stream");
    resp.setHeader(std::string_view("Cache-Control"), std::string_view("no-cache"));
    resp.setBody("event: ready\ndata: {\"transport\":\"sse\",\"phase\":\"M2\"}\n\n");
}

void register_routes(onh::Server& server) {
    server.post("/mcp",     handle_jsonrpc);
    server.get ("/mcp/sse", handle_sse);
}

} // namespace apollocore_v2::mcp