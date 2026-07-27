// SPDX-License-Identifier: MIT
//
// test_mcp_sse.cpp — unit tests for SSE wire framing (mcp_sse_frame.hpp)
// and the notification bus (mcp_sse_bus).
//
// The socket-facing half of mcp_sse.cpp is OmniNet glue and stays
// untested here (consistent with the other mcp tests bypassing OmniNet);
// everything that decides *what bytes go on the wire* is covered.

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

#include "mcp_sse_bus.hpp"
#include "mcp_sse_frame.hpp"

using nlohmann::json;
using apollo::mcp::McpSseBus;
using apollo::mcp::make_sse_comment;
using apollo::mcp::make_sse_event;
using apollo::mcp::make_sse_frame;

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

int main()
{
    // ── frame: data-only ─────────────────────────────────────────────────
    APOLLO_ASSERT(make_sse_frame("{}") == "data: {}\r\n\r\n",
                  "data frame format");
    APOLLO_ASSERT(make_sse_frame("") == "data: \r\n\r\n",
                  "empty payload frame");

    // ── comment (keep-alive) ─────────────────────────────────────────────
    APOLLO_ASSERT(make_sse_comment("keepalive") == ": keepalive\r\n\r\n",
                  "comment frame format");

    // ── typed event ──────────────────────────────────────────────────────
    APOLLO_ASSERT(make_sse_event("message", "{\"a\":1}") ==
                  "event: message\r\ndata: {\"a\":1}\r\n\r\n",
                  "event frame format");

    // ── every frame ends with the SSE blank-line terminator ─────────────
    for (const std::string& f : {make_sse_frame("x"), make_sse_comment("x"),
                                 make_sse_event("e", "x")}) {
        APOLLO_ASSERT(f.size() >= 4 && f.compare(f.size() - 4, 4, "\r\n\r\n") == 0,
                      "frame terminated by blank line: " << f);
    }

    // ── bus: subscribe → push → pop roundtrip ────────────────────────────
    {
        McpSseBus bus;
        auto q = bus.subscribe("sess-a");
        bus.push("sess-a", json{{"method", "notifications/tools/list_changed"}});
        auto got = q->pop(std::chrono::milliseconds(50));
        APOLLO_ASSERT(got.has_value(), "pushed notification pops");
        APOLLO_ASSERT((*got)["method"] == "notifications/tools/list_changed",
                      "payload preserved through the bus");
    }

    // ── bus: pop on empty queue times out ────────────────────────────────
    {
        McpSseBus bus;
        auto q = bus.subscribe("sess-b");
        auto got = q->pop(std::chrono::milliseconds(20));
        APOLLO_ASSERT(!got.has_value(), "empty queue pop times out");
    }

    // ── bus: sessions are isolated ───────────────────────────────────────
    {
        McpSseBus bus;
        auto qa = bus.subscribe("sess-c1");
        auto qb = bus.subscribe("sess-c2");
        bus.push("sess-c1", json{{"n", 1}});
        APOLLO_ASSERT(qa->pop(std::chrono::milliseconds(50)).has_value(),
                      "target session receives");
        APOLLO_ASSERT(!qb->pop(std::chrono::milliseconds(20)).has_value(),
                      "other session stays empty");
    }

    // ── bus: push with no subscriber is a no-op ──────────────────────────
    {
        McpSseBus bus;
        bus.push("nobody-home", json{{"n", 2}});  // must not crash or buffer
        APOLLO_ASSERT(bus.subscriber_count() == 0, "no subscribers registered");
    }

    // ── bus: drain flushes backlog, then is empty ────────────────────────
    {
        McpSseBus bus;
        auto q = bus.subscribe("sess-d");
        bus.push("sess-d", json{{"n", 1}});
        bus.push("sess-d", json{{"n", 2}});
        auto backlog = q->drain();
        APOLLO_ASSERT(backlog.size() == 2, "drain returns backlog");
        APOLLO_ASSERT(q->drain().empty(), "queue empty after drain");
    }

    // ── bus: two subscribers on one session share the broadcast ──────────
    {
        McpSseBus bus;
        auto q1 = bus.subscribe("sess-e");
        auto q2 = bus.subscribe("sess-e");
        APOLLO_ASSERT(q1 == q2, "same session shares one queue");
        auto st = bus.stats();
        APOLLO_ASSERT(st.sessions == 1 && st.subscribers == 2,
                      "stats count sessions and subscribers separately");
    }

    // ── bus: unsubscribe tears down the session when the last leaves ─────
    {
        McpSseBus bus;
        auto q1 = bus.subscribe("sess-f");
        auto q2 = bus.subscribe("sess-f");
        bus.unsubscribe("sess-f", q1);
        APOLLO_ASSERT(bus.subscriber_count() == 1, "one subscriber left");
        bus.unsubscribe("sess-f", q2);
        APOLLO_ASSERT(bus.subscriber_count() == 0, "session torn down");
        APOLLO_ASSERT(bus.session_ids().empty(), "no sessions remain");
    }

    // ── bus: close wakes a blocked pop ───────────────────────────────────
    {
        McpSseBus bus;
        auto q = bus.subscribe("sess-g");
        q->close();
        auto got = q->pop(std::chrono::seconds(30));
        APOLLO_ASSERT(!got.has_value(), "closed queue pop returns nullopt");
        APOLLO_ASSERT(q->closed(), "closed flag set");
    }

    // ── bus: queue_for resolves only subscribed sessions ─────────────────
    {
        McpSseBus bus;
        APOLLO_ASSERT(bus.queue_for("ghost") == nullptr,
                      "unknown session -> nullptr");
        auto q = bus.subscribe("sess-h");
        APOLLO_ASSERT(bus.queue_for("sess-h") == q, "subscribed session resolves");
    }

    std::cout << "ALL MCP SSE TESTS PASSED\n";
    return 0;
}
