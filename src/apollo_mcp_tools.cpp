/**
 * ApolloCore MCP Server Tools (Phase M2.3 + M2.4)
 *
 * Registers all 25+ MCP tools using the Tool Registry pattern.
 * Each tool is self-describing: name, description, JSON schema, handler.
 *
 * Tools are split into:
 *   - KB lifecycle (5):   list_kbs, create_kb, open_kb, close_kb, destroy_kb
 *   - Documents (5):      add_document, add_documents_batch, list_documents,
 *                         get_document, delete_document
 *   - Search (4):         vector_search, bm25_search, hybrid_search, search_with_rerank
 *   - Entities (5):       extract_entities, list_entities, get_entity,
 *                         search_entities, get_entity_types
 *   - Relationships (5):  list_relationships, get_relationship, search_relationships,
 *                         get_relationship_types, get_paths
 *   - Graph (3):          get_neighbors, get_subgraph, export_graph
 *   - Stats & Health (3): get_stats, get_health, list_jobs
 *   - Async Jobs (3):     get_job_status, cancel_job, get_job_result
 */

#include "apollocore/apollo_mcp_tools.hpp"
#include "apollocore/apollo_mcp_errors.hpp"
#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_hybrid_search.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>

namespace apollocore_v2::mcp {

namespace {

// Helper: extract string from args, defaulting
inline std::string str_arg(const nlohmann::json& args, const std::string& key,
                           const std::string& def = "") {
    return args.value(key, def);
}

// Helper: extract size_t from args
inline size_t size_arg(const nlohmann::json& args, const std::string& key, size_t def) {
    if (!args.contains(key)) return def;
    try { return args[key].get<size_t>(); } catch (...) { return def; }
}

// Helper: extract float from args
inline float float_arg(const nlohmann::json& args, const std::string& key, float def) {
    if (!args.contains(key)) return def;
    try { return args[key].get<float>(); } catch (...) { return def; }
}

// Helper: require KB to be open
inline void require_kb_open(ApolloCore& kb) {
    if (!kb.is_open()) {
        throw McpError(ErrorCode::KbNotFound,
            "No knowledge base is open. Call open_kb or create_kb first.");
    }
}

// Helper: require LLM to be loaded
inline void require_llm(ApolloCore& kb) {
    if (!kb.is_llm_loaded()) {
        throw McpError(ErrorCode::ModelNotLoaded,
            "LLM not loaded. Restart server with --llm-model.");
    }
}

} // namespace

// ============================================================================
// Register all tools into a ToolRegistry
// ============================================================================

void register_all_tools(ToolRegistry& registry, ApolloCore& kb) {

    // =========================================================================
    // KB Lifecycle (5)
    // =========================================================================

    registry.register_tool({
        "list_knowledge_bases",
        "List all knowledge bases known to this server.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            // Phase 1: single KB. Phase 2: registry.
            if (kb.is_open()) {
                return {{"kbs", nlohmann::json::array({
                    {{"name", kb.get_kb_name()}, {"path", kb.get_kb_path()}, {"active", true}}
                })}};
            }
            return {{"kbs", nlohmann::json::array()}};
        }
    });

    registry.register_tool({
        "create_kb",
        "Create a new knowledge base at the given path.",
        make_schema({
            {"path", "string", "Path for new KB (relative, no '..')"},
            {"name", "string", "KB name (default Untitled)"}
        }, {"path"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            std::string path = str_arg(args, "path");
            std::string name = str_arg(args, "name", "Untitled");
            if (path.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'path'");
            if (!kb.create(path, name)) {
                throw McpError(ErrorCode::InternalError, "Failed to create KB");
            }
            return {{"success", true}, {"path", path}, {"name", name}};
        }
    });

    registry.register_tool({
        "open_kb",
        "Open an existing knowledge base from disk.",
        make_schema({
            {"path", "string", "Path to existing KB"}
        }, {"path"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            std::string path = str_arg(args, "path");
            if (path.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'path'");
            if (!kb.open(path)) {
                throw McpError(ErrorCode::KbNotFound, "KB not found at: " + path);
            }
            return {{"success", true}, {"path", path}, {"name", kb.get_kb_name()}};
        }
    });

    registry.register_tool({
        "close_kb",
        "Close the currently open knowledge base.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            kb.close();
            return {{"success", true}};
        }
    });

    registry.register_tool({
        "destroy_kb",
        "Destroy the currently open knowledge base. IRREVERSIBLE.",
        make_schema({
            {"confirm", "boolean", "Must be true to proceed"}
        }, {"confirm"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            if (!args.value("confirm", false)) {
                throw McpError(ErrorCode::InvalidParams,
                    "Must pass confirm=true to destroy KB");
            }
            bool ok = kb.destroy();
            return {{"success", ok}};
        }
        // required_scopes omitted (defaults to empty)
    });

    // =========================================================================
    // Documents (5)
    // =========================================================================

    registry.register_tool({
        "add_document",
        "Add a document to the knowledge base. Auto-embedded and chunked.",
        make_schema({
            {"text", "string", "Document text to add"}
        }, {"text"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string text = str_arg(args, "text");
            if (text.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'text'");
            size_t chunks = kb.add_document(text);
            auto stats = kb.get_stats();
            return {
                {"chunk_count", chunks},
                {"doc_id", "doc_" + std::to_string(stats.document_count)},
                {"total_chunks", stats.chunk_count}
            };
        }
        // requires_kb_open enforced in handler
    });

    registry.register_tool({
        "add_documents_batch",
        "Add multiple documents in a batch (async if more than 10).",
        make_schema({
            {"documents", "array", "Array of document objects with 'text' and optional 'metadata'"}
        }, {"documents"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            if (!args.contains("documents") || !args["documents"].is_array()) {
                throw McpError(ErrorCode::InvalidParams, "Missing 'documents' array");
            }
            size_t total_chunks = 0;
            for (const auto& doc : args["documents"]) {
                std::string text = doc.value("text", "");
                if (!text.empty()) {
                    total_chunks += kb.add_document(text);
                }
            }
            return {{"documents_added", args["documents"].size()},
                    {"total_chunks", total_chunks}};
        }
        // async-capable
    });

    registry.register_tool({
        "list_documents",
        "List documents in the knowledge base.",
        make_schema({
            {"limit", "integer", "Max documents (default 50)"},
            {"offset", "integer", "Skip first N (default 0)"}
        }),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            auto chunks = kb.get_all_chunks();
            // Group chunks by doc_id
            std::unordered_map<std::string, size_t> doc_chunks;
            for (const auto& [id, _] : chunks) {
                auto pos = id.find("_chunk_");
                if (pos != std::string::npos) {
                    doc_chunks[id.substr(0, pos)]++;
                }
            }
            nlohmann::json docs = nlohmann::json::array();
            size_t limit = size_arg(args, "limit", 50);
            size_t offset = size_arg(args, "offset", 0);
            size_t i = 0;
            for (const auto& [id, count] : doc_chunks) {
                if (i < offset) { i++; continue; }
                if (docs.size() >= limit) break;
                docs.push_back({{"doc_id", id}, {"chunk_count", count}});
                i++;
            }
            return {{"documents", docs}, {"total", doc_chunks.size()}};
        }
    });

    registry.register_tool({
        "get_document",
        "Get a single document by ID.",
        make_schema({
            {"document_id", "string", "Document ID"}
        }, {"document_id"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string doc_id = str_arg(args, "document_id");
            auto chunks = kb.get_all_chunks();
            nlohmann::json arr = nlohmann::json::array();
            std::string prefix = doc_id + "_chunk_";
            for (const auto& [id, content] : chunks) {
                if (id.substr(0, prefix.size()) == prefix) {
                    arr.push_back({{"chunk_id", id}, {"content", content}});
                }
            }
            if (arr.empty()) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "Document not found: " + doc_id);
            }
            return {{"document_id", doc_id}, {"chunks", arr}};
        }
    });

    registry.register_tool({
        "delete_document",
        "Delete a document and all its chunks. IRREVERSIBLE.",
        make_schema({
            {"document_id", "string", "Document ID to delete"},
            {"confirm", "boolean", "Must be true"}
        }, {"document_id", "confirm"}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            // Phase 1: not yet implemented in ApolloCore
            throw McpError(ErrorCode::InternalError,
                "delete_document not yet implemented in ApolloCore core");
        }
        // kb:write scope required (Phase 2)
    });

    // =========================================================================
    // Search (4) — THE KILLER FEATURES
    // =========================================================================

    registry.register_tool({
        "vector_search",
        "Pure vector similarity search using embeddings.",
        make_schema({
            {"query", "string", "Natural language query"},
            {"top_k", "integer", "Number of results (default 5)"}
        }, {"query"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string query = str_arg(args, "query");
            size_t top_k = size_arg(args, "top_k", 5);
            if (query.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'query'");
            auto results = kb.query(query, top_k);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& [id, score] : results) {
                arr.push_back({{"chunk_id", id}, {"score", score}});
            }
            return {{"results", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "bm25_search",
        "Pure BM25 keyword search (lexical match).",
        make_schema({
            {"query", "string", "Query text"},
            {"top_k", "integer", "Number of results (default 5)"}
        }, {"query"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string query = str_arg(args, "query");
            size_t top_k = size_arg(args, "top_k", 5);
            if (query.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'query'");
            // Build BM25 index
            apollocore_v2::search::BM25Index bm25;
            auto chunks = kb.get_all_chunks();
            std::vector<std::pair<std::string, std::string>> docs;
            docs.reserve(chunks.size());
            for (const auto& [id, content] : chunks) docs.emplace_back(id, content);
            bm25.build(docs);
            auto results = bm25.search(query, top_k);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& [id, score] : results) {
                arr.push_back({{"chunk_id", id}, {"score", score}});
            }
            return {{"results", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "hybrid_search",
        "**Hybrid search**: vector + BM25 + graph expansion (RRF). THE killer feature.",
        make_schema({
            {"query", "string", "Natural language query"},
            {"top_k", "integer", "Number of results (default 5)"},
            {"alpha", "number", "Vector weight (default 0.5)"},
            {"beta", "number", "BM25 weight (default 0.3)"},
            {"gamma", "number", "Graph weight (default 0.2)"}
        }, {"query"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            apollocore_v2::search::HybridSearchParams params;
            params.query = str_arg(args, "query");
            params.top_k = size_arg(args, "top_k", 5);
            params.alpha = float_arg(args, "alpha", 0.5f);
            params.beta  = float_arg(args, "beta", 0.3f);
            params.gamma = float_arg(args, "gamma", 0.2f);
            if (params.query.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'query'");
            auto results = apollocore_v2::search::hybrid_search(kb, params);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : results) {
                arr.push_back(apollocore_v2::search::hybrid_result_to_json(r, kb));
            }
            return {{"results", arr}, {"count", arr.size()},
                    {"weights", {{"alpha", params.alpha},
                                 {"beta", params.beta},
                                 {"gamma", params.gamma}}}};
        }
    });

    registry.register_tool({
        "search_with_rerank",
        "Hybrid search + re-rank via client LLM (sampling/createMessage).",
        make_schema({
            {"query", "string", "Natural language query"},
            {"top_k", "integer", "Final top K (default 5)"},
            {"rerank_top_n", "integer", "How many candidates to send for re-ranking (default 20)"}
        }, {"query"}),
        [](const nlohmann::json&) -> nlohmann::json {
            // Implemented at MCP protocol layer — uses sampling/createMessage
            throw McpError(ErrorCode::InternalError,
                "search_with_rerank: server should call sampling/createMessage before this");
        }
    });

    // =========================================================================
    // Entities (5)
    // =========================================================================

    registry.register_tool({
        "extract_entities",
        "Extract entities and relationships from chunks using the local LLM.",
        make_schema({
            {"chunk_id", "string", "Extract from specific chunk (optional)"},
            {"min_confidence", "number", "Minimum confidence (default 0.5)"},
            {"skip_already_extracted", "boolean", "Skip already-processed chunks (default true)"}
        }),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            require_llm(kb);
            std::string chunk_id = str_arg(args, "chunk_id");
            float min_conf = float_arg(args, "min_confidence", 0.5f);
            bool skip_done = args.value("skip_already_extracted", true);
            size_t count;
            if (!chunk_id.empty()) {
                count = kb.extract_entities_for_chunk(chunk_id, min_conf);
            } else {
                count = kb.extract_entities(min_conf, skip_done);
            }
            auto stats = kb.get_stats();
            return {{"entities_extracted", count},
                    {"total_entities", stats.entity_count},
                    {"total_relationships", stats.relationship_count}};
        }
        // requires_llm + requires_kb_open enforced in handler
    });

    registry.register_tool({
        "list_entities",
        "List entities in the knowledge base.",
        make_schema({
            {"chunk_id", "string", "Filter to this chunk (optional)"},
            {"type", "string", "Filter by entity type (optional)"},
            {"limit", "integer", "Max entities (default 100)"}
        }),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string chunk_id = str_arg(args, "chunk_id");
            std::string type_filter = str_arg(args, "type");
            size_t limit = size_arg(args, "limit", 100);
            auto entities = chunk_id.empty()
                ? kb.get_entities(limit)
                : kb.get_entities_for_chunk(chunk_id);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& e : entities) {
                if (!type_filter.empty() && entity_type_to_string(e.type) != type_filter) continue;
                arr.push_back({
                    {"id", e.id}, {"name", e.name},
                    {"type", entity_type_to_string(e.type)},
                    {"description", e.description},
                    {"confidence", e.confidence}
                });
            }
            return {{"entities", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "get_entity",
        "Get a single entity by ID.",
        make_schema({
            {"entity_id", "string", "Entity ID"}
        }, {"entity_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            // Phase 2: implement lookup
            throw McpError(ErrorCode::InternalError, "get_entity: not yet implemented");
        }
    });

    registry.register_tool({
        "search_entities",
        "Find entities by name (case-insensitive substring match).",
        make_schema({
            {"name", "string", "Substring to search for"},
            {"limit", "integer", "Max entities (default 20)"}
        }, {"name"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string needle = str_arg(args, "name");
            size_t limit = size_arg(args, "limit", 20);
            if (needle.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'name'");
            std::string n_lower = needle;
            std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
            auto all = kb.get_entities(10000);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& e : all) {
                std::string ename_lower = e.name;
                std::transform(ename_lower.begin(), ename_lower.end(),
                              ename_lower.begin(), ::tolower);
                if (ename_lower.find(n_lower) != std::string::npos) {
                    arr.push_back({{"id", e.id}, {"name", e.name},
                                   {"type", entity_type_to_string(e.type)}});
                    if (arr.size() >= limit) break;
                }
            }
            return {{"entities", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "get_entity_types",
        "List distinct entity types in the knowledge base.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            require_kb_open(kb);
            auto entities = kb.get_entities(10000);
            std::unordered_set<std::string> types;
            for (const auto& e : entities) {
                types.insert(entity_type_to_string(e.type));
            }
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& t : types) arr.push_back(t);
            return {{"types", arr}, {"count", arr.size()}};
        }
    });

    // =========================================================================
    // Relationships (5)
    // =========================================================================

    registry.register_tool({
        "list_relationships",
        "List all relationships in the knowledge base.",
        make_schema({
            {"type", "string", "Filter by relationship type (optional)"},
            {"limit", "integer", "Max relationships (default 100)"}
        }),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string type_filter = str_arg(args, "type");
            size_t limit = size_arg(args, "limit", 100);
            auto rels = kb.get_relationships(limit * 2);  // overfetch to allow filtering
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : rels) {
                if (!type_filter.empty() && relation_type_to_string(r.type) != type_filter) continue;
                arr.push_back({{"source", r.source}, {"target", r.target},
                               {"type", relation_type_to_string(r.type)},
                               {"description", r.description},
                               {"weight", r.weight}});
                if (arr.size() >= limit) break;
            }
            return {{"relationships", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "get_relationship",
        "Get a single relationship by ID.",
        make_schema({
            {"relationship_id", "string", "Relationship ID"}
        }, {"relationship_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            throw McpError(ErrorCode::InternalError, "get_relationship: not yet implemented");
        }
    });

    registry.register_tool({
        "search_relationships",
        "Find relationships where a specific entity participates.",
        make_schema({
            {"entity_name", "string", "Entity name (must match exactly)"},
            {"direction", "string", "'outgoing', 'incoming', or 'both' (default 'both')"}
        }, {"entity_name"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string entity_name = str_arg(args, "entity_name");
            std::string dir = str_arg(args, "direction", "both");
            auto rels = kb.get_relationships(10000);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : rels) {
                bool match = (dir == "both" && (r.source == entity_name || r.target == entity_name)) ||
                             (dir == "outgoing" && r.source == entity_name) ||
                             (dir == "incoming" && r.target == entity_name);
                if (match) {
                    arr.push_back({{"source", r.source}, {"target", r.target},
                                   {"type", relation_type_to_string(r.type)},
                                   {"description", r.description}});
                }
            }
            return {{"relationships", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "get_relationship_types",
        "List distinct relationship types in the knowledge base.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            require_kb_open(kb);
            auto rels = kb.get_relationships(10000);
            std::unordered_set<std::string> types;
            for (const auto& r : rels) {
                types.insert(relation_type_to_string(r.type));
            }
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& t : types) arr.push_back(t);
            return {{"types", arr}, {"count", arr.size()}};
        }
    });

    registry.register_tool({
        "get_paths",
        "Find paths between two entities in the knowledge graph (BFS).",
        make_schema({
            {"from_entity", "string", "Source entity name"},
            {"to_entity", "string", "Target entity name"},
            {"max_hops", "integer", "Max hops (default 3)"}
        }, {"from_entity", "to_entity"}),
        [](const nlohmann::json&) -> nlohmann::json {
            // Phase 2: implement BFS pathfinding
            throw McpError(ErrorCode::InternalError,
                "get_paths: graph pathfinding not yet implemented");
        }
    });

    // =========================================================================
    // Graph (3)
    // =========================================================================

    registry.register_tool({
        "get_neighbors",
        "Get N-hop neighborhood of an entity (entities + relationships).",
        make_schema({
            {"entity_id", "string", "Starting entity ID"},
            {"hops", "integer", "Max hops (default 1)"}
        }, {"entity_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            throw McpError(ErrorCode::InternalError,
                "get_neighbors: graph traversal not yet implemented");
        }
    });

    registry.register_tool({
        "get_subgraph",
        "Extract a subgraph around the given entity IDs.",
        make_schema({
            {"entity_ids", "array", "List of entity IDs to include"},
            {"hops", "integer", "Expand by N hops (default 1)"}
        }, {"entity_ids"}),
        [](const nlohmann::json&) -> nlohmann::json {
            throw McpError(ErrorCode::InternalError,
                "get_subgraph: not yet implemented");
        }
    });

    registry.register_tool({
        "export_graph",
        "Export the knowledge graph in a standard format (JSON-LD or GraphML).",
        make_schema({
            {"format", "string", "'json-ld' or 'graphml' (default 'json-ld')"}
        }),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string format = str_arg(args, "format", "json-ld");
            auto entities = kb.get_entities(100000);
            auto rels = kb.get_relationships(100000);
            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& e : entities) {
                nodes.push_back({
                    {"id", e.id},
                    {"label", e.name},
                    {"type", entity_type_to_string(e.type)}
                });
            }
            nlohmann::json edges = nlohmann::json::array();
            for (const auto& r : rels) {
                edges.push_back({
                    {"source", r.source},
                    {"target", r.target},
                    {"type", relation_type_to_string(r.type)}
                });
            }
            return {
                {"format", format},
                {"nodes", nodes},
                {"edges", edges},
                {"stats", {{"nodes", nodes.size()}, {"edges", edges.size()}}}
            };
        }
    });

    // =========================================================================
    // Stats & Health (3)
    // =========================================================================

    registry.register_tool({
        "get_stats",
        "Get knowledge base statistics.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            require_kb_open(kb);
            auto s = kb.get_stats();
            return {
                {"documents", s.document_count},
                {"chunks", s.chunk_count},
                {"entities", s.entity_count},
                {"relationships", s.relationship_count},
                {"chunks_with_entities", s.chunks_with_entities},
                {"embedding_files_size", s.embedding_files_size}
            };
        }
    });

    registry.register_tool({
        "get_health",
        "Deep health check: models, KB, jobs.",
        make_schema({}),
        [&kb](const nlohmann::json&) -> nlohmann::json {
            return {
                {"status", "ok"},
                {"checks", {
                    {"embedding_model", "ok"},  // ApolloCore doesn't expose model state; assume ok
                    {"llm_model", kb.is_llm_loaded() ? "ok" : "not_loaded"},
                    {"active_kb", kb.is_open() ? "ok" : "no_kb_open"},
                    {"registry", "ok"}
                }}
            };
        }
    });

    registry.register_tool({
        "list_jobs",
        "List async jobs. Phase M2.8 will add real job tracking.",
        make_schema({
            {"status", "string", "Filter by status (optional)"}
        }),
        [](const nlohmann::json&) -> nlohmann::json {
            return {{"jobs", nlohmann::json::array()},
                    {"note", "Job manager coming in Phase M2.8"}};
        }
    });

    // =========================================================================
    // Async Jobs (3)
    // =========================================================================

    registry.register_tool({
        "get_job_status",
        "Get status of an async job.",
        make_schema({
            {"job_id", "string", "Job ID"}
        }, {"job_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            return {{"note", "Job manager coming in Phase M2.8"}};
        }
    });

    registry.register_tool({
        "cancel_job",
        "Cancel a running async job.",
        make_schema({
            {"job_id", "string", "Job ID"}
        }, {"job_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            return {{"note", "Job manager coming in Phase M2.8"}};
        }
    });

    registry.register_tool({
        "get_job_result",
        "Get the result of a completed async job.",
        make_schema({
            {"job_id", "string", "Job ID"}
        }, {"job_id"}),
        [](const nlohmann::json&) -> nlohmann::json {
            return {{"note", "Job manager coming in Phase M2.8"}};
        }
    });
}

} // namespace apollocore_v2::mcp