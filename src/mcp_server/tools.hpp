// SPDX-License-Identifier: MIT
//
// tools.hpp — MCP tool registry + implementations
//
// Each tool is registered with: a unique name, a description (the agent reads
// this in the prompt!), an input schema (JSON Schema 2020-12), and a handler
// that returns a CallToolResult.
//
// Design principles (from MCP best-practice guides, 2026):
//   * Outcome-oriented: one tool = one user intent (don't expose search ops
//     separately from vector/BM25/graph).
//   * Flat, typed args: top-level primitives + enums, no nested config.
//   * Service-prefixed names: every tool starts with `apollo_`.
//   * Curated count: 5-15 tools per server (we have 12).

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jsonrpc.hpp"
#include "server_state.hpp"

namespace apollo::mcp {

using nlohmann::json;

/// A MCP Tool — see modelcontextprotocol.io spec §"tools".
struct ToolDef {
    std::string name;
    std::string description;             // tells the agent when/why to use
    json input_schema;                   // JSON Schema 2020-12 object
    std::string title;                   // human-readable display name (MCP 2025-06+)
    bool read_only_hint = false;
    bool destructive_hint = false;
    bool idempotent_hint = false;
    bool open_world_hint = false;

    json to_list_item() const;
};

/// One ContentBlock in a CallToolResult.
struct ContentBlock {
    std::string type = "text";          // we only emit "text" for now
    std::string text;
    json to_json() const;
};

struct CallToolResult {
    std::vector<ContentBlock> content;
    bool is_error = false;
    json structured_content = nullptr;  // optional; populated when useful
    json to_json() const;

    static CallToolResult text(std::string s) {
        CallToolResult r;
        ContentBlock cb;
        cb.type = "text";
        cb.text = std::move(s);
        r.content.push_back(std::move(cb));
        return r;
    }
    static CallToolResult error(std::string s) {
        CallToolResult r;
        r.is_error = true;
        ContentBlock cb;
        cb.type = "text";
        cb.text = std::move(s);
        r.content.push_back(std::move(cb));
        return r;
    }
};

using ToolHandler = std::function<CallToolResult(const json& args, ServerState& state)>;

/// Registry of all MCP tools. Built once at startup, queried by the
/// dispatcher on `tools/list` and `tools/call`.
class ToolRegistry {
public:
    void register_tool(ToolDef def, ToolHandler handler);

    /// Returns the `tools/list` response payload.
    json list() const;

    /// Invokes a tool by name. Throws std::invalid_argument if not found,
    /// returns CallToolResult with `is_error=true` if the tool itself fails.
    CallToolResult call(const std::string& name,
                       const json& args,
                       ServerState& state) const;

    size_t size() const { return tools_.size(); }

private:
    struct Entry {
        ToolDef def;
        ToolHandler handler;
    };
    std::vector<Entry> tools_;
};

/// Build the default registry used by apollo_mcp_server. Twelve tools, all
/// service-prefixed `apollo_*`. See `tools.cpp` for descriptions.
ToolRegistry build_default_registry();

}  // namespace apollo::mcp