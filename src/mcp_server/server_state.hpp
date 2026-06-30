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

private:
    std::unique_ptr<apollocore_v2::ApolloCore> core_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Session> sessions_;   // keyed by session id

    static std::string make_uuid_v4();
};

}  // namespace apollo::mcp
