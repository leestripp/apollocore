/**
 * ApolloCore MCP Errors (Phase M2.18)
 *
 * Standard MCP error codes + ApolloCore-specific extensions.
 * Every error has: code (int), message (human-readable),
 * data (structured context), retryable (bool).
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace apollocore_v2::mcp {

enum class ErrorCode : int {
    // JSON-RPC standard (per spec)
    ParseError     = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams  = -32602,
    InternalError  = -32603,

    // ApolloCore-specific (-32000 to -32099)
    KbNotFound         = -32001,
    KbAlreadyOpen      = -32002,
    ModelNotLoaded     = -32003,
    LlmTimeout         = -32004,
    JobNotFound        = -32005,
    JobCancelled       = -32006,
    ResourceNotFound   = -32007,
    PermissionDenied   = -32008,
    RateLimited        = -32009,
    InvalidPath        = -32010,
    ExtractionFailed   = -32011,
};

inline std::string code_to_string(ErrorCode c) {
    switch (c) {
        case ErrorCode::ParseError:         return "ParseError";
        case ErrorCode::InvalidRequest:     return "InvalidRequest";
        case ErrorCode::MethodNotFound:     return "MethodNotFound";
        case ErrorCode::InvalidParams:      return "InvalidParams";
        case ErrorCode::InternalError:      return "InternalError";
        case ErrorCode::KbNotFound:         return "KbNotFound";
        case ErrorCode::KbAlreadyOpen:      return "KbAlreadyOpen";
        case ErrorCode::ModelNotLoaded:     return "ModelNotLoaded";
        case ErrorCode::LlmTimeout:         return "LlmTimeout";
        case ErrorCode::JobNotFound:        return "JobNotFound";
        case ErrorCode::JobCancelled:       return "JobCancelled";
        case ErrorCode::ResourceNotFound:   return "ResourceNotFound";
        case ErrorCode::PermissionDenied:   return "PermissionDenied";
        case ErrorCode::RateLimited:        return "RateLimited";
        case ErrorCode::InvalidPath:        return "InvalidPath";
        case ErrorCode::ExtractionFailed:   return "ExtractionFailed";
    }
    return "Unknown";
}

inline bool is_retryable(ErrorCode c) {
    switch (c) {
        case ErrorCode::LlmTimeout:
        case ErrorCode::RateLimited:
        case ErrorCode::InternalError:
            return true;
        default:
            return false;
    }
}

/**
 * Build a JSON-RPC error envelope.
 *
 *   {
 *     "jsonrpc": "2.0",
 *     "id": <id>,
 *     "error": {
 *       "code": -32001,
 *       "message": "KbNotFound: data/foo not found",
 *       "data": {"retryable": false, "context": {...}}
 *     }
 *   }
 */
inline nlohmann::json make_error(
    const nlohmann::json& id,
    ErrorCode code,
    const std::string& message,
    const nlohmann::json& context = nlohmann::json::object())
{
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", static_cast<int>(code)},
            {"message", code_to_string(code) + ": " + message},
            {"data", {
                {"retryable", is_retryable(code)},
                {"context", context}
            }}
        }}
    };
}

/**
 * Convenience: throw an exception that catches cleanly to an error response.
 */
struct McpError : public std::runtime_error {
    ErrorCode code;
    nlohmann::json context;

    McpError(ErrorCode c, const std::string& msg, nlohmann::json ctx = nlohmann::json::object())
        : std::runtime_error(msg), code(c), context(std::move(ctx)) {}
};

} // namespace apollocore_v2::mcp