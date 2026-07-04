// SPDX-License-Identifier: MIT
//
// test_mcp_tools.cpp — focused tests on individual tool handlers.
//
// These tests run inside the same process as the dispatcher — no HTTP.

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>

#include "jsonrpc.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "server_state.hpp"
#include "tools.hpp"
#include "dispatcher.hpp"
#include "mcp_sse_bus.hpp"

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

int main()
{
    using nlohmann::json;
    int failures = 0;

    namespace fs = std::filesystem;
    char tmpl[] = "/tmp/apollo_mcp_tools_XXXXXX";
    if (mkdtemp(tmpl) == nullptr) { perror("mkdtemp"); return 2; }
    fs::path kb_dir = fs::path(tmpl) / "kb";

    apollo::mcp::ServerState state;
    apollo::mcp::McpSseBus sse_bus;     // unit tests don't push notifications
    apollo::mcp::ToolRegistry tools = apollo::mcp::build_default_registry();
    apollo::mcp::ResourceRegistry resources = apollo::mcp::build_default_resource_registry();
    apollo::mcp::PromptRegistry prompts = apollo::mcp::build_default_prompt_registry();
    apollo::mcp::Dispatcher d(state, sse_bus, tools, resources, prompts);

    auto call = [&](const json& req) {
        return d.dispatch(apollo::mcp::parse_request(req.dump())).to_json();
    };
    auto is_err = [](const json& r) {
        if (!r.contains("result"))               return true;
        if (!r["result"].contains("isError"))    return false;
        if (!r["result"]["isError"].is_boolean()) return false;
        return r["result"]["isError"].get<bool>();
    };

    // Initialize for protocol-level niceness (not strictly required by the
    // dispatcher itself, but tools assume some state is set).
    call(json{
        {"jsonrpc","2.0"},{"id",1},{"method","initialize"},
        {"params",{
            {"protocolVersion","2025-11-25"},
            {"capabilities", json::object()},
            {"clientInfo",{{"name","t"},{"version","0"}}}
        }}
    });

    // ---- T1: apollo_add_document (KB auto-opened at startup) ----
    {
        json r = call(json{
            {"jsonrpc","2.0"},{"id",11},{"method","tools/call"},
            {"params",{
                {"name","apollo_add_document"},
                {"arguments",{
                    {"text","The Phoenix rocket is reusable. SpaceX lands it on a drone ship."},
                    {"doc_id","rocket"},
                    {"chunk_size",512}
                }}
            }}
        });
        APOLLO_ASSERT(!is_err(r), "add_document: should succeed");
        APOLLO_ASSERT(r["result"]["content"][0]["text"].get<std::string>().find("chunk") != std::string::npos,
                      "add_document: should mention chunks");
    }

    // ---- T2: apollo_query_kb with no embedding model — accept either outcome ----
    {
        json r = call(json{
            {"jsonrpc","2.0"},{"id",13},{"method","tools/call"},
            {"params",{
                {"name","apollo_query_kb"},
                {"arguments",{{"query","rocket"},{"mode","vector"},{"top_k",3}}}
            }}
        });
        APOLLO_ASSERT(r.contains("result"), "query_kb: no result");
    }

    // ---- T3: apollo_get_stats returns JSON object as text ----
    {
        json r = call(json{
            {"jsonrpc","2.0"},{"id",14},{"method","tools/call"},
            {"params",{
                {"name","apollo_get_stats"},
                {"arguments", json::object()}
            }}
        });
        APOLLO_ASSERT(!is_err(r), "get_stats: should succeed");
        auto text = r["result"]["content"][0]["text"].get<std::string>();
        APOLLO_ASSERT(text.find("chunks") != std::string::npos, "stats: missing 'chunks'");
        APOLLO_ASSERT(text.find("\"documents\"") != std::string::npos,
                      "stats: missing 'documents'");
    }

    // ---- T4: resources/read apollo://stats works ----
    {
        json r = call(json{
            {"jsonrpc","2.0"},{"id",15},{"method","resources/read"},
            {"params",{{"uri","apollo://stats"}}}
        });
        APOLLO_ASSERT(r["result"]["contents"].is_array(), "stats resource: no contents");
        APOLLO_ASSERT(!r["result"]["contents"].empty(), "stats resource: empty");
    }

    // ---- T5: prompts/get summarize_kb returns messages ----
    {
        json r = call(json{
            {"jsonrpc","2.0"},{"id",16},{"method","prompts/get"},
            {"params",{{"name","summarize_kb"},{"arguments", json::object()}}}
        });
        APOLLO_ASSERT(!r["result"]["messages"].empty(),
                      "summarize_kb: empty messages");
        bool has_user = false;
        for (const auto& m : r["result"]["messages"]) {
            if (m["role"] == "user") has_user = true;
        }
        APOLLO_ASSERT(has_user, "summarize_kb: missing user message");
    }

    // Cleanup
    std::error_code ec;
    fs::remove_all(tmpl, ec);

    if (failures == 0) {
        std::cout << "\nALL MCP TOOLS TESTS PASSED\n";
        return 0;
    }
    std::cout << "\n" << failures << " FAILURE(S)\n";
    return 1;
}
