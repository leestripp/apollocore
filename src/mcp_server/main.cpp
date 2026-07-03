// SPDX-License-Identifier: MIT
//
// main.cpp — ApolloCore MCP server entry point
//
// Usage:
//   apollo_mcp_server [--host HOST] [--port PORT]
//                     [--data-dir DIR]      (ApolloCore KB directory)
//                     [--embed-model PATH]  (GGUF embedding model)
//                     [--llm-model PATH]    (GGUF LLM model, optional)
//                     [--ui-static-dir DIR] (web UI assets; "" or --no-ui disables)
//                     [--no-ui]             (don't serve the web UI at /)
//                     [--admin]             (enable /omninet admin UI)
//
// Environment variables (defaults if --flag not given):
//   APOLLO_DATA_DIR     default "data"
//   APOLLO_EMBED_MODEL  default "models/nomic-embed-text-v1.5.Q4_K_M.gguf"
//   APOLLO_LLM_MODEL    default "models/qwen2.5-0.5b-instruct-q4_k_m.gguf"
//   OMNINET_DATA_DIR    OmniNet's own ~/.omninet location
//   APOLLO_NO_UI        set to "1" or "true" to disable the web UI

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "OmniNet.h"   // <-- bring in the OmniNet namespace

#include "http_server.hpp"
#include "server_state.hpp"

namespace fs = std::filesystem;

namespace {

struct CliArgs {
    // MCP spec §6.2 (Streamable HTTP): "Bind to 127.0.0.1, NOT 0.0.0.0,
    // for local servers." We default to loopback for safety. Use
    // --host 0.0.0.0 to expose on the LAN (only if you trust the
    // network — there's no auth beyond APOLLO_AUTH_TOKEN).
    std::string host = "127.0.0.1";
    int port = 8765;
    std::string data_dir = "data";
    std::string embed_model = "models/nomic-embed-text-v1.5.Q4_K_M.gguf";
    std::string llm_model = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";
    std::string ui_static_dir;       // empty = try default; --no-ui disables
    bool no_ui = false;
    bool admin = false;
    bool verbose = false;
};

CliArgs parse_args(int argc, char** argv)
{
    CliArgs a;
    auto env_or = [](const char* key, const std::string& def) -> std::string {
        const char* v = std::getenv(key);
        return v && *v ? std::string(v) : def;
    };
    a.data_dir    = env_or("APOLLO_DATA_DIR",    a.data_dir);
    a.embed_model = env_or("APOLLO_EMBED_MODEL", a.embed_model);
    a.llm_model   = env_or("APOLLO_LLM_MODEL",   a.llm_model);
    if (auto* no_ui = std::getenv("APOLLO_NO_UI"); no_ui && *no_ui) {
        std::string v = no_ui;
        a.no_ui = (v == "1" || v == "true" || v == "yes" || v == "on");
    }

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](const char* opt) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << opt << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (k == "--host")           a.host = need("--host");
        else if (k == "--port")           a.port = std::stoi(need("--port"));
        else if (k == "--data-dir")       a.data_dir = need("--data-dir");
        else if (k == "--embed-model")    a.embed_model = need("--embed-model");
        else if (k == "--llm-model")      a.llm_model = need("--llm-model");
        else if (k == "--ui-static-dir")  a.ui_static_dir = need("--ui-static-dir");
        else if (k == "--no-ui")          a.no_ui = true;
        else if (k == "--admin")          a.admin = true;
        else if (k == "-v" || k == "--verbose") a.verbose = true;
        else if (k == "-h" || k == "--help") {
            std::cout <<
                "apollo_mcp_server — ApolloCore MCP server\n"
                "  --host HOST              (default 127.0.0.1; pass 0.0.0.0 for LAN)\n"
                "  --port PORT              (default 8765)\n"
                "  --data-dir DIR           ApolloCore KB directory (default data)\n"
                "  --embed-model PATH       GGUF embedding model\n"
                "  --llm-model PATH         GGUF LLM model (entity extraction)\n"
                "  --ui-static-dir DIR      web UI assets (default: bundled)\n"
                "  --no-ui                  disable the web UI at /\n"
                "  --admin                  enable /omninet admin UI\n"
                "  -v, --verbose            verbose logging\n";
            std::exit(0);
        }
        else {
            std::cerr << "Unknown argument: " << k << " (try --help)\n";
            std::exit(2);
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv)
{
    auto args = parse_args(argc, argv);

    // Ignore SIGPIPE. OmniNet's Socket::send does a raw ::send() and
    // when a client closes the connection, the kernel sends SIGPIPE to
    // the whole process — which would terminate the server even though
    // the offending write is in a worker thread that's already
    // supposed to clean up on socket failure. Disabling SIGPIPE at
    // process startup means ::send returns -1/EPIPE and our writer
    // thread can exit cleanly. This is the standard pattern for
    // long-running HTTP servers (nginx, redis, etc. all do this).
#ifdef __APPLE__
    (void)std::signal(SIGPIPE, SIG_IGN);
#else
    // POSIX.1-2008 marks signal() as obsolete for SIG_IGN; use
    // sigaction for portability on Linux.
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGPIPE, &sa, nullptr);
#endif

    // Bootstrap OmniNet (logger, signal handlers, config dir).
    OmniNet::init();

    // Configure OmniNet logging.
    auto& omni_cfg = OmniNet::Config::instance();
    std::error_code ec;
    fs::create_directories(omni_cfg.dataDir() / "logs", ec);
    OmniNet::Logger::instance().setLogFile(
        (omni_cfg.dataDir() / "logs" / "apollo_mcp_server.log").string());
    OmniNet::Logger::instance().setLevel(
        args.verbose ? OmniNet::LogLevel::DEBUG : OmniNet::LogLevel::INFO);

    // Construct server state (owns ApolloCore).
    apollo::mcp::ServerState state;

    // Tell state where the data dir lives so it can persist runtime
    // config (e.g. model paths changed via /api/config).
    state.set_data_dir(args.data_dir);

    // Register the install-path models directory as a search target for
    // resolving bare filenames (e.g. when the user picks a file via the
    // browser's <input type="file">, which doesn't expose the full path).
    {
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            state.add_search_dir((exe.parent_path().parent_path() / "share" / "apollocore" / "models").string());
            state.add_search_dir((exe.parent_path().parent_path() / "models").string());
        }
    }

    // Default models-dir: the project's models/ directory next to the
    // source tree, plus the install-path models dir if it exists. The
    // user can change this from the Settings page and the dropdown
    // updates immediately.
    {
        std::vector<std::string> defaults;
        if (fs::is_directory("./models"))             defaults.push_back(fs::absolute("./models").string());
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            fs::path share = exe.parent_path().parent_path() / "share" / "apollocore" / "models";
            if (fs::is_directory(share)) defaults.push_back(fs::absolute(share).string());
        }
        state.set_models_dirs(defaults);
        state.rescan_models();
    }

    // Resolve model paths. Priority:
    //   1. CLI / env override (user picked a path explicitly)
    //   2. <data-dir>/config.json (Settings page persistence)
    //   3. CLI default placeholder (relative path, e.g. models/...)
    {
        std::string cfg_embed, cfg_llm;
        state.read_config_paths(cfg_embed, cfg_llm);

        const char* env_embed = std::getenv("APOLLO_EMBED_MODEL");
        const char* env_llm   = std::getenv("APOLLO_LLM_MODEL");
        bool cli_embed_explicit = env_embed && *env_embed;
        bool cli_llm_explicit   = env_llm   && *env_llm;
        const std::string default_embed = "models/nomic-embed-text-v1.5.Q4_K_M.gguf";
        const std::string default_llm   = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";
        if (!cli_embed_explicit && args.embed_model != default_embed) cli_embed_explicit = true;
        if (!cli_llm_explicit   && args.llm_model   != default_llm)   cli_llm_explicit   = true;

        // Apply config.json when the user did NOT pick a path explicitly.
        if (!cli_embed_explicit && !cfg_embed.empty()) args.embed_model = cfg_embed;
        if (!cli_llm_explicit   && !cfg_llm.empty())   args.llm_model   = cfg_llm;
    }

    // Record the resolved paths in state (so /api/config and
    // apollo://config report them even on a fresh start). Loading is
    // still done below — this only updates state, no model reload.
    state.record_resolved_models(args.embed_model, args.llm_model);

    // Optional: load embedding model now if the file exists.
    if (fs::exists(args.embed_model)) {
        if (state.core().load_embedding_model(args.embed_model)) {
            OmniNet::Logger::instance().info(
                "Loaded embedding model: " + args.embed_model);
            state.report_embed_load(true, "");
        } else {
            OmniNet::Logger::instance().warn(
                "Failed to load embedding model: " + args.embed_model);
            state.report_embed_load(false, "load_embedding_model returned false");
        }
    } else {
        OmniNet::Logger::instance().warn(
            "Embedding model not found at " + args.embed_model +
            " — query_kb tool will fail until one is loaded.");
        state.report_embed_load(false, "file not found");
    }
    // LLM model is optional — only needed for entity extraction / reranking.
    if (fs::exists(args.llm_model)) {
        if (state.core().load_llm_model(args.llm_model)) {
            OmniNet::Logger::instance().info(
                "Loaded LLM model: " + args.llm_model);
            state.report_llm_load(true, "");
        } else {
            OmniNet::Logger::instance().warn(
                "Failed to load LLM model: " + args.llm_model);
            state.report_llm_load(false, "load_llm_model returned false");
        }
    } else {
        OmniNet::Logger::instance().info(
            "No LLM model at " + args.llm_model +
            " — extract_entities/with_rerank tools will be unavailable.");
        state.report_llm_load(false, "file not found");
    }

    // Configure HTTP server.
    apollo::mcp::HttpConfig hcfg;
    hcfg.host = args.host;
    hcfg.port = args.port;
    hcfg.enable_admin_dashboard = args.admin;

    // Resolve UI static directory.
    // Priority (first-existing wins):
    //   1. --no-ui  → "" (disabled)
    //   2. --ui-static-dir (explicit user override)
    //   3. ${CMAKE_SOURCE_DIR}/src/mcp_server/static/ (dev, current dir)
    //   4. <executable_dir>/../share/apollocore/ui/static/ (install)
    if (!args.no_ui) {
        std::vector<fs::path> candidates;
        if (!args.ui_static_dir.empty()) candidates.emplace_back(args.ui_static_dir);
        candidates.emplace_back("src/mcp_server/static");

        // Install-path candidate: <exe>/../share/apollocore/ui/static/
        // /proc/self/exe on Linux, _NSGetExecutablePath on macOS.
        #if defined(__linux__)
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) candidates.emplace_back(exe.parent_path().parent_path() / "share" / "apollocore" / "ui" / "static");
        #elif defined(__APPLE__)
        // macOS support can be added via _NSGetExecutablePath; deferred.
        #endif

        for (const auto& c : candidates) {
            std::error_code ec;
            if (fs::is_directory(c, ec)) {
                hcfg.ui_static_dir = fs::absolute(c, ec).string();
                break;
            }
        }
        if (hcfg.ui_static_dir.empty()) {
            OmniNet::Logger::instance().warn(std::string(
                "Web UI directory not found; UI disabled. "
                "Pass --ui-static-dir or run from the repo root."));
        }
    }

    OmniNet::Logger::instance().info(std::string(
        "Starting ApolloCore MCP server on " + args.host + ":" +
        std::to_string(args.port) +
        " (MCP spec 2025-11-25, Streamable HTTP transport)" +
        (hcfg.ui_static_dir.empty() ? ", UI disabled" : ", UI at /")));

    apollo::mcp::HttpServer server(state, hcfg);
    server.run();  // blocks

    OmniNet::cleanup();
    return 0;
}