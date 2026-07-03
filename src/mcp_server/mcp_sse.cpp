// SPDX-License-Identifier: MIT
//
// mcp_sse.cpp — Server-Sent Events bridge for GET /mcp.

#include "mcp_sse.hpp"

#include <thread>
#include <utility>

namespace apollo::mcp {

namespace {

// Per-event keep-alive. SSE comments are lines beginning with ':'.
constexpr auto kKeepAlive = std::chrono::seconds(15);

// SSE write helper. Returns false on socket failure (caller closes).
bool write_sse_frame(OmniNet::Socket& sock, const std::string& data) {
    // Each frame is `data: <payload>\n\n` per the SSE spec. We use
    // a single `data:` line because each notification is a single
    // JSON object; multi-line events would need multiple data: lines.
    std::string frame;
    frame.reserve(data.size() + 8);
    frame.append("data: ");
    frame.append(data);
    frame.append("\r\n\r\n");
    std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    sock.sendAll(bytes);
    return sock.isOpen();
}

bool write_sse_comment(OmniNet::Socket& sock, const std::string& text) {
    std::string frame = ": " + text + "\r\n\r\n";
    std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    sock.sendAll(bytes);
    return sock.isOpen();
}

bool write_sse_event(OmniNet::Socket& sock, const std::string& event,
                     const std::string& data) {
    std::string frame;
    frame.reserve(event.size() + data.size() + 16);
    frame.append("event: ");
    frame.append(event);
    frame.append("\r\ndata: ");
    frame.append(data);
    frame.append("\r\n\r\n");
    std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
    sock.sendAll(bytes);
    return sock.isOpen();
}

// Throwing wrappers — the helper functions above don't catch the
// SocketException that OmniNet's Socket::send() throws when the
// client has gone away. Wrap each call in a try/catch so a single
// dead client can't take down the whole server. Returns false on any
// failure (broken pipe, connection reset, etc.) so the writer_loop
// can exit cleanly.
bool safe_write_sse_frame(OmniNet::Socket& sock, const std::string& data) {
    try { return write_sse_frame(sock, data); }
    catch (const std::exception&) { return false; }
}
bool safe_write_sse_comment(OmniNet::Socket& sock, const std::string& text) {
    try { return write_sse_comment(sock, text); }
    catch (const std::exception&) { return false; }
}
bool safe_write_sse_event(OmniNet::Socket& sock, const std::string& event,
                          const std::string& data) {
    try { return write_sse_event(sock, event, data); }
    catch (const std::exception&) { return false; }
}

}  // namespace

McpSseBridge::McpSseBridge(onh::Server& server, McpSseBus& bus)
    : bus_(bus) {
    // Default: read the standard Mcp-Session-Id header.
    session_resolver_ = [](const onh::Request& req) -> std::string {
        auto h = req.header("mcp-session-id");
        if (!h) return {};
        return std::string(*h);
    };

    server.registerProtocolSwitch("/mcp", {
        .on_upgrade = [this](const onh::Request& req, onh::Response& resp) {
            on_upgrade(req, resp);
        },
        .on_post_upgrade = [this](std::unique_ptr<OmniNet::Socket> socket,
                                  const onh::Request& req) {
            on_post_upgrade(std::move(socket), req);
        },
    });
}

McpSseBridge::~McpSseBridge() {
    // Bus destructor closes all queues; writer threads see close() on
    // their next pop() and exit. We don't join them explicitly — the
    // bus's destructor signals shutdown and the threads self-terminate.
}

void McpSseBridge::on_upgrade(const onh::Request& req, onh::Response& resp) {
    using onh::Method;
    using onh::Response;
    using onh::StatusCode;

    // 1. Only GET is interesting here. POST/DELETE must fall through
    //    to the regular Router (handle_post_mcp / handle_delete_mcp).
    //    We achieve that by *not* setting isProtocolSwitch — leaving
    //    the response as the default 200 OK, which is the same
    //    StatusCode::OK the Router checks for. The Router will then
    //    handle the request normally.
    if (req.method() != Method::GET) {
        // Leave resp untouched: status 200 OK, no body. The Router
        // will be called next and will route POST/DELETE correctly.
        return;
    }

    // 2. GET /mcp requires Accept: text/event-stream.
    auto accept = req.header("accept");
    if (!accept ||
        std::string(*accept).find("text/event-stream") == std::string::npos) {
        // Per spec: 405 Method Not Allowed, with a JSON-RPC error
        // body that has no `id`. We set 405 here (not a protocol
        // switch) so the Router doesn't try to override it; the
        // connection's send path will write whatever status we set.
        resp.setStatus(StatusCode::METHOD_NOT_ALLOWED);
        resp.setContentType("application/json");
        resp.setBody(R"({"error":"GET /mcp requires Accept: text/event-stream"})");
        return;
    }

    // 3. GET /mcp requires a known session.
    std::string session_id = session_resolver_(req);
    if (session_id.empty()) {
        resp.setStatus(StatusCode::BAD_REQUEST);
        resp.setContentType("application/json");
        resp.setBody(R"({"error":"MCP-Session-Id header required for GET /mcp"})");
        return;
    }

    // 4. Build the SSE response. Status 200 + event-stream headers
    //    + an initial `: connected` comment so the client knows the
    //    stream is alive even before any notification arrives.
    resp.setStatus(StatusCode::OK);
    resp.setContentType("text/event-stream");
    resp.setHeader(std::string_view("Cache-Control"), std::string_view("no-cache"));
    // Per spec §Transports#Streamable-HTTP, the GET /mcp SSE stream is
    // a *separate* logical channel from POSTs. We force
    // Connection: close so subsequent POSTs come on a fresh TCP
    // socket. Without this, the SDK's HTTP keep-alive reuses our
    // socket and the SDK's POST handler races the SSE writer's
    // keepalive writes — we'd send ": keepalive\r\n\r\n" in the
    // middle of the POST response body, corrupting the JSON-RPC
    // reply. With Connection: close, every SSE call is on its own
    // TCP and writes are serialized by the kernel.
    resp.setHeader(std::string_view("Connection"), std::string_view("close"));
    resp.setHeader(std::string_view("X-Accel-Buffering"), std::string_view("no"));  // disable nginx buffering
    // Initial SSE comment. Body must be non-empty (OmniNet's
    // Router::handle will downgrade 200+empty to 404 if it ever sees
    // this — it won't, because we markProtocolSwitch, but be safe).
    resp.setBody(": connected\r\n\r\n");
    resp.markProtocolSwitch();
}

void McpSseBridge::on_post_upgrade(std::unique_ptr<OmniNet::Socket> socket,
                                    const onh::Request& req) {
    // The 200 OK has already been sent over the wire by the
    // Connection. We now own the socket. The HTTP server's
    // protocol-switch wiring guarantees the request body has been
    // consumed; we are the only thing reading/writing this socket
    // from here on.
    std::string session_id = session_resolver_(req);
    if (session_id.empty() || !socket || !socket->isOpen()) {
        if (socket) socket->close();
        return;
    }

    auto queue = bus_.subscribe(session_id);
    live_.fetch_add(1);

    // Spawn the writer thread. We pass everything by value so the
    // thread doesn't depend on `this` after the request returns.
    std::thread([this, sock = std::move(socket),
                 sid = std::move(session_id),
                 q = std::move(queue)]() mutable {
        writer_loop(std::move(sock), std::move(sid), std::move(q));
    }).detach();
}

void McpSseBridge::writer_loop(std::unique_ptr<OmniNet::Socket> socket,
                                std::string session_id,
                                std::shared_ptr<NotificationQueue> queue) {
    // 1. Flush any events that piled up while the client was offline.
    //    These are real notifications, so each gets an `event: message`
    //    frame.
    for (auto& n : queue->drain()) {
        if (!safe_write_sse_frame(*socket, n.dump())) goto done;
    }

    // 2. Pump loop. Block on the queue with a 15s keep-alive. On
    //    timeout, send an SSE comment so proxies and the client
    //    know the connection is still healthy.
    while (!queue->closed()) {
        auto item = queue->pop(kKeepAlive);
        if (queue->closed()) break;
        if (!item) {
            // Timeout → keep-alive.
            if (!safe_write_sse_comment(*socket, "keepalive")) goto done;
            continue;
        }
        if (!safe_write_sse_event(*socket, "message", item->dump())) goto done;
    }

done:
    // Send a final close event so well-behaved clients can render a
    // "stream closed" UI state. Then drop the socket.
    if (socket && socket->isOpen()) {
        safe_write_sse_event(*socket, "close", "{\"reason\":\"server_close\"}");
        socket->close();
    }
    bus_.unsubscribe(session_id, queue);
    live_.fetch_sub(1);
}

}  // namespace apollo::mcp