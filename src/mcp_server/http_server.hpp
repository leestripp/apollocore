// SPDX-License-Identifier: MIT
//
// http_server.hpp — OmniNet glue for the MCP server
//
// Wires OmniNet's HTTP server to our Dispatcher. Provides:
//
//   POST /mcp    — JSON-RPC request/response (Streamable HTTP body transport)
//   GET  /mcp    — SSE stream of optional server→client notifications
//   GET  /health — JSON liveness probe (used by docker/k8s, not MCP-aware)
//   GET  /info   — Server card-style info (tool count, capabilities)
//
// Routes are registered in the order OmniNet requires: specifics first,
// catch-all last.

#pragma once

#include <memory>
#include <string>

#include "dispatcher.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "server_state.hpp"
#include "tools.hpp"

namespace apollo::mcp {

struct HttpConfig {
    std::string host = "0.0.0.0";
    int port = 8765;
    int worker_count = 4;
    int request_timeout_seconds = 30;
    /// If true, register the OmniNet admin dashboard at /omninet/*.
    bool enable_admin_dashboard = false;
};

class HttpServer {
public:
    HttpServer(ServerState& state,
               const HttpConfig& cfg);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Blocks until the server stops.
    void run();

    /// Stop the server from another thread.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace apollo::mcp