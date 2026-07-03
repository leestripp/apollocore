// SPDX-License-Identifier: MIT
//
// mcp_stdio.cpp — stdio transport runner.
//
// Per MCP spec §Transports#stdio:
//   * Server reads from stdin, writes to stdout.
//   * Frame format: newline-delimited JSON (LSP-style).
//   * Logs MUST go to stderr only — never to stdout.
//   * On EOF (stdin closed), the server exits cleanly.
//
// We use unbuffered std::getline + an explicit std::cout << ... << '\n'
// followed by std::cout.flush() so each response is on its own line
// and visible to the client immediately. C stdio is line-buffered
// when stdout is a terminal but block-buffered when it's a pipe
// (which is the usual stdio case), so the flush is mandatory.

#include "mcp_stdio.hpp"
#include "mcp_dispatch.hpp"
#include "mcp_sse_bus.hpp"

#include <iostream>
#include <string>

namespace apollo::mcp {

void run_stdio_transport(Dispatcher& dispatcher, ServerState&) {
    // Tell the world we're live. stderr only — never stdout.
    std::cerr << "[apollo_mcp_server] stdio transport ready "
                 "(MCP spec 2025-11-25)\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        // Tolerate trailing \r (Windows clients).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip empty / comment lines.
        if (line.empty() || line[0] == '#') continue;

        // Parse and dispatch. dispatch_one() handles parse errors
        // (returns a JSON-RPC parse-error object) and notifications
        // (returns nullopt).
        nlohmann::json raw;
        try {
            raw = nlohmann::json::parse(line);
        } catch (const std::exception& e) {
            // Couldn't parse as JSON. Per spec §Transports#stdio:
            // "If the input cannot be parsed as JSON, the server SHOULD
            // respond with a JSON-RPC parse error."
            nlohmann::json err_body = make_error(std::nullopt, errc::PARSE_ERROR,
                                       std::string("parse error: ") + e.what()).to_json();
            std::cout << err_body.dump() << '\n';
            std::cout.flush();
            continue;
        }

        auto resp = dispatch_one(dispatcher, raw, /*session_id=*/"");
        if (resp) {
            std::cout << resp->dump() << '\n';
            std::cout.flush();
        }
        // resp == nullopt → notification; spec says don't respond.
    }

    // EOF on stdin. Per spec §Transports#stdio:
    // "When the parent process closes stdin, the MCP server SHOULD
    // exit cleanly."
    std::cerr << "[apollo_mcp_server] stdin closed, exiting\n";
}

}  // namespace apollo::mcp