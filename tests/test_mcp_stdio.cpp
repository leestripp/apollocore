// SPDX-License-Identifier: MIT
//
// test_mcp_stdio.cpp — unit tests for the stdio transport line loop.
//
// run_stdio_transport() reads newline-delimited JSON-RPC from std::cin and
// writes responses to std::cout. We redirect both via rdbuf swaps and feed
// it framed input, then assert on the exact output lines:
//
//   - valid request          → exactly one response line, id echoed
//   - notification           → silence
//   - garbage line           → -32700 parse error line
//   - '#' comments / blanks  → skipped
//   - trailing '\r'          → tolerated (Windows clients)

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "jsonrpc.hpp"
#include "dispatcher.hpp"
#include "mcp_sse_bus.hpp"
#include "mcp_stdio.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "tools.hpp"

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

namespace {

// Run the transport against `input`, return the stdout lines it produced.
std::vector<std::string> run_transport(const std::string& input)
{
    apollo::mcp::ServerState state;
    apollo::mcp::McpSseBus sse_bus;
    apollo::mcp::ToolRegistry tools = apollo::mcp::build_default_registry();
    apollo::mcp::ResourceRegistry resources = apollo::mcp::build_default_resource_registry();
    apollo::mcp::PromptRegistry prompts = apollo::mcp::build_default_prompt_registry();
    apollo::mcp::Dispatcher d(state, sse_bus, tools, resources, prompts);

    std::istringstream in(input);
    std::ostringstream out;
    auto* old_in  = std::cin.rdbuf(in.rdbuf());
    auto* old_out = std::cout.rdbuf(out.rdbuf());
    apollo::mcp::run_stdio_transport(d, state);
    std::cin.rdbuf(old_in);
    std::cout.rdbuf(old_out);

    std::vector<std::string> lines;
    std::string line;
    std::istringstream os(out.str());
    while (std::getline(os, line)) lines.push_back(line);
    return lines;
}

}  // namespace

int main()
{
    // ── valid request → exactly one line, id echoed ──────────────────────
    {
        auto lines = run_transport(
            "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ping\"}\n");
        APOLLO_ASSERT(lines.size() == 1, "one response line for one request");
        auto r = nlohmann::json::parse(lines[0]);
        APOLLO_ASSERT(r["id"] == 9, "response id echoed");
        APOLLO_ASSERT(r.contains("result"), "ping result present");
    }

    // ── notification → no output at all ──────────────────────────────────
    {
        auto lines = run_transport(
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        APOLLO_ASSERT(lines.empty(), "notification must produce no output");
    }

    // ── garbage → one parse-error line (-32700), loop continues ─────────
    {
        auto lines = run_transport(
            "this is not json\n"
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\n");
        APOLLO_ASSERT(lines.size() == 2, "garbage + valid → two lines");
        auto err = nlohmann::json::parse(lines[0]);
        APOLLO_ASSERT(err["error"]["code"] == -32700, "garbage → -32700");
        auto ok = nlohmann::json::parse(lines[1]);
        APOLLO_ASSERT(ok["id"] == 2 && ok.contains("result"),
                      "loop keeps running after a parse error");
    }

    // ── comments, blank lines and CRLF tolerated ────────────────────────
    {
        auto lines = run_transport(
            "# a comment line\n"
            "\n"
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\r\n");
        APOLLO_ASSERT(lines.size() == 1, "comments/blanks skipped");
        auto r = nlohmann::json::parse(lines[0]);
        APOLLO_ASSERT(r["id"] == 3, "CRLF-terminated frame parsed");
    }

    std::cout << "ALL MCP STDIO TESTS PASSED\n";
    return 0;
}
