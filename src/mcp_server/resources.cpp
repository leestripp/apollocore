// SPDX-License-Identifier: MIT
//
// resources.cpp — resource definitions + handlers
//
// Resources are URI-addressed. We use the `apollo://` scheme. The dispatcher
// strips the scheme before calling read().

#include "resources.hpp"

#include <stdexcept>

#include <apollocore/entity_extractor.hpp>  // for entity_type_to_string

namespace apollo::mcp {

using nlohmann::json;

json ResourceDef::to_list_item() const
{
    json item = {
        {"uri",         uri},
        {"name",        name},
        {"description", description},
        {"mimeType",    mime_type},
    };
    if (!title.empty()) item["title"] = title;
    return item;
}

json ResourceReadResult::to_json() const
{
    json arr = json::array();
    for (const auto& c : contents) {
        arr.push_back({
            {"uri",      c.first},
            {"mimeType", "application/json"},
            {"text",     c.second}
        });
    }
    return {{"contents", arr}};
}

void ResourceRegistry::register_resource(ResourceDef def, ResourceHandler handler)
{
    resources_.push_back({std::move(def), std::move(handler)});
}

json ResourceRegistry::list() const
{
    json arr = json::array();
    for (const auto& r : resources_) arr.push_back(r.def.to_list_item());
    return {{"resources", arr}};
}

std::string ResourceRegistry::strip_scheme(const std::string& uri) const
{
    static const std::string SCHEME = "apollo://";
    if (uri.rfind(SCHEME, 0) == 0) return uri.substr(SCHEME.size());
    return uri;  // pass through unchanged
}

ResourceReadResult ResourceRegistry::read(const std::string& uri,
                                          ServerState& state) const
{
    for (const auto& r : resources_) {
        if (r.def.uri == uri) {
            try {
                return r.handler(uri, state);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Resource '") + uri + "' failed: " + e.what());
            }
        }
    }
    throw std::invalid_argument("Unknown resource: " + uri);
}

// ---------------------------------------------------------------------------
// Default handlers
// ---------------------------------------------------------------------------

static ResourceReadResult res_projects(const std::string& uri, ServerState& state)
{
    auto j = state.core().list_projects();
    return {{ {uri, j.dump(2)} }};
}

static ResourceReadResult res_config(const std::string& uri, ServerState& state)
{
    // state.config_json() emits the same shape as GET /api/config so MCP
    // clients and the Web UI get identical field names.
    return {{ {uri, state.config_json()} }};
}

static ResourceReadResult res_stats(const std::string& uri, ServerState& state)
{
    // GLOBAL counts — the KB is all-inclusive (projects are tags, not
    // isolation). Plus a per-project breakdown so the UI can show the
    // user what each "deletion unit" contains.
    auto s = state.core().get_stats();
    auto breakdown = state.core().get_project_breakdown();

    nlohmann::json projects_arr = nlohmann::json::array();
    for (const auto& p : breakdown) {
        projects_arr.push_back({
            {"name",          p.name},
            {"documents",     p.document_count},
            {"chunks",        p.chunk_count},
            {"entities",      p.entity_count},
            {"relationships", p.relationship_count},
        });
    }

    nlohmann::json j = {
        {"documents",     s.document_count},
        {"chunks",        s.chunk_count},
        {"entities",      s.entity_count},
        {"relationships", s.relationship_count},
        {"projects",      projects_arr},
    };
    return {{ {uri, j.dump(2)} }};
}

static ResourceReadResult res_entities(const std::string& uri, ServerState& state)
{
    auto ents = state.core().get_entities(200);
    json arr = json::array();
    for (const auto& e : ents) {
        arr.push_back({{"name", e.name}, {"type", entity_type_to_string(e.type)}});
    }
    return {{ {uri, arr.dump(2)} }};
}

static ResourceReadResult res_graph(const std::string& uri, ServerState& state)
{
    auto ents = state.core().get_entities(500);
    auto rels = state.core().get_relationships(500);
    json j = {
        {"entities", json::array()},
        {"relationships", json::array()}
    };
    for (const auto& e : ents) {
        j["entities"].push_back({
            {"name", e.name},
            {"type", entity_type_to_string(e.type)}
        });
    }
    for (const auto& r : rels) {
        j["relationships"].push_back({
            {"source", r.source},
            {"target", r.target},
            {"type",   std::string(relation_type_to_string(r.type))}
        });
    }
    return {{ {uri, j.dump(2)} }};
}

static ResourceReadResult res_chunk(const std::string& uri, ServerState& state)
{
    // ApolloCore doesn't expose a direct get_chunk_by_id; we approximate by
    // dumping all chunks (id, content). The agent can then search them.
    // URI is "apollo://chunk/{id}".
    auto chunks = state.core().get_all_chunks();
    json arr = json::array();
    size_t limit = 50;
    for (size_t i = 0; i < chunks.size() && i < limit; ++i) {
        arr.push_back({
            {"id",      chunks[i].first},
            {"content", chunks[i].second.substr(0, 1024)}
        });
    }
    json j = {
        {"note",   "Single-chunk lookup not exposed by ApolloCore; returning a "
                   "sample. Use apollo_query_kb to find a specific chunk by content."},
        {"count",  arr.size()},
        {"chunks", arr}
    };
    return {{ {uri, j.dump(2)} }};
}

ResourceRegistry build_default_resource_registry()
{
    ResourceRegistry r;
    r.register_resource(
        {"apollo://projects", "projects", "Projects",
         "List of all projects in the current knowledge base."},
        res_projects);
    r.register_resource(
        {"apollo://config", "config", "Server configuration",
         "Current model paths and load status. Same shape as GET /api/config."},
        res_config);
    r.register_resource(
        {"apollo://stats", "stats", "Statistics",
         "Document/chunk/entity counts for the current project."},
        res_stats);
    r.register_resource(
        {"apollo://entities", "entities", "Entities",
         "All entities in the current project (up to 200)."},
        res_entities);
    r.register_resource(
        {"apollo://graph", "graph", "Knowledge graph",
         "Subgraph (entities + relationships) for the current project."},
        res_graph);
    r.register_resource(
        {"apollo://chunk/{id}", "chunk", "Chunk",
         "Read a single chunk by id. URI: apollo://chunk/<id>"},
        res_chunk);
    return r;
}

}  // namespace apollo::mcp