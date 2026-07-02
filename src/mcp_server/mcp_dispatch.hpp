// SPDX-License-Identifier: MIT
//
// mcp_dispatch.hpp — JSON-RPC dispatch helper shared by HTTP and stdio.
//
// `dispatch_one()` takes a raw JSON request envelope and runs it through
// the Dispatcher. It handles the parse-error / invalid-request cases
// and the notification-must-have-no-response case so the two
// transports (HTTP `handle_one` and stdio's line loop) can't drift.
//
// All HTTP-specific concerns (sessions, headers, security guards)
// stay in http_server.cpp; all stdio-specific concerns (line buffering,
// stdin handling) stay in main.cpp's stdio runner. This file is the
// minimum common surface.

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "dispatcher.hpp"

namespace apollo::mcp {

/// Run one JSON-RPC request envelope through `dispatcher`.
///
/// Returns:
///   - `nullopt` for notifications (caller MUST NOT write anything)
///   - a json object with `result` or `error` for ordinary requests
///   - a JSON-RPC parse-error object for malformed input (no `id`)
///
/// `session_id` is the MCP session id (header-derived for HTTP; empty
/// for stdio). Pass through to the Dispatcher so initialize can
/// record the negotiated version on the session.
std::optional<nlohmann::json>
dispatch_one(Dispatcher& dispatcher,
             const nlohmann::json& raw_request,
             const std::string& session_id);

}  // namespace apollo::mcp