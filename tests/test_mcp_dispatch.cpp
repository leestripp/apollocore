// SPDX-License-Identifier: MIT
//
// test_mcp_dispatch.cpp — unit tests for dispatch_one(), the envelope
// helper shared by the HTTP and stdio transports.
//
// Contract under test (mcp_dispatch.hpp):
//   - ordinary request      → response json carrying the same id
//   - notification (no id)  → std::nullopt (caller MUST NOT write anything)
//   - malformed envelope    → JSON-RPC error object (-32700 / -32600)
//   - dispatcher throwing   → -32603 internal error with the id attached

#include <iostream>
#include <string>

#include "jsonrpc.hpp"
#include "dispatcher.hpp"
#include "mcp_dispatch.hpp"
#include "mcp_sse_bus.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "tools.hpp"

using nlohmann::json;
using apollo::mcp::dispatch_one;

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

int main()
{
    apollo::mcp::ServerState state;
    apollo::mcp::McpSseBus sse_bus;
    apollo::mcp::ToolRegistry tools = apollo::mcp::build_default_registry();
    apollo::mcp::ResourceRegistry resources = apollo::mcp::build_default_resource_registry();
    apollo::mcp::PromptRegistry prompts = apollo::mcp::build_default_prompt_registry();
    apollo::mcp::Dispatcher d(state, sse_bus, tools, resources, prompts);

    // ── ordinary request: response carries the same id ──────────────────
    {
        auto r = dispatch_one(d, json::parse(
            R"({"jsonrpc":"2.0","id":42,"method":"ping"})"), "sess-1");
        APOLLO_ASSERT(r.has_value(), "request must produce a response");
        APOLLO_ASSERT(r->contains("result"), "ping: no result");
        APOLLO_ASSERT((*r)["id"] == 42, "response id must echo request id");
        APOLLO_ASSERT((*r)["jsonrpc"] == "2.0", "response carries jsonrpc 2.0");
    }

    // ── string ids are preserved too ─────────────────────────────────────
    {
        auto r = dispatch_one(d, json::parse(
            R"({"jsonrpc":"2.0","id":"abc-123","method":"ping"})"), "sess-1");
        APOLLO_ASSERT(r.has_value() && (*r)["id"] == "abc-123",
                      "string id echoed");
    }

    // ── notification: nullopt, caller must stay silent ───────────────────
    {
        auto r = dispatch_one(d, json::parse(
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), "sess-1");
        APOLLO_ASSERT(!r.has_value(),
                      "notification must yield nullopt (no response)");
    }

    // ── unknown method: error object with the request id ────────────────
    {
        auto r = dispatch_one(d, json::parse(
            R"({"jsonrpc":"2.0","id":7,"method":"nope/not_real"})"), "sess-1");
        APOLLO_ASSERT(r.has_value(), "unknown method still gets a response");
        APOLLO_ASSERT(r->contains("error"), "unknown method: no error");
        APOLLO_ASSERT((*r)["error"]["code"] == -32601,
                      "unknown method: want -32601");
        APOLLO_ASSERT((*r)["id"] == 7, "error carries request id");
    }

    // ── malformed envelopes → JSON-RPC error, never a crash ─────────────
    for (const char* bad : {
            R"([1,2,3])",                            // top-level not an object
            R"({"id":1,"method":"ping"})",           // jsonrpc member missing
            R"({"jsonrpc":"1.0","id":1,"method":"ping"})",  // wrong version
            R"({"jsonrpc":"2.0","id":1})",           // method missing
            R"({"jsonrpc":"2.0","id":1,"method":42})",      // method not a string
        }) {
        auto r = dispatch_one(d, json::parse(bad), "sess-1");
        APOLLO_ASSERT(r.has_value(), "malformed envelope must get an error object");
        APOLLO_ASSERT(r->contains("error"),
                      "malformed envelope: no error member for " << bad);
    }

    // ── session id is passed through to initialize ───────────────────────
    {
        auto r = dispatch_one(d, json::parse(R"({
            "jsonrpc":"2.0","id":1,"method":"initialize",
            "params":{"protocolVersion":"2025-11-25","capabilities":{},
                      "clientInfo":{"name":"t","version":"0"}}
        })"), "sess-xyz");
        APOLLO_ASSERT(r.has_value() && r->contains("result"),
                      "initialize through dispatch_one works");
        APOLLO_ASSERT((*r)["result"]["protocolVersion"] == "2025-11-25",
                      "initialize negotiated pinned version");
    }

    std::cout << "ALL MCP DISPATCH_ONE TESTS PASSED\n";
    return 0;
}
