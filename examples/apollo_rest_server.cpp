/**
 * ApolloCore REST Server (Phase R1)
 *
 * HTTP server exposing ApolloCore via REST API.
 * Uses OmniNet for HTTP, links to apollo_core for KB operations.
 *
 * Usage:
 *   ./apollo_rest_server --embedding-model models/embed.gguf [--llm-model models/qwen.gguf] [--port 18080]
 *
 * Then in another terminal:
 *   curl http://localhost:18080/api/health
 */

#include "OmniNet.h"
#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_rest_api.hpp"  // REST API route handlers

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;

// ============================================================================
// CLI flag parsing (R1.9) — minimal, no extra deps
// ============================================================================

struct Args {
    std::string embedding_model;
    std::string llm_model;        // optional
    std::string kb_path;          // optional: open existing on startup
    std::string kb_name = "REST KB";
    int         port = 18080;
    std::string address = "0.0.0.0";
    int         workers = 4;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const std::string& opt) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << opt << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (arg == "--embedding-model") a.embedding_model = next(arg);
        else if (arg == "--llm-model")       a.llm_model       = next(arg);
        else if (arg == "--kb")              a.kb_path         = next(arg);
        else if (arg == "--kb-name")         a.kb_name         = next(arg);
        else if (arg == "--port")            a.port            = std::stoi(next(arg));
        else if (arg == "--address")         a.address         = next(arg);
        else if (arg == "--workers")         a.workers         = std::stoi(next(arg));
        else if (arg == "-h" || arg == "--help") {
            std::cout <<
                "ApolloCore REST Server (Phase R1)\n"
                "\n"
                "Usage: apollo_rest_server [options]\n"
                "\n"
                "Options:\n"
                "  --embedding-model PATH  GGUF embedding model (required)\n"
                "  --llm-model PATH        GGUF LLM model for entity extraction (optional)\n"
                "  --kb PATH               Open existing KB on startup (optional)\n"
                "  --kb-name NAME          KB name if creating new (default: REST KB)\n"
                "  --port PORT             HTTP port (default: 18080)\n"
                "  --address ADDR          Bind address (default: 0.0.0.0)\n"
                "  --workers N             Worker threads (default: 4)\n"
                "  -h, --help              Show this help\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::exit(2);
        }
    }
    if (a.embedding_model.empty()) {
        std::cerr << "Error: --embedding-model is required\n";
        std::exit(2);
    }
    return a;
}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    // Init OmniNet
    on::init();
    auto& cfg = on::Config::instance();
    std::filesystem::create_directories(cfg.dataDir() / "logs");
    on::Logger::instance().setLogFile(
        (cfg.dataDir() / "logs" / "apollo_rest.log").string());
    on::Logger::instance().setLevel(on::LogLevel::INFO);

    // Server config
    onh::Server::Config sc;
    sc.address        = args.address;
    sc.port           = static_cast<uint16_t>(args.port);
    sc.workerCount    = args.workers;
    sc.requestTimeout = std::chrono::seconds(120);  // LLM calls can be slow
    onh::Server server(sc);

    // =========================================================================
    // Banner
    // =========================================================================
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ApolloCore REST Server (Phase R1)                          ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  HTTP:     http://" << args.address << ":" << args.port
              << std::string(std::max(0, 30 - (int)(args.address.size() +
                  std::to_string(args.port).size())), ' ') << "║\n";
    std::cout << "║  Workers:  " << args.workers << std::string(49, ' ') << "║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Loading models...                                          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";

    // =========================================================================
    // Load models (R1 — on startup)
    // =========================================================================
    auto kb = std::make_unique<apollocore_v2::ApolloCore>();

    std::cout << "  Embedding model: " << args.embedding_model << "\n";
    if (!kb->load_embedding_model(args.embedding_model)) {
        std::cerr << "Failed to load embedding model\n";
        return 1;
    }
    std::cout << "  ✓ Embedding model loaded\n";

    bool llm_ok = false;
    if (!args.llm_model.empty()) {
        std::cout << "  LLM model:       " << args.llm_model << "\n";
        if (kb->load_llm_model(args.llm_model)) {
            std::cout << "  ✓ LLM model loaded (entity extraction enabled)\n";
            llm_ok = true;
        } else {
            std::cerr << "  ✗ Failed to load LLM (entity extraction disabled)\n";
        }
    } else {
        std::cout << "  • No LLM model — entity extraction disabled\n";
    }
    (void)llm_ok;  // Currently only informational

    // =========================================================================
    // Open or create KB
    // =========================================================================
    if (!args.kb_path.empty()) {
        if (kb->open(args.kb_path)) {
            std::cout << "  ✓ Opened KB: " << args.kb_path << "\n";
        } else {
            std::cerr << "  ✗ Failed to open KB: " << args.kb_path << "\n";
            return 1;
        }
    }

    // Hand the global KB pointer to the REST API module
    apollocore_v2::rest::g_kb = std::move(kb);

    // =========================================================================
    // Register routes
    // =========================================================================
    apollocore_v2::rest::register_routes(server);

    std::cout << "\n";
    std::cout << "  Routes registered: 11\n";
    std::cout << "  Try: curl http://localhost:" << args.port << "/api/health\n";
    std::cout << "  Press Ctrl+C to stop\n\n";

    // Run
    server.start();

    on::cleanup();
    return 0;
}
