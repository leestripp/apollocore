// SPDX-License-Identifier: MIT
//
// jsonrpc.hpp — JSON-RPC 2.0 envelope types for MCP dispatch
//
// Implements the wire format described in:
//   https://www.jsonrpc.org/specification
//   https://modelcontextprotocol.io/specification/2025-11-25
//
// We use nlohmann/json (already a transitive dependency of apollo_core and
// OmniNet). All parsing is permissive: extra fields are ignored; missing
// optional fields are tolerated.

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace apollo::mcp {

using nlohmann::json;

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 envelope
// ---------------------------------------------------------------------------

/// A request id may be a string, integer, or null. JsonRpcId is a tagged
/// variant so we round-trip the same type the client sent.
struct JsonRpcId {
    std::variant<std::monostate, int64_t, std::string> value;

    static JsonRpcId from_json(const json& j);
    json to_json() const;
    std::string to_string() const;   // for log messages
};

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 error codes
// ---------------------------------------------------------------------------

namespace errc {
    constexpr int PARSE_ERROR      = -32700;   // Invalid JSON was received
    constexpr int INVALID_REQUEST  = -32600;   // The JSON sent is not a valid Request object
    constexpr int METHOD_NOT_FOUND = -32601;   // The method does not exist or is not available
    constexpr int INVALID_PARAMS   = -32602;   // Invalid method parameter(s)
    constexpr int INTERNAL_ERROR   = -32603;   // Internal JSON-RPC error
    // MCP-defined errors (per MCP spec appendix):
    constexpr int MCP_TOOL_NOT_FOUND        = -32602; // (already covered by INVALID_PARAMS)
}

// ---------------------------------------------------------------------------
// A parsed JSON-RPC 2.0 request
// ---------------------------------------------------------------------------

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";   // always "2.0" per spec
    std::optional<JsonRpcId> id;   // present for "request"; absent for "notification"
    std::string method;           // e.g. "tools/list", "initialize"
    json params = json::object(); // method-specific

    /// True if this is a notification (no `id` field).
    bool is_notification() const { return !id.has_value(); }
};

// ---------------------------------------------------------------------------
// A JSON-RPC 2.0 response (or error)
// ---------------------------------------------------------------------------

struct JsonRpcError {
    int code = 0;
    std::string message;
    json data = nullptr;  // optional

    json to_json() const;
};

struct JsonRpcResponse {
    std::optional<JsonRpcId> id;      // echoes the request id (null id → null here)
    std::optional<json> result;       // set on success
    std::optional<JsonRpcError> err;  // set on protocol error
    // Notifications get neither result nor err — they get no response at all.

    json to_json() const;
};

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

/// Parse a JSON-RPC request from a raw JSON body.
/// Throws std::runtime_error on protocol errors (not on tool errors).
JsonRpcRequest parse_request(const std::string& body);

/// Build a successful JSON-RPC response.
JsonRpcResponse make_result(const JsonRpcId& id, json result);

/// Build a JSON-RPC error response.
JsonRpcResponse make_error(const std::optional<JsonRpcId>& id,
                           int code,
                           const std::string& message,
                           json data = nullptr);

}  // namespace apollo::mcp
