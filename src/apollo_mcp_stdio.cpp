/**
 * ApolloCore MCP stdio Transport — Implementation (Phase M2.14)
 *
 * JSON-RPC 2.0 over stdin/stdout with Content-Length framing.
 * Reuses the ToolRegistry + error handling from M2.A.
 */

#include "apollocore/apollo_mcp_stdio.hpp"
#include "apollocore/apollo_mcp_tools.hpp"   // ToolRegistry, register_all_tools
#include "apollocore/apollo_mcp_prompts.hpp" // PromptRegistry, register_all_prompts
#include "apollocore/apollo_mcp_completion.hpp" // provide_completion
#include "apollocore/apollo_mcp_errors.hpp"  // make_error, ErrorCode
#include "apollocore/apollo_persistence.hpp"  // ApolloCore

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace apollocore_v2::mcp {

using json = nlohmann::json;

namespace {

// ============================================================================
// Logging — everything to stderr so stdout stays pure JSON-RPC
// ============================================================================

#define LOG(msg) do { std::fprintf(stderr, "[apollo_mcp_stdio] " msg "\n"); std::fflush(stderr); } while (0)
#define LOGF(fmt, ...) do { std::fprintf(stderr, "[apollo_mcp_stdio] " fmt "\n", __VA_ARGS__); std::fflush(stderr); } while (0)

// ============================================================================
// Framing: Content-Length headers (LSP-style), the MCP stdio standard
// ============================================================================

/**
 * Read a single framed JSON-RPC message from stdin.
 * Returns the parsed JSON object, or std::nullopt on EOF / error.
 */
std::optional<nlohmann::json> read_message() {
    // Read headers
    std::string headers;
    char buf[4096];
    size_t content_length = 0;
    bool found_cl = false;

    while (true) {
        if (!std::fgets(buf, sizeof(buf), stdin)) {
            return std::nullopt;  // EOF
        }
        std::string line(buf);
        // Strip trailing \r\n
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        // Empty line marks end of headers
        if (line.empty()) break;

        // Content-Length header
        if (line.find("Content-Length:") == 0 || line.find("Content-length:") == 0) {
            content_length = std::stoul(line.substr(15));
            found_cl = true;
        }
        // Other headers ignored
    }

    if (!found_cl) {
        LOG("missing Content-Length header");
        return std::nullopt;
    }

    // Read body
    std::string body;
    body.reserve(content_length);
    while (body.size() < content_length) {
        size_t need = content_length - body.size();
        size_t got = std::fread(buf, 1, std::min(need, sizeof(buf)), stdin);
        if (got == 0) {
            LOG("unexpected EOF reading body");
            return std::nullopt;
        }
        body.append(buf, got);
    }

    try {
        return nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        LOGF("JSON parse error: %s", e.what());
        return std::nullopt;
    }
}

/**
 * Write a single framed JSON-RPC message to stdout.
 */
void write_message(const nlohmann::json& msg) {
    std::string body = msg.dump();
    std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", body.size());
    std::fwrite(body.data(), 1, body.size(), stdout);
    std::fflush(stdout);
}

// ============================================================================
// JSON-RPC handler — operates on a passed-in registry + kb
// (No global state; stdio is self-contained)
// ============================================================================

nlohmann::json handle_jsonrpc_request(
    const nlohmann::json& rpc,
    ToolRegistry& registry,
    PromptRegistry& prompt_registry,
    ApolloCore& kb,
    const std::vector<std::string>& tool_names,
    const std::vector<std::string>& prompt_names)
{
    nlohmann::json id = rpc.value("id", json(nullptr));
    std::string method = rpc.value("method", "");
    nlohmann::json params = rpc.value("params", json::object());

    nlohmann::json envelope = {{"jsonrpc", "2.0"}, {"id", id}};

    try {
        json result;

        if (method == "initialize") {
            result = {
                {"protocolVersion", "2024-11-05"},
                {"serverInfo", {
                    {"name", "apollocore-mcp"},
                    {"version", "2.0.0-m2-stdio"}
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
            result = registry.list_tools();
        }
        else if (method == "tools/call") {
            std::string tool_name = params.value("name", "");
            json args = params.value("arguments", json::object());
            json tool_result = registry.call(tool_name, args);
            result = {
                {"content", json::array({
                    {{"type", "text"}, {"text", tool_result.dump()}}
                })},
                {"isError", false}
            };
        }
        else if (method == "prompts/list") {
            result = prompt_registry.list_prompts();
        }
        else if (method == "prompts/get") {
            std::string prompt_name = params.value("name", "");
            json args = params.value("arguments", json::object());
            result = prompt_registry.get_prompt(prompt_name, args);
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

            result = provide_completion(kb, tool_names, prompt_names,
                                       ref_type, ref_name, arg_name,
                                       partial, ctx_args);
        }
        else if (method == "ping" || method == "notifications/initialized") {
            result = json::object();
        }
        else if (method == "notifications/cancelled") {
            LOG("client cancelled");
            return json();  // notifications don't get replies
        }
        else {
            return make_error(id, ErrorCode::MethodNotFound,
                              "Unknown method: " + method);
        }

        envelope["result"] = result;
        return envelope;
    }
    catch (const McpError& e) {
        return make_error(id, e.code, e.what(), e.context);
    }
    catch (const std::exception& e) {
        return make_error(id, ErrorCode::InternalError, e.what());
    }
}

} // namespace

int run_stdio_server(ApolloCore& kb) {
    LOG("starting stdio server (Phase M2.14)");

    // Build local registries — self-contained, no global state.
    ToolRegistry tool_registry;
    register_all_tools(tool_registry, kb);

    PromptRegistry prompt_registry;
    register_all_prompts(prompt_registry, kb);

    // Build name lists once for completion
    std::vector<std::string> tool_names = tool_registry.names();
    std::vector<std::string> prompt_names = prompt_registry.names();

    LOGF("ready, %zu tools + %zu prompts registered, reading from stdin...",
         tool_registry.size(), prompt_registry.size());

    // Main message loop
    while (true) {
        auto rpc_opt = read_message();
        if (!rpc_opt) {
            LOG("EOF on stdin, shutting down");
            break;
        }

        const json& rpc = *rpc_opt;

        // Notifications get no reply (per JSON-RPC spec)
        bool is_notification = !rpc.contains("id");

        json response = handle_jsonrpc_request(rpc, tool_registry, prompt_registry, kb,
                                        tool_names, prompt_names);

        if (!is_notification) {
            write_message(response);
        }
    }

    LOG("stdio server stopped");
    return 0;
}

} // namespace apollocore_v2::mcp