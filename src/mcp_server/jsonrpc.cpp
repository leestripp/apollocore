// SPDX-License-Identifier: MIT
//
// jsonrpc.cpp — JSON-RPC 2.0 envelope parsing and serialization

#include "jsonrpc.hpp"

#include <sstream>
#include <stdexcept>

namespace apollo::mcp {

// ---------------------------------------------------------------------------
// JsonRpcId
// ---------------------------------------------------------------------------

JsonRpcId JsonRpcId::from_json(const json& j)
{
    JsonRpcId id;
    if (j.is_null())             return id;       // monostate
    if (j.is_string())           { id.value = j.get<std::string>(); return id; }
    if (j.is_number_integer())   { id.value = j.get<int64_t>();     return id; }
    // Spec says only string, number, or null. Anything else is invalid.
    throw std::runtime_error("JSON-RPC id must be string, integer, or null");
}

json JsonRpcId::to_json() const
{
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)    return nullptr;
        else if constexpr (std::is_same_v<T, int64_t>)      return v;
        else if constexpr (std::is_same_v<T, std::string>)  return v;
    }, value);
}

std::string JsonRpcId::to_string() const
{
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)   return "null";
        else if constexpr (std::is_same_v<T, int64_t>)     return std::to_string(v);
        else if constexpr (std::is_same_v<T, std::string>) return v;
    }, value);
}

// ---------------------------------------------------------------------------
// JsonRpcError
// ---------------------------------------------------------------------------

json JsonRpcError::to_json() const
{
    json j = {{"code", code}, {"message", message}};
    if (!data.is_null()) j["data"] = data;
    return j;
}

// ---------------------------------------------------------------------------
// JsonRpcResponse
// ---------------------------------------------------------------------------

json JsonRpcResponse::to_json() const
{
    json j = {{"jsonrpc", "2.0"}};
    if (id)            j["id"]     = id->to_json();
    if (result)        j["result"] = *result;
    if (err)           j["error"]  = err->to_json();
    return j;
}

// ---------------------------------------------------------------------------
// parse_request
// ---------------------------------------------------------------------------

JsonRpcRequest parse_request(const std::string& body)
{
    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("parse error: ") + e.what());
    }
    if (!j.is_object()) {
        throw std::runtime_error("invalid request: top-level must be object");
    }
    if (j.value("jsonrpc", std::string{}) != "2.0") {
        throw std::runtime_error("invalid request: jsonrpc must be \"2.0\"");
    }
    if (!j.contains("method") || !j["method"].is_string()) {
        throw std::runtime_error("invalid request: method must be a string");
    }

    JsonRpcRequest req;
    req.method = j["method"].get<std::string>();
    if (j.contains("id") && !j["id"].is_null()) {
        req.id = JsonRpcId::from_json(j["id"]);
    }
    if (j.contains("params")) {
        req.params = j["params"];
        if (!req.params.is_object() && !req.params.is_array()) {
            // MCP allows either object or array. Force object internally.
            req.params = json{{"_", req.params}};
        }
    }
    return req;
}

// ---------------------------------------------------------------------------
// builders
// ---------------------------------------------------------------------------

JsonRpcResponse make_result(const JsonRpcId& id, json result)
{
    JsonRpcResponse r;
    r.id = id;
    r.result = std::move(result);
    return r;
}

JsonRpcResponse make_error(const std::optional<JsonRpcId>& id,
                           int code,
                           const std::string& message,
                           json data)
{
    JsonRpcResponse r;
    r.id = id;
    JsonRpcError e;
    e.code = code;
    e.message = message;
    e.data = std::move(data);
    r.err = std::move(e);
    return r;
}

}  // namespace apollo::mcp