// SPDX-License-Identifier: MIT
//
// dispatcher.cpp — JSON-RPC method router
//
// Implements the MCP protocol methods we support. Each method handler returns
// a `json` value that becomes the `result` field of the JSON-RPC response.

#include "dispatcher.hpp"
#include "mcp_security.hpp"

#include <stdexcept>
#include <string>

namespace apollo::mcp {

using nlohmann::json;

Dispatcher::Dispatcher(ServerState& state,
                       McpSseBus& sse_bus,
                       const ToolRegistry& tools,
                       const ResourceRegistry& resources,
                       const PromptRegistry& prompts)
    : state_(state), sse_bus_(sse_bus),
      tools_(tools), resources_(resources), prompts_(prompts)
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
        if (req.method == "notifications/cancelled") { handle_cancelled(req); return {}; }
        if (req.method == "ping")              return make_result(req.id.value(), handle_ping());
        if (req.method == "logging/setLevel")  return make_result(req.id.value(), handle_set_log_level(req));
        if (req.method == "completion/complete") return make_result(req.id.value(), handle_complete(req));
        if (req.method == "tools/list")        return make_result(req.id.value(), handle_tools_list());
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
    // Phase B3 — protocol-version negotiation.
    //
    // Per spec §Transports#Version-Negotiation: if the client offers a
    // version we support, we reply with that exact version (so client
    // and server agree on the negotiated dialect). Otherwise we reply
    // with our latest supported version and let the client decide
    // whether to continue.
    //
    // The negotiated version is recorded on the session so post-init
    // requests can verify it (server-side check is defence-in-depth;
    // the protocol-version header is the actual wire check).
    std::string negotiated = info_.protocol_version;  // our preferred default
    if (req.params.is_object()) {
        auto& p = req.params;
        if (p.contains("protocolVersion") && p["protocolVersion"].is_string()) {
            const std::string requested = p["protocolVersion"].get<std::string>();
            for (const auto& v : security::supported_protocol_versions()) {
                if (v == requested) { negotiated = v; break; }
            }
            // If requested is unknown, we fall back to our pinned default
            // (2025-11-25). The client should re-initialize if it can't
            // accept that version.
        }
    }
    initialized_ = true;
    state_.set_negotiated_version(req.session_id, negotiated);
    return {
        {"protocolVersion", negotiated},
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

// -------------------------------------------------------------------
// logging/setLevel (Phase D2)
// Per spec §Logging: client MAY set the server's log level.
// We store it per-session on ServerState and return {} on success.
// Unknown levels → Invalid Params (per spec §Errors).
// -------------------------------------------------------------------
json Dispatcher::handle_set_log_level(const JsonRpcRequest& req)
{
    const std::string level = req.params.contains("level") &&
                              req.params["level"].is_string()
                              ? req.params["level"].get<std::string>()
                              : std::string{};
    if (level != "debug" && level != "info" && level != "warning" &&
        level != "error") {
        throw std::invalid_argument("Invalid level: '" + level +
            "' (must be one of debug|info|warning|error)");
    }
    state_.set_session_log_level(req.session_id, level);
    return json::object();
}

// -------------------------------------------------------------------
// completion/complete (Phase D2)
// Per spec §Completion: provides argument suggestions for prompts
// or resources. We don't currently have completion data; return an
// empty completions array. Clients must handle this gracefully —
// most SDKs treat empty as "no suggestions available right now".
// -------------------------------------------------------------------
json Dispatcher::handle_complete(const JsonRpcRequest& /*req*/)
{
    // Schema: { ref: {...}, argument: {name, value}, context? }
    // Return: { completion: { values: [{value, ...meta}], ...meta? } }
    return json{{"completion", {{"values", json::array()}}}};
}

// -------------------------------------------------------------------
// notifications/cancelled (Phase D3)
// Per spec §Cancellation: client notifies server to cancel an
// in-flight request. We don't have a token map for active requests
// today, so this is a no-op acknowledgement. Future: store a map of
// in-flight tokens and check periodically from long-running tools.
// -------------------------------------------------------------------
void Dispatcher::handle_cancelled(const JsonRpcRequest& req)
{
    // Phase D2: recording the cancellation in the state would let
    // long tools poll it. For now we accept the notification and do
    // nothing — all our current tools complete well within a second
    // so cancellation wouldn't help much anyway.
    (void)req;
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

// ---------------------------------------------------------------------------
// list_changed notifications (Phase B2)
// ---------------------------------------------------------------------------

void Dispatcher::notify_list_changed(const std::string& kind,
                                      const std::string& session_id) {
    // Spec §Notifications/List_Changed:
    //   { "jsonrpc": "2.0", "method": "notifications/tools/list_changed" }
    //   { "jsonrpc": "2.0", "method": "notifications/resources/list_changed" }
    //   { "jsonrpc": "2.0", "method": "notifications/prompts/list_changed" }
    //
    // We validate `kind` so callers can't push arbitrary junk. Future work
    // (when the registries become mutable) will call this from tool_call /
    // resource_register paths.
    std::string method;
    if      (kind == "tools")     method = "notifications/tools/list_changed";
    else if (kind == "resources") method = "notifications/resources/list_changed";
    else if (kind == "prompts")   method = "notifications/prompts/list_changed";
    else {
        return;  // bad kind — silently ignore to avoid leaking method names
    }

    json notification = {
        {"jsonrpc", "2.0"},
        {"method",  method},
        // Spec: notifications MUST NOT have an `id`.
    };

    if (session_id.empty()) {
        // Broadcast to every subscribed session. Walk the bus's session
        // map and push to each queue. The bus only stores a key when
        // there's at least one SSE subscriber, so this is naturally
        // scoped to "live SSE clients".
        std::vector<std::string> sids = sse_bus_.session_ids();
        for (const auto& sid : sids) {
            sse_bus_.push(sid, notification);
        }
    } else {
        sse_bus_.push(session_id, notification);
    }
}

}  // namespace apollo::mcp