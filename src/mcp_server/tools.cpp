// SPDX-License-Identifier: MIT
//
// tools.cpp — tool definitions + handlers
//
// Twelve tools, all service-prefixed `apollo_*`. Each is a thin wrapper
// over an ApolloCore method. Argument validation happens here; tool errors
// are returned as `CallToolResult{is_error=true}` so the LLM can self-correct
// rather than seeing a transport-level failure.

#include "tools.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <apollocore/apollo_hybrid_search.hpp>
#include <apollocore/apollo_persistence.hpp>
#include <apollocore/apollo_codebase_ingest.hpp>
#include <apollocore/entity_extractor.hpp>
#include <set>

namespace apollo::mcp {

using nlohmann::json;

// ---------------------------------------------------------------------------
// ToolDef / CallToolResult / ContentBlock helpers
// ---------------------------------------------------------------------------

json ToolDef::to_list_item() const
{
    json ann = json::object();
    if (read_only_hint)     ann["readOnlyHint"]   = true;
    if (destructive_hint)   ann["destructiveHint"]= true;
    if (idempotent_hint)    ann["idempotentHint"] = true;
    if (open_world_hint)    ann["openWorldHint"]  = true;

    json item = {
        {"name",        name},
        {"description", description},
        {"inputSchema", input_schema},
    };
    if (!title.empty()) item["title"] = title;
    if (!output_schema.is_null()) item["outputSchema"] = output_schema;
    if (!ann.empty())   item["annotations"] = ann;
    return item;
}

json ContentBlock::to_json() const
{
    return {{"type", type}, {"text", text}};
}

json CallToolResult::to_json() const
{
    json j;
    j["content"] = json::array();
    for (const auto& c : content) j["content"].push_back(c.to_json());
    if (is_error)         j["isError"] = true;
    if (!structured_content.is_null()) j["structuredContent"] = structured_content;
    return j;
}

void ToolRegistry::register_tool(ToolDef def, ToolHandler handler)
{
    tools_.push_back({std::move(def), std::move(handler)});
}

json ToolRegistry::list() const
{
    json arr = json::array();
    for (const auto& t : tools_) arr.push_back(t.def.to_list_item());
    return {{"tools", arr}};
}

CallToolResult ToolRegistry::call(const std::string& name,
                                  const json& args,
                                  ServerState& state) const
{
    for (const auto& t : tools_) {
        if (t.def.name == name) {
            try {
                return t.handler(args, state);
            } catch (const std::exception& e) {
                return CallToolResult::error(
                    std::string("Tool '") + name + "' failed: " + e.what());
            }
        }
    }
    return CallToolResult::error("Unknown tool: " + name);
}

// ---------------------------------------------------------------------------
// Argument helpers
// ---------------------------------------------------------------------------

namespace {

[[noreturn]] void bad(const std::string& msg) { throw std::invalid_argument(msg); }

std::string get_str(const json& args, const char* key, bool required = true)
{
    if (!args.contains(key) || args[key].is_null()) {
        if (required) bad(std::string("missing required argument: ") + key);
        return {};
    }
    if (!args[key].is_string()) bad(std::string(key) + " must be a string");
    return args[key].get<std::string>();
}

int get_int(const json& args, const char* key, int def)
{
    if (!args.contains(key) || args[key].is_null()) return def;
    if (!args[key].is_number_integer()) bad(std::string(key) + " must be an integer");
    return args[key].get<int>();
}

double get_double(const json& /*args*/, const char* /*key*/, double def)
{
    return def;
}

std::string dump_results_to_text(const std::vector<std::pair<std::string, float>>& r)
{
    std::ostringstream o;
    o << "Found " << r.size() << " result(s):\n";
    for (size_t i = 0; i < r.size(); ++i) {
        o << "  [" << (i+1) << "] (score " << r[i].second << ")\n"
          << "      " << r[i].first.substr(0, 300) << "\n";
    }
    return o.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// Local helper: implements `apollocore_v2::ingest_directory()` inline.
//
// We do NOT add this to the apollo_core static library because the original
// declaration in `apollo_codebase_ingest.hpp` is unimplemented (it's an
// existing gap in the library — outside this refactor's scope). Instead we
// provide a minimal version here that walks a directory and feeds each
// supported file into ApolloCore via add_document().
// ---------------------------------------------------------------------------

namespace {

struct IngestReport {
    size_t files_processed = 0;
    size_t files_skipped   = 0;
    size_t total_chunks    = 0;
    size_t total_bytes     = 0;     // sum of bytes ingested (per-file cap, not stop)
    bool   safety_limit_hit = false;
    std::vector<std::string> errors;
    std::vector<std::string> doc_ids;
};

bool has_extension(const std::string& path, const std::vector<std::string>& exts)
{
    for (const auto& e : exts) {
        if (path.size() >= e.size() &&
            path.compare(path.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

// Safety limits on apollo_ingest_codebase. The walker is recursive with
// no bounds by default — a path like /home/me/projects/myrepo will
// happily descend into myrepo/build/_deps/llama_cpp-src/ and ingest
// hundreds of MB of third-party sources. The user gets the agent
// they asked for (a runaway recursive scan), not the one they wanted
// (their code). These constants cap that blast radius.
namespace ingest_limits {
    constexpr size_t MAX_FILES            = 5000;   // hard cap per call
    constexpr size_t MAX_TOTAL_BYTES      = 64 * 1024 * 1024;  // 64 MB
    constexpr int    MAX_DEPTH            = 12;      // recursive_directory_iterator depth
    // Directories that should NEVER be walked, even if they exist inside
    // the user's "ingest this repo" path. They contain huge generated/
    // vendored content that makes the embeddings KB useless and burns
    // hours of GPU. Matched against any path component.
    static const std::set<std::string> EXCLUDED_DIRS = {
        "build",         "dist",        "out",         "target",
        ".git",          ".hg",         ".svn",
        ".cache",        ".npm",        ".next",       ".parcel-cache",
        "node_modules",  "__pycache__", ".pytest_cache", ".mypy_cache",
        ".venv",         "venv",        "env",          ".tox",
        ".idea",         ".vscode",     ".gradle",      ".terraform",
        "CMakeFiles",    "CMakeCache",
    };
    // Hidden directories (any leading dot) are skipped by default.
    inline bool is_hidden(const std::filesystem::path& p) {
        auto s = p.filename().string();
        return !s.empty() && s[0] == '.';
    }
    inline bool is_excluded(const std::filesystem::path& p) {
        auto name = p.filename().string();
        if (is_hidden(p)) return true;
        return EXCLUDED_DIRS.count(name) > 0;
    }
}  // namespace ingest_limits

IngestReport ingest_directory_impl(
    apollocore_v2::ApolloCore& kb,
    const std::string& root,
    const std::vector<std::string>& extensions,
    size_t max_file_size,
    bool recursive)
{
    IngestReport rep;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        rep.errors.push_back("path does not exist or is not a directory: " + root);
        return rep;
    }

    // Hard-cap the walk. Once we hit either limit we stop and report
    // a clear error so the caller knows we stopped, not "ran forever".
    bool limit_hit = false;
    std::string limit_reason;

    auto walker = [&](const fs::path& dir) {
        try {
            // depth(0) = root itself. Skip descending past MAX_DEPTH.
            auto it = fs::recursive_directory_iterator(
                dir,
                fs::directory_options::skip_permission_denied,
                ec);
            if (ec) return;
            for (const auto& entry : it) {
                if (limit_hit) break;

                // Skip excluded/hidden directories BEFORE descending.
                if (entry.is_directory(ec) && !ec) {
                    if (ingest_limits::is_excluded(entry.path())) {
                        it.disable_recursion_pending();
                        ++rep.files_skipped;
                        continue;
                    }
                }
                // (Also skip them as files, just in case.)
                if (ingest_limits::is_excluded(entry.path()) && !entry.is_directory(ec)) {
                    ++rep.files_skipped;
                    continue;
                }

                if (ec) { rep.errors.push_back(ec.message()); ec.clear(); continue; }

                // Depth check (root = 0, next dir down = 1, ...).
                if (recursive == false) {
                    if (entry.path().parent_path() != fs::path(root)) {
                        it.disable_recursion_pending();
                    }
                }
                // When recursive=true we still cap the descent.
                if (recursive) {
                    auto rel = fs::relative(entry.path(), fs::path(root));
                    int depth = 0;
                    for (auto p = rel.begin(); p != rel.end(); ++p) ++depth;
                    if (depth > ingest_limits::MAX_DEPTH) {
                        it.disable_recursion_pending();
                        ++rep.files_skipped;
                        continue;
                    }
                }

                if (!entry.is_regular_file(ec)) continue;
                const auto& p = entry.path();
                const std::string path_str = p.string();

                if (!has_extension(path_str, extensions)) {
                    ++rep.files_skipped;
                    continue;
                }

                std::ifstream f(p, std::ios::binary);
                if (!f) { ++rep.files_skipped; continue; }
                f.seekg(0, std::ios::end);
                const auto sz = f.tellg();
                if (sz < 0) { ++rep.files_skipped; continue; }
                const size_t fsize = (size_t)sz;
                if (fsize > max_file_size) {
                    ++rep.files_skipped;
                    continue;
                }
                // Per-file size already capped; stop if total bytes would
                // exceed our blast-radius cap.
                if (rep.total_bytes + fsize > ingest_limits::MAX_TOTAL_BYTES) {
                    limit_hit = true;
                    limit_reason =
                        "total ingest bytes would exceed " +
                        std::to_string(ingest_limits::MAX_TOTAL_BYTES) +
                        " (already at " +
                        std::to_string(rep.total_bytes) + ")";
                    break;
                }
                f.seekg(0);
                std::stringstream ss; ss << f.rdbuf();
                try {
                    size_t chunks = kb.add_document(ss.str(), path_str, 512);
                    rep.total_bytes += fsize;
                    rep.total_chunks += chunks;
                    rep.doc_ids.push_back(path_str);
                    ++rep.files_processed;
                } catch (const std::exception& e) {
                    rep.errors.push_back(std::string(path_str) + ": " + e.what());
                }

                // Hard file-count cap.
                if (rep.files_processed >= ingest_limits::MAX_FILES) {
                    limit_hit = true;
                    limit_reason = "reached max-files cap of " +
                                   std::to_string(ingest_limits::MAX_FILES);
                    break;
                }
            }
        } catch (const std::exception& e) {
            rep.errors.push_back(std::string("walker threw: ") + e.what());
        }
    };
    walker(root);

    if (limit_hit) {
        rep.errors.insert(rep.errors.begin(),
            "SAFETY LIMIT HIT: " + limit_reason +
            ". Re-run with a narrower path, exclude noise directories, or split the work into smaller batches.");
    }

    nlohmann::json out = {
        {"files_processed", rep.files_processed},
        {"files_skipped",   rep.files_skipped},
        {"total_chunks",    rep.total_chunks},
        {"total_bytes",     rep.total_bytes},
        {"safety_limit_hit", limit_hit},
        {"errors",          rep.errors},
        {"doc_ids",         rep.doc_ids}
    };
    nlohmann::json result = out;
    // We can't return IngestReport directly; wrap into nlohmann::json via
    // a static helper to avoid colliding with the unimplemented
    // apollocore_v2::ingest_directory.
    return rep;
}

nlohmann::json ingest_directory_local(
    apollocore_v2::ApolloCore& kb,
    const std::string& path,
    const std::vector<std::string>& extensions,
    size_t max_file_size = 1'000'000,
    bool recursive = true)
{
    auto rep = ingest_directory_impl(kb, path, extensions, max_file_size, recursive);
    return nlohmann::json{
        {"files_processed", rep.files_processed},
        {"files_skipped",   rep.files_skipped},
        {"total_chunks",    rep.total_chunks},
        {"errors",          rep.errors},
        {"doc_ids",         rep.doc_ids}
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// 12 tool handlers
// ---------------------------------------------------------------------------

static CallToolResult apollo_query_kb(const json& args, ServerState& state)
{
    auto& core = state.core();
    const std::string query = get_str(args, "query");
    const std::string mode  = args.value("mode", std::string("hybrid"));
    const int top_k         = get_int(args, "top_k", 5);

    if (mode == "vector") {
        return CallToolResult::text(
            dump_results_to_text(core.query(query, top_k)));
    }
    if (mode == "hybrid") {
        auto r = core.query_hybrid(query, top_k);
        std::ostringstream o;
        o << "Hybrid search returned " << r.size() << " result(s):\n";
        // Fetch chunk contents to display alongside each hit.
        auto all_chunks = core.get_all_chunks();
        std::unordered_map<std::string, std::string> cmap;
        for (auto& p : all_chunks) cmap.emplace(std::move(p.first), std::move(p.second));
        for (size_t i = 0; i < r.size(); ++i) {
            auto it = cmap.find(r[i].chunk_id);
            std::string excerpt = it != cmap.end()
                ? it->second.substr(0, 300)
                : std::string{"(no preview)"};
            o << "  [" << (i+1) << "] (score " << r[i].score
              << ", chunk " << r[i].chunk_id << ")\n"
              << "      " << excerpt << "\n";
        }
        return CallToolResult::text(o.str());
    }
    if (mode == "rerank") {
        auto r = core.query_with_rerank(query, top_k);
        std::ostringstream o;
        o << "Vector + rerank returned " << r.size() << " result(s):\n";
        for (size_t i = 0; i < r.size(); ++i) {
            o << "  [" << (i+1) << "] (rerank " << r[i].rerank_score << ")\n"
              << "      " << r[i].content.substr(0, 300) << "\n";
        }
        return CallToolResult::text(o.str());
    }
    if (mode == "full") {
        auto r = core.query_full(query, top_k);
        std::ostringstream o;
        o << "Hybrid + rerank returned " << r.size() << " result(s):\n";
        for (size_t i = 0; i < r.size(); ++i) {
            o << "  [" << (i+1) << "] (score " << r[i].final_score
              << ", rerank " << r[i].rerank_score << ")\n"
              << "      " << r[i].content.substr(0, 300) << "\n";
        }
        return CallToolResult::text(o.str());
    }
    return CallToolResult::error(
        "mode must be one of: vector, hybrid, rerank, full (got '" + mode + "')");
}

static CallToolResult apollo_add_document(const json& args, ServerState& state)
{
    const std::string text = get_str(args, "text");
    const std::string doc_id = args.contains("doc_id")
        ? get_str(args, "doc_id", false) : std::string{};
    const int chunk_size = get_int(args, "chunk_size", 512);

    size_t added = 0;
    if (!doc_id.empty()) {
        added = state.core().add_document(text, doc_id, chunk_size);
    } else {
        added = state.core().add_document(text, chunk_size);
    }
    return CallToolResult::text("Added " + std::to_string(added) + " chunk(s).");
}

static CallToolResult apollo_extract_entities(const json& args, ServerState& state)
{
    if (!state.core().is_llm_loaded()) {
        return CallToolResult::error(
            "No LLM model loaded. Load one by setting APOLLO_LLM_MODEL before "
            "starting the server (or call apollo_load_llm_model).");
    }

    // `limit` caps how many chunks we run the LLM on in a single call.
    // Without a cap, a 4000-chunk KB takes ~30 minutes of GPU time
    // (entity extraction is LLM-bound, ~500 ms per chunk). Default 500:
    // enough to populate a useful graph, fast enough for an interactive
    // tool call. Pass 0 for "no limit" (use sparingly).
    int limit = args.value("limit", 500);

    // `skip_already_extracted` is default-on so calling this tool twice
    // is idempotent — second call only processes chunks that weren't
    // covered the first time.
    bool skip = args.value("skip_already_extracted", true);

    // ApolloCore's extract_entities() processes chunks already in the KB;
    // there is no per-text entry point. Run it now.
    size_t n = 0;
    try {
        if (limit > 0) {
            n = state.core().extract_entities(/*min_confidence=*/0.5f,
                                             /*skip_already_extracted=*/skip,
                                             /*limit=*/static_cast<size_t>(limit));
        } else {
            n = state.core().extract_entities(/*min_confidence=*/0.5f,
                                             /*skip_already_extracted=*/skip);
        }
    } catch (const std::exception& e) {
        return CallToolResult::error(std::string("Entity extraction failed: ") + e.what());
    }
    return CallToolResult::text(
        "Extraction pass complete: extracted " + std::to_string(n) +
        " entity/relation(s)" +
        (limit > 0 ? " (limit=" + std::to_string(limit) + ")" : " (no limit)") +
        ". Use apollo_get_stats to see updated counts, or apollo://entities "
        "resource to list them.");
}

static CallToolResult apollo_ingest_codebase(const json& args, ServerState& state)
{
    const std::string path    = get_str(args, "path");
    const std::string project = get_str(args, "project");

    // Register/switch to the project.
    if (!state.core().register_project(project, "Ingested via apollo_ingest_codebase")) {
        // may already exist — that's fine
    }
    if (!state.core().set_current_project(project)) {
        return CallToolResult::error("Failed to switch to project '" + project + "'");
    }

    // Optional extensions override.
    std::vector<std::string> extensions = apollocore_v2::default_code_extensions();
    if (args.contains("extensions") && args["extensions"].is_array()) {
        extensions.clear();
        for (const auto& e : args["extensions"]) {
            if (e.is_string()) extensions.push_back(e.get<std::string>());
        }
    }

    try {
        auto report = ingest_directory_local(state.core(), path, extensions);
        size_t total_chunks = report.contains("total_chunks") &&
                              report["total_chunks"].is_number_integer()
                                  ? report["total_chunks"].get<size_t>()
                                  : 0;
        size_t files_processed = report.contains("files_processed") &&
                                report["files_processed"].is_number_integer()
                                    ? report["files_processed"].get<size_t>()
                                    : 0;

        // Optionally trigger an entity-extraction pass right after
        // ingest. The graph is the value-add for codebases (entities
        // like FUNCTION, CLASS, plus relationships) and many agents
        // never remember to call apollo_extract_entities separately.
        //
        // Default true so users get a populated graph by default;
        // capped at `auto_extract_limit` chunks (default 200) so the
        // tool call doesn't take 30 minutes on a 4000-chunk KB.
        // Skip if the LLM isn't loaded (extract_entities would just
        // fail anyway).
        size_t extracted = 0;
        bool auto_extract = args.value("auto_extract", true);
        size_t auto_limit = args.value("auto_extract_limit", 200);
        if (auto_extract && total_chunks > 0 &&
            state.core().is_llm_loaded()) {
            try {
                extracted = state.core().extract_entities(
                    /*min_confidence=*/0.5f,
                    /*skip_already_extracted=*/true,
                    /*limit=*/auto_limit);
            } catch (const std::exception& e) {
                // Don't fail the whole ingest if extraction fails —
                // the agent can retry apollo_extract_entities separately.
                std::string warn = std::string("(warning: auto_extract failed: ") + e.what() + ")";
                return CallToolResult::text(
                    "Ingested " + std::to_string(total_chunks) +
                    " chunk(s) from " + path + " into project '" + project + "'. "
                    "Report: " + report.dump(2) + "\n" + warn);
            }
        }

        std::string summary =
            "Ingested " + std::to_string(total_chunks) +
            " chunk(s) from " + path + " into project '" + project + "'. ";
        if (auto_extract && total_chunks > 0 && state.core().is_llm_loaded()) {
            summary += "Auto-extracted " + std::to_string(extracted) +
                       " entity/relation(s) on up to " +
                       std::to_string(auto_limit) + " chunk(s). ";
        } else if (auto_extract && !state.core().is_llm_loaded()) {
            summary += "(no LLM loaded — call apollo_extract_entities separately). ";
        }
        summary += "Report: " + report.dump(2);
        return CallToolResult::text(summary);
    } catch (const std::exception& e) {
        return CallToolResult::error(
            std::string("Codebase ingest failed: ") + e.what());
    }
}

static CallToolResult apollo_search_symbols(const json& args, ServerState& state)
{
    // ApolloCore doesn't expose a dedicated symbol search, but the knowledge
    // graph stores extracted entities by type (FUNCTION, CLASS, etc.).
    // We approximate symbol search by querying for the name and filtering
    // for code-related entity types in the results.
    const std::string name = get_str(args, "name");
    const std::string kind = args.value("kind", std::string("ALL"));
    const int limit        = get_int(args, "limit", 20);

    auto hits = state.core().query_hybrid(name, std::max(limit * 4, 40));
    auto all_chunks = state.core().get_all_chunks();
    std::unordered_map<std::string, std::string> cmap;
    for (auto& p : all_chunks) cmap.emplace(std::move(p.first), std::move(p.second));

    std::ostringstream o;
    o << "Found " << hits.size() << " candidate(s) for '" << name << "':\n";
    size_t shown = 0;
    for (const auto& h : hits) {
        if (shown >= (size_t)limit) break;
        auto it = cmap.find(h.chunk_id);
        std::string preview = it != cmap.end()
            ? it->second : std::string{"(no preview)"};
        if (kind != "ALL" && preview.find(kind) == std::string::npos) continue;
        o << "  - [chunk " << h.chunk_id << "] (score " << h.score << ")\n"
          << "      " << preview.substr(0, 200) << "\n";
        ++shown;
    }
    return CallToolResult::text(o.str());
}

static CallToolResult apollo_get_neighbors(const json& args, ServerState& state)
{
    // ApolloCore doesn't expose a direct graph-traversal API. We approximate
    // "neighbors" by:
    //   1) finding chunks that mention the entity by name (vector+keyword), and
    //   2) listing other entities mentioned in those same chunks.
    const std::string entity = get_str(args, "entity");
    const int hops          = get_int(args, "hops", 1);

    auto hits = state.core().query_hybrid(entity, 20);
    std::unordered_set<std::string> seen;
    std::ostringstream o;
    o << "Approximate neighbors of '" << entity
      << "' (hops=" << hops << ", " << hits.size() << " source chunk(s)):\n";
    for (const auto& h : hits) {
        if (!h.chunk_id.empty()) seen.insert(h.chunk_id);
    }
    if (seen.empty()) {
        o << "  (no chunks mention this entity yet — try apollo_extract_entities "
             "after ingesting text that mentions it)\n";
    } else {
        for (const auto& cid : seen) {
            auto ents = state.core().get_entities_for_chunk(cid);
            for (const auto& e : ents) {
                o << "  - " << e.name << " (" << entity_type_to_string(e.type) << ")\n";
            }
        }
    }
    return CallToolResult::text(o.str());
}

static CallToolResult apollo_create_kb(const json& args, ServerState& state)
{
    const std::string path = get_str(args, "path");
    const std::string name = args.value("name", std::string("Untitled"));
    if (!state.open_kb(path, name)) {
        return CallToolResult::error("Failed to create KB at " + path);
    }
    return CallToolResult::text("Created knowledge base '" + name + "' at " + path);
}

static CallToolResult apollo_open_kb(const json& args, ServerState& state)
{
    const std::string path = get_str(args, "path");
    if (!state.core().open(path)) {
        return CallToolResult::error("Failed to open KB at " + path);
    }
    return CallToolResult::text("Opened knowledge base at " + path);
}

static CallToolResult apollo_save_kb(const json& /*args*/, ServerState& state)
{
    if (!state.core().save()) {
        return CallToolResult::error("save() returned false (no KB open?)");
    }
    return CallToolResult::text("Saved.");
}

static CallToolResult apollo_get_stats(const json& args, ServerState& state)
{
    const std::string project = args.contains("project")
        ? get_str(args, "project", false) : std::string{};
    if (!project.empty()) state.core().set_current_project(project);
    auto s = state.core().get_stats();
    json j = {
        {"documents",     s.document_count},
        {"chunks",        s.chunk_count},
        {"entities",      s.entity_count},
        {"relationships", s.relationship_count},
    };
    // Phase D: emit structuredContent so MCP clients can validate the
    // shape without parsing the text dump. The matching outputSchema
    // is registered with the tool below.
    CallToolResult r;
    ContentBlock cb;
    cb.text = j.dump(2);
    r.content.push_back(cb);
    r.structured_content = j;
    return r;
}

static CallToolResult apollo_set_project(const json& args, ServerState& state)
{
    const std::string project = get_str(args, "project");
    if (!state.core().set_current_project(project)) {
        return CallToolResult::error("Project '" + project + "' does not exist. "
                                     "Call apollo_register_project first.");
    }
    return CallToolResult::text("Switched to project: " + project);
}

static CallToolResult apollo_list_projects(const json& /*args*/, ServerState& state)
{
    // Phase D: emit structuredContent (array of project objects) so
    // clients can iterate without parsing the text dump.
    CallToolResult r;
    ContentBlock cb;
    cb.text = state.core().list_projects().dump(2);
    r.content.push_back(cb);
    r.structured_content = state.core().list_projects();
    return r;
}

static CallToolResult apollo_set_config(const json& args, ServerState& state)
{
    // Either field may be missing or empty — that's "keep current".
    std::string embed = args.contains("embed_model") && args["embed_model"].is_string()
                        ? args["embed_model"].get<std::string>() : std::string{};
    std::string llm   = args.contains("llm_model") && args["llm_model"].is_string()
                        ? args["llm_model"].get<std::string>() : std::string{};

    if (embed.empty() && llm.empty()) {
        return CallToolResult::error(
            "Provide at least one of: embed_model, llm_model. "
            "Empty values keep the current path.");
    }

    auto r = state.set_models(embed, llm);

    json out = {
        {"embed_model",  r.embed_model},
        {"llm_model",    r.llm_model},
        {"embed_loaded", r.embed_loaded},
        {"llm_loaded",   r.llm_loaded},
        {"embed_error",  r.embed_error},
        {"llm_error",    r.llm_error},
        {"config_path",  state.config_path()},
    };

    // If user asked to set a path and it failed to load, return is_error
    // so the agent can self-correct.
    bool wanted_embed = !embed.empty();
    bool wanted_llm   = !llm.empty();
    bool failed = (wanted_embed && !r.embed_loaded) ||
                  (wanted_llm   && !r.llm_loaded);
    if (failed) {
        auto res = CallToolResult::text(out.dump(2));
        res.is_error = true;
        return res;
    }
    return CallToolResult::text(out.dump(2));
}

// ---------------------------------------------------------------------------
// JSON Schemas (JSON Schema 2020-12 subset used by MCP)
// ---------------------------------------------------------------------------

namespace {
json schema_query_kb() {
    return json{
        {"type", "object"},
        {"properties", {
            {"query",     {{"type", "string"}, {"description", "Natural-language query"}}},
            {"mode",      {{"type", "string"},
                           {"enum", json::array({"vector","hybrid","rerank","full"})},
                           {"default", "hybrid"},
                           {"description", "Query pipeline to use"}}},
            {"top_k",     {{"type", "integer"},
                           {"minimum", 1}, {"maximum", 100}, {"default", 5}}}
        }},
        {"required", json::array({"query"})}
    };
}
json schema_add_document() {
    return json{
        {"type", "object"},
        {"properties", {
            {"text",       {{"type", "string"},
                            {"description", "Document text to ingest (will be chunked)"}}},
            {"doc_id",     {{"type", "string"},
                            {"description", "Optional custom document id"}}},
            {"chunk_size", {{"type", "integer"},
                            {"minimum", 64}, {"maximum", 8192}, {"default", 512}}}
        }},
        {"required", json::array({"text"})}
    };
}
json schema_extract_entities() {
    return json{
        {"type", "object"},
        {"properties", {
            {"limit", {
                {"type", "integer"},
                {"minimum", 0},
                {"default", 500},
                {"description", "Max chunks to process in this call. "
                                "0 = no limit (use sparingly). Default 500 — "
                                "entity extraction is LLM-bound (~500 ms per "
                                "chunk) so a 4000-chunk KB would take ~30 "
                                "minutes without a cap."}
            }},
            {"skip_already_extracted", {
                {"type", "boolean"},
                {"default", true},
                {"description", "If true, skip chunks that already have "
                                "entities extracted. Makes repeated calls "
                                "idempotent."}
            }},
            {"min_confidence", {
                {"type", "number"},
                {"default", 0.5},
                {"minimum", 0.0}, {"maximum", 1.0}
            }}
        }},
        {"required", json::array()}
    };
}
json schema_ingest_codebase() {
    return json{
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}}},
            {"project", {{"type", "string"}}},
            {"extensions", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "File extensions to include. "
                                "Defaults to a sensible code+docs set."}
            }},
            {"auto_extract", {
                {"type", "boolean"},
                {"default", true},
                {"description", "Run an entity-extraction pass on the "
                                "ingested chunks immediately after ingest. "
                                "Default true so the knowledge graph is "
                                "populated by default; pass false to skip."}
            }},
            {"auto_extract_limit", {
                {"type", "integer"},
                {"minimum", 1},
                {"default", 200},
                {"description", "When auto_extract=true, max chunks to "
                                "extract entities from in this call. "
                                "Default 200 (fast). The user can call "
                                "apollo_extract_entities later to process "
                                "more."}
            }}
        }},
        {"required", json::array({"path","project"})}
    };
}
json schema_search_symbols() {
    return json{
        {"type", "object"},
        {"properties", {
            {"name",  {{"type", "string"}}},
            {"kind",  {{"type", "string"},
                       {"enum", json::array({"FUNCTION","CLASS","NAMESPACE","VARIABLE","ALL"})},
                       {"default", "ALL"}}},
            {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 20}}}
        }},
        {"required", json::array({"name"})}
    };
}
json schema_get_neighbors() {
    return json{
        {"type", "object"},
        {"properties", {
            {"entity", {{"type", "string"}}},
            {"hops",   {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}, {"default", 1}}}
        }},
        {"required", json::array({"entity"})}
    };
}
json schema_create_kb() {
    return json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}},
            {"name", {{"type", "string"}, {"default", "Untitled"}}}
        }},
        {"required", json::array({"path"})}
    };
}
json schema_open_kb() {
    return json{
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}}}
        }},
        {"required", json::array({"path"})}
    };
}
json schema_get_stats() {
    return json{
        {"type", "object"},
        {"properties", {
            {"project", {{"type", "string"}}}
        }}
    };
}
json schema_set_project() {
    return json{
        {"type", "object"},
        {"properties", {
            {"project", {{"type", "string"}}}
        }},
        {"required", json::array({"project"})}
    };
}

json schema_set_config() {
    // Both fields are optional. Empty/missing means "keep current value".
    return json{
        {"type", "object"},
        {"properties", {
            {"embed_model", {{"type", "string"},
                             {"description", "Absolute path to a GGUF embedding model. Empty keeps the current path."}}},
            {"llm_model",   {{"type", "string"},
                             {"description", "Absolute path to a GGUF LLM (instruct-tuned). Empty keeps the current path."}}}
        }},
        // No "required" — both are optional.
    };
}
}  // namespace

// ---------------------------------------------------------------------------
// Default registry
// ---------------------------------------------------------------------------

ToolRegistry build_default_registry()
{
    ToolRegistry r;

    r.register_tool(
        {"apollo_query_kb",
         "Search the knowledge base. Combines vector + BM25 + graph signals "
         "by default. Use 'vector' for pure semantic, 'rerank' for precision, "
         "'full' for highest quality. Always use this when the user asks "
         "questions about content the KB contains.",
         schema_query_kb(),
         "Query knowledge base",
         /*read_only_hint*/ true,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true},
        apollo_query_kb);

    r.register_tool(
        {"apollo_add_document",
         "Ingest a document into the knowledge base. Chunks it automatically. "
         "Use this when the user shares text they want remembered.",
         schema_add_document(),
         "Add document",
         /*read_only_hint*/ false,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ false},
        apollo_add_document);

    r.register_tool(
        {"apollo_extract_entities",
         "Extract named entities (people, organizations, concepts, code symbols) "
         "from text using the loaded LLM. Requires an LLM model to be loaded.",
         schema_extract_entities(),
         "Extract entities",
         /*read_only_hint*/ false,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true},
        apollo_extract_entities);

    r.register_tool(
        {"apollo_ingest_codebase",
         "Walk a source directory and ingest every supported file into a named "
         "project. Uses tree-sitter for code files (auto-extracts FUNCTION, "
         "CLASS, etc.) and chunked embeddings for everything else.",
         schema_ingest_codebase(),
         "Ingest a codebase",
         /*read_only_hint*/ false,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ false},
        apollo_ingest_codebase);

    r.register_tool(
        {"apollo_search_symbols",
         "Find code symbols (functions, classes, namespaces) by name. "
         "Faster and more precise than apollo_query_kb for symbol lookup.",
         schema_search_symbols(),
         "Search code symbols",
         /*read_only_hint*/ true,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true},
        apollo_search_symbols);

    r.register_tool(
        {"apollo_get_neighbors",
         "Get entities connected to a given entity in the knowledge graph. "
         "Use hops=2 to expand the neighborhood.",
         schema_get_neighbors(),
         "Get graph neighbors",
         /*read_only_hint*/ true,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true},
        apollo_get_neighbors);

    r.register_tool(
        {"apollo_create_kb",
         "Create a new (empty) knowledge base at the given path. Subsequent "
         "add_document calls fill it. Fails if a KB already exists at path.",
         schema_create_kb(),
         "Create knowledge base",
         /*read_only_hint*/ false,
         /*destructive_hint*/ true},
        apollo_create_kb);

    r.register_tool(
        {"apollo_open_kb",
         "Open an existing knowledge base from disk. Replaces the currently "
         "open one in this process.",
         schema_open_kb(),
         "Open knowledge base",
         /*read_only_hint*/ false,
         /*destructive_hint*/ true},
        apollo_open_kb);

    r.register_tool(
        {"apollo_save_kb",
         "Persist the in-memory KB to disk. Call this if you want durability "
         "between server restarts.",
         json{{"type", "object"}, {"properties", json::object()}},
         "Save knowledge base"},
        apollo_save_kb);

    r.register_tool(
        {"apollo_get_stats",
         "Get document/chunk/entity counts, optionally filtered by project.",
         schema_get_stats(),
         "Get KB statistics",
         /*read_only_hint*/ true,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true,
         /*open_world_hint*/ false,
         json{
             {"type", "object"},
             {"properties", {
                 {"documents",     {{"type", "integer"}, {"minimum", 0}}},
                 {"chunks",        {{"type", "integer"}, {"minimum", 0}}},
                 {"entities",      {{"type", "integer"}, {"minimum", 0}}},
                 {"relationships", {{"type", "integer"}, {"minimum", 0}}}
             }},
             {"required", {"documents", "chunks", "entities", "relationships"}},
             {"additionalProperties", false}
         }},
        apollo_get_stats);

    r.register_tool(
        {"apollo_set_project",
         "Switch the active project. All subsequent reads/writes are scoped "
         "to this project.",
         schema_set_project(),
         "Set current project",
         /*read_only_hint*/ false,
         /*destructive_hint*/ true},
        apollo_set_project);

    r.register_tool(
        {"apollo_list_projects",
         "List all projects in the current knowledge base.",
         json{{"type","object"},{"properties", json::object()}},
         "List projects",
         /*read_only_hint*/ true,
         /*destructive_hint*/ false,
         /*idempotent_hint*/ true,
         /*open_world_hint*/ false,
         json{
             {"type", "array"},
             {"items", {
                 {"type", "object"},
                 {"properties", {
                     {"name",        {{"type", "string"}}},
                     {"description", {{"type", "string"}}},
                     {"is_shared",   {{"type", "boolean"}}},
                     {"owner",       {{"type", "string"}}},
                     {"created_at",  {{"type", "integer"}, {"description", "Unix epoch seconds"}}},
                     {"is_current",  {{"type", "boolean"}}}
                 }},
                 {"required", {"name", "is_shared", "is_current"}},
                 {"additionalProperties", true}
             }}
         }},
        apollo_list_projects);

    r.register_tool(
        {"apollo_set_config",
         "Set the embedding and/or LLM model paths, hot-reload them, "
         "and persist to <data-dir>/config.json. Empty values keep the "
         "current path. host/port/data_dir cannot be changed at runtime.",
         schema_set_config(),
         "Update model configuration",
         /*read_only_hint*/ false,
         /*destructive_hint*/ true,
         /*idempotent_hint*/ true},
        apollo_set_config);

    return r;
}

}  // namespace apollo::mcp