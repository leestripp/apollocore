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
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <apollocore/apollo_persistence.hpp>  // for ApolloCore

namespace apollo::mcp {

/// A session is just an opaque id (a UUID) plus the project it is "in".
/// Future: also store auth tokens here.
struct Session {
    std::string id;
    std::string project;                         // current project (or "")
    std::string negotiated_version;              // Phase B3 — set by `initialize`
    std::string log_level = "info";              // Phase D2 — set by `logging/setLevel`
    /// Phase D3 — cancellation token table. The dispatcher registers an
    /// entry per in-flight `tools/call` (keyed by request id string);
    /// `notifications/cancelled` flips the atomic to true; the tool
    /// handler checks it periodically. Removed when the tool returns.
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
        cancel_tokens;
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

    /// Look up an existing session by id. Returns the id if found,
/// empty string if unknown. Does NOT create anything. This is the
/// path the HTTP layer should use for everything except `initialize`
/// (where auto-create is the spec-allowed behavior).
std::string find_session(const std::string& mcp_session_id) const;

/// Get-or-create a session. If `mcp_session_id` is empty, a fresh
/// UUID is minted. If it's non-empty and known, that session is
/// returned. The behaviour on a *non-empty but unknown* id is
/// controlled by `auto_create_on_unknown` (default true in this
/// signature for backwards compatibility):
///   * true  → create a fresh session with that id (legacy / dev mode)
///   * false → return empty string (caller MUST respond 400 Bad Request)
///
/// Per MCP 2025-11-25 §Transports#Session-Management: "If the server
/// receives a request with an unknown session ID, the server SHOULD
/// respond with HTTP 400." We default to strict (auto_create=false)
/// in production and let dev users opt back into the auto-create
/// behaviour via APOLLO_AUTO_CREATE_SESSION=1.
std::string get_or_create_session(const std::string& mcp_session_id,
                                 bool auto_create_on_unknown = false);

    /// Drop sessions idle longer than `idle_ttl`.
    void reap_idle_sessions(std::chrono::seconds idle_ttl);

    /// Record the negotiated MCP protocol version for a session.
    /// Phase B3 — used to verify a client's protocol-version header on
    /// post-init requests. Falls back to the server's pinned version
    /// if the session is unknown.
    void set_negotiated_version(const std::string& session_id,
                                const std::string& version);

    /// Look up the negotiated version for a session, or the server's
    /// pinned default if unknown. Returns empty string only if the
    /// server's default is unset (shouldn't happen).
    std::string negotiated_version(const std::string& session_id) const;

    /// Phase D2 — record the client's preferred log level for a session
    /// (via `logging/setLevel`). Stored per-session so concurrent
    /// clients can have different verbosity. Currently advisory —
    /// our log output goes to stderr with the OmniNet logger level,
    /// not per-session. Wire `state_.session_log_level(sid)` into a
    /// real per-session sink in a future phase.
    void set_session_log_level(const std::string& session_id,
                               const std::string& level);

    /// Look up the session's log level (default "info"). Empty string
    /// if the session is unknown (caller should use the default).
    std::string session_log_level(const std::string& session_id) const;

    // ----- Phase D3 cancellation plumbing -----

    /// Register a cancel token for `request_id` under `session_id`.
    /// Returns a token the caller can read from the tool handler.
    /// If the request_id already has a token, the existing one is
    /// returned (the second tool call with the same id is unusual but
    /// we don't want to silently leak the first). If the session is
    /// unknown, returns nullptr.
    std::shared_ptr<std::atomic<bool>> register_cancel_token(
        const std::string& session_id,
        const std::string& request_id);

    /// Mark a registered cancel token as cancelled. Used by
    /// `notifications/cancelled`. Silently no-op if either id is
    /// unknown — the spec says cancellation is a best-effort hint.
    void cancel_request(const std::string& session_id,
                        const std::string& request_id);

    /// Remove a registered token. Called by the dispatcher after the
    /// tool handler returns (whether successfully, with error, or
    /// cancelled). Token destruction is the signal to the writer
    /// that the slot is freed.
    void clear_cancel_token(const std::string& session_id,
                            const std::string& request_id);

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

    /// Where the SQLite KB store lives. Set once at startup.
    const std::string& data_dir() const { return data_dir_; }

    /// Tell the state where the data dir is. Called once at startup,
    /// after the data-dir is known. Sets up the config.json path.
    /// Note: changing the data_dir at runtime needs a restart — the
    /// SQLite KB and embedding files are open at startup. The Settings
    /// page persists the user's choice to config.json and prompts the
    /// user to restart.
    void set_data_dir(const std::string& data_dir);

    /// Set the verbose logging flag at runtime. Hot-reloadable.
    void set_verbose(bool enabled);

    /// Whether verbose logging is currently on.
    bool verbose() const;

    /// Set the default search mode used by the Search tab when the user
    /// doesn't override per-query. One of: vector / hybrid / rerank / full.
    void set_default_search_mode(const std::string& mode);

    /// Current default search mode.
    std::string default_search_mode() const;

    /// Register an extra directory to search when a model path is a
    /// bare filename. Used at startup to add the install-path models
    /// directory and any other well-known location.
    void add_search_dir(const std::string& dir);

    // -- Models-dir scan ----------------------------------------------
    //
    // The user-facing flow is "pick a model from a dropdown", not
    // "type a path". ApolloCore scans one or more directories for
    // *.gguf files and exposes them through /api/models. The Settings
    // UI presents that as a filterable list.
    //
    // models_dir is RUNTIME-MUTABLE — change it from Settings, the
    // server rescans, the dropdown refreshes. data_dir is NOT.

    struct ModelInfo {
        std::string name;          // basename (e.g. "nomic-embed.gguf")
        std::string path;          // absolute path
        std::uint64_t size_bytes = 0;
        std::time_t mtime = 0;     // last-modified time
        std::string dir;           // parent directory (which models_dir it came from)
    };

    /// Set the list of directories to scan for *.gguf models.
    /// Replaces any previous list. Immediately rescans; the result is
    /// cached until set_models_dirs() or add_models_dir() is called
    /// again. Empty list is valid (no models dir; user must type paths).
    void set_models_dirs(const std::vector<std::string>& dirs);

    /// Add a directory to the models-dir list. Triggers a rescan.
    void add_models_dir(const std::string& dir);

    /// The current models-dir list.
    std::vector<std::string> models_dirs() const;

    /// Snapshot of the most recent scan result.
    std::vector<ModelInfo> list_models() const;

    /// Trigger a rescan of every models_dir and return the new list.
    /// Synchronous; safe to call at runtime.
    std::vector<ModelInfo> rescan_models();

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

    /// Phase A4 — terminate a session by id. Returns true if the session
    /// existed and was erased; false if it was already gone (so the
    /// HTTP layer can reply 404 per spec §Session-Management).
    bool end_session(const std::string& session_id);

#include <thread>
#include <atomic>

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
    std::string data_dir_;                            // mirror of --data-dir
    std::string default_search_mode_ = "vector";      // user-facing default
    bool        verbose_ = false;                     // verbose log toggle
    std::vector<std::string> extra_search_dirs_;
    std::vector<std::string> models_dirs_;            // scanned for *.gguf
    std::vector<ModelInfo>    models_cache_;          // last scan result
    /// Background thread: reaps idle sessions every 5 min.
    std::thread reaper_;
    std::atomic<bool> reaper_stop_{false};
    std::condition_variable_any reaper_cv_;
    std::mutex reaper_mu_;

    /// Resolve a path that may be a bare filename. See .cpp for the
    /// full search order.
    std::string resolve_model_path(const std::string& input) const;

    static std::string make_uuid_v4();
};

}  // namespace apollo::mcp
