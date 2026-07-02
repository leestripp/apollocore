// SPDX-License-Identifier: MIT
//
// mcp_sse_bus.hpp — per-session notification queue used by GET /mcp
//                   Server-Sent Events streaming (MCP spec
//                   §Transports#Streamable-HTTP and §Notifications).
//
// The bus is a thin in-process pub/sub:
//
//   * Anyone can `push(sid, json)` to enqueue a notification for a
//     session. Called from the Dispatcher / tool handlers when the
//     server wants to send a `notifications/...` message.
//
//   * The GET /mcp SSE writer thread calls `subscribe(sid)` to get
//     a `std::shared_ptr<NotificationQueue>`. It pops events from
//     the queue and writes them as SSE `event: message` frames.
//     When the client disconnects (socket write fails), the writer
//     calls `unsubscribe(sid, q)`.
//
// The bus is server-singleton (one instance per HttpServer::Impl).
// It uses one mutex per session (s_id → mutex map) so pushes and
// pops on different sessions don't contend.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace apollo::mcp {

using nlohmann::json;

class NotificationQueue {
public:
    // Block up to `timeout` for a notification. Returns nullopt on
    // timeout (caller should send a keep-alive SSE comment) or on
    // close (caller should exit the loop).
    std::optional<json> pop(std::chrono::milliseconds timeout);

    // Non-blocking drain — returns everything currently in the queue.
    // Used at SSE connect time to flush any events that piled up
    // while the client was offline.
    std::vector<json> drain();

    // Wake any blocked poppers and tell them to exit.
    void close();

    bool closed() const { return closed_.load(); }

private:
    friend class McpSseBus;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<json> q_;
    std::atomic<bool> closed_{false};
};

class McpSseBus {
public:
    McpSseBus() = default;
    ~McpSseBus();

    // No copies — the bus owns session-scoped state.
    McpSseBus(const McpSseBus&) = delete;
    McpSseBus& operator=(const McpSseBus&) = delete;

    // Enqueue a notification for the given session. No-op if no one
    // is subscribed to this session (we don't buffer for offline
    // clients beyond the queue's max size; the next SSE reconnect
    // re-fetches via drain()).
    void push(const std::string& session_id, const json& notification);

    // Register a new queue for a session. Idempotent: multiple
    // concurrent SSE connections on the same session all get the
    // same events (broadcast within a session).
    std::shared_ptr<NotificationQueue> subscribe(const std::string& session_id);

    // Drop a queue. If it was the last subscriber, the session's
    // queue is destroyed.
    void unsubscribe(const std::string& session_id, std::shared_ptr<NotificationQueue> q);

    // Number of live SSE subscriptions across all sessions.
    size_t subscriber_count() const;

    // Used by tests / /info to expose server stats.
    struct Stats {
        size_t sessions = 0;      // distinct sessions with at least one subscriber
        size_t subscribers = 0;   // total live SSE connections
    };
    Stats stats() const;

    // Snapshot of all session ids with at least one subscriber. Used by
    // Dispatcher::notify_list_changed(kind, "") to broadcast to everyone.
    std::vector<std::string> session_ids() const;

private:
    struct Session {
        std::shared_ptr<NotificationQueue> queue;  // shared by all subscribers
        size_t ref_count = 0;
    };

    mutable std::mutex registry_mtx_;
    std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;
};

}  // namespace apollo::mcp