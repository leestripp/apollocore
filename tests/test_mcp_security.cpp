// SPDX-License-Identifier: MIT
//
// test_mcp_security.cpp — unit tests for the HTTP-level security helpers
// (Origin validation, MCP-Protocol-Version checks, bearer auth).
//
// All helpers under test are pure (no I/O); env-dependent cases set and
// restore the environment around themselves.

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mcp_security.hpp"

using namespace apollo::mcp::security;

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

namespace {

// RAII guard for environment variables so a failing assert can't leak
// state into the next case.
struct EnvGuard {
    explicit EnvGuard(const char* name) : name_(name) {
        const char* v = std::getenv(name);
        if (v) { had_value_ = true; old_value_ = v; }
    }
    ~EnvGuard() {
        if (had_value_) setenv(name_.c_str(), old_value_.c_str(), 1);
        else            unsetenv(name_.c_str());
    }
    std::string name_;
    bool had_value_ = false;
    std::string old_value_;
};

}  // namespace

int main() {
    const std::vector<std::string> no_allowlist;

    // ── Origin: missing / empty is allowed ───────────────────────────────
    APOLLO_ASSERT(is_valid_origin(std::nullopt, no_allowlist),
                  "missing Origin must be allowed");
    APOLLO_ASSERT(is_valid_origin(std::string_view(""), no_allowlist),
                  "empty Origin must be allowed");

    // ── Origin: loopback hosts allowed, any scheme/port ─────────────────
    for (std::string_view o : {
            "http://localhost", "http://localhost:3000",
            "https://localhost:8443", "http://127.0.0.1",
            "http://127.0.0.1:8765", "http://[::1]:8765",
            "http://[::1]", "http://LOCALHOST:9999"}) {
        APOLLO_ASSERT(is_valid_origin(o, no_allowlist),
                      "loopback origin must be allowed: " << o);
    }

    // ── Origin: non-loopback rejected without allowlist ──────────────────
    for (std::string_view o : {
            "http://evil.example.com", "https://attacker.io:443",
            "http://192.168.1.50:8080", "http://localhost.evil.com"}) {
        APOLLO_ASSERT(!is_valid_origin(o, no_allowlist),
                      "non-loopback origin must be rejected: " << o);
    }

    // ── Origin: explicit allowlist entries pass (case-insensitive) ───────
    const std::vector<std::string> allowlist = {"https://app.example.com"};
    APOLLO_ASSERT(is_valid_origin("https://app.example.com", allowlist),
                  "allowlisted origin must pass");
    APOLLO_ASSERT(is_valid_origin("https://APP.EXAMPLE.COM", allowlist),
                  "allowlist match is case-insensitive");
    APOLLO_ASSERT(!is_valid_origin("https://other.example.com", allowlist),
                  "non-allowlisted origin must still fail");

    // ── parse_allowed_origins_env ────────────────────────────────────────
    {
        EnvGuard g("APOLLO_ALLOWED_ORIGINS");
        unsetenv("APOLLO_ALLOWED_ORIGINS");
        APOLLO_ASSERT(parse_allowed_origins_env().empty(),
                      "unset env -> empty list");
        setenv("APOLLO_ALLOWED_ORIGINS", "", 1);
        APOLLO_ASSERT(parse_allowed_origins_env().empty(),
                      "empty env -> empty list");
        setenv("APOLLO_ALLOWED_ORIGINS", "https://a.example.com", 1);
        auto one = parse_allowed_origins_env();
        APOLLO_ASSERT(one.size() == 1 && one[0] == "https://a.example.com",
                      "single entry parsed");
        setenv("APOLLO_ALLOWED_ORIGINS",
               " https://a.example.com , https://b.example.com,,  ", 1);
        auto many = parse_allowed_origins_env();
        APOLLO_ASSERT(many.size() == 2 &&
                      many[0] == "https://a.example.com" &&
                      many[1] == "https://b.example.com",
                      "comma list trimmed, blanks dropped");
    }

    // ── Protocol version ─────────────────────────────────────────────────
    APOLLO_ASSERT(PINNED_VERSION == "2025-11-25", "pinned version");
    APOLLO_ASSERT(is_supported_protocol_version("2025-11-25"),
                  "pinned version must be supported");
    APOLLO_ASSERT(is_supported_protocol_version("2025-06-18"),
                  "2025-06-18 supported");
    APOLLO_ASSERT(is_supported_protocol_version("2025-03-26"),
                  "2025-03-26 supported");
    for (std::string_view v : {"", "2024-11-05", "garbage", "2025-11-25 "}) {
        APOLLO_ASSERT(!is_supported_protocol_version(v),
                      "unsupported version rejected: '" << v << "'");
    }
    APOLLO_ASSERT(supported_protocol_versions().size() == 3,
                  "exactly three supported versions advertised");

    // ── Bearer auth: disabled when APOLLO_AUTH_TOKEN unset ──────────────
    {
        EnvGuard g("APOLLO_AUTH_TOKEN");
        unsetenv("APOLLO_AUTH_TOKEN");
        APOLLO_ASSERT(!is_auth_enabled(), "auth disabled when token unset");
        APOLLO_ASSERT(is_valid_bearer(std::nullopt),
                      "no header passes when auth disabled");
        APOLLO_ASSERT(is_valid_bearer("Bearer anything"),
                      "any header passes when auth disabled");
    }

    // ── Bearer auth: enabled when APOLLO_AUTH_TOKEN set ──────────────────
    {
        EnvGuard g("APOLLO_AUTH_TOKEN");
        setenv("APOLLO_AUTH_TOKEN", "s3cret-token", 1);
        APOLLO_ASSERT(is_auth_enabled(), "auth enabled when token set");
        APOLLO_ASSERT(!is_valid_bearer(std::nullopt),
                      "missing header rejected");
        APOLLO_ASSERT(!is_valid_bearer(std::string_view("")),
                      "empty header rejected");
        APOLLO_ASSERT(!is_valid_bearer("Bearer wrong-token"),
                      "wrong token rejected");
        APOLLO_ASSERT(!is_valid_bearer("Basic s3cret-token"),
                      "non-Bearer scheme rejected");
        APOLLO_ASSERT(!is_valid_bearer("Bearer"),
                      "scheme without token rejected");
        APOLLO_ASSERT(is_valid_bearer("Bearer s3cret-token"),
                      "correct token accepted");
        APOLLO_ASSERT(is_valid_bearer("bearer s3cret-token"),
                      "scheme is case-insensitive");
        APOLLO_ASSERT(is_valid_bearer("Bearer  s3cret-token "),
                      "extra whitespace around token tolerated");
    }

    std::cout << "ALL MCP SECURITY TESTS PASSED\n";
    return 0;
}
