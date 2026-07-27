// SPDX-License-Identifier: MIT
//
// mcp_security.cpp — see mcp_security.hpp.

#include "mcp_security.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace apollo::mcp::security {

namespace {

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Extract the host portion from a full origin URL. We don't pull in a
// URL parser for this — the format is strict enough (scheme://host[:port][/path])
// that we can hand-roll it. Bracketed IPv6 literals (`[::1]`) are matched
// up to and including the closing bracket so the port separator isn't
// confused with the colons inside the address.
std::string_view host_of_origin(std::string_view origin)
{
    auto scheme_end = origin.find("://");
    if (scheme_end == std::string_view::npos) return {};
    auto host_start = scheme_end + 3;
    if (host_start < origin.size() && origin[host_start] == '[') {
        auto bracket_end = origin.find(']', host_start);
        if (bracket_end == std::string_view::npos) return {};
        return origin.substr(host_start, bracket_end - host_start + 1);
    }
    auto host_end   = origin.find_first_of(":/", host_start);
    if (host_end == std::string_view::npos) host_end = origin.size();
    return origin.substr(host_start, host_end - host_start);
}

bool is_loopback_host(std::string_view host)
{
    return iequals(host, "localhost")
        || iequals(host, "127.0.0.1")
        || iequals(host, "[::1]")
        || iequals(host, "::1");
}

}  // namespace

// ─── Origin ───────────────────────────────────────────────────────────────

bool is_valid_origin(std::optional<std::string_view> origin,
                     const std::vector<std::string>& allowed_origins)
{
    // Empty Origin: most CLI/server-to-server callers omit it. Allowed.
    if (!origin || origin->empty()) return true;

    const std::string_view o = *origin;

    // Loopback origins are always trusted (browser dev pages, local Web UI).
    if (is_loopback_host(host_of_origin(o))) return true;

    // Allowlist match — exact-origin comparison, case-insensitive on host.
    for (const auto& allowed : allowed_origins) {
        if (iequals(o, allowed)) return true;
    }

    return false;
}

std::vector<std::string> parse_allowed_origins_env()
{
    const char* raw = std::getenv("APOLLO_ALLOWED_ORIGINS");
    if (!raw || !*raw) return {};

    std::vector<std::string> out;
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace.
        auto begin = item.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) continue;
        auto end = item.find_last_not_of(" \t\r\n");
        out.emplace_back(item.substr(begin, end - begin + 1));
    }
    return out;
}

// ─── Protocol version ─────────────────────────────────────────────────────

const std::vector<std::string>& supported_protocol_versions()
{
    // Order matters only for diagnostic messages; equality check is set-based.
    static const std::vector<std::string> v = {
        "2025-03-26",
        "2025-06-18",
        "2025-11-25",
    };
    return v;
}

bool is_supported_protocol_version(std::string_view version)
{
    if (version.empty()) return false;
    const auto& set = supported_protocol_versions();
    return std::any_of(set.begin(), set.end(),
                       [&](const std::string& v) { return v == version; });
}

// ─── Bearer token ─────────────────────────────────────────────────────────

bool is_auth_enabled()
{
    const char* t = std::getenv("APOLLO_AUTH_TOKEN");
    return t && *t;
}

bool is_valid_bearer(std::optional<std::string_view> authorization_header)
{
    if (!is_auth_enabled()) return true;  // auth disabled — open access

    if (!authorization_header || authorization_header->empty()) return false;

    const std::string_view h = *authorization_header;

    // Expect "Bearer <token>" (case-insensitive scheme).
    constexpr std::string_view PREFIX = "Bearer ";
    if (h.size() <= PREFIX.size()) return false;
    if (!iequals(h.substr(0, PREFIX.size()), PREFIX)) return false;

    const std::string_view token = h.substr(PREFIX.size());
    // Trim leading whitespace in the token portion (some clients do
    // `Bearer  <token>` with two spaces).
    auto b = token.find_first_not_of(' ');
    if (b == std::string_view::npos) return false;
    auto e = token.find_last_not_of(' ');
    const std::string_view trimmed = token.substr(b, e - b + 1);

    const char* expected = std::getenv("APOLLO_AUTH_TOKEN");
    if (!expected) return false;
    return std::string_view(expected) == trimmed;
}

}  // namespace apollo::mcp::security