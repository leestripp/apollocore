// SPDX-License-Identifier: MIT
//
// test_mcp_dispatcher.cpp — unit tests for the JSON-RPC 2.0 dispatcher
//
// These tests instantiate a Dispatcher directly (no HTTP) and feed it raw
// JSON-RPC envelopes, then check the response. They use a tiny on-disk KB
// in a temp directory so the test is hermetic.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "jsonrpc.hpp"
#include "dispatcher.hpp"
#include "prompts.hpp"
#include "resources.hpp"
#include "tools.hpp"

namespace fs = std::filesystem;

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

namespace {

// Tiny helper: run a JSON-RPC string and return the parsed response.
nlohmann::json call(apollo::mcp::Dispatcher& d, const std::string& req)
{
    auto parsed = apollo::mcp::parse_request(req);
    auto resp = d.dispatch(parsed);
    return resp.to_json();
}

// Extract `isError` from a tools/call result, treating a missing/null
// `isError` field as success (false).
bool is_error_result(const nlohmann::json& r)
{
    if (!r.contains("result"))               return true;
    if (!r["result"].contains("isError"))    return false;
    if (!r["result"]["isError"].is_boolean()) return false;
    return r["result"]["isError"].get<bool>();
}

std::string make_tmp_kb_path()
{
    char tmpl[] = "/tmp/apollo_mcp_test_XXXXXX";
    if (mkdtemp(tmpl) == nullptr) {
        std::perror("mkdtemp");
        std::exit(2);
    }
    return std::string(tmpl);
}

}  // namespace

int main()
{
    using nlohmann::json;
    int failures = 0;

    auto kb_path = make_tmp_kb_path() + "/kb";
    apollo::mcp::ServerState state;
    apollo::mcp::ToolRegistry tools = apollo::mcp::build_default_registry();
    apollo::mcp::ResourceRegistry resources = apollo::mcp::build_default_resource_registry();
    apollo::mcp::PromptRegistry prompts = apollo::mcp::build_default_prompt_registry();
    apollo::mcp::Dispatcher d(state, tools, resources, prompts);

    // ---- Test 1: initialize handshake ----
    std::cout << "[test] initialize handshake...\n";
    {
        json r = call(d, R"({
            "jsonrpc":"2.0","id":1,"method":"initialize",
            "params":{"protocolVersion":"2025-11-25","capabilities":{},
                      "clientInfo":{"name":"t","version":"0"}}
        })");
        APOLLO_ASSERT(r.contains("result"),                "initialize: no result");
        APOLLO_ASSERT(r["result"]["protocolVersion"] == "2025-11-25",
                      "initialize: wrong protocolVersion");
        APOLLO_ASSERT(r["result"]["serverInfo"]["name"] == "apollo_mcp_server",
                      "initialize: wrong server name");
        APOLLO_ASSERT(r["result"]["capabilities"].contains("tools"),     "initialize: no tools cap");
        APOLLO_ASSERT(r["result"]["capabilities"].contains("resources"), "initialize: no resources cap");
        APOLLO_ASSERT(r["result"]["capabilities"].contains("prompts"),   "initialize: no prompts cap");
    }

    // ---- Test 2: tools/list returns 12 tools with annotations ----
    std::cout << "[test] tools/list returns 12 tools...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
        APOLLO_ASSERT(r.contains("result"), "tools/list: no result");
        const auto& arr = r["result"]["tools"];
        APOLLO_ASSERT(arr.is_array(),         "tools/list: not an array");
        APOLLO_ASSERT(arr.size() == 12,       "tools/list: expected 12 tools");
        // Check that all tools have a non-empty description.
        for (const auto& t : arr) {
            APOLLO_ASSERT(t.contains("name"),        "tool: no name");
            APOLLO_ASSERT(t.contains("description"), "tool: no description");
            APOLLO_ASSERT(!t["description"].get<std::string>().empty(),
                          "tool: empty description");
            APOLLO_ASSERT(t.contains("inputSchema"), "tool: no inputSchema");
        }
    }

    // ---- Test 3: resources/list returns 5 resources ----
    std::cout << "[test] resources/list returns 5 resources...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":3,"method":"resources/list"})");
        APOLLO_ASSERT(r.contains("result"), "resources/list: no result");
        APOLLO_ASSERT(r["result"]["resources"].size() == 5,
                      "resources/list: expected 5 resources");
    }

    // ---- Test 4: prompts/list returns 4 prompts ----
    std::cout << "[test] prompts/list returns 4 prompts...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":4,"method":"prompts/list"})");
        APOLLO_ASSERT(r["result"]["prompts"].size() == 4,
                      "prompts/list: expected 4 prompts");
        bool has_answer = false;
        for (const auto& p : r["result"]["prompts"]) {
            if (p["name"] == "answer_with_citations") has_answer = true;
        }
        APOLLO_ASSERT(has_answer, "prompts/list: missing answer_with_citations");
    }

    // ---- Test 5: ping returns empty object ----
    std::cout << "[test] ping...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":5,"method":"ping"})");
        APOLLO_ASSERT(r["result"].is_object(), "ping: result not an object");
        APOLLO_ASSERT(r["result"].empty(),     "ping: result not empty");
    }

    // ---- Test 6: unknown method returns JSON-RPC error ----
    std::cout << "[test] unknown method → -32601...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":6,"method":"nope/not_real"})");
        APOLLO_ASSERT(r.contains("error"),    "unknown: no error");
        APOLLO_ASSERT(r["error"]["code"] == -32601, "unknown: wrong code");
    }

    // ---- Test 7: tools/call apollo_create_kb then apollo_get_stats ----
    std::cout << "[test] end-to-end: create KB + stats...\n";
    {
        // Create KB.
        std::string create_req = std::string(R"({
            "jsonrpc":"2.0","id":7,"method":"tools/call",
            "params":{"name":"apollo_create_kb",
                      "arguments":{"path":")") + kb_path + R"(","name":"t"}}
        })";
        json r1 = call(d, create_req);
        APOLLO_ASSERT(!is_error_result(r1), "create_kb: error result");
        APOLLO_ASSERT(r1["result"]["content"][0]["text"].get<std::string>().find("Created") != std::string::npos,
                      "create_kb: no 'Created' in result");

        // Stats.
        json r2 = call(d, R"({"jsonrpc":"2.0","id":8,"method":"tools/call",
            "params":{"name":"apollo_get_stats","arguments":{}}})");
        APOLLO_ASSERT(!is_error_result(r2), "get_stats: error");
    }

    // ---- Test 8: tools/call with unknown tool name → result isError=true ----
    std::cout << "[test] unknown tool returns isError=true result...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":9,"method":"tools/call",
            "params":{"name":"apollo_nonexistent","arguments":{}}})");
        APOLLO_ASSERT(is_error_result(r),
                      "unknown tool: should be isError=true");
    }

    // ---- Test 9: tools/call with missing required arg → isError=true ----
    std::cout << "[test] missing required arg → isError=true...\n";
    {
        json r = call(d, R"({"jsonrpc":"2.0","id":10,"method":"tools/call",
            "params":{"name":"apollo_query_kb","arguments":{}}})");
        APOLLO_ASSERT(is_error_result(r),
                      "missing arg: should be isError=true");
    }

    // ---- Test 10: notification → empty response (no id, no result, no error) ----
    std::cout << "[test] notification → no response payload...\n";
    {
        // notifications/initialized — we configured the dispatcher to NOT
        // send a response for notifications.
        auto parsed = apollo::mcp::parse_request(
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        auto resp = d.dispatch(parsed);
        auto j = resp.to_json();
        APOLLO_ASSERT(!j.contains("result"), "notification: should not have result");
        APOLLO_ASSERT(!j.contains("error"),  "notification: should not have error");
    }

    // ---- Cleanup ----
    std::error_code ec;
    fs::remove_all(fs::path(kb_path).parent_path(), ec);

    if (failures == 0) {
        std::cout << "\nALL MCP DISPATCHER TESTS PASSED\n";
        return 0;
    }
    std::cout << "\n" << failures << " FAILURE(S)\n";
    return 1;
}