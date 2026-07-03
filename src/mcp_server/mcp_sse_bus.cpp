// SPDX-License-Identifier: MIT
//
// mcp_sse_bus.cpp — implementation of McpSseBus and NotificationQueue.

#include "mcp_sse_bus.hpp"

#include <algorithm>
#include <utility>

namespace apollo::mcp {

// ---------------------------------------------------------------------------
// NotificationQueue
// ---------------------------------------------------------------------------

std::optional<json> NotificationQueue::pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mtx_);
    if (q_.empty()) {
        // Wait until something arrives, the queue is closed, or the
        // timeout fires. cv_.wait_for returns false on timeout.
        cv_.wait_for(lk, timeout, [this] {
            return !q_.empty() || closed_.load();
        });
        if (q_.empty()) {
            // Either timeout (return nullopt) or closed (return nullopt).
            return std::nullopt;
        }
    }
    json item = std::move(q_.front());
    q_.pop_front();
    return item;
}

std::vector<json> NotificationQueue::drain() {
    std::vector<json> out;
    std::lock_guard<std::mutex> lk(mtx_);
    out.reserve(q_.size());
    while (!q_.empty()) {
        out.push_back(std::move(q_.front()));
        q_.pop_front();
    }
    return out;
}

void NotificationQueue::close() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
    }
    closed_.store(true);
    cv_.notify_all();
}

// ---------------------------------------------------------------------------
// McpSseBus
// ---------------------------------------------------------------------------

McpSseBus::~McpSseBus() {
    // Close all queues so blocked poppers wake up and exit cleanly.
    std::lock_guard<std::mutex> lk(registry_mtx_);
    for (auto& [sid, sess] : sessions_) {
        if (sess && sess->queue) sess->queue->close();
    }
}

void McpSseBus::push(const std::string& session_id, const json& notification) {
    // Find the session under the registry lock; release before
    // touching the queue to keep contention low.
    std::shared_ptr<NotificationQueue> q;
    {
        std::lock_guard<std::mutex> lk(registry_mtx_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return;
        q = it->second->queue;
    }
    if (!q) return;
    {
        std::lock_guard<std::mutex> qlk(q->mtx_);
        q->q_.push_back(notification);
        // Cap the queue at 256 to bound memory if no one is reading.
        while (q->q_.size() > 256) q->q_.pop_front();
    }
    q->cv_.notify_one();
}

std::shared_ptr<NotificationQueue> McpSseBus::subscribe(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(registry_mtx_);
    auto& slot = sessions_[session_id];
    if (!slot) {
        slot = std::make_unique<Session>();
        slot->queue = std::make_shared<NotificationQueue>();
    }
    slot->ref_count++;
    return slot->queue;
}

void McpSseBus::unsubscribe(const std::string& session_id,
                            std::shared_ptr<NotificationQueue> q) {
    std::unique_lock<std::mutex> lk(registry_mtx_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    auto& sess = it->second;
    if (sess->ref_count > 0) sess->ref_count--;
    // Only destroy the session if no one is subscribed AND the queue
    // is exhausted (no pending writes). We don't try to be clever —
    // leaving a dead session in the map is fine; it costs ~64 bytes
    // and the next push() is a no-op.
    lk.unlock();
    // Best-effort: if this was the last subscriber, signal close so
    // any blocking popper (e.g. the writer for the just-disconnected
    // client) returns immediately.
    if (q) q->close();
}

size_t McpSseBus::subscriber_count() const {
    std::lock_guard<std::mutex> lk(registry_mtx_);
    size_t n = 0;
    for (const auto& [_, sess] : sessions_) {
        n += sess->ref_count;
    }
    return n;
}

McpSseBus::Stats McpSseBus::stats() const {
    std::lock_guard<std::mutex> lk(registry_mtx_);
    Stats s;
    for (const auto& [_, sess] : sessions_) {
        s.sessions++;
        s.subscribers += sess->ref_count;
    }
    return s;
}

std::vector<std::string> McpSseBus::session_ids() const {
    std::lock_guard<std::mutex> lk(registry_mtx_);
    std::vector<std::string> out;
    out.reserve(sessions_.size());
    for (const auto& [sid, sess] : sessions_) {
        if (sess && sess->ref_count > 0) out.push_back(sid);
    }
    return out;
}

std::shared_ptr<NotificationQueue>
McpSseBus::queue_for(const std::string& session_id) const {
    std::lock_guard<std::mutex> lk(registry_mtx_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return nullptr;
    return it->second->queue;
}

}  // namespace apollo::mcp