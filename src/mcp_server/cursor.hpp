// SPDX-License-Identifier: MIT
//
// cursor.hpp — opaque pagination cursor for MCP list methods.
//
// The MCP spec §8.5 requires "Respect cursor / pagination on resources/list,
// tools/list, prompts/list." The cursor is opaque to the client — the server
// decides how to encode it. We use base64(uint32_t) as a trivial opaque
// encoding (the index of the next item to return). Opaque keeps clients from
// depending on the format.
//
// Thread-safety: stateless, safe to call from multiple threads.
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace apollo::mcp::pagination {

/// Encode a numeric offset as an opaque cursor string.
/// Returns nullopt for offset==0 (means "start from the beginning").
std::optional<std::string> encode_cursor(uint32_t offset);

/// Decode an opaque cursor string back to a numeric offset.
/// Returns nullopt for any of:
///   - empty string (means "start from the beginning")
///   - "0" or other invalid base64
///   - decoded value > INT32_MAX (sanity)
std::optional<uint32_t> decode_cursor(const std::string& cursor);

}  // namespace apollo::mcp::pagination