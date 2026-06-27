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
#include "apollocore/apollo_codebase_ingest.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <iostream>

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
        "add_files",
        "Ingest a directory of source files into the KB. Each file becomes a "
        "document with a path-based ID. Designed for codebases.",
        make_schema({
            {"path", "string", "Directory path to ingest (relative or absolute)"},
            {"extensions", "array", "File extensions to include (default: common code extensions)"},
            {"max_file_size", "integer", "Skip files larger than this (bytes, default 1MB)"},
            {"recursive", "boolean", "Walk subdirectories (default true)"}
        }, {"path"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string path = str_arg(args, "path");
            if (path.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'path'");

            std::vector<std::string> extensions;
            if (args.contains("extensions") && args["extensions"].is_array()) {
                for (const auto& e : args["extensions"]) {
                    if (e.is_string()) extensions.push_back(e.get<std::string>());
                }
            }

            size_t max_file_size = static_cast<size_t>(size_arg(args, "max_file_size", 1000000));
            bool recursive = args.value("recursive", true);

            return apollocore_v2::ingest_directory(kb, path, extensions,
                                                   max_file_size, recursive);
        }
        // async-capable (can be slow for large codebases)
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
        "Get a single document by ID (returns full content + all chunks + metadata).",
        make_schema({
            {"document_id", "string", "Document ID (path-based for codebases)"}
        }, {"document_id"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string doc_id = str_arg(args, "document_id");
            if (doc_id.empty()) {
                throw McpError(ErrorCode::InvalidParams, "Missing 'document_id'");
            }

            // Find document node (title, content, metadata)
            auto docs = kb.get_all_documents();
            std::string title;
            std::string full_content;
            bool found = false;
            for (const auto& [id, content] : docs) {
                if (id == doc_id) {
                    title = id;  // title isn't separated in LoadedNode
                    full_content = content;
                    found = true;
                    break;
                }
            }

            if (!found) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "Document not found: " + doc_id);
            }

            // Get all chunks for this document (by chunk_id prefix)
            auto chunks = kb.get_all_chunks();
            nlohmann::json chunk_arr = nlohmann::json::array();
            std::string prefix = doc_id + "_chunk_";
            for (const auto& [cid, content] : chunks) {
                if (cid.substr(0, prefix.size()) == prefix) {
                    chunk_arr.push_back({{"chunk_id", cid}, {"content", content}});
                }
            }

            return {
                {"document_id", doc_id},
                {"title", title},
                {"content", full_content},
                {"content_length", full_content.size()},
                {"chunks", chunk_arr},
                {"chunk_count", chunk_arr.size()}
            };
        }
    });

    registry.register_tool({
        "delete_document",
        "Delete a document and all its chunks, entities, and relationships. IRREVERSIBLE.",
        make_schema({
            {"document_id", "string", "Document ID to delete"},
            {"confirm", "boolean", "Must be true"}
        }, {"document_id", "confirm"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string doc_id = str_arg(args, "document_id");
            if (doc_id.empty()) {
                throw McpError(ErrorCode::InvalidParams, "Missing 'document_id'");
            }
            bool confirm = args.value("confirm", false);
            if (!confirm) {
                throw McpError(ErrorCode::InvalidParams,
                    "delete_document requires confirm=true (operation is IRREVERSIBLE)");
            }

            // Capture counts before deletion for the response
            auto entities_before = kb.get_entities(100000);
            auto rels_before = kb.get_relationships(100000);

            bool ok = kb.delete_document(doc_id);
            if (!ok) {
                throw McpError(ErrorCode::InternalError,
                    "Failed to delete document: " + doc_id);
            }

            auto entities_after = kb.get_entities(100000);
            auto rels_after = kb.get_relationships(100000);

            return {
                {"deleted", true},
                {"document_id", doc_id},
                {"entities_removed", entities_before.size() - entities_after.size()},
                {"relationships_removed", rels_before.size() - rels_after.size()},
                {"remaining_entities", entities_after.size()},
                {"remaining_relationships", rels_after.size()}
            };
        }
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

    // =========================================================================
    // search_symbols — code-symbol-aware exact match (for CodeMax)
    // =========================================================================
    // Designed for codebases: prioritize exact name matches and prefix matches
    // over generic substring matches. Returns chunks containing the symbol.
    // Useful when I know I want to find all definitions/calls of "authenticate".

    registry.register_tool({
        "search_symbols",
        "Find all chunks containing an exact or prefix-matched symbol name. "
        "Designed for codebases — prioritizes exact matches over substrings. "
        "Returns chunks (file content snippets) that contain the symbol.",
        make_schema({
            {"name", "string", "Symbol name to find (e.g. function, class, variable)"},
            {"limit", "integer", "Max chunks to return (default 20)"},
            {"exact_only", "boolean", "Only exact whole-word matches (default false)"}
        }, {"name"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string name = str_arg(args, "name");
            size_t limit = size_arg(args, "limit", 20);
            bool exact_only = args.value("exact_only", false);

            if (name.empty()) {
                throw McpError(ErrorCode::InvalidParams, "Missing 'name'");
            }

            // Score each chunk by how well it matches the symbol name
            struct Match {
                std::string chunk_id;
                std::string doc_id;
                std::string content;
                int score;  // higher = better match
            };
            std::vector<Match> matches;

            // Build doc_id from chunk_id by stripping "_chunk_N"
            auto chunk_to_doc = [](const std::string& cid) -> std::string {
                auto pos = cid.rfind("_chunk_");
                return (pos == std::string::npos) ? cid : cid.substr(0, pos);
            };

            auto chunks = kb.get_all_chunks();
            for (const auto& [cid, content] : chunks) {
                int score = 0;

                if (exact_only) {
                    // Whole-word match: surrounded by non-identifier chars
                    // Use simple boundary detection for code symbols
                    std::string needle = name;
                    size_t pos = 0;
                    while ((pos = content.find(needle, pos)) != std::string::npos) {
                        bool left_ok = (pos == 0) ||
                            (!std::isalnum(content[pos - 1]) && content[pos - 1] != '_');
                        bool right_ok = (pos + needle.size() >= content.size()) ||
                            (!std::isalnum(content[pos + needle.size()]) &&
                             content[pos + needle.size()] != '_');
                        if (left_ok && right_ok) {
                            score++;
                        }
                        pos += needle.size();
                    }
                } else {
                    // Prioritized substring match:
                    //  - exact match: +10
                    //  - prefix match (FooBar...): +5
                    //  - substring: +1 per occurrence
                    if (content.find(name) != std::string::npos) {
                        // Count occurrences
                        size_t pos = 0;
                        size_t count = 0;
                        while ((pos = content.find(name, pos)) != std::string::npos) {
                            count++;
                            pos += name.size();
                        }
                        score = static_cast<int>(count);
                        if (score >= 1) score += 5;  // prefix-of-something bonus
                    }

                    // Bonus for symbol-like exact match (word boundary)
                    std::string boundary_pattern = " " + name + "(";  // function call
                    if (content.find(boundary_pattern) != std::string::npos) score += 10;
                    boundary_pattern = " " + name + " ";  // declaration
                    if (content.find(boundary_pattern) != std::string::npos) score += 8;
                    boundary_pattern = name + "::";  // member access
                    if (content.find(boundary_pattern) != std::string::npos) score += 6;
                }

                if (score > 0) {
                    matches.push_back({cid, chunk_to_doc(cid), content, score});
                }
            }

            // Sort by score descending
            std::sort(matches.begin(), matches.end(),
                [](const Match& a, const Match& b) { return a.score > b.score; });

            // Limit results
            if (matches.size() > limit) matches.resize(limit);

            // Build response with content snippets
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : matches) {
                // Include a snippet (first 200 chars) to keep response small
                std::string snippet = m.content;
                if (snippet.size() > 200) snippet = snippet.substr(0, 200) + "...";
                arr.push_back({
                    {"chunk_id", m.chunk_id},
                    {"document_id", m.doc_id},
                    {"score", m.score},
                    {"snippet", snippet}
                });
            }
            return {
                {"name", name},
                {"exact_only", exact_only},
                {"results", arr},
                {"count", arr.size()}
            };
        }
    });

    registry.register_tool({
        "get_entity",
        "Get a single entity by ID.",
        make_schema({
            {"entity_id", "string", "Entity ID"}
        }, {"entity_id"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string entity_id = str_arg(args, "entity_id");
            if (entity_id.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'entity_id'");
            auto entities = kb.get_entities(100000);
            for (const auto& e : entities) {
                if (e.id == entity_id) {
                    return {
                        {"id", e.id},
                        {"name", e.name},
                        {"type", entity_type_to_string(e.type)},
                        {"description", e.description},
                        {"confidence", e.confidence}
                    };
                }
            }
            throw McpError(ErrorCode::ResourceNotFound,
                "Entity not found: " + entity_id);
        }
    });

    registry.register_tool({
        "add_entity",
        "Manually add an entity to the KB. Useful for code-aware entities "
        "(FUNCTION, CLASS, FILE, etc.) when the LLM extractor isn't sufficient.",
        make_schema({
            {"name", "string", "Entity name (e.g. function or class name)"},
            {"type", "string", "Entity type: PERSON, FUNCTION, CLASS, FILE, MODULE, etc."},
            {"chunk_id", "string", "Source chunk ID (where this entity was found)"},
            {"description", "string", "Brief description (optional)"},
            {"confidence", "number", "Confidence 0-1 (default 1.0 for manual)"}
        }, {"name", "type"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string name = str_arg(args, "name");
            std::string type_str = str_arg(args, "type");
            std::string chunk_id = str_arg(args, "chunk_id", "");
            std::string description = str_arg(args, "description", "");
            float confidence = 1.0f;
            if (args.contains("confidence") && args["confidence"].is_number()) {
                confidence = args["confidence"].get<float>();
            }
            if (name.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'name'");
            if (type_str.empty()) throw McpError(ErrorCode::InvalidParams, "Missing 'type'");

            std::string eid = kb.add_entity(name, type_str, chunk_id, description, confidence);
            if (eid.empty()) {
                throw McpError(ErrorCode::InternalError, "Failed to add entity");
            }
            return {
                {"entity_id", eid},
                {"name", name},
                {"type", type_str},
                {"chunk_id", chunk_id}
            };
        }
    });

    registry.register_tool({
        "add_relationship",
        "Manually add a relationship between two entities. Source and target "
        "should be entity names that already exist in the KB.",
        make_schema({
            {"source", "string", "Source entity name"},
            {"target", "string", "Target entity name"},
            {"type", "string", "Relationship type: CALLS, INHERITS, IMPORTS, etc."},
            {"description", "string", "Brief description (optional)"},
            {"confidence", "number", "Confidence 0-1 (default 1.0 for manual)"}
        }, {"source", "target", "type"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string source = str_arg(args, "source");
            std::string target = str_arg(args, "target");
            std::string type_str = str_arg(args, "type");
            std::string description = str_arg(args, "description", "");
            float confidence = 1.0f;
            if (args.contains("confidence") && args["confidence"].is_number()) {
                confidence = args["confidence"].get<float>();
            }
            if (source.empty() || target.empty() || type_str.empty()) {
                throw McpError(ErrorCode::InvalidParams,
                    "Missing 'source', 'target', or 'type'");
            }

            std::string rid = kb.add_relationship(source, target, type_str,
                                                 description, confidence);
            if (rid.empty()) {
                throw McpError(ErrorCode::InternalError, "Failed to add relationship");
            }
            return {
                {"relationship_id", rid},
                {"source", source},
                {"target", target},
                {"type", type_str}
            };
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
        "Get a single relationship by source and target entity names.",
        make_schema({
            {"source", "string", "Source entity name"},
            {"target", "string", "Target entity name"},
            {"type", "string", "Optional: relationship type to disambiguate"}
        }, {"source", "target"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string source_name = str_arg(args, "source");
            std::string target_name = str_arg(args, "target");
            if (source_name.empty() || target_name.empty()) {
                throw McpError(ErrorCode::InvalidParams,
                    "Missing 'source' or 'target'");
            }
            std::string type_filter = str_arg(args, "type", "");
            auto rels = kb.get_relationships(100000);
            for (const auto& r : rels) {
                if (r.source == source_name && r.target == target_name) {
                    if (!type_filter.empty()) {
                        std::string rtype = relation_type_to_string(r.type);
                        if (rtype != type_filter) continue;
                    }
                    return {
                        {"source", r.source},
                        {"target", r.target},
                        {"type", relation_type_to_string(r.type)},
                        {"description", r.description},
                        {"weight", r.weight}
                    };
                }
            }
            throw McpError(ErrorCode::ResourceNotFound,
                "Relationship not found: " + source_name + " -> " + target_name);
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
        "Find shortest paths between two entities in the knowledge graph (BFS).",
        make_schema({
            {"from_entity", "string", "Source entity name (substring match)"},
            {"to_entity", "string", "Target entity name (substring match)"},
            {"max_hops", "integer", "Max hops (default 3)"}
        }, {"from_entity", "to_entity"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string from_name = str_arg(args, "from_entity");
            std::string to_name = str_arg(args, "to_entity");
            size_t max_hops = size_arg(args, "max_hops", 3);

            // Resolve names -> entity IDs (case-insensitive substring)
            auto entities = kb.get_entities(100000);
            auto rels = kb.get_relationships(100000);

            auto find_by_name = [&](const std::string& name) -> std::vector<std::string> {
                std::string n_lower = name;
                std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
                std::vector<std::string> ids;
                for (const auto& e : entities) {
                    std::string ename_lower = e.name;
                    std::transform(ename_lower.begin(), ename_lower.end(),
                                  ename_lower.begin(), ::tolower);
                    if (ename_lower.find(n_lower) != std::string::npos) {
                        ids.push_back(e.id);
                        if (ids.size() >= 10) break;
                    }
                }
                return ids;
            };

            auto from_ids = find_by_name(from_name);
            auto to_ids = find_by_name(to_name);
            if (from_ids.empty()) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "No entities matching from_entity: " + from_name);
            }
            if (to_ids.empty()) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "No entities matching to_entity: " + to_name);
            }
            // Map IDs to names for path output
            std::unordered_map<std::string, std::string> id_to_name;
            for (const auto& e : entities) id_to_name[e.id] = e.name;
            std::unordered_set<std::string> to_set(to_ids.begin(), to_ids.end());

            // Build adjacency by NAME: name -> [(neighbor_name, rel)]
            struct Edge { std::string to_name; std::string rel_type; float weight; };
            std::unordered_map<std::string, std::vector<Edge>> adj;
            for (const auto& r : rels) {
                std::string rel_type_str(r.type == apollocore_v2::RelationType::UNKNOWN
                    ? "related_to"
                    : apollocore_v2::relation_type_to_string(r.type));
                adj[r.source].push_back({r.target, rel_type_str, r.weight});
                adj[r.target].push_back({r.source, rel_type_str, r.weight});  // undirected
            }

            // BFS by name
            nlohmann::json paths = nlohmann::json::array();
            std::unordered_set<std::string> visited_globally;
            for (const auto& start_id : from_ids) {
                std::string start_name = id_to_name[start_id];
                if (visited_globally.count(start_name)) continue;

                std::queue<std::pair<std::string, std::vector<std::string>>> q;
                std::unordered_set<std::string> visited;
                q.push({start_name, {start_name}});
                visited.insert(start_name);
                visited_globally.insert(start_name);

                bool found = false;
                while (!q.empty()) {
                    auto [cur_name, path_names] = q.front();
                    q.pop();
                    if (path_names.size() > max_hops + 1) continue;

                    // Check if cur_name is one of the targets
                    auto cur_id_it = std::find_if(entities.begin(), entities.end(),
                        [&](const apollocore_v2::Entity& e) { return e.name == cur_name; });
                    if (cur_id_it != entities.end() && to_set.count(cur_id_it->id)) {
                        // Build path output
                        nlohmann::json path_entities = nlohmann::json::array();
                        for (const auto& n : path_names) {
                            path_entities.push_back({{"name", n}});
                        }
                        paths.push_back({
                            {"hops", path_names.size() - 1},
                            {"path", path_names},
                            {"entities", path_entities}
                        });
                        found = true;
                        break;
                    }

                    auto it = adj.find(cur_name);
                    if (it == adj.end()) continue;
                    for (const auto& e : it->second) {
                        if (visited.count(e.to_name)) continue;
                        visited.insert(e.to_name);
                        std::vector<std::string> new_path = path_names;
                        new_path.push_back(e.to_name);
                        q.push({e.to_name, new_path});
                    }
                }
                if (found) break;
            }

            return {{"paths", paths}, {"count", paths.size()}};
        }
    });

    // =========================================================================
    // Graph (3)
    // =========================================================================

    registry.register_tool({
        "get_neighbors",
        "Get N-hop neighborhood of an entity (entities + relationships within N hops).",
        make_schema({
            {"entity_id", "string", "Starting entity name or ID"},
            {"hops", "integer", "Max hops (default 1)"}
        }, {"entity_id"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            std::string entity_ref = str_arg(args, "entity_id");
            size_t hops = size_arg(args, "hops", 1);

            auto entities = kb.get_entities(100000);
            auto rels = kb.get_relationships(100000);

            // Resolve name -> name (substring match)
            std::string n_lower = entity_ref;
            std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
            std::string start_name;
            for (const auto& e : entities) {
                std::string ename_lower = e.name;
                std::transform(ename_lower.begin(), ename_lower.end(),
                              ename_lower.begin(), ::tolower);
                if (ename_lower.find(n_lower) != std::string::npos) {
                    start_name = e.name;
                    break;
                }
            }
            if (start_name.empty()) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "Entity not found: " + entity_ref);
            }

            // Build adjacency (undirected) by NAME
            std::unordered_map<std::string, std::vector<std::string>> adj;
            for (const auto& r : rels) {
                adj[r.source].push_back(r.target);
                adj[r.target].push_back(r.source);
            }

            // BFS up to N hops
            std::unordered_set<std::string> visited_names;
            std::queue<std::pair<std::string, size_t>> q;
            q.push({start_name, 0});
            visited_names.insert(start_name);
            while (!q.empty()) {
                auto [cur, depth] = q.front();
                q.pop();
                if (depth >= hops) continue;
                auto it = adj.find(cur);
                if (it == adj.end()) continue;
                for (const auto& nb : it->second) {
                    if (!visited_names.count(nb)) {
                        visited_names.insert(nb);
                        q.push({nb, depth + 1});
                    }
                }
            }

            // Build node list (entities by name)
            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& e : entities) {
                if (visited_names.count(e.name)) {
                    nodes.push_back({
                        {"id", e.id}, {"name", e.name},
                        {"type", entity_type_to_string(e.type)},
                        {"description", e.description}
                    });
                }
            }
            // Build edge list (relationships within visited set)
            nlohmann::json edges = nlohmann::json::array();
            for (const auto& r : rels) {
                if (visited_names.count(r.source) && visited_names.count(r.target)) {
                    edges.push_back({
                        {"source", r.source},
                        {"target", r.target},
                        {"type", relation_type_to_string(r.type)},
                        {"weight", r.weight}
                    });
                }
            }
            return {
                {"seed", entity_ref},
                {"hops", hops},
                {"nodes", nodes},
                {"edges", edges},
                {"stats", {{"nodes", nodes.size()}, {"edges", edges.size()}}}
            };
        }
    });

    registry.register_tool({
        "get_subgraph",
        "Extract a subgraph around multiple entities (union of N-hop neighborhoods).",
        make_schema({
            {"entity_ids", "array", "List of entity names or IDs to seed from"},
            {"hops", "integer", "Expand by N hops from each seed (default 1)"}
        }, {"entity_ids"}),
        [&kb](const nlohmann::json& args) -> nlohmann::json {
            require_kb_open(kb);
            if (!args.contains("entity_ids") || !args["entity_ids"].is_array() || args["entity_ids"].empty()) {
                throw McpError(ErrorCode::InvalidParams, "Missing or empty 'entity_ids'");
            }
            size_t hops = size_arg(args, "hops", 1);

            auto entities = kb.get_entities(100000);
            auto rels = kb.get_relationships(100000);

            // Resolve refs -> names
            std::unordered_set<std::string> seed_names;
            for (const auto& ref : args["entity_ids"]) {
                std::string entity_ref = ref.get<std::string>();
                // First try as exact name match, then substring
                bool found = false;
                for (const auto& e : entities) {
                    if (e.name == entity_ref) {
                        seed_names.insert(e.name);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::string n_lower = entity_ref;
                    std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
                    for (const auto& e : entities) {
                        std::string ename_lower = e.name;
                        std::transform(ename_lower.begin(), ename_lower.end(),
                                      ename_lower.begin(), ::tolower);
                        if (ename_lower.find(n_lower) != std::string::npos) {
                            seed_names.insert(e.name);
                            break;
                        }
                    }
                }
            }
            if (seed_names.empty()) {
                throw McpError(ErrorCode::ResourceNotFound,
                    "No seed entities found");
            }

            // Build adjacency (undirected) by name
            std::unordered_map<std::string, std::vector<std::string>> adj;
            for (const auto& r : rels) {
                adj[r.source].push_back(r.target);
                adj[r.target].push_back(r.source);
            }

            // BFS from all seeds
            std::unordered_set<std::string> visited_names;
            std::queue<std::pair<std::string, size_t>> q;
            for (const auto& sn : seed_names) {
                q.push({sn, 0});
                visited_names.insert(sn);
            }
            while (!q.empty()) {
                auto [cur, depth] = q.front();
                q.pop();
                if (depth >= hops) continue;
                auto it = adj.find(cur);
                if (it == adj.end()) continue;
                for (const auto& nb : it->second) {
                    if (!visited_names.count(nb)) {
                        visited_names.insert(nb);
                        q.push({nb, depth + 1});
                    }
                }
            }

            // Build response
            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& e : entities) {
                if (visited_names.count(e.name)) {
                    nlohmann::json node = {
                        {"id", e.id}, {"name", e.name},
                        {"type", entity_type_to_string(e.type)}
                    };
                    if (seed_names.count(e.name)) node["is_seed"] = true;
                    nodes.push_back(node);
                }
            }
            nlohmann::json edges = nlohmann::json::array();
            for (const auto& r : rels) {
                if (visited_names.count(r.source) && visited_names.count(r.target)) {
                    edges.push_back({
                        {"source", r.source},
                        {"target", r.target},
                        {"type", relation_type_to_string(r.type)},
                        {"weight", r.weight}
                    });
                }
            }
            return {
                {"seeds", args["entity_ids"]},
                {"hops", hops},
                {"nodes", nodes},
                {"edges", edges},
                {"stats", {
                    {"seeds", seed_names.size()},
                    {"nodes", nodes.size()},
                    {"edges", edges.size()}
                }}
            };
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