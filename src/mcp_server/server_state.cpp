// SPDX-License-Identifier: MIT
//
// server_state.cpp — ServerState implementation
//
// Owns the single ApolloCore instance shared by all HTTP requests.

#include "server_state.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace apollo::mcp {

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

}  // namespace apollo::mcp