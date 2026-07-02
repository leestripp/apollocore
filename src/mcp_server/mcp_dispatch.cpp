// SPDX-License-Identifier: MIT
//
// mcp_dispatch.cpp — implementation of dispatch_one().

#include "mcp_dispatch.hpp"

#include <exception>

namespace apollo::mcp {

std::optional<nlohmann::json>
dispatch_one(Dispatcher& dispatcher,
             const nlohmann::json& raw_request,
             const std::string& session_id) {
    using nlohmann::json;

    // Parse the envelope. A bad parse is a JSON-RPC Parse Error
    // (no id, code -32700), per the spec.
    JsonRpcRequest req;
    try {
        req = parse_request(raw_request.dump());
    } catch (const std::exception& e) {
        return make_error(std::nullopt, errc::PARSE_ERROR,
                          std::string("parse error: ") + e.what()).to_json();
    }
    req.session_id = session_id;

    JsonRpcResponse resp;
    try {
        resp = dispatcher.dispatch(req);
    } catch (const std::exception& e) {
        // Dispatcher throws std::invalid_argument for bad params and
        // std::exception for internal failures. Both are reported as
        // JSON-RPC errors with the appropriate code.
        resp = make_error(req.id, errc::INTERNAL_ERROR, e.what());
    }

    // Notifications have no id and no result/error — the caller
    // (HTTP or stdio) MUST NOT write anything. Spec §Notifications.
    if (!resp.id && !resp.result && !resp.err) {
        return std::nullopt;
    }
    return resp.to_json();
}

}  // namespace apollo::mcp