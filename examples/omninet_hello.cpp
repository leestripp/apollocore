/**
 * ApolloCore + OmniNet — Hello World
 *
 * Minimal example showing how to embed an HTTP server in an ApolloCore
 * application. This is the foundation for future REST API and MCP
 * HTTP transport.
 *
 * Build:  cmake --build build --target omninet_hello
 * Run:    ./build/examples/omninet_hello
 * Test:   curl http://localhost:18080/api/health
 *
 * Stop:   Ctrl+C
 */

#include "OmniNet.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;

int main() {
    // Initialise OmniNet (logging, signal handlers, config)
    on::init();

    // Configure logging
    auto& cfg = on::Config::instance();
    std::filesystem::create_directories(cfg.dataDir() / "logs");
    on::Logger::instance().setLogFile(
        (cfg.dataDir() / "logs" / "apollocore_omninet.log").string());
    on::Logger::instance().setLevel(on::LogLevel::INFO);

    // Server config
    onh::Server::Config sc;
    sc.address        = "0.0.0.0";
    sc.port           = 18080;          // avoid common dev ports
    sc.workerCount    = 4;
    sc.requestTimeout = std::chrono::seconds(30);
    onh::Server server(sc);

    // --- Routes ---

    // Health check — proves OmniNet is wired into ApolloCore
    server.get("/api/health", [](const onh::Request& /*req*/,
                                 onh::Response& resp) {
        resp.setStatus(onh::StatusCode::OK);
        resp.setContentType("application/json");
        resp.setBody(R"({
            "status": "ok",
            "service": "apollocore",
            "version": "1.0.0",
            "transport": "omninet"
        })");
    });

    // KB info — placeholder for future REST API
    server.get("/api/kb/info", [](const onh::Request& /*req*/,
                                  onh::Response& resp) {
        resp.setStatus(onh::StatusCode::OK);
        resp.setContentType("application/json");
        resp.setBody(R"({
            "name": "default",
            "documents": 0,
            "chunks": 0,
            "entities": 0,
            "note": "REST API not yet implemented - see Phase REST in CodeMax.md"
        })");
    });

    // Root — redirect to health
    server.get("/", [](const onh::Request& /*req*/,
                       onh::Response& resp) {
        resp.setStatus(onh::StatusCode::SEE_OTHER);
        resp.setHeader(std::string_view("Location"), std::string_view("/api/health"));
    });

    // Print startup banner
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ApolloCore + OmniNet Hello World                         ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Listening on: http://0.0.0.0:18080                       ║\n";
    std::cout << "║  Try:                                                      ║\n";
    std::cout << "║    curl http://localhost:18080/api/health                  ║\n";
    std::cout << "║    curl http://localhost:18080/api/kb/info                 ║\n";
    std::cout << "║                                                            ║\n";
    std::cout << "║  Press Ctrl+C to stop                                      ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // Run. start() blocks; spawn a thread for background work if needed.
    server.start();

    on::cleanup();
    return 0;
}
