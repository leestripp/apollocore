// SPDX-License-Identifier: MIT
//
// test_ui_static.cpp — verifies the Web UI's static files exist and have
// the expected structure. No network round-trip (those happen via curl in
// docs/manual-ui-test.md and in the developer's smoke test).
//
// This runs at build time and protects against accidentally deleting
// or breaking the UI without anyone noticing.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace fs = std::filesystem;

#define ASSERT(cond, msg) do {                                          \
    if (!(cond)) {                                                      \
        std::cerr << "FAIL: " << msg                                    \
                  << " (at " << __FILE__ << ":" << __LINE__ << ")\n";   \
        return 1;                                                       \
    }                                                                   \
} while (0)

struct Req {
    std::string path;
    size_t min_bytes = 1;
    std::vector<std::string> must_contain;
};

int main() {
    // Find the static directory relative to the project root.
    // CTest sets WORKING_DIRECTORY = ${CMAKE_SOURCE_DIR} for our tests.
    fs::path ui_root = "src/mcp_server/static";
    if (!fs::is_directory(ui_root)) {
        std::cerr << "FAIL: UI directory not found at " << fs::absolute(ui_root) << "\n";
        return 1;
    }

    std::vector<Req> reqs = {
        { "index.html",         200,  { "<!doctype html>", "ApolloCore", "/js/app.js" } },
        { "css/style.css",      500,  { ":root", "--bg", ".tabs" } },
        { "js/api.js",          200,  { "export async function rpc", "MCP_ENDPOINT" } },
        { "js/app.js",          100,  { "import", "showTab", "hashchange" } },
        { "js/health.js",       100,  { "renderHealth", "/health" } },
        { "js/search.js",      100,  { "renderSearch", "apollo_query_kb" } },
        { "js/graph.js",       200,  { "renderGraph", "frStep", "Fruchterman" } },
        { "js/settings.js",    100,  { "renderSettings", "apollo_set_project" } },
        { "js/about.js",       100,  { "renderAbout", "mcpServers" } },
    };

    int failures = 0;

    for (const auto& r : reqs) {
        fs::path p = ui_root / r.path;
        if (!fs::exists(p)) {
            std::cerr << "FAIL: missing " << p << "\n";
            ++failures;
            continue;
        }
        auto size = fs::file_size(p);
        if (size < r.min_bytes) {
            std::cerr << "FAIL: " << p << " is only " << size << " bytes (expected >= " << r.min_bytes << ")\n";
            ++failures;
            continue;
        }
        std::ifstream f(p);
        std::stringstream ss; ss << f.rdbuf();
        std::string content = ss.str();
        for (const auto& needle : r.must_contain) {
            if (content.find(needle) == std::string::npos) {
                std::cerr << "FAIL: " << p << " is missing required content: \"" << needle << "\"\n";
                ++failures;
                break;
            }
        }
        std::cout << "OK   " << p << " (" << size << " bytes)\n";
    }

    // Also verify no JS file references any external CDN (must be self-hosted).
    for (const auto& r : reqs) {
        if (r.path.substr(r.path.find_last_of('.') + 1) != "js") continue;
        fs::path p = ui_root / r.path;
        std::ifstream f(p);
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        for (const auto& host : { "cdn.", "unpkg.", "jsdelivr.", "googleapis." }) {
            if (content.find(host) != std::string::npos) {
                std::cerr << "FAIL: " << p << " references external CDN (" << host << ")\n";
                ++failures;
            }
        }
    }

    if (failures) {
        std::cerr << "\n" << failures << " UI static-file checks failed.\n";
        return 1;
    }

    std::cout << "\nAll " << reqs.size() << " UI static files pass structural checks.\n";
    return 0;
}