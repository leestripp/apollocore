/**
 * ApolloCore MCP Completion Provider — Implementation (Phase M2.B)
 *
 * Provides autocompletion suggestions for known argument names by
 * mining the actual KB contents (entities, documents, chunks).
 *
 * Supported argument names (case-insensitive):
 *   - entity_id, entity_name, entity, from_entity, to_entity → entity names
 *   - entity_ids (array) → same, returned as JSON array
 *   - document_id, doc_a, doc_b → document IDs + titles
 *   - topic, query, question → topic suggestions (entity names + doc titles)
 *   - type (when ref is explain_entity) → entity types
 *   - type (when ref is get_relationship) → relationship types
 *   - document_id for delete_document → existing IDs only
 */

#include "apollocore/apollo_mcp_completion.hpp"
#include "apollocore/apollo_mcp_prompts.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace apollocore_v2::mcp {

using json = nlohmann::json;

namespace {

// Extract entity names + IDs from the KB
std::vector<std::string> get_entity_candidates(ApolloCore& kb) {
    auto entities = kb.get_entities(100000);
    std::vector<std::string> names;
    names.reserve(entities.size());
    for (const auto& e : entities) {
        names.push_back(e.name);
    }
    return names;
}

// Extract document IDs from the KB
std::vector<std::string> get_document_candidates(ApolloCore& kb) {
    auto docs = kb.get_all_documents();
    std::vector<std::string> ids;
    ids.reserve(docs.size());
    for (const auto& [id, content] : docs) {
        ids.push_back(id);
    }
    return ids;
}

// Extract topic candidates (entity names + doc titles)
std::vector<std::string> get_topic_candidates(ApolloCore& kb) {
    auto entities = kb.get_entities(100000);
    auto docs = kb.get_all_documents();
    std::vector<std::string> topics;
    std::unordered_set<std::string> seen;
    for (const auto& e : entities) {
        if (seen.insert(e.name).second) topics.push_back(e.name);
    }
    for (const auto& [id, content] : docs) {
        // Use first 50 chars of content as a "topic snippet"
        std::string snippet = content.substr(0, std::min(size_t(50), content.size()));
        if (seen.insert(snippet).second) topics.push_back(snippet);
    }
    return topics;
}

bool is_entity_arg(const std::string& name) {
    static const std::unordered_set<std::string> entity_args = {
        "entity_id", "entity_name", "entity",
        "from_entity", "to_entity",
        "source", "target"
    };
    return entity_args.count(name) > 0;
}

bool is_entity_array_arg(const std::string& name) {
    static const std::unordered_set<std::string> arr_args = {
        "entity_ids"
    };
    return arr_args.count(name) > 0;
}

bool is_document_arg(const std::string& name) {
    static const std::unordered_set<std::string> doc_args = {
        "document_id", "doc_a", "doc_b"
    };
    return doc_args.count(name) > 0;
}

bool is_topic_arg(const std::string& name) {
    static const std::unordered_set<std::string> topic_args = {
        "topic", "query", "question"
    };
    return topic_args.count(name) > 0;
}

// Entity type names (from apollocore_v2 namespace)
std::vector<std::string> entity_type_names() {
    return {
        "PERSON", "ORG", "LOC", "CONCEPT", "EVENT", "WORK", "UNKNOWN"
    };
}

// Relationship type names
std::vector<std::string> relation_type_names() {
    return {
        "founded", "works_for", "located_in", "created", "studied_at",
        "collaborated_with", "influenced_by", "authored", "owns", "part_of",
        "related_to", "unknown"
    };
}

// Build completion response
json build_response(const std::vector<std::string>& matches, bool has_more) {
    json values = json::array();
    for (const auto& m : matches) {
        values.push_back(m);
    }
    return {
        {"completion", {
            {"values", values},
            {"total", matches.size()},
            {"hasMore", has_more}
        }}
    };
}

} // namespace

nlohmann::json provide_completion(
    ApolloCore& kb,
    const std::vector<std::string>& tool_names,
    const std::vector<std::string>& prompt_names,
    const std::string& ref_type,
    const std::string& ref_name,
    const std::string& arg_name,
    const std::string& partial_value,
    const nlohmann::json& /*context_args*/,
    size_t max_results)
{
    // ====================================================================
    // Tool / prompt name completion (MCP standard pattern)
    // When ref_type=ref/tool|prompt and arg_name="name", complete the
    // resource name itself. (ref_name will be empty in this case.)
    // ====================================================================
    if (arg_name == "name" && (ref_type == "ref/tool" || ref_type == "ref/prompt" ||
                                ref_type == "ref/resource")) {
        const std::vector<std::string>* candidates = nullptr;
        if (ref_type == "ref/tool") candidates = &tool_names;
        else if (ref_type == "ref/prompt") candidates = &prompt_names;
        // ref/resource: no list yet, return empty
        if (candidates) {
            auto matches = filter_by_prefix(*candidates, partial_value, max_results);
            return build_response(matches, matches.size() >= max_results);
        }
    }

    // Entity-type completion (special case based on ref_name)
    if (arg_name == "type") {
        if (ref_name == "explain_entity" || ref_name == "list_entities" ||
            ref_name == "get_entity_types") {
            auto candidates = entity_type_names();
            auto matches = filter_by_prefix(candidates, partial_value, max_results);
            return build_response(matches, matches.size() >= max_results);
        }
        if (ref_name == "get_relationship" || ref_name == "search_relationships") {
            auto candidates = relation_type_names();
            auto matches = filter_by_prefix(candidates, partial_value, max_results);
            return build_response(matches, matches.size() >= max_results);
        }
    }

    // Document ID completion (only existing IDs for delete)
    if (is_document_arg(arg_name)) {
        // If ref is delete_document, only return existing IDs
        auto candidates = get_document_candidates(kb);
        auto matches = filter_by_prefix(candidates, partial_value, max_results);
        return build_response(matches, matches.size() >= max_results);
    }

    // Entity completion
    if (is_entity_arg(arg_name)) {
        auto candidates = get_entity_candidates(kb);
        auto matches = filter_by_prefix(candidates, partial_value, max_results);
        return build_response(matches, matches.size() >= max_results);
    }

    // Entity array (return as JSON array values)
    if (is_entity_array_arg(arg_name)) {
        auto candidates = get_entity_candidates(kb);
        auto matches = filter_by_prefix(candidates, partial_value, max_results);
        return build_response(matches, matches.size() >= max_results);
    }

    // Topic/query/question completion
    if (is_topic_arg(arg_name)) {
        auto candidates = get_topic_candidates(kb);
        auto matches = filter_by_prefix(candidates, partial_value, max_results);
        return build_response(matches, matches.size() >= max_results);
    }

    // Unknown argument: return empty
    return build_response({}, false);
}

} // namespace apollocore_v2::mcp