// SPDX-License-Identifier: MIT
//
// mcp_sse_frame.hpp — pure SSE wire-format builders.
//
// Extracted from mcp_sse.cpp so the framing logic can be unit-tested
// without an OmniNet socket. Each function builds one complete SSE frame
// (terminated by a blank line). We use CRLF line endings throughout; the
// SSE spec allows any of CR/LF/CRLF as long as usage is consistent.
//
// Note on `data:` lines: each notification is a single JSON object with
// no embedded newlines, so a single `data:` line per frame is correct.
// Multi-line payloads would need one `data:` line per line.

#pragma once

#include <string>
#include <string_view>

namespace apollo::mcp {

// `data: <payload>\r\n\r\n` — a plain message frame.
inline std::string make_sse_frame(std::string_view data) {
    std::string frame;
    frame.reserve(data.size() + 8);
    frame.append("data: ");
    frame.append(data);
    frame.append("\r\n\r\n");
    return frame;
}

// `: <text>\r\n\r\n` — a comment frame (used as keep-alive).
inline std::string make_sse_comment(std::string_view text) {
    std::string frame;
    frame.reserve(text.size() + 6);
    frame.append(": ");
    frame.append(text);
    frame.append("\r\n\r\n");
    return frame;
}

// `event: <event>\r\ndata: <payload>\r\n\r\n` — a typed event frame.
inline std::string make_sse_event(std::string_view event,
                                  std::string_view data) {
    std::string frame;
    frame.reserve(event.size() + data.size() + 16);
    frame.append("event: ");
    frame.append(event);
    frame.append("\r\ndata: ");
    frame.append(data);
    frame.append("\r\n\r\n");
    return frame;
}

}  // namespace apollo::mcp
