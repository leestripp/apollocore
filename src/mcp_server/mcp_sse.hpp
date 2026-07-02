// SPDX-License-Identifier: MIT
//
// mcp_sse.hpp — Server-Sent Events bridge for GET /mcp
//
// Registers /mcp as an OmniNet "protocol switch" so that:
//   * GET /mcp with Accept: text/event-stream  → opens a long-lived
//     SSE stream and pumps server-initiated notifications
//   * GET /mcp with anything else              → 405 Method Not Allowed
//   * POST/DELETE /mcp                          → fall through to the
//     regular Router (handled by handle_post_mcp / handle_delete_mcp)
//
// The SSE writer thread:
//
//   1. Subscribes to the per-session McpSseBus queue.
//   2. Drains any notifications that piled up while the client was
//      disconnected.
//   3. Loops: pop with a 15s timeout. On a notification, write an
//      `event: message\ndata: <json>\n\n` frame. On timeout, write
//      a `: keepalive` comment. Stop on socket write failure (client
//      disconnected) or queue close (server shutdown).

#pragma once

#include "mcp_sse_bus.hpp"

#include "OmniNet.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "net/Socket.h"

#include <memory>
#include <string>

namespace onh = OmniNet::HTTP;
using onh::Request;
using onh::Response;

namespace apollo::mcp {

class McpSseBridge {
public:
    // `server` is the OmniNet HTTP server we register against.
    // `bus` is the per-server notification bus (lifetime is the
    //  HttpServer::Impl's). Both must outlive this object.
    McpSseBridge(onh::Server& server, McpSseBus& bus);

    // No copies — we hold a thread by handle.
    McpSseBridge(const McpSseBridge&) = delete;
    McpSseBridge& operator=(const McpSseBridge&) = delete;

    ~McpSseBridge();

    // For tests / /info: how many live SSE connections.
    size_t live_connection_count() const { return live_.load(); }

    // Resolves the Mcp-Session-Id header to a session string. Empty
    // string means "no session" (caller should 400). Override is
    // mostly for tests; default reads the header.
    using SessionResolver = std::function<std::string(const onh::Request&)>;
    void set_session_resolver(SessionResolver r) {
        session_resolver_ = std::move(r);
    }

private:
    // The two halves of `registerProtocolSwitch`.
    void on_upgrade(const onh::Request& req, onh::Response& resp);
    void on_post_upgrade(std::unique_ptr<OmniNet::Socket> socket,
                         const onh::Request& req);

    // The blocking loop that pumps events to the socket.
    void writer_loop(std::unique_ptr<OmniNet::Socket> socket,
                     std::string session_id,
                     std::shared_ptr<NotificationQueue> queue);

    McpSseBus& bus_;
    SessionResolver session_resolver_;
    std::atomic<size_t> live_{0};
};

}  // namespace apollo::mcp