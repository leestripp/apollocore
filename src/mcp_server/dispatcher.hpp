// SPDX-License-Identifier: MIT
//
// dispatcher.hpp — JSON-RPC method router
//
// Routes incoming JSON-RPC requests to the right handler. Implements the
// `initialize`/`initialized` handshake and the `ping` capability check.
// Tool, resource, prompt handling is delegated to the three registries.

#pragma once

#include <nlohmann/json.hpp>

#include "dispatcher.hpp"
#include "jsonrpc.hpp"
#include "mcp_sse_bus.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "server_state.hpp"
#include "tools.hpp"

namespace apollo::mcp {

using nlohmann::json;

class Dispatcher {
public:
    Dispatcher(ServerState& state,
               McpSseBus& sse_bus,
               const ToolRegistry& tools,
               const ResourceRegistry& resources,
               const PromptRegistry& prompts);

    /// Dispatch a parsed JSON-RPC request and return the response.
    /// For notifications (no `id`), the response has neither `result` nor
    /// `error` and the caller (HTTP layer) should NOT send anything.
    JsonRpcResponse dispatch(const JsonRpcRequest& req);

    /// Phase B2 — push a `notifications/{tools,resources,prompts}/list_changed`
    /// JSON-RPC notification into every active SSE channel for `session_id`.
    /// If `session_id` is empty, broadcasts to ALL live SSE channels on the
    /// bus (useful when a static registry is reloaded server-wide). The
    /// dispatcher does NOT itself emit these — they're opt-in plumbing for
    /// future code paths that mutate the registries. Today the registries
    /// are static, so nothing calls this.
    void notify_list_changed(const std::string& kind,
                             const std::string& session_id = "");

    /// Phase D4 — push a `notifications/progress` JSON-RPC notification
    /// to a session's SSE channel. `progress_token` is an opaque
    /// client-supplied id from the original request's `_meta.progressToken`
    /// (we don't track that today — pass anything and the client matches
    /// by value). `progress` and `total` are doubles; `total` may be nullopt
    /// for indeterminate progress.
    void notify_progress(const std::string& session_id,
                         const std::string& progress_token,
                         double progress,
                         std::optional<double> total);

    /// Server-side metadata used in `initialize` and `/health`.
    struct Info {
        std::string name = "apollo_mcp_server";
        std::string version = "1.0.0";
        std::string protocol_version = "2025-11-25";
        std::string vendor = "ApolloCore";
    };
    const Info& info() const { return info_; }

private:
    // Method handlers
    json handle_initialize(const JsonRpcRequest& req);
    void handle_initialized(const JsonRpcRequest& req);
    void handle_cancelled(const JsonRpcRequest& req);
    json handle_ping();
    json handle_set_log_level(const JsonRpcRequest& req);
    json handle_complete(const JsonRpcRequest& req);
    json handle_tools_list();
    json handle_tools_call(const JsonRpcRequest& req);
    json handle_resources_list();
    json handle_resources_templates_list();
    json handle_resources_read(const JsonRpcRequest& req);
    json handle_prompts_list();
    json handle_prompts_get(const JsonRpcRequest& req);

    ServerState& state_;
    McpSseBus& sse_bus_;
    const ToolRegistry& tools_;
    const ResourceRegistry& resources_;
    const PromptRegistry& prompts_;
    Info info_;
    bool initialized_ = false;
};

}  // namespace apollo::mcp