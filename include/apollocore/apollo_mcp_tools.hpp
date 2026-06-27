/**
 * ApolloCore MCP Tool Registry (Phase M2.3)
 *
 * Foundation for M2: every MCP tool is self-describing:
 *   - name
 *   - description (natural language, shown to AI)
 *   - input_schema (JSON Schema)
 *   - handler (function that takes args JSON, returns result JSON)
 *
 * Adding a new tool is one register_tool() call. No central switch.
 */

#pragma once

#include "apollocore/apollo_mcp_errors.hpp"

#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "apollocore/apollo_mcp_errors.hpp"

namespace apollocore_v2 {

// Forward declaration — ApolloCore is defined in apollo_persistence.hpp
class ApolloCore;

namespace mcp {

class ToolRegistry {
public:
    /**
     * Tool definition: everything needed to advertise + invoke a tool.
     */
    struct ToolDefinition {
        std::string name;
        std::string description;
        nlohmann::json input_schema;  // JSON Schema draft 2020-12
        std::function<nlohmann::json(const nlohmann::json& args)> handler;

        // Optional metadata
        std::vector<std::string> required_scopes;  // auth scopes (Phase M2.19)
        bool requires_llm = false;
        bool requires_kb_open = false;
        bool is_async_capable = false;
    };

    ToolRegistry() = default;

    /**
     * Register a tool. Throws if a tool with the same name already exists.
     */
    void register_tool(ToolDefinition def) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tools_.count(def.name)) {
            throw std::runtime_error("Tool already registered: " + def.name);
        }
        tools_[def.name] = std::move(def);
    }

    /**
     * Check if a tool is registered.
     */
    bool has_tool(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tools_.count(name) > 0;
    }

    /**
     * List all tools in MCP format:
     *   {"tools": [{"name", "description", "inputSchema"}, ...]}
     */
    nlohmann::json list_tools() const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [name, def] : tools_) {
            arr.push_back({
                {"name", def.name},
                {"description", def.description},
                {"inputSchema", def.input_schema}
            });
        }
        return {{"tools", arr}};
    }

    /**
     * Call a tool by name. Throws McpError on failure.
     */
    nlohmann::json call(const std::string& name, const nlohmann::json& args) {
        std::function<nlohmann::json(const nlohmann::json&)> handler;
        bool is_async_capable = false;
        bool requires_llm = false;
        bool requires_kb_open = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = tools_.find(name);
            if (it == tools_.end()) {
                throw McpError(ErrorCode::MethodNotFound, "Tool not found: " + name);
            }
            handler = it->second.handler;
            is_async_capable = it->second.is_async_capable;
            requires_llm = it->second.requires_llm;
            requires_kb_open = it->second.requires_kb_open;
        }

        // Pre-flight checks (the handler will repeat these but that's cheap)
        // These are hints — the handler should still validate.

        try {
            return handler(args);
        } catch (const McpError&) {
            throw;  // re-throw as-is
        } catch (const std::exception& e) {
            throw McpError(ErrorCode::InternalError,
                std::string("Tool '") + name + "' failed: " + e.what());
        }
    }

    /**
     * Number of registered tools.
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tools_.size();
    }

    /// List names of all registered tools (for completion).
    std::vector<std::string> names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        result.reserve(tools_.size());
        for (const auto& [name, _] : tools_) {
            result.push_back(name);
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ToolDefinition> tools_;
};

/**
 * Helper: build a JSON Schema for a tool with simple typed properties.
 *
 *   make_schema({
 *     {"query", "string", "Natural language query"},
 *     {"top_k", "integer", "Max results (default 5)"}
 *   }, {"query"})  // required list
 */
inline nlohmann::json make_schema(
    const std::vector<std::tuple<std::string, std::string, std::string>>& properties,
    const std::vector<std::string>& required = {})
{
    nlohmann::json props = nlohmann::json::object();
    for (const auto& [name, type, desc] : properties) {
        props[name] = {{"type", type}, {"description", desc}};
    }
    return {
        {"type", "object"},
        {"properties", props},
        {"required", required}
    };
}

/**
 * Register all 25+ ApolloCore MCP tools into a registry.
 * Defined in apollo_mcp_tools.cpp.
 *
 * @param registry The registry to populate
 * @param kb       ApolloCore instance (tools will use it directly)
 */
void register_all_tools(ToolRegistry& registry, ApolloCore& kb);

} // namespace mcp
} // namespace apollocore_v2