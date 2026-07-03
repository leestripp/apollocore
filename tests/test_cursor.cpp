// SPDX-License-Identifier: MIT
//
// test_cursor.cpp — round-trip tests for opaque pagination cursors.
//
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "cursor.hpp"

using apollo::mcp::pagination::encode_cursor;
using apollo::mcp::pagination::decode_cursor;

#define APOLLO_ASSERT(cond, msg) do {                                   \
    if (!(cond)) {                                                     \
        std::cerr << "FAIL: " << msg << " (at "                       \
                  << __FILE__ << ":" << __LINE__ << ")\n";             \
        return 1;                                                      \
    }                                                                  \
} while (0)

int main() {
    (void)0;  // for symmetry

    // ── encode/decode roundtrip for several values ─────────────────────
    for (uint32_t v : {1u, 5u, 100u, 1234u, 99999u, 0xFFFFFFu, 0xFFFFFFFFu}) {
        auto enc = encode_cursor(v);
        if (v == 0) {
            APOLLO_ASSERT(!enc.has_value(), "offset 0 must encode as nullopt");
            continue;
        }
        APOLLO_ASSERT(enc.has_value(), "non-zero must encode to a string");
        APOLLO_ASSERT(enc->size() == 8, "cursor must be 8 chars (4 bytes b64)");

        auto dec = decode_cursor(*enc);
        APOLLO_ASSERT(dec.has_value(), "fresh cursor must decode");
        APOLLO_ASSERT(*dec == v, "roundtrip preserved");
    }

    // ── decode of empty / malformed → nullopt ─────────────────────────
    APOLLO_ASSERT(!decode_cursor("").has_value(),  "empty cursor -> nullopt");
    APOLLO_ASSERT(!decode_cursor("AAAA").has_value(), "short cursor -> nullopt");
    APOLLO_ASSERT(!decode_cursor("$$$$$$$$").has_value(), "non-base64 -> nullopt");

    // 8 chars without = padding is not a valid cursor in our format
    // (we always encode with 2 padding chars at the end).
    APOLLO_ASSERT(!decode_cursor("ABCDEFGH").has_value(),
                  "8 chars without = padding -> nullopt");

    // A real cursor round-trips.
    auto e1 = encode_cursor(42);
    APOLLO_ASSERT(e1.has_value(), "encode 42 -> some cursor");
    auto d1 = decode_cursor(*e1);
    APOLLO_ASSERT(d1.has_value() && *d1 == 42, "real cursor decodes back to 42");

    std::cout << "ALL CURSOR TESTS PASSED\n";
    return 0;
}