// SPDX-License-Identifier: MIT
//
// mcp_security.hpp — HTTP-level security helpers for MCP transports.
//
// Implements the spec MUSTs from
//   https://modelcontextprotocol.io/specification/2025-11-25/basic/transports#security-warning
// and the protocol-version header check from
//   basic/transports#protocol-version-header.
//
// All three helpers are pure (no I/O) so they're trivially unit-testable.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apollo::mcp::security {

// ─── Origin validation ────────────────────────────────────────────────────
//
// Per MCP spec §Transports#Security-Warning: "Servers MUST validate the
// `Origin` header on all incoming connections to prevent DNS rebinding
// attacks. If the `Origin` header is present and invalid, servers MUST
// respond with HTTP 403 Forbidden."
//
// Our policy:
//   * Empty Origin  → ALLOWED. CLI tools (curl), browser fetch() and many
//                     libraries don't set Origin.
//   * Loopback hosts (`127.0.0.1`, `[::1]`, `localhost`) at any port → ALLOWED.
//   * Exact match against `allowed_origins` list → ALLOWED.
//   * Anything else → DENIED.
//
// `allowed_origins` is a list of full origins the operator wants to trust
// (e.g. `https://app.example.com`). Empty list → no allowlist entries;
// only loopback + empty Origin allowed.

bool is_valid_origin(std::optional<std::string_view> origin,
                     const std::vector<std::string>& allowed_origins);

// Parse the `APOLLO_ALLOWED_ORIGINS` env var (comma-sep). Empty if unset.
std::vector<std::string> parse_allowed_origins_env();

// ─── MCP-Protocol-Version header validation ───────────────────────────────
//
// Per spec §Transports#Protocol-Version-Header: "If the server receives a
// request with an invalid or unsupported MCP-Protocol-Version, it MUST
// respond with `400 Bad Request`."
//
// Our policy:
//   * Header absent   → ALLOWED for the `initialize` request (the
//                         initialize response carries the version we'll use).
//   * Header present  → MUST match the negotiated version for this
//                         session, OR be one of our supported versions.
//                         Otherwise 400.
//   * Empty string     → treated as absent.

bool is_supported_protocol_version(std::string_view version);

// The single version we currently pin and advertise.
inline constexpr std::string_view PINNED_VERSION = "2025-11-25";

// The set of versions this server will accept in the protocol-version header.
// Anything outside this set → 400.
const std::vector<std::string>& supported_protocol_versions();

// ─── Bearer-token authentication (minimal) ──────────────────────────────
//
// Spec §Transports says HTTP implementations SHOULD support
// Authorization. We don't ship full OAuth 2.1 here (out of scope, see
// CodeMax.md §5), but we DO honor a static bearer token if
// `APOLLO_AUTH_TOKEN` is set in the environment. Empty env → no auth.
//
// Clients send `Authorization: Bearer <token>`. Anything else on the
// Authorization header is rejected.

bool is_auth_enabled();
bool is_valid_bearer(std::optional<std::string_view> authorization_header);

}  // namespace apollo::mcp::security