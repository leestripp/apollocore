// SPDX-License-Identifier: MIT
//
// server_state.hpp — owns the ApolloCore instance behind the MCP server
//
// One ApolloCore per process. Sessions (Mcp-Session-Id) are tracked here as
// a uuid-to-project_id map so multiple agents can share the same running
// server without stomping on each other's `current_project`.
//
// We deliberately keep the state object free of HTTP/OmniNet headers so it
// can be unit-tested in tests/test_mcp_server_state.cpp without spinning up a
// real server.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <apollocore/apollo_persistence.hpp>  // for ApolloCore

namespace apollo::mcp {

/// A session is just an opaque id (a UUID) plus the project it is "in".
/// Future: also store auth tokens here.
struct Session {
    std::string id;
    std::string project;                         // current project (or "")
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point last_used;
};

class ServerState {
public:
    ServerState();
    ~ServerState();

    ServerState(const ServerState&) = delete;
    ServerState& operator=(const ServerState&) = delete;

    /// Underlying ApolloCore access. Pointer is owned by ServerState.
    apollocore_v2::ApolloCore& core() { return *core_; }
    const apollocore_v2::ApolloCore& core() const { return *core_; }

    /// Open (or create) the knowledge base at `path`. Returns true on success.
    /// Mirrors `KB_MCP_OPEN_KB` tool semantics.
    bool open_kb(const std::string& path, const std::string& name);

    /// Get-or-create a session. MCP spec allows omitting session id; we
    /// generate one on first call when `mcp_session_id` is empty.
    std::string get_or_create_session(const std::string& mcp_session_id);

    /// Drop sessions idle longer than `idle_ttl`.
    void reap_idle_sessions(std::chrono::seconds idle_ttl);

    /// Search statistics for diagnostics.
    struct Stats {
        size_t live_sessions;
        std::string current_project;     // empty if none
        size_t document_count;
        size_t chunk_count;
        size_t entity_count;
        size_t relationship_count;
    };
    Stats snapshot() const;

    // -- Runtime config ------------------------------------------------
    //
    // Model paths can be changed while the server is running. Changes
    // are persisted to <config_path>/config.json so they survive restarts.
    // host/port/data_dir cannot be changed at runtime — those are bind
    // params and require a restart.

    /// Result of a set_models call. embed_loaded / llm_loaded are false if
    /// the path was set but the model failed to load (e.g. file missing or
    /// wrong format). *_error holds a human-readable reason on failure.
    struct ConfigResult {
        bool embed_loaded = false;
        bool llm_loaded = false;
        std::string embed_error;
        std::string llm_error;
        std::string embed_model;
        std::string llm_model;
    };

    /// Set model paths, hot-reload, and persist to disk.
    /// Empty `embed_path` / `llm_path` keeps the current value.
    ConfigResult set_models(const std::string& embed_path,
                            const std::string& llm_path);

    /// JSON snapshot of the runtime model config — used by /api/config
    /// and apollo://config.
    std::string config_json() const;

    /// Where config.json lives. Empty until set_data_dir() is called.
    const std::string& config_path() const { return config_path_; }

    /// Tell the state where the data dir is. Called once at startup,
    /// after the data-dir is known. Sets up the config.json path.
    void set_data_dir(const std::string& data_dir);

    /// Register an extra directory to search when a model path is a
    /// bare filename. Used at startup to add the install-path models
    /// directory and any other well-known location.
    void add_search_dir(const std::string& dir);

    /// Read <config_path> from disk and apply model paths. If the file
    /// exists and has valid JSON, both paths are loaded and the
    /// underlying models are hot-reloaded. Returns true on success.
    /// Missing file is not an error — returns false silently.
    bool load_config_from_disk();

    /// Read paths from <config_path> without touching the loaded models.
    /// Used at startup so main() can resolve "CLI override vs. config.json
    /// default" without a double-load. Out params empty if not set.
    bool read_config_paths(std::string& embed_out, std::string& llm_out) const;

    /// Record the resolved startup model paths into state without
    /// loading the models. Updates the in-memory paths so /api/config
    /// and apollo://config report them on a fresh start, even before
    /// main() has actually loaded them.
    void record_resolved_models(const std::string& embed_path,
                                const std::string& llm_path);

    /// Record the outcome of an embedding-model load attempt. Called
    /// by main() after it calls core().load_embedding_model().
    void report_embed_load(bool loaded, const std::string& error);

    /// Record the outcome of an LLM-model load attempt.
    void report_llm_load(bool loaded, const std::string& error);

private:
    std::unique_ptr<apollocore_v2::ApolloCore> core_;
    std::unordered_map<std::string, Session> sessions_;   // keyed by session id

    // Reentrant because resolve_model_path() and set_models() /
    // record_resolved_models() both take it. (Covers both sessions_
    // and the model config below.)
    mutable std::recursive_mutex mu_;

    // Runtime model config (under mu_).
    std::string embed_model_path_;
    std::string llm_model_path_;
    std::string embed_error_;
    std::string llm_error_;
    std::string config_path_;
    std::vector<std::string> extra_search_dirs_;

    /// Resolve a path that may be a bare filename. See .cpp for the
    /// full search order.
    std::string resolve_model_path(const std::string& input) const;

    static std::string make_uuid_v4();
};

}  // namespace apollo::mcp
