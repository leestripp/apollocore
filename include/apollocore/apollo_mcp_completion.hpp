/**
 * ApolloCore MCP Completion Provider (Phase M2.B - Completion)
 *
 * Implements the MCP `completion/complete` method for argument
 * autocompletion. When a user starts typing an entity name,
 * document ID, or topic, the server can suggest completions
 * from the actual KB contents.
 *
 * Usage:
 *   client → {"method": "completion/complete",
 *             "params": {"ref": {"type": "ref/tool", "name": "get_neighbors"},
 *                        "argument": {"name": "entity_id", "value": "ein"},
 *                        "context": {"arguments": {}}}}
 *
 *   server ← {"completion": {"values": ["einstein", "einsteinium", ...],
 *                            "total": 5, "hasMore": false}}
 */

#pragma once

#include "apollocore/apollo_persistence.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace apollocore_v2::mcp {

/**
 * Provide completion suggestions for an argument value.
 *
 * @param kb ApolloCore instance with KB data
 * @param tool_names List of all available tool names (for tool name completion)
 * @param prompt_names List of all available prompt names (for prompt name completion)
 * @param ref_type "ref/tool" or "ref/prompt"
 * @param ref_name Name of the tool or prompt
 * @param arg_name Name of the argument being completed
 * @param partial_value What the user has typed so far (may be empty)
 * @param context_args Already-supplied arguments (for context-aware completion)
 * @param max_results Max suggestions to return (default 20)
 * @return JSON object: {"completion": {"values": [...], "total": N, "hasMore": bool}}
 */
nlohmann::json provide_completion(
    ApolloCore& kb,
    const std::vector<std::string>& tool_names,
    const std::vector<std::string>& prompt_names,
    const std::string& ref_type,
    const std::string& ref_name,
    const std::string& arg_name,
    const std::string& partial_value,
    const nlohmann::json& context_args,
    size_t max_results = 20);

/**
 * Helper: case-insensitive substring match
 */
inline bool ci_contains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    std::string h = to_lower(haystack);
    std::string n = to_lower(needle);
    return h.find(n) != std::string::npos;
}

/**
 * Helper: filter a list of strings by partial value (case-insensitive substring)
 * Returns up to max_results matches.
 */
inline std::vector<std::string> filter_by_prefix(
    const std::vector<std::string>& candidates,
    const std::string& partial,
    size_t max_results)
{
    std::vector<std::string> matches;
    for (const auto& c : candidates) {
        if (ci_contains(c, partial)) {
            matches.push_back(c);
            if (matches.size() >= max_results) break;
        }
    }
    return matches;
}

} // namespace apollocore_v2::mcp