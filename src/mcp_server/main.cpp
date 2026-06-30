// SPDX-License-Identifier: MIT
//
// main.cpp — ApolloCore MCP server entry point
//
// Usage:
//   apollo_mcp_server [--host HOST] [--port PORT]
//                     [--data-dir DIR]      (ApolloCore KB directory)
//                     [--embed-model PATH]  (GGUF embedding model)
//                     [--llm-model PATH]    (GGUF LLM model, optional)
//                     [--admin]             (enable /omninet admin UI)
//
// Environment variables (defaults if --flag not given):
//   APOLLO_DATA_DIR     default "data"
//   APOLLO_EMBED_MODEL  default "models/nomic-embed-text-v1.5.Q4_K_M.gguf"
//   APOLLO_LLM_MODEL    default "models/qwen2.5-0.5b-instruct-q4_k_m.gguf"
//   OMNINET_DATA_DIR    OmniNet's own ~/.omninet location

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
    std::string host = "0.0.0.0";
    int port = 8765;
    std::string data_dir = "data";
    std::string embed_model = "models/nomic-embed-text-v1.5.Q4_K_M.gguf";
    std::string llm_model = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";
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

    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](const char* opt) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << opt << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (k == "--host")          a.host = need("--host");
        else if (k == "--port")          a.port = std::stoi(need("--port"));
        else if (k == "--data-dir")      a.data_dir = need("--data-dir");
        else if (k == "--embed-model")   a.embed_model = need("--embed-model");
        else if (k == "--llm-model")     a.llm_model = need("--llm-model");
        else if (k == "--admin")         a.admin = true;
        else if (k == "-v" || k == "--verbose") a.verbose = true;
        else if (k == "-h" || k == "--help") {
            std::cout <<
                "apollo_mcp_server — ApolloCore MCP server\n"
                "  --host HOST         (default 0.0.0.0)\n"
                "  --port PORT         (default 8765)\n"
                "  --data-dir DIR      ApolloCore KB directory (default data)\n"
                "  --embed-model PATH  GGUF embedding model\n"
                "  --llm-model PATH    GGUF LLM model (entity extraction)\n"
                "  --admin             enable /omninet admin UI\n"
                "  -v, --verbose       verbose logging\n";
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

    // Optional: load embedding model now if the file exists.
    if (fs::exists(args.embed_model)) {
        if (state.core().load_embedding_model(args.embed_model)) {
            OmniNet::Logger::instance().info(
                "Loaded embedding model: " + args.embed_model);
        } else {
            OmniNet::Logger::instance().warn(
                "Failed to load embedding model: " + args.embed_model);
        }
    } else {
        OmniNet::Logger::instance().warn(
            "Embedding model not found at " + args.embed_model +
            " — query_kb tool will fail until one is loaded.");
    }
    // LLM model is optional — only needed for entity extraction / reranking.
    if (fs::exists(args.llm_model)) {
        if (state.core().load_llm_model(args.llm_model)) {
            OmniNet::Logger::instance().info(
                "Loaded LLM model: " + args.llm_model);
        } else {
            OmniNet::Logger::instance().warn(
                "Failed to load LLM model: " + args.llm_model);
        }
    } else {
        OmniNet::Logger::instance().info(
            "No LLM model at " + args.llm_model +
            " — extract_entities/with_rerank tools will be unavailable.");
    }

    // Configure HTTP server.
    apollo::mcp::HttpConfig hcfg;
    hcfg.host = args.host;
    hcfg.port = args.port;
    hcfg.enable_admin_dashboard = args.admin;

    OmniNet::Logger::instance().info(
        "Starting ApolloCore MCP server on " + args.host + ":" +
        std::to_string(args.port) +
        " (MCP spec 2025-11-25, Streamable HTTP transport)");

    apollo::mcp::HttpServer server(state, hcfg);
    server.run();  // blocks

    OmniNet::cleanup();
    return 0;
}