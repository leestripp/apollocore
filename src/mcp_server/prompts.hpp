// SPDX-License-Identifier: MIT
//
// prompts.hpp — MCP prompts (pre-built workflows the agent/user can invoke)
//
// Prompts are templates with arguments. The user explicitly invokes them
// (via `prompts/get`) — they're not auto-called.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server_state.hpp"

namespace apollo::mcp {

using nlohmann::json;

struct PromptArgument {
    std::string name;
    std::string description;
    bool required = true;
    json to_json() const;
};

struct PromptDef {
    std::string name;
    std::string title;
    std::string description;
    std::vector<PromptArgument> arguments;
    json to_list_item() const;
};

struct PromptMessage {
    std::string role;       // "user" or "assistant"
    std::string content;    // we only emit "text" content for now
    json to_json() const;
};

struct PromptGetResult {
    std::string description;
    std::vector<PromptMessage> messages;
    json to_json() const;
};

using PromptHandler = std::function<PromptGetResult(const json& args,
                                                    ServerState& state)>;

class PromptRegistry {
public:
    void register_prompt(PromptDef def, PromptHandler handler);
    json list() const;
    PromptGetResult get(const std::string& name,
                        const json& args,
                        ServerState& state) const;
    size_t size() const { return prompts_.size(); }

private:
    struct Entry { PromptDef def; PromptHandler handler; };
    std::vector<Entry> prompts_;
};

PromptRegistry build_default_prompt_registry();

}  // namespace apollo::mcp