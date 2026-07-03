// SPDX-License-Identifier: MIT
//
// server_state.cpp — ServerState implementation
//
// Owns the single ApolloCore instance shared by all HTTP requests.

#include "server_state.hpp"

#include "mcp_security.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace apollo::mcp {

namespace fs = std::filesystem;

/// Escape a string for inclusion in a JSON literal. Used by
/// config_json() and set_models() to render model paths safely.
std::string json_escape(const std::string& s);

// ---------------------------------------------------------------------------
// UUID v4 — small inline generator. We don't need cryptographic strength
// here; we just need uniqueness across sessions.
// ---------------------------------------------------------------------------
std::string ServerState::make_uuid_v4()
{
    static thread_local std::mt19937_64 rng(
        std::random_device{}() ^ static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    uint64_t a = rng();
    uint64_t b = rng();

    // Set version (4) and variant (10) per RFC 4122 §4.4.
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream o;
    o << std::hex << std::setfill('0');
    o << std::setw(8) << ((a >> 32) & 0xFFFFFFFFULL) << '-';
    o << std::setw(4) << ((a >> 16) & 0xFFFFULL) << '-';
    o << std::setw(4) << (a & 0xFFFFULL) << '-';
    o << std::setw(4) << ((b >> 48) & 0xFFFFULL) << '-';
    o << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
    return o.str();
}

ServerState::ServerState()
    : core_(std::make_unique<apollocore_v2::ApolloCore>())
{}

ServerState::~ServerState() = default;

namespace {
// `dirs_contains` declared early so set_models / record_resolved_models
// can use it when assembling the search order for bare filenames.
bool contains_dir(const std::vector<fs::path>& v, const fs::path& p) {
    std::error_code e;
    for (const auto& x : v) {
        if (fs::equivalent(x, p, e)) return true;
        e.clear();
    }
    return false;
}
}  // namespace

bool ServerState::open_kb(const std::string& path, const std::string& name)
{
    return core_->create(path, name);
}

std::string ServerState::find_session(const std::string& mcp_session_id) const
{
    if (mcp_session_id.empty()) return {};
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sessions_.find(mcp_session_id);
    if (it == sessions_.end()) return {};
    // Touch last_used under the same lock so the reaper doesn't reap
    // us mid-request.
    const_cast<Session&>(it->second).last_used =
        std::chrono::system_clock::now();
    return it->first;
}

std::string ServerState::get_or_create_session(const std::string& mcp_session_id,
                                              bool auto_create_on_unknown)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto now = std::chrono::system_clock::now();

    if (!mcp_session_id.empty()) {
        auto it = sessions_.find(mcp_session_id);
        if (it != sessions_.end()) {
            it->second.last_used = now;
            return it->first;
        }
        // Unknown non-empty id.
        if (!auto_create_on_unknown) {
            // Strict mode: tell the caller (via empty string) that the
            // id is bogus so they can 400 the client. Per spec:
            //   "If the server receives a request with an unknown
            //    session ID, the server SHOULD respond with HTTP 400."
            return {};
        }
        // Legacy/dev mode: keep the friendly auto-create behaviour.
    }

    Session s;
    s.id = make_uuid_v4();
    s.created = now;
    s.last_used = now;
    auto inserted = sessions_.emplace(s.id, std::move(s));
    return inserted.first->first;   // the key we just inserted
}

void ServerState::reap_idle_sessions(std::chrono::seconds idle_ttl)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto now = std::chrono::system_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (now - it->second.last_used > idle_ttl) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void ServerState::set_negotiated_version(const std::string& session_id,
                                         const std::string& version) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.negotiated_version = version;
    }
    // Unknown session: ignore. Sessions are lazily created on first
    // request, and `initialize` is normally the first request that
    // creates one — but if a client calls `initialize` twice (legal)
    // we just update the existing entry.
}

std::string ServerState::negotiated_version(const std::string& session_id) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end() && !it->second.negotiated_version.empty()) {
        return it->second.negotiated_version;
    }
    return std::string(security::PINNED_VERSION);
}

void ServerState::set_session_log_level(const std::string& session_id,
                                       const std::string& level) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.log_level = level;
    }
}

std::string ServerState::session_log_level(const std::string& session_id) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) return it->second.log_level;
    return "info";
}

ServerState::Stats ServerState::snapshot() const
{
    Stats s{};
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        s.live_sessions = sessions_.size();
    }
    auto apollo_stats = core_->get_stats();
    s.document_count        = apollo_stats.document_count;
    s.chunk_count           = apollo_stats.chunk_count;
    s.entity_count          = apollo_stats.entity_count;
    s.relationship_count    = apollo_stats.relationship_count;
    return s;
}

// ---------------------------------------------------------------------------
// Runtime config: model paths
// ---------------------------------------------------------------------------

void ServerState::set_data_dir(const std::string& data_dir)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (data_dir.empty()) {
        config_path_.clear();
        data_dir_.clear();
        return;
    }
    std::error_code ec;
    fs::create_directories(data_dir, ec);
    config_path_ = (fs::path(data_dir) / "config.json").string();
    data_dir_    = data_dir;
}

void ServerState::set_verbose(bool enabled)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    verbose_ = enabled;
}

bool ServerState::verbose() const
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return verbose_;
}

void ServerState::set_default_search_mode(const std::string& mode)
{
    // Reject unknown modes to keep the Search tab from sending garbage.
    static const std::set<std::string> allowed = {"vector", "hybrid", "rerank", "full"};
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (allowed.count(mode)) default_search_mode_ = mode;
}

std::string ServerState::default_search_mode() const
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return default_search_mode_;
}

void ServerState::add_search_dir(const std::string& dir)
{
    if (dir.empty()) return;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    extra_search_dirs_.push_back(dir);
}

// ---------------------------------------------------------------------------
// Models-dir scan
// ---------------------------------------------------------------------------

void ServerState::set_models_dirs(const std::vector<std::string>& dirs)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    models_dirs_.clear();
    for (const auto& d : dirs) {
        if (!d.empty()) models_dirs_.push_back(d);
    }
}

void ServerState::add_models_dir(const std::string& dir)
{
    if (dir.empty()) return;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (std::find(models_dirs_.begin(), models_dirs_.end(), dir) == models_dirs_.end()) {
        models_dirs_.push_back(dir);
    }
}

std::vector<std::string> ServerState::models_dirs() const
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return models_dirs_;  // copy
}

std::vector<ServerState::ModelInfo> ServerState::list_models() const
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return models_cache_;  // copy
}

namespace {
ServerState::ModelInfo make_model_info(const std::filesystem::path& p,
                                       const std::string& dir)
{
    ServerState::ModelInfo m;
    std::error_code ec;
    m.name = p.filename().string();
    m.path = std::filesystem::absolute(p, ec).string();
    m.dir  = dir;
    if (auto sz = std::filesystem::file_size(p, ec); !ec) m.size_bytes = sz;
    // file_clock → system_clock → time_t. C++20/23.
    if (auto t = std::filesystem::last_write_time(p, ec); !ec) {
        auto sys = std::chrono::file_clock::to_sys(t);
        m.mtime = std::chrono::system_clock::to_time_t(sys);
    }
    return m;
}
}  // namespace

std::vector<ServerState::ModelInfo> ServerState::rescan_models()
{
    std::vector<ModelInfo> out;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        std::set<std::string> seen;  // dedupe by canonical path
        for (const auto& dir : models_dirs_) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                auto p = entry.path();
                if (p.extension() != ".gguf") continue;
                auto abs = std::filesystem::absolute(p, ec).string();
                if (!seen.insert(abs).second) continue;  // already listed
                out.push_back(make_model_info(p, dir));
            }
        }
        std::sort(out.begin(), out.end(),
                  [](const ModelInfo& a, const ModelInfo& b) {
                      return a.name < b.name;
                  });
        models_cache_ = out;  // cache
    }
    return out;
}

// Resolve a path that may be a bare filename or a partial path.
//  - If the input is already an existing absolute path, return as-is.
//  - If the input contains a path separator, return as-is (the user
//    was specific; the eventual "file not found" error is correct).
//  - Otherwise, treat the input as a bare filename and search:
//      1. The dir of the currently-configured embed_model_path_
//      2. The dir of the currently-configured llm_model_path_
//      3. <data_dir>/../models/  (sibling of data dir — common layout)
//      4. <data_dir>/models/
//      5. ./models/              (CWD-relative; dev default)
//      6. Any extra search dirs registered via add_search_dir()
//    The first match wins. The basename match is exact.
std::string ServerState::resolve_model_path(const std::string& input) const
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (input.empty()) return input;

    // 1. Already a real, existing path.
    if (fs::exists(input, ec)) {
        ec.clear();
        return fs::absolute(input, ec).string();
    }

    // 2. Has a separator but doesn't exist — user was specific. Don't
    //    search; the eventual "file not found" is the right outcome.
    if (input.find('/') != std::string::npos || input.find('\\') != std::string::npos) {
        return input;
    }

    // 3. Bare filename — build a deduped list of search dirs.
    std::vector<fs::path> dirs;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto push_dir = [&](const std::string& p) {
            if (p.empty()) return;
            fs::path d = fs::path(p).parent_path();
            if (d.empty()) return;
            // De-dup.
            for (const auto& existing : dirs) {
                std::error_code e;
                if (fs::equivalent(existing, d, e)) return;
            }
            dirs.push_back(d);
        };
        push_dir(embed_model_path_);
        push_dir(llm_model_path_);
        for (const auto& d : extra_search_dirs_) push_dir(d);
    }
    if (!config_path_.empty()) {
        fs::path data_dir = fs::path(config_path_).parent_path();
        // <data_dir>/../models/  (project root models dir)
        std::error_code e;
        fs::path project_models = fs::canonical(data_dir.parent_path(), e) / "models";
        if (!e && !contains_dir(dirs, project_models)) dirs.push_back(project_models);
        // <data_dir>/models/
        fs::path data_models = data_dir / "models";
        if (!contains_dir(dirs, data_models)) dirs.push_back(data_models);
    }
    {
        std::error_code e;
        fs::path cwd_models = fs::current_path(e) / "models";
        if (!e && !contains_dir(dirs, cwd_models)) dirs.push_back(cwd_models);
    }

    for (const auto& d : dirs) {
        std::error_code e;
        fs::path candidate = d / input;
        if (fs::exists(candidate, e)) {
            return fs::absolute(candidate, e).string();
        }
    }
    return input;  // unresolved — let the caller show the "file not found" error.
}

ServerState::ConfigResult ServerState::set_models(const std::string& embed_path,
                                                  const std::string& llm_path)
{
    ConfigResult result;
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!embed_path.empty()) embed_model_path_ = resolve_model_path(embed_path);
        if (!llm_path.empty())   llm_model_path_   = resolve_model_path(llm_path);
        result.embed_model = embed_model_path_;
        result.llm_model   = llm_model_path_;
    }
    // Reload embedding model if a path is set.
    if (!result.embed_model.empty()) {
        std::error_code ec;
        if (!fs::exists(result.embed_model, ec)) {
            result.embed_error  = "file not found (tried: " + result.embed_model + ")";
            result.embed_loaded = false;
        } else if (!core_->load_embedding_model(result.embed_model)) {
            result.embed_error  = "load_embedding_model returned false";
            result.embed_loaded = false;
        } else {
            result.embed_loaded = true;
            result.embed_error.clear();
        }
    }

    if (!result.llm_model.empty()) {
        std::error_code ec;
        if (!fs::exists(result.llm_model, ec)) {
            result.llm_error  = "file not found (tried: " + result.llm_model + ")";
            result.llm_loaded = false;
        } else if (!core_->load_llm_model(result.llm_model)) {
            result.llm_error  = "load_llm_model returned false";
            result.llm_loaded = false;
        } else {
            result.llm_loaded = true;
            result.llm_error.clear();
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        embed_error_ = result.embed_error;
        llm_error_   = result.llm_error;
    }

    // Persist to disk (best-effort; failures here are non-fatal).
    // Both paths are always written — even if one failed to load — so the
    // user can come back later and see exactly which path is configured.
    if (!config_path_.empty()) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        std::ofstream out(config_path_);
        if (out) {
            out << "{\n";
            // data_dir is informational; the binary writes its data-dir
            // value so the Settings page can show it (and so a downstream
            // tool can find the same KB store).
            out << "  \"data_dir\": \"" << json_escape(data_dir_) << "\",\n";
            out << "  \"models_dir\": [\n";
            for (size_t i = 0; i < models_dirs_.size(); ++i) {
                out << "    \"" << json_escape(models_dirs_[i]) << "\""
                    << (i + 1 < models_dirs_.size() ? "," : "") << "\n";
            }
            out << "  ],\n";
            out << "  \"embed_model\": \"" << json_escape(result.embed_model) << "\",\n"
                << "  \"llm_model\":   \"" << json_escape(result.llm_model)   << "\",\n"
                << "  \"default_search_mode\": \"" << json_escape(default_search_mode_) << "\",\n"
                << "  \"verbose\": " << (verbose_ ? "true" : "false") << "\n"
                << "}\n";
        }
    }
    return result;
}

std::string ServerState::config_json() const
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::ostringstream o;
    o << "{\n"
      << "  \"data_dir\": \""   << json_escape(data_dir_)   << "\",\n"
      << "  \"models_dir\": [";
    for (size_t i = 0; i < models_dirs_.size(); ++i) {
        o << (i == 0 ? "\n" : ",\n")
          << "    \"" << json_escape(models_dirs_[i]) << "\"";
    }
    if (!models_dirs_.empty()) o << "\n  ";
    o << "],\n"
      << "  \"embed_model\": \"" << json_escape(embed_model_path_) << "\",\n"
      << "  \"llm_model\":   \"" << json_escape(llm_model_path_)   << "\",\n"
      << "  \"embed_loaded\": " << (embed_error_.empty() && !embed_model_path_.empty() ? "true" : "false") << ",\n"
      << "  \"llm_loaded\":   " << (llm_error_.empty()   && !llm_model_path_.empty()   ? "true" : "false") << ",\n"
      << "  \"embed_error\": \"" << json_escape(embed_error_) << "\",\n"
      << "  \"llm_error\":   \"" << json_escape(llm_error_)   << "\",\n"
      << "  \"default_search_mode\": \"" << json_escape(default_search_mode_) << "\",\n"
      << "  \"verbose\": " << (verbose_ ? "true" : "false") << ",\n"
      << "  \"config_path\": \"" << json_escape(config_path_) << "\"\n"
      << "}\n";
    return o.str();
}

bool ServerState::load_config_from_disk()
{
    std::string embed, llm;
    if (!read_config_paths(embed, llm)) return false;
    if (embed.empty() && llm.empty()) return false;
    set_models(embed, llm);
    return true;
}

bool ServerState::read_config_paths(std::string& embed_out,
                                     std::string& llm_out) const
{
    embed_out.clear();
    llm_out.clear();
    if (config_path_.empty()) return false;
    std::error_code ec;
    if (!fs::exists(config_path_, ec)) return false;

    std::ifstream in(config_path_);
    if (!in) return false;

    // Hand-rolled mini-parser — config.json is small and well-known.
    // Avoids pulling in nlohmann/json for a 4-key file.
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    auto grab = [&](const std::string& key) -> std::string {
        auto kpos = content.find("\"" + key + "\"");
        if (kpos == std::string::npos) return "";
        auto colon = content.find(':', kpos);
        if (colon == std::string::npos) return "";
        auto q1 = content.find('"', colon);
        if (q1 == std::string::npos) return "";
        auto q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";
        return content.substr(q1 + 1, q2 - q1 - 1);
    };

    embed_out = grab("embed_model");
    llm_out   = grab("llm_model");
    return true;
}

void ServerState::record_resolved_models(const std::string& embed_path,
                                          const std::string& llm_path)
{
    // Update in-memory paths (under mu_) but DON'T load here — main()
    // will load below. We need the paths set first so config_json()
    // is meaningful even if the load fails.
    {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!embed_path.empty()) embed_model_path_ = resolve_model_path(embed_path);
        if (!llm_path.empty())   llm_model_path_   = resolve_model_path(llm_path);
    }
}

void ServerState::report_embed_load(bool loaded, const std::string& error)
{
    (void)loaded;  // success / failure is implicit in `error.empty()`
    std::lock_guard<std::recursive_mutex> lk(mu_);
    embed_error_ = error;
}

void ServerState::report_llm_load(bool loaded, const std::string& error)
{
    (void)loaded;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    llm_error_ = error;
}

bool ServerState::end_session(const std::string& session_id)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return sessions_.erase(session_id) > 0;
}

// Helper used by both set_models and config_json. Keeps the on-disk and
// on-wire JSON clean even when paths contain quotes or backslashes.
std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace apollo::mcp