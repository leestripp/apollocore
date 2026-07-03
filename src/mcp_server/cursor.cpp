// SPDX-License-Identifier: MIT
//
// cursor.cpp — see cursor.hpp.
//
// Encoding: 4 bytes (big-endian uint32_t) → 8 base64 chars
//           (4 chars for first triple + 4 chars for last single byte + "==").
// We deliberately use a minimal in-tree base64 to avoid a new dependency
// for what amounts to ~20 lines of code.
//
#include "cursor.hpp"

#include <cstring>
#include <string_view>

namespace apollo::mcp::pagination {

namespace {

constexpr char kB64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::optional<std::string> encode_cursor(uint32_t offset) {
    if (offset == 0) return std::nullopt;
    // Big-endian: high byte first.
    const uint8_t b0 = (offset >> 24) & 0xFF;
    const uint8_t b1 = (offset >> 16) & 0xFF;
    const uint8_t b2 = (offset >>  8) & 0xFF;
    const uint8_t b3 =  offset        & 0xFF;
    // First triple (b0, b1, b2) → 4 chars
    // Last single (b3)          → 2 chars + "=="
    std::string out;
    out.reserve(8);
    out.push_back(kB64Alphabet[(b0 >> 2) & 0x3F]);
    out.push_back(kB64Alphabet[((b0 & 0x3) << 4) | ((b1 >> 4) & 0xF)]);
    out.push_back(kB64Alphabet[((b1 & 0xF) << 2) | ((b2 >> 6) & 0x3)]);
    out.push_back(kB64Alphabet[b2 & 0x3F]);
    out.push_back(kB64Alphabet[(b3 >> 2) & 0x3F]);
    out.push_back(kB64Alphabet[(b3 & 0x3) << 4]);
    out.push_back('=');
    out.push_back('=');
    return out;
}

std::optional<uint32_t> decode_cursor(const std::string& cursor) {
    if (cursor.empty()) return std::nullopt;
    if (cursor.size() != 8) return std::nullopt;
    // Expect the last two chars to be padding.
    if (cursor[6] != '=' || cursor[7] != '=') return std::nullopt;
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = b64_decode_char(cursor[i]);
        if (v[i] < 0) return std::nullopt;
    }
    // Reconstruct the 4 bytes from the 6 6-bit values.
    const uint8_t b0 = (uint8_t)((v[0] << 2) | (v[1] >> 4));
    const uint8_t b1 = (uint8_t)(((v[1] & 0xF) << 4) | (v[2] >> 2));
    const uint8_t b2 = (uint8_t)(((v[2] & 0x3) << 6) | v[3]);
    const uint8_t b3 = (uint8_t)((v[4] << 2) | (v[5] >> 4));
    return (uint32_t(b0) << 24) |
           (uint32_t(b1) << 16) |
           (uint32_t(b2) <<  8) |
            uint32_t(b3);
}

}  // namespace apollo::mcp::pagination