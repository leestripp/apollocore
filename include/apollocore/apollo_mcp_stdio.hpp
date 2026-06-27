/**
 * ApolloCore MCP stdio Transport (Phase M2.14)
 *
 * JSON-RPC 2.0 over stdin/stdout, framed with Content-Length headers.
 * Standard MCP stdio transport — works with Claude Desktop, Continue.dev,
 * and any MCP-compatible client that spawns subprocesses.
 *
 * Wire format (per MCP spec):
 *
 *   Content-Length: 123\r\n
 *   \r\n
 *   {"jsonrpc":"2.0","id":1,"method":"tools/list",...}
 *
 * Responses written back to stdout in the same format.
 * All logs/diagnostics go to stderr so they don't pollute the JSON channel.
 */

#pragma once

#include "apollocore/apollo_persistence.hpp"

namespace apollocore_v2::mcp {

/**
 * Run the MCP stdio server on stdin/stdout.
 *
 * Blocks until EOF on stdin or a fatal error.
 * Self-contained: creates its own ToolRegistry, no global state.
 *
 * @param kb ApolloCore instance with models loaded
 * @return exit code (0 = clean shutdown)
 */
int run_stdio_server(ApolloCore& kb);

} // namespace apollocore_v2::mcp