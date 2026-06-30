// SPDX-License-Identifier: MIT
//
// http_server.cpp — OmniNet glue
//
// Wires OmniNet's HTTP server to our Dispatcher. Routes registered:
//   POST /mcp    — JSON-RPC request/response
//   GET  /health — liveness probe (not part of MCP spec; for k8s/docker)
//   GET  /info   — server card-style info
//
// We do NOT register a static-file catch-all (unlike OmniNet demos) — this
// is an API server, not a website.

#include "http_server.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "OmniNet.h"

namespace apollo::mcp {

using nlohmann::json;

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;

// ---------------------------------------------------------------------------
// Impl — PIMPL holding the OmniNet server + dispatchers + a stop signal.
// ---------------------------------------------------------------------------

struct HttpServer::Impl {
    HttpConfig cfg;
    ServerState& state;
    ToolRegistry tools;
    ResourceRegistry resources;
    PromptRegistry prompts;
    Dispatcher dispatcher;
    std::unique_ptr<onh::Server> server;
    std::atomic<bool> stop_requested{false};

    Impl(ServerState& s, const HttpConfig& c)
        : cfg(c),
          state(s),
          tools(build_default_registry()),
          resources(build_default_resource_registry()),
          prompts(build_default_prompt_registry()),
          dispatcher(state, tools, resources, prompts)
    {
        onh::Server::Config sc;
        sc.address        = cfg.host;
        sc.port           = cfg.port;
        sc.workerCount    = cfg.worker_count;
        sc.requestTimeout = std::chrono::seconds(cfg.request_timeout_seconds);
        sc.corsEnabled    = false;
        server = std::make_unique<onh::Server>(sc);
        register_routes();
    }

    void register_routes()
    {
        // ------------------------------------------------------------------
        // POST /mcp — JSON-RPC over Streamable HTTP body transport
        // ------------------------------------------------------------------
        server->post("/mcp", [this](const onh::Request& req, onh::Response& resp) {
            handle_post_mcp(req, resp);
        });

        // ------------------------------------------------------------------
        // GET /mcp — Streamable HTTP SSE endpoint (only if Accept says SSE)
        // Per spec 2025-11-25: when client GETs /mcp with Accept: text/event-stream
        // the server may open an SSE channel for server-initiated requests.
        // For Phase 1 we accept but immediately 405 — Phase 4 (SSE) can extend.
        // ------------------------------------------------------------------
        server->get("/mcp", [this](const onh::Request& req, onh::Response& resp) {
            handle_get_mcp(req, resp);
        });

        // ------------------------------------------------------------------
        // GET /health — JSON liveness probe.
        // ------------------------------------------------------------------
        server->get("/health", [this](const onh::Request& /*req*/, onh::Response& resp) {
            handle_health(resp);
        });

        // ------------------------------------------------------------------
        // GET /info — Server card-style info (analogous to MCP server card)
        // ------------------------------------------------------------------
        server->get("/info", [this](const onh::Request& /*req*/, onh::Response& resp) {
            handle_info(resp);
        });

        // Optional admin dashboard from OmniNet.
        if (cfg.enable_admin_dashboard) {
            try {
                server->adminHandler().mount(*server);
            } catch (const std::exception& e) {
                on::Logger::instance().warn(
                    std::string("Failed to mount admin handler: ") + e.what());
            }
        }
    }

    // -----------------------------------------------------------------------
    // Handlers
    // -----------------------------------------------------------------------

    void write_json(onh::Response& resp, int status, const json& body)
    {
        resp.setStatus(static_cast<onh::StatusCode>(status));
        resp.setContentType("application/json");
        resp.setBody(body.dump());
    }

    void handle_post_mcp(const onh::Request& req, onh::Response& resp)
    {
        // Make sure the client wants JSON.
        auto accept = req.header("accept");
        if (accept && std::string(*accept).find("application/json") == std::string::npos
                  && std::string(*accept).find("text/event-stream") == std::string::npos) {
            write_json(resp, 406, {{"error", "Accept must include application/json"}});
            return;
        }

        std::string body(req.bodyAsString());

        // Single request OR batch. MCP spec allows batch; we support it.
        json parsed;
        try {
            parsed = json::parse(body);
        } catch (const std::exception& e) {
            JsonRpcResponse err = make_error(std::nullopt, errc::PARSE_ERROR,
                                             std::string("parse error: ") + e.what());
            write_json(resp, 400, err.to_json());
            return;
        }

        // Session id (MCP 2025-11-25, optional but we honor it).
        std::string session_id;
        if (auto h = req.header("mcp-session-id")) {
            session_id = std::string(*h);
        }
        std::string effective_session = state.get_or_create_session(session_id);

        if (parsed.is_array()) {
            // Batch.
            json out = json::array();
            for (const auto& item : parsed) {
                out.push_back(handle_one(item, effective_session));
            }
            write_json(resp, 200, out);
            // Echo session id back if the client sent one.
            if (!session_id.empty()) {
                resp.setHeader("Mcp-Session-Id", effective_session);
            }
            return;
        }

        auto resp_json = handle_one(parsed, effective_session);
        write_json(resp, 200, resp_json);
        if (!session_id.empty()) {
            resp.setHeader("Mcp-Session-Id", effective_session);
        }
    }

    json handle_one(const json& raw_request, const std::string& /*session_id*/)
    {
        JsonRpcRequest req;
        try {
            req = parse_request(raw_request.dump());
        } catch (const std::exception& e) {
            return make_error(std::nullopt, errc::INVALID_REQUEST, e.what()).to_json();
        }
        auto resp = dispatcher.dispatch(req);
        if (resp.id || resp.result || resp.err) {
            return resp.to_json();
        }
        // Notification — no response.
        return json::object();
    }

    void handle_get_mcp(const onh::Request& req, onh::Response& resp)
    {
        // We don't yet push SSE notifications; tell the client.
        auto accept = req.header("accept");
        if (accept && std::string(*accept).find("text/event-stream") != std::string::npos) {
            write_json(resp, 405, {{"error",
                "SSE server-initiated requests not implemented yet (Phase 4 work)"}});
            return;
        }
        write_json(resp, 405, {{"error", "GET /mcp requires Accept: text/event-stream"}});
    }

    void handle_health(onh::Response& resp)
    {
        auto s = state.snapshot();
        json body = {
            {"status",      "ok"},
            {"name",        dispatcher.info().name},
            {"version",     dispatcher.info().version},
            {"protocol",    dispatcher.info().protocol_version},
            {"tools",       tools.size()},
            {"resources",   resources.size()},
            {"prompts",     prompts.size()},
            {"sessions",    s.live_sessions},
            {"documents",   s.document_count},
            {"chunks",      s.chunk_count},
            {"entities",    s.entity_count}
        };
        write_json(resp, 200, body);
    }

    void handle_info(onh::Response& resp)
    {
        json body = {
            {"name",         dispatcher.info().name},
            {"title",        "ApolloCore MCP Server"},
            {"version",      dispatcher.info().version},
            {"protocol",     dispatcher.info().protocol_version},
            {"vendor",       dispatcher.info().vendor},
            {"transport",    "streamable-http"},
            {"endpoint",     "POST /mcp"},
            {"tools",        tools.size()},
            {"resources",    resources.size()},
            {"prompts",      prompts.size()},
        };
        write_json(resp, 200, body);
    }
};

// ---------------------------------------------------------------------------
// HttpServer
// ---------------------------------------------------------------------------

HttpServer::HttpServer(ServerState& state, const HttpConfig& cfg)
    : p_(std::make_unique<Impl>(state, cfg))
{}

HttpServer::~HttpServer() = default;

void HttpServer::run()
{
    // OmniNet's Server::start() blocks until shutdown.
    p_->server->start();
}

void HttpServer::stop()
{
    // OmniNet exposes stop via its server; if not available we rely on signals.
    // For now, we just flip the flag so future OmniNet versions can read it.
    p_->stop_requested.store(true);
    // Best-effort: try the API.
    try {
        p_->server->stop();
    } catch (...) {
        // ignored — older OmniNet versions may not have stop().
    }
}

}  // namespace apollo::mcp