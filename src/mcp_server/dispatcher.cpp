// SPDX-License-Identifier: MIT
//
// dispatcher.cpp — JSON-RPC method router
//
// Implements the MCP protocol methods we support. Each method handler returns
// a `json` value that becomes the `result` field of the JSON-RPC response.

#include "dispatcher.hpp"

#include <stdexcept>
#include <string>

namespace apollo::mcp {

using nlohmann::json;

Dispatcher::Dispatcher(ServerState& state,
                       const ToolRegistry& tools,
                       const ResourceRegistry& resources,
                       const PromptRegistry& prompts)
    : state_(state), tools_(tools), resources_(resources), prompts_(prompts)
{}

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------

JsonRpcResponse Dispatcher::dispatch(const JsonRpcRequest& req)
{
    try {
        // Notifications have no id — we still process them but return an
        // empty response which the caller should NOT send over the wire.
        if (req.method == "initialize")        return make_result(req.id.value(), handle_initialize(req));
        if (req.method == "notifications/initialized") { handle_initialized(req); return {}; }
        if (req.method == "ping")              return make_result(req.id.value(), handle_ping());
        if (req.method == "tools/list")        return make_result(req.id.value(), handle_tools_list());
        if (req.method == "tools/call")        return make_result(req.id.value(), handle_tools_call(req));
        if (req.method == "resources/list")    return make_result(req.id.value(), handle_resources_list());
        if (req.method == "resources/templates/list")
                                               return make_result(req.id.value(), handle_resources_templates_list());
        if (req.method == "resources/read")    return make_result(req.id.value(), handle_resources_read(req));
        if (req.method == "prompts/list")      return make_result(req.id.value(), handle_prompts_list());
        if (req.method == "prompts/get")       return make_result(req.id.value(), handle_prompts_get(req));

        return make_error(req.id, errc::METHOD_NOT_FOUND,
                          "Method not found: " + req.method);
    } catch (const std::invalid_argument& e) {
        return make_error(req.id, errc::INVALID_PARAMS, e.what());
    } catch (const std::exception& e) {
        return make_error(req.id, errc::INTERNAL_ERROR, e.what());
    }
}

// ---------------------------------------------------------------------------
// initialize / initialized
// ---------------------------------------------------------------------------

json Dispatcher::handle_initialize(const JsonRpcRequest& req)
{
    // We don't strictly validate protocolVersion yet — accept whatever the
    // client offers and respond with our pinned version. Strict version
    // negotiation can be added in Phase 5.
    (void)req;
    initialized_ = true;
    return {
        {"protocolVersion", info_.protocol_version},
        {"capabilities", {
            {"tools",     json::object()},
            {"resources", {{"subscribe", false}}},
            {"prompts",   json::object()},
            {"logging",   json::object()}
        }},
        {"serverInfo", {
            {"name",    info_.name},
            {"title",   "ApolloCore MCP Server"},
            {"version", info_.version},
            {"vendor",  info_.vendor}
        }},
        {"instructions",
         "ApolloCore is a local-first RAG knowledge base. Use apollo_query_kb to "
         "search, apollo_add_document to ingest text, apollo_ingest_codebase for "
         "code directories, and apollo_search_symbols for code symbol lookup. "
         "Set the active project with apollo_set_project before reading."}
    };
}

void Dispatcher::handle_initialized(const JsonRpcRequest& /*req*/)
{
    // Notification — nothing to do beyond acknowledging we've seen it.
    initialized_ = true;
}

json Dispatcher::handle_ping()
{
    return json::object();  // empty object is the spec'd response
}

// ---------------------------------------------------------------------------
// tools
// ---------------------------------------------------------------------------

json Dispatcher::handle_tools_list()
{
    return tools_.list();
}

json Dispatcher::handle_tools_call(const JsonRpcRequest& req)
{
    const std::string name = req.params.value("name", std::string{});
    if (name.empty()) {
        throw std::invalid_argument("tools/call requires params.name");
    }
    json args = req.params.value("arguments", json::object());
    auto result = tools_.call(name, args, state_);
    return result.to_json();
}

// ---------------------------------------------------------------------------
// resources
// ---------------------------------------------------------------------------

json Dispatcher::handle_resources_list()
{
    return resources_.list();
}

json Dispatcher::handle_resources_templates_list()
{
    // We don't expose URI templates beyond what's in list(); return empty.
    return {{"resourceTemplates", json::array()}};
}

json Dispatcher::handle_resources_read(const JsonRpcRequest& req)
{
    const std::string uri = req.params.value("uri", std::string{});
    if (uri.empty()) {
        throw std::invalid_argument("resources/read requires params.uri");
    }
    auto out = resources_.read(uri, state_);
    return out.to_json();
}

// ---------------------------------------------------------------------------
// prompts
// ---------------------------------------------------------------------------

json Dispatcher::handle_prompts_list()
{
    return prompts_.list();
}

json Dispatcher::handle_prompts_get(const JsonRpcRequest& req)
{
    const std::string name = req.params.value("name", std::string{});
    if (name.empty()) {
        throw std::invalid_argument("prompts/get requires params.name");
    }
    json args = req.params.value("arguments", json::object());
    auto result = prompts_.get(name, args, state_);
    return result.to_json();
}

}  // namespace apollo::mcp