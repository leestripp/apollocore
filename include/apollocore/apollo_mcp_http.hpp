/**
 * ApolloCore MCP HTTP Transport (Phase M1.16)
 *
 * MCP (Model Context Protocol) JSON-RPC 2.0 over HTTP.
 * This is a thin layer on top of the same ApolloCore operations
 * that the REST API uses.
 *
 * MCP Methods exposed:
 *   initialize         - Server handshake
 *   tools/list         - List 8 available tools
 *   tools/call         - Invoke a tool (delegates to ApolloCore)
 *   resources/list     - List 3 KB resources
 *   resources/read     - Read a resource by URI
 *
 * Transport:
 *   POST /mcp            - JSON-RPC 2.0 requests
 *   GET  /mcp/sse        - Server-Sent Events stream (placeholder)
 */

#pragma once

#include "OmniNet.h"

namespace apollocore_v2::mcp {

/**
 * Register MCP JSON-RPC routes on the given OmniNet HTTP server.
 *
 * Routes:
 *   POST /mcp        - JSON-RPC 2.0 requests
 *   GET  /mcp/sse    - SSE endpoint (placeholder for streaming)
 */
void register_routes(OmniNet::HTTP::Server& server);

} // namespace apollocore_v2::mcp