// SPDX-License-Identifier: MIT
//
// http_server.cpp — OmniNet glue
//
// Wires OmniNet's HTTP server to our Dispatcher. Routes registered:
//   POST /mcp    — JSON-RPC request/response
//   GET  /health — liveness probe (not part of MCP spec; for k8s/docker)
//   GET  /info   — server card-style info
//   GET  /       — Web UI (when cfg.ui_static_dir is set)
//   GET  /static/* — Static files for the Web UI
//
// The UI is a pure client of POST /mcp — no separate API. Disabling UI:
// pass an empty `ui_static_dir` or run with `--no-ui`.

#include "http_server.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "OmniNet.h"
#include "http/StaticHandler.h"

#include "mcp_security.hpp"
#include "mcp_sse.hpp"
#include "mcp_sse_bus.hpp"
#include "mcp_dispatch.hpp"

namespace apollo::mcp {

using nlohmann::json;

namespace fs = std::filesystem;
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
    std::unique_ptr<onh::StaticHandler> static_handler;
    std::atomic<bool> stop_requested{false};

    // Phase B — SSE notification bus + bridge. The bus is the
    // shared state (one queue per session). The bridge is the
    // HTTP-layer plumbing that maps GET /mcp → queue pops.
    McpSseBus sse_bus;
    std::unique_ptr<McpSseBridge> sse_bridge;

    Impl(ServerState& s, const HttpConfig& c)
        : cfg(c),
          state(s),
          tools(build_default_registry()),
          resources(build_default_resource_registry()),
          prompts(build_default_prompt_registry()),
          dispatcher(state, sse_bus, tools, resources, prompts)
    {
        onh::Server::Config sc;
        sc.address        = cfg.host;
        sc.port           = cfg.port;
        sc.workerCount    = cfg.worker_count;
        sc.requestTimeout = std::chrono::seconds(cfg.request_timeout_seconds);
        sc.corsEnabled    = false;
        server = std::make_unique<onh::Server>(sc);

        // Mount static file handler if a UI directory is configured.
        if (!cfg.ui_static_dir.empty()) {
            std::error_code ec;
            if (fs::is_directory(cfg.ui_static_dir, ec)) {
                static_handler = std::make_unique<onh::StaticHandler>(cfg.ui_static_dir);
                static_handler->setDefaultFile("index.html");
                static_handler->setIndexFiles({"index.html"});
                static_handler->allowDirectoryListing(false);
                static_handler->setCacheMaxAge(std::chrono::seconds(0)); // dev: re-validate
                on::Logger::instance().info(
                    "Web UI mounted at / from " + cfg.ui_static_dir);
            } else {
                on::Logger::instance().warn(
                    "ui_static_dir is set but the directory does not exist: " +
                    cfg.ui_static_dir + " — UI disabled");
            }
        }

        // ------------------------------------------------------------------
        // SSE bridge (Phase B1). Registered BEFORE the regular routes so
        // OmniNet's protocol-switch wiring runs first and the Router
        // doesn't get a chance to 404 the GET. McpSseBridge owns its
        // thread(s) and is destroyed with `*this`.
        // ------------------------------------------------------------------
        sse_bridge = std::make_unique<McpSseBridge>(*server, sse_bus);

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

        // NOTE: GET /mcp is handled by the protocol-switch handler registered
        // in the ctor (McpSseBridge), NOT here. Adding server->get("/mcp", ...)
        // would shadow the protocol-switch path.

        // ------------------------------------------------------------------
        // DELETE /mcp — terminate the session identified by MCP-Session-Id.
        // Phase A4 (spec §Transports#Session-Management).
        // Note: OmniNet's API names this `delete_` because `delete` is a
        // reserved word in C++.
        // ------------------------------------------------------------------
        server->delete_("/mcp", [this](const onh::Request& req, onh::Response& resp) {
            handle_delete_mcp(req, resp);
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

        // ------------------------------------------------------------------
        // GET /api/config — Runtime model configuration
        // Returns the current model paths, load status, and on-disk config
        // location. Used by the Web UI Settings tab to populate form fields.
        // ------------------------------------------------------------------
        server->get("/api/config", [this](const onh::Request& /*req*/, onh::Response& resp) {
            handle_get_config(resp);
        });

        // ------------------------------------------------------------------
        // GET /api/models — Discovered .gguf files in every models_dir
        // Returns [{name, path, size_bytes, mtime, dir}, ...]. The Settings
        // UI uses this as the source for the "pick a model" dropdown so
        // the user doesn't have to type paths.
        // ------------------------------------------------------------------
        server->get("/api/models", [this](const onh::Request& /*req*/, onh::Response& resp) {
            handle_get_models(resp);
        });

        // ------------------------------------------------------------------
        // POST /api/config — Apply a model configuration
        // Body: {"embed_model": "...", "llm_model": "..."} (both optional).
        // Empty values keep the current path. Models are hot-reloaded.
        // Persisted to <data_dir>/config.json.
        // ------------------------------------------------------------------
        server->post("/api/config", [this](const onh::Request& req, onh::Response& resp) {
            handle_post_config(req, resp);
        });

        // ------------------------------------------------------------------
        // GET /* — Web UI catch-all. Mounted LAST so the more specific
        // /mcp, /health, /info routes win. The StaticHandler resolves
        // /<path> → <rootDir>/<path>, with / → index.html.
        // ------------------------------------------------------------------
        if (static_handler) {
            server->get("/*", [this](const onh::Request& req, onh::Response& resp) {
                // If the static handler serves the file, the response is
                // already set. If not (file missing), the response status
                // is whatever the handler set (typically 404).
                if (static_handler) static_handler->handle(req, resp);
                else                write_json(resp, 404, {{"error", "UI disabled"}});
            });
        }

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

    // ─── Security guards (MCP spec §Transports#Security-Warning) ───────
    //
    // All three checks run on every MCP method (POST/GET/DELETE /mcp).
    //   1. Origin validation — DNS-rebind defense. Empty Origin is OK.
    //   2. Bearer-token auth — only enforced when APOLLO_AUTH_TOKEN is set.
    //   3. MCP-Protocol-Version header — only on requests after
    //      `initialize`; pre-init requests are exempt (the response carries
    //      the version).
    //
    // Returns true if all three pass; otherwise writes the appropriate
    // error response to `resp` and returns false.
    bool check_security(const onh::Request& req, onh::Response& resp,
                        const std::string& /*effective_session*/,
                        bool pre_init)
    {
        static const auto allowed_origins =
            security::parse_allowed_origins_env();

        // (1) Origin.
        std::optional<std::string_view> origin;
        if (auto h = req.header("origin")) origin = *h;
        if (!security::is_valid_origin(origin, allowed_origins)) {
            // Spec: 403 Forbidden with a JSON-RPC error body that has no id.
            auto body = make_error(std::nullopt, errc::INVALID_REQUEST,
                "Origin header rejected (DNS-rebind defense)").to_json();
            write_json(resp, 403, body);
            return false;
        }

        // (2) Bearer auth (only if enabled).
        std::optional<std::string_view> auth;
        if (auto h = req.header("authorization")) auth = *h;
        if (!security::is_valid_bearer(auth)) {
            auto body = make_error(std::nullopt, -32001,
                "Unauthorized: invalid or missing Authorization header").to_json();
            write_json(resp, 401, body);
            return false;
        }

        // (3) MCP-Protocol-Version header (post-init only).
        if (!pre_init) {
            std::optional<std::string_view> ver;
            if (auto h = req.header("mcp-protocol-version")) ver = *h;
            if (ver) {
                // The header should match the version we negotiated during
                // initialize. We accept any supported version here; strict
                // session-version pinning is enforced by the client (they
                // can't lie about which version they negotiated — they got
                // it from us).
                if (!security::is_supported_protocol_version(*ver)) {
                    auto body = make_error(std::nullopt, errc::INVALID_REQUEST,
                        "Unsupported MCP-Protocol-Version: " + std::string(*ver) +
                        ". Supported: " +
                        [&]{
                            const auto& v = security::supported_protocol_versions();
                            std::string s;
                            for (size_t i = 0; i < v.size(); ++i) {
                                if (i) s += ", ";
                                s += v[i];
                            }
                            return s;
                        }()
                    ).to_json();
                    write_json(resp, 400, body);
                    return false;
                }
            }
        }
        return true;
    }

    void handle_post_mcp(const onh::Request& req, onh::Response& resp)
    {
        // Per MCP 2025-11-25, clients SHOULD send Accept with both
        // application/json and text/event-stream. We allow */* (default
        // curl, browsers) for friendliness — it's the same thing.
        auto accept = req.header("accept");
        if (accept) {
            const std::string a(*accept);
            const bool has_json     = a.find("application/json")   != std::string::npos;
            const bool has_sse      = a.find("text/event-stream")  != std::string::npos;
            const bool has_wildcard = a.find("*/*")                != std::string::npos;
            const bool has_json_wc  = a.find("application/*")      != std::string::npos;
            if (!has_json && !has_sse && !has_wildcard && !has_json_wc) {
                write_json(resp, 406, {{"error", "Accept must include application/json or text/event-stream"}});
                return;
            }
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

        // `pre_init` is true only if the very first message in this body is
        // an `initialize` request and we haven't seen one before in this
        // session. We approximate it as: initialize + no notifications/initialized
        // observed yet on this session. The simplest correct check is
        // "the body is exactly an initialize method".
        bool pre_init = false;
        auto check_pre_init = [&](const json& m) -> bool {
            if (!m.is_object()) return false;
            if (m.value("method", std::string{}) != "initialize") return false;
            // Notifications don't have id; initialize must have one.
            return m.contains("id") && !m["id"].is_null();
        };
        if (parsed.is_array()) {
            if (!parsed.empty() && check_pre_init(parsed.front())) pre_init = true;
        } else {
            pre_init = check_pre_init(parsed);
        }

        if (!check_security(req, resp, effective_session, pre_init)) return;

        if (parsed.is_array()) {
            // Batch.
            json out = json::array();
            for (const auto& item : parsed) {
                out.push_back(handle_one(item, effective_session));
            }
            write_json(resp, 200, out);
            // Always echo the session id back.
            resp.setHeader("Mcp-Session-Id", effective_session);
            return;
        }

        // Single request.
        auto resp_json = handle_one(parsed, effective_session);
        write_json(resp, 200, resp_json);
        // Always echo the session id back. Per spec §Transports#Session-
        // Management the server MAY assign one at initialize, and once
        // it does the client MUST include it on every subsequent request.
        resp.setHeader("Mcp-Session-Id", effective_session);
    }

    json handle_one(const json& raw_request, const std::string& session_id)
    {
        // Delegates to the shared dispatch_one() helper (see mcp_dispatch.{hpp,cpp})
        // so the parse + dispatch logic isn't duplicated here. Returns json::object()
        // for notifications (vs. nullopt from the helper) because the HTTP layer
        // here needs a concrete "no body" sentinel for the no-response path.
        auto resp = dispatch_one(dispatcher, raw_request, session_id);
        return resp.value_or(json::object());
    }

    // GET /mcp is no longer handled here — McpSseBridge's
    // protocol-switch handler intercepts it before the Router.
    // The stub below is kept commented for historical context.

    void handle_delete_mcp(const onh::Request& req, onh::Response& resp)
    {
        // Phase A4 — Session termination per spec §Transports#Session-Management.
        // "Clients that no longer need a particular session SHOULD send an
        //  HTTP DELETE to the MCP endpoint with the MCP-Session-Id header."
        if (!check_security(req, resp, /*effective_session*/ "",
                            /*pre_init*/ false)) return;

        std::optional<std::string_view> sid;
        if (auto h = req.header("mcp-session-id")) sid = *h;
        if (!sid || sid->empty()) {
            // Per spec: 400 Bad Request if session id is missing on a non-init.
            write_json(resp, 400, {{"error",
                "MCP-Session-Id header required for DELETE"}});
            return;
        }
        if (state.end_session(std::string(*sid))) {
            resp.setStatus(static_cast<onh::StatusCode>(200));
            resp.setBody("{}");
        } else {
            // Per spec: "respond to requests containing that session ID
            // with HTTP 404 Not Found" once the session is gone.
            resp.setStatus(static_cast<onh::StatusCode>(404));
            resp.setBody("{}");
        }
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

    void handle_get_config(onh::Response& resp)
    {
        // The ServerState emits its own JSON to keep the field names in
        // sync with what on-disk config.json uses.
        std::string body = state.config_json();
        resp.setStatus(static_cast<onh::StatusCode>(200));
        resp.setContentType("application/json");
        resp.setBody(body);
    }

    // GET /api/models — return the discovered models as JSON.
    void handle_get_models(onh::Response& resp)
    {
        auto list = state.list_models();
        json out = json::array();
        for (const auto& m : list) {
            out.push_back({
                {"name",       m.name},
                {"path",       m.path},
                {"size_bytes", m.size_bytes},
                {"mtime",      static_cast<std::int64_t>(m.mtime)},
                {"dir",        m.dir},
            });
        }
        json body = {
            {"models", out},
            {"dirs",   [&]{
                json a = json::array();
                for (const auto& d : state.models_dirs()) a.push_back(d);
                return a;
            }()},
        };
        write_json(resp, 200, body);
    }

    void handle_post_config(const onh::Request& req, onh::Response& resp)
    {
        std::string body(req.bodyAsString());
        if (body.empty()) {
            write_json(resp, 400, {{"error", "empty body"}});
            return;
        }
        json parsed;
        try {
            parsed = json::parse(body);
        } catch (const std::exception& e) {
            write_json(resp, 400, {{"error", std::string("parse error: ") + e.what()}});
            return;
        }
        if (!parsed.is_object()) {
            write_json(resp, 400, {{"error", "expected JSON object"}});
            return;
        }
        // Order matters: every state mutation has to happen BEFORE
        // set_models(), because set_models is what writes config.json
        // to disk. If we wrote to disk first then mutated state, the
        // on-disk file would miss the new verbose/mode/data_dir values.
        std::string embed = parsed.value("embed_model", "");
        std::string llm   = parsed.value("llm_model",   "");
        std::vector<std::string> models_dirs;
        bool data_dir_changed = false;
        if (parsed.contains("data_dir") && parsed["data_dir"].is_string()) {
            std::string new_dd = parsed["data_dir"].get<std::string>();
            if (new_dd != state.data_dir() && !new_dd.empty()) {
                state.set_data_dir(new_dd);
                data_dir_changed = true;
            }
        }
        if (parsed.contains("models_dir") && parsed["models_dir"].is_array()) {
            for (const auto& item : parsed["models_dir"]) {
                if (item.is_string()) models_dirs.push_back(item.get<std::string>());
            }
            state.set_models_dirs(models_dirs);
            state.rescan_models();
        }
        if (parsed.contains("verbose") && parsed["verbose"].is_boolean()) {
            state.set_verbose(parsed["verbose"].get<bool>());
            if (state.verbose()) {
                OmniNet::Logger::instance().setLevel(OmniNet::LogLevel::DEBUG);
            } else {
                OmniNet::Logger::instance().setLevel(OmniNet::LogLevel::INFO);
            }
        }
        if (parsed.contains("default_search_mode") && parsed["default_search_mode"].is_string()) {
            state.set_default_search_mode(parsed["default_search_mode"].get<std::string>());
        }

        // set_models LAST — it persists config.json using the current
        // state (which now reflects all the changes above). It also
        // hot-reloads the embed/llm models if their paths were set.
        auto r = state.set_models(embed, llm);

        // Verbose logging toggle and default search mode are
        // hot-reloadable. Apply immediately, no restart needed.
        if (parsed.contains("verbose") && parsed["verbose"].is_boolean()) {
            state.set_verbose(parsed["verbose"].get<bool>());
            if (state.verbose()) {
                OmniNet::Logger::instance().setLevel(OmniNet::LogLevel::DEBUG);
            } else {
                OmniNet::Logger::instance().setLevel(OmniNet::LogLevel::INFO);
            }
        }
        if (parsed.contains("default_search_mode") && parsed["default_search_mode"].is_string()) {
            state.set_default_search_mode(parsed["default_search_mode"].get<std::string>());
        }

        // "ok" is true if the request completed AND any user-supplied
        // path loaded successfully. If a user supplied a path and that
        // path failed to load, ok is false → 400 so the client can show
        // the error. If both fields are empty (keep current), ok is
        // trivially true.
        bool user_asked_embed = !embed.empty();
        bool user_asked_llm   = !llm.empty();
        bool any_failed = (user_asked_embed && !r.embed_loaded) ||
                          (user_asked_llm   && !r.llm_loaded);
        bool ok = !any_failed;

        json out = {
            {"ok",                 ok},
            {"embed_model",        r.embed_model},
            {"llm_model",          r.llm_model},
            {"embed_loaded",       r.embed_loaded},
            {"llm_loaded",         r.llm_loaded},
            {"embed_error",        r.embed_error},
            {"llm_error",          r.llm_error},
            {"config_path",        state.config_path()},
            {"data_dir",           state.data_dir()},
            {"data_dir_restart_required", data_dir_changed},
            {"default_search_mode", state.default_search_mode()},
            {"verbose",            state.verbose()},
        };
        int status = ok ? 200 : 400;
        write_json(resp, status, out);
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