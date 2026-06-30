// SPDX-License-Identifier: MIT
//
// dispatcher.hpp — JSON-RPC method router
//
// Routes incoming JSON-RPC requests to the right handler. Implements the
// `initialize`/`initialized` handshake and the `ping` capability check.
// Tool, resource, prompt handling is delegated to the three registries.

#pragma once

#include <nlohmann/json.hpp>

#include "jsonrpc.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "server_state.hpp"
#include "tools.hpp"

namespace apollo::mcp {

using nlohmann::json;

class Dispatcher {
public:
    Dispatcher(ServerState& state,
               const ToolRegistry& tools,
               const ResourceRegistry& resources,
               const PromptRegistry& prompts);

    /// Dispatch a parsed JSON-RPC request and return the response.
    /// For notifications (no `id`), the response has neither `result` nor
    /// `error` and the caller (HTTP layer) should NOT send anything.
    JsonRpcResponse dispatch(const JsonRpcRequest& req);

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
    json handle_ping();
    json handle_tools_list();
    json handle_tools_call(const JsonRpcRequest& req);
    json handle_resources_list();
    json handle_resources_templates_list();
    json handle_resources_read(const JsonRpcRequest& req);
    json handle_prompts_list();
    json handle_prompts_get(const JsonRpcRequest& req);

    ServerState& state_;
    const ToolRegistry& tools_;
    const ResourceRegistry& resources_;
    const PromptRegistry& prompts_;
    Info info_;
    bool initialized_ = false;
};

}  // namespace apollo::mcp