// SPDX-License-Identifier: MIT
//
// mcp_stdio.hpp — stdio transport runner for ApolloCore MCP server.
//
// Reads newline-delimited JSON-RPC envelopes from stdin (one per line),
// runs them through the Dispatcher, writes responses to stdout (one
// per line). Logs go to stderr. Per MCP spec §Transports#stdio.

#pragma once

#include "dispatcher.hpp"
#include "server_state.hpp"

#include <iosfwd>

namespace apollo::mcp {

/// Run the stdio loop until EOF on stdin or fatal error.
/// Logs go to stderr; stdout is reserved for JSON-RPC frames.
void run_stdio_transport(Dispatcher& dispatcher,
                         ServerState& state);

}  // namespace apollo::mcp