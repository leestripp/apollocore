/**
 * ApolloCore MCP Prompts Registry (Phase M2.B - Prompts)
 *
 * Prompts are reusable, parameterized message templates that AI clients
 * can invoke. They differ from tools: tools are ACTIONS, prompts are
 * CONVERSATIONS.
 *
 * Each prompt is self-describing:
 *   - name
 *   - description (natural language, shown to AI)
 *   - arguments (typed params with required flags)
 *   - renderer (function that produces {messages: [{role, content}]})
 *
 * Adding a new prompt is one register_prompt() call.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "apollocore/apollo_mcp_errors.hpp"
#include "apollocore/apollo_persistence.hpp"

namespace apollocore_v2::mcp {

/**
 * One argument definition for a prompt.
 * name, description, required flag.
 */
struct PromptArgument {
    std::string name;
    std::string description;
    bool required = false;
};

/**
 * One message in a rendered prompt conversation.
 * role: "user" | "assistant" | "system"
 * content: text content
 */
struct PromptMessage {
    std::string role;
    std::string content;
};

/**
 * Definition of one prompt template.
 */
struct PromptDefinition {
    std::string name;
    std::string description;
    std::vector<PromptArgument> arguments;
    // Renderer: takes (args JSON) -> vector<PromptMessage>
    std::function<std::vector<PromptMessage>(const nlohmann::json&)> render;
};

/**
 * Registry of all available prompts.
 * Thread-safe.
 */
class PromptRegistry {
public:
    void register_prompt(PromptDefinition def) {
        std::lock_guard<std::mutex> lock(mutex_);
        prompts_[def.name] = std::move(def);
    }

    /// List all prompts as MCP JSON-RPC response payload.
    nlohmann::json list_prompts() {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [name, def] : prompts_) {
            nlohmann::json args = nlohmann::json::array();
            for (const auto& a : def.arguments) {
                args.push_back({
                    {"name", a.name},
                    {"description", a.description},
                    {"required", a.required}
                });
            }
            arr.push_back({
                {"name", def.name},
                {"description", def.description},
                {"arguments", args}
            });
        }
        return {{"prompts", arr}};
    }

    /// Render a prompt by name with given args. Throws McpError on failure.
    nlohmann::json get_prompt(const std::string& name, const nlohmann::json& args) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = prompts_.find(name);
        if (it == prompts_.end()) {
            throw McpError(ErrorCode::ResourceNotFound,
                "Prompt not found: " + name);
        }

        // Validate required args
        for (const auto& a : it->second.arguments) {
            if (a.required && (!args.contains(a.name) ||
                               (args[a.name].is_string() && args[a.name].get<std::string>().empty()))) {
                throw McpError(ErrorCode::InvalidParams,
                    "Missing required argument: " + a.name);
            }
        }

        // Render
        std::vector<PromptMessage> msgs;
        try {
            msgs = it->second.render(args);
        } catch (const std::exception& e) {
            throw McpError(ErrorCode::InternalError,
                "Prompt render failed: " + std::string(e.what()));
        }

        // Format as MCP response
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& m : msgs) {
            messages.push_back({
                {"role", m.role},
                {"content", {{"type", "text"}, {"text", m.content}}}
            });
        }
        return {
            {"name", name},
            {"description", it->second.description},
            {"messages", messages}
        };
    }

    size_t size() const { return prompts_.size(); }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PromptDefinition> prompts_;
};

/**
 * Helper: get a string arg from JSON with default.
 */
inline std::string prompt_str_arg(const nlohmann::json& args,
                                  const std::string& key,
                                  const std::string& def = "") {
    if (args.contains(key) && args[key].is_string()) {
        return args[key].get<std::string>();
    }
    return def;
}

/**
 * Helper: get an integer arg from JSON with default.
 */
inline int prompt_int_arg(const nlohmann::json& args,
                          const std::string& key,
                          int def = 0) {
    if (args.contains(key) && args[key].is_number_integer()) {
        return args[key].get<int>();
    }
    return def;
}

/**
 * Register all built-in prompts into a registry.
 * Defined in apollo_mcp_prompts.cpp.
 */
void register_all_prompts(PromptRegistry& registry, ApolloCore& kb);

} // namespace apollocore_v2::mcp