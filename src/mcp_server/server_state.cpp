// SPDX-License-Identifier: MIT
//
// server_state.cpp — ServerState implementation
//
// Owns the single ApolloCore instance shared by all HTTP requests.

#include "server_state.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

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

bool ServerState::open_kb(const std::string& path, const std::string& name)
{
    return core_->create(path, name);
}

std::string ServerState::get_or_create_session(const std::string& mcp_session_id)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto now = std::chrono::system_clock::now();

    if (!mcp_session_id.empty()) {
        auto it = sessions_.find(mcp_session_id);
        if (it != sessions_.end()) {
            it->second.last_used = now;
            return it->first;
        }
        // Unknown id: create it. (MCP 2025-11-25 says server may reject, but
        // generating on unknown id is friendly and matches the spec's
        // "create-or-attach" recommendation.)
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
    std::lock_guard<std::mutex> lk(mu_);
    auto now = std::chrono::system_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (now - it->second.last_used > idle_ttl) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

ServerState::Stats ServerState::snapshot() const
{
    Stats s{};
    {
        std::lock_guard<std::mutex> lk(mu_);
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
    std::lock_guard<std::mutex> lk(mu_);
    if (data_dir.empty()) {
        config_path_.clear();
        return;
    }
    std::error_code ec;
    fs::create_directories(data_dir, ec);
    config_path_ = (fs::path(data_dir) / "config.json").string();
}

ServerState::ConfigResult ServerState::set_models(const std::string& embed_path,
                                                  const std::string& llm_path)
{
    ConfigResult result;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!embed_path.empty()) embed_model_path_ = embed_path;
        if (!llm_path.empty())   llm_model_path_   = llm_path;
        result.embed_model = embed_model_path_;
        result.llm_model   = llm_model_path_;
    }

    // Reload embedding model if a path is set.
    if (!result.embed_model.empty()) {
        std::error_code ec;
        if (!fs::exists(result.embed_model, ec)) {
            result.embed_error  = "file not found";
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
            result.llm_error  = "file not found";
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
        std::lock_guard<std::mutex> lk(mu_);
        embed_error_ = result.embed_error;
        llm_error_   = result.llm_error;
    }

    // Persist to disk (best-effort; failures here are non-fatal).
    if (!config_path_.empty()) {
        std::ofstream out(config_path_);
        if (out) {
            out << "{\n"
                << "  \"embed_model\": \"" << json_escape(result.embed_model) << "\",\n"
                << "  \"llm_model\":   \"" << json_escape(result.llm_model)   << "\"\n"
                << "}\n";
        }
    }
    return result;
}

std::string ServerState::config_json() const
{
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream o;
    o << "{\n"
      << "  \"embed_model\": \"" << json_escape(embed_model_path_) << "\",\n"
      << "  \"llm_model\":   \"" << json_escape(llm_model_path_)   << "\",\n"
      << "  \"embed_loaded\": " << (embed_error_.empty() && !embed_model_path_.empty() ? "true" : "false") << ",\n"
      << "  \"llm_loaded\":   " << (llm_error_.empty()   && !llm_model_path_.empty()   ? "true" : "false") << ",\n"
      << "  \"embed_error\": \"" << json_escape(embed_error_) << "\",\n"
      << "  \"llm_error\":   \"" << json_escape(llm_error_)   << "\",\n"
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
        std::lock_guard<std::mutex> lk(mu_);
        if (!embed_path.empty()) embed_model_path_ = embed_path;
        if (!llm_path.empty())   llm_model_path_   = llm_path;
    }
}

void ServerState::report_embed_load(bool loaded, const std::string& error)
{
    (void)loaded;  // success / failure is implicit in `error.empty()`
    std::lock_guard<std::mutex> lk(mu_);
    embed_error_ = error;
}

void ServerState::report_llm_load(bool loaded, const std::string& error)
{
    (void)loaded;
    std::lock_guard<std::mutex> lk(mu_);
    llm_error_ = error;
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