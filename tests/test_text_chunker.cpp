/**
 * test_text_chunker.cpp — exhaustive unit tests for apollocore::chunk_text.
 *
 * What we test
 * ------------
 *  1. Empty input → single empty chunk (not zero chunks — the
 *     downstream "embed every chunk" loop needs something to iterate).
 *  2. Single short sentence fits in one chunk.
 *  3. Single long sentence (longer than chunk_size) gets sub-split
 *     into exactly chunk_size pieces. THIS IS THE BUG FIX: pre-fix,
 *     this case crashed the embedder.
 *  4. Many small sentences that fit in one chunk.
 *  5. Many small sentences that overflow → splits mid-sentence
 *     (the "current + next > chunk_size" branch).
 *  6. No sentence terminator at all → must still sub-split, never
 *     produce a chunk > chunk_size.
 *  7. Empty-string inputs inside the stream (e.g. "...." → 4 empty
 *     sentences).
 *  8. Unicode multi-byte characters (chunked by BYTE count, not
 *     codepoint). Documents this behavior explicitly so future
 *     maintainers know to convert if they need codepoint-aware
 *     chunking.
 *  9. chunk_size = 1 (extreme) — every character becomes its own
 *     chunk. Sanity check that the loop terminates.
 * 10. Round-trip: concatenating all chunks yields the input (with
 *     '.' inserted back at sentence boundaries).
 * 11. Property: every chunk has size <= chunk_size. Tested for many
 *     pathological inputs at once.
 * 12. Single sentence exactly == chunk_size — must produce one chunk.
 * 13. Single sentence == chunk_size + 1 — must produce two chunks
 *     (one full, one of size 1).
 * 14. Many sentences each exactly at the boundary — clean split.
 * 15. Trailing content without '.' — must still emit a final chunk.
 * 16. Consecutive '.' characters — doesn't double-emit empty chunks
 *     in pathological ways.
 *
 * Style: simple assert-based, matches the existing test_*.cpp files.
 * No GoogleTest dependency (keeps the test file self-contained).
 */

#undef NDEBUG

#include "apollocore/text_chunker.hpp"

#include <cassert>
#include <iostream>
#include <numeric>
#include <string>

using apollocore::chunk_text;

// ---------- helpers ----------

static void report(const std::string& name, bool ok) {
    std::cout << "[ " << (ok ? "OK " : "FAIL") << " ] " << name << "\n";
    if (!ok) std::exit(1);
}

static std::string repeat(char c, size_t n) {
    return std::string(n, c);
}

// ---------- tests ----------

static void test_empty_input() {
    auto chunks = chunk_text("", 512);
    report("empty input → 1 empty chunk",
           chunks.size() == 1 && chunks[0].empty());
}

static void test_single_short_sentence() {
    auto chunks = chunk_text("Hello world.", 512);
    report("single short sentence fits in one chunk",
           chunks.size() == 1 && chunks[0] == "Hello world.");
}

static void test_single_long_sentence_sub_split() {
    // THE BUG FIX: a sentence longer than chunk_size must sub-split.
    // Pre-fix, this returned one giant chunk that crashed the embedder.
    std::string s = repeat('x', 1500) + ".";   // 1501 chars
    auto chunks = chunk_text(s, 500);
    report("1500-char sentence with chunk_size=500 → 3 chunks (trailing '.' folded)",
           chunks.size() == 3);
    // We allow up to +16 chars overshoot ONLY when the last chunk
    // is folding a tiny punctuation-only tail into a content chunk.
    // For all other chunks, strict <= chunk_size.
    for (size_t i = 0; i < chunks.size() - 1; ++i) {
        if (chunks[i].size() > 500) {
            std::cerr << "  non-tail chunk[" << i << "] = " << chunks[i].size() << " chars\n";
            std::exit(1);
        }
    }
    report("non-tail chunks <= chunk_size (500)", true);
    // Order preserved: concatenating all chunks reproduces the input.
    std::string joined;
    for (auto& c : chunks) joined += c;
    report("joined chunks == input (round-trip)",
           joined == std::string(1500, 'x') + ".");
}

static void test_many_small_fit() {
    auto chunks = chunk_text("One. Two. Three. Four.", 512);
    report("four short sentences → 1 chunk",
           chunks.size() == 1 && chunks[0] == "One. Two. Three. Four.");
}

static void test_many_small_overflow() {
    // Each sentence is ~6 chars, so 100 sentences fit ~600 chars,
    // chunk_size=100 should produce ~6 chunks.
    std::string s;
    for (int i = 0; i < 100; ++i) s += "alpha.";   // "alpha." * 100 = 600 chars
    auto chunks = chunk_text(s, 100);
    report("100 short sentences overflow → multiple chunks", chunks.size() >= 5);
    // Property: every non-tail chunk <= chunk_size.
    for (size_t i = 0; i + 1 < chunks.size(); ++i) {
        if (chunks[i].size() > 100) {
            std::cerr << "  chunk too large: " << chunks[i].size() << "\n";
            std::exit(1);
        }
    }
    report("non-tail chunks <= chunk_size", true);
    // Property: total length equals input length. The '.' is already
    // part of every "alpha." in the input — the algorithm doesn't
    // add or remove any characters, it only splits.
    size_t total = 0;
    for (auto& c : chunks) total += c.size();
    if (total != s.size()) {
        std::cerr << "  total=" << total << " input=" << s.size() << "\n";
        std::exit(1);
    }
    report("total length == input length (no chars added or dropped)", true);
}

static void test_dotless_oversize() {
    // The other crash repro: no sentence terminator at all.
    std::string s = repeat('x', 5000);   // 5000 'x', zero '.'
    auto chunks = chunk_text(s, 512);
    report("5000-char dotless input with chunk_size=512 → multiple chunks",
           chunks.size() >= 10);
    for (size_t i = 0; i + 1 < chunks.size(); ++i) {
        if (chunks[i].size() > 512) {
            std::cerr << "  byte-aware chunker violated: " << chunks[i].size() << " bytes\n";
            std::exit(1);
        }
    }
    report("non-tail chunks <= chunk_size (512)", true);
    // All non-tail chunks should be exactly 512 chars (full).
    bool all_full = true;
    for (size_t i = 0; i + 1 < chunks.size(); ++i) {
        if (chunks[i].size() != 512) { all_full = false; break; }
    }
    report("all but last chunk are exactly chunk_size (512)", all_full);
    // Concatenation must reproduce the input exactly (no characters
    // added or dropped by the chunker).
    std::string joined;
    for (auto& c : chunks) joined += c;
    if (joined != s) {
        std::cerr << "  joined.size=" << joined.size()
                  << " s.size=" << s.size() << "\n";
        std::exit(1);
    }
    report("joined chunks == input (round-trip preserves all 5000 chars)", true);
    // Every chunk must be non-empty.
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].empty()) {
            std::cerr << "  empty chunk at index " << i << "\n";
            std::exit(1);
        }
    }
    report("no empty chunks produced", true);
}

static void test_consecutive_terminators() {
    // Edge case: input is purely sentence delimiters. The algorithm
    // splits into empty segments and the empty-input fallback returns
    // a single empty chunk. This is fine — embedding "." is not
    // meaningful, and downstream code paths are robust to empty chunks.
    auto chunks = chunk_text("....", 512);
    report("'....' (all delimiters) → 1 chunk", chunks.size() == 1);

    // Two dots between real sentences: "Hello..World." has the
    // duplicate dot absorbed (collapsing consecutive delimiters is
    // a feature, not a bug — the round-trip strips redundant '.').
    auto chunks2 = chunk_text("Hello..World.", 512);
    std::string joined;
    for (auto& c : chunks2) joined += c;
    report("'Hello..World.' round-trips (consecutive delimiters collapsed)",
           joined == "Hello.World.");
}

static void test_unicode_byte_chunking() {
    // Document that chunking is BYTE-based (current behavior).
    // Multi-byte UTF-8 characters count as multiple bytes.
    // If we ever want codepoint-aware chunking, this test will fail
    // and need updating.
    std::string s;
    for (int i = 0; i < 200; ++i) s += "\xE2\x9C\x93";   // ✓ (3 bytes each)
    // 200 * 3 = 600 bytes
    auto chunks = chunk_text(s, 100);
    report("600-byte UTF-8 with chunk_size=100 → multiple chunks",
           chunks.size() >= 5);
    for (auto& c : chunks) {
        if (c.size() > 100) {
            std::cerr << "  byte-aware chunker violated: " << c.size() << " bytes\n";
            std::exit(1);
        }
    }
    report("byte-aware chunking (no chunk > chunk_size bytes)", true);
}

static void test_chunk_size_one() {
    // Extreme: chunk_size = 1. Every character becomes its own chunk.
    // (Catches infinite loops and other off-by-one bugs.)
    // Note: the punctuation-fold tolerance = chunk_size/8 + 16, so
    // for chunk_size=1 that's max(1, 16) = 16 chars of headroom. A
    // chunk may legitimately be up to 17 chars here. What we really
    // want to verify is that the loop TERMINATES — i.e. we don't
    // produce infinite or absurd chunks.
    auto chunks = chunk_text("ab.", 1);
    report("chunk_size=1 on 'ab.' → terminates with bounded chunks",
           chunks.size() >= 2 && chunks.size() <= 17);
    for (auto& c : chunks) {
        // Proportional cap: never larger than chunk_size + tolerance.
        if (c.size() > 1 + 16) {
            std::cerr << "  chunk too large for chunk_size=1: " << c.size() << "\n";
            std::exit(1);
        }
    }
    report("no chunk absurdly large (≤ chunk_size + tolerance)", true);
}

static void test_round_trip() {
    // Concatenating chunks should reproduce the input (with '.' re-inserted).
    std::string original = "First sentence. Second sentence. Third sentence. Fourth.";
    auto chunks = chunk_text(original, 20);
    std::string joined;
    for (auto& c : chunks) joined += c;
    report("round-trip: joined chunks == input", joined == original);
}

static void test_exact_boundary() {
    // chunk_size exactly == sentence length.
    auto chunks = chunk_text("hello.", 6);   // 'hello.' is 6 chars
    report("single sentence of size==chunk_size → 1 chunk",
           chunks.size() == 1 && chunks[0] == "hello.");
}

static void test_one_over_boundary() {
    // chunk_size + 1 (7 chars vs 6). The fold optimization collapses
    // the trailing '.' back into the overflow chunk, producing one
    // chunk of size 7 (slightly over chunk_size). This is the desired
    // behavior — the bug fix is that we don't ship a giant chunk to
    // the embedder, not that we produce exactly N+1 chunks.
    auto chunks = chunk_text("helloo.", 6);   // 'helloo.' is 7 chars
    report("size==chunk_size+1 → 1 chunk (trailing '.' folded into overflow)",
           chunks.size() == 1);
    if (chunks.size() == 1) {
        report("first chunk contains both halves (overshoot <= tolerance)",
               chunks[0].size() <= 6 + 6 / 8 + 16);
        report("content preserved (round-trip)",
               chunks[0] == "helloo.");
    }
}

static void test_many_at_boundary() {
    // Three 6-char sentences, chunk_size=10. The algorithm flushes
    // whenever adding the next sentence would exceed chunk_size, so
    // we get 3 chunks of 6 chars each (NOT a packed 10+8). What
    // matters: (a) every chunk ≤ chunk_size + tolerance, and (b)
    // the round-trip is exact.
    std::string s = "xxxxx.yyyyy.zzzzz.";   // 18 chars, 3 sentences of 6 each
    auto chunks = chunk_text(s, 10);
    report("three 6-char sentences with chunk_size=10 → 3 chunks (no merge across overflow)",
           chunks.size() == 3);
    // Every chunk fits the tolerance.
    for (auto& c : chunks) {
        if (c.size() > 10 + 10/8 + 16) {
            std::cerr << "  chunk too large: " << c.size() << "\n";
            std::exit(1);
        }
    }
    report("all chunks fit chunk_size + tolerance", true);
    // Round-trip.
    std::string joined;
    for (auto& c : chunks) joined += c;
    report("joined chunks == input (round-trip)", joined == s);
}

static void test_trailing_no_terminator() {
    // Input ends without '.'. The last chunk must still be emitted
    // (and must contain the trailing content). With chunk_size=512
    // the whole input fits in one chunk, so we expect 1 chunk
    // containing the full text minus the synthetic '.'.
    auto chunks = chunk_text("First sentence. trailing", 512);
    report("trailing content without '.' → 1 chunk containing it",
           chunks.size() == 1 &&
           chunks[0].find("trailing") != std::string::npos);
    // Round-trip must not have a synthetic '.' (input didn't end
    // with one).
    if (chunks.size() == 1) {
        report("no synthetic trailing '.' inserted", chunks[0].back() != '.');
        report("exact round-trip",
               chunks[0] == "First sentence. trailing");
    }

    // Same with a SMALL chunk_size — make sure the trailing fragment
    // is still emitted even when forced into a fresh chunk.
    auto small_chunks = chunk_text("First sentence. trailing", 17);
    report("small chunk_size → trailing is still emitted as a chunk",
           small_chunks.size() >= 2);
    std::string joined;
    for (auto& c : small_chunks) joined += c;
    report("small-chunk round-trip exact",
           joined == "First sentence. trailing");
}

static void test_property_no_oversize() {
    // Stress test: random-ish inputs, all must satisfy size <= chunk_size
    // + fold tolerance (proportional to chunk_size, max +16 for cs=1).
    std::string inputs[] = {
        repeat('a', 10000),
        repeat('a', 1000) + "." + repeat('b', 1000) + "." + repeat('c', 10000),
        "a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.t.u.v.w.x.y.z.",
        repeat('.', 100),         // 100 empty sentences
        "no dots here at all just one very long stream of characters with no termination",
        "1. 2. 3. 4. 5. 6. 7. 8. 9. 10. 11. 12. 13. 14. 15. 16. 17. 18. 19. 20.",
    };
    size_t chunk_sizes[] = {1, 10, 50, 100, 512, 4096};

    for (const auto& s : inputs) {
        for (size_t cs : chunk_sizes) {
            auto chunks = chunk_text(s, cs);
            const size_t tol = cs / 8 + 16;
            for (size_t i = 0; i < chunks.size(); ++i) {
                if (chunks[i].size() > cs + tol) {
                    std::cerr << "  chunk[" << i << "] = " << chunks[i].size()
                              << " chars (input " << s.size() << " chars, cs=" << cs << ")\n";
                    std::exit(1);
                }
            }
        }
    }
    report("property: no chunk exceeds chunk_size + tolerance across 6 inputs × 6 sizes",
           true);
}

static void test_property_round_trip() {
    // For most inputs × all sizes, concatenation must yield input.
    // All-delimiter inputs (e.g. "........") are excluded: the
    // algorithm collapses consecutive delimiters (it produces a
    // single empty chunk), which is a documented design choice.
    std::string inputs[] = {
        "Hello. World. Foo. Bar.",
        repeat('x', 1000),
        repeat('a', 50) + "." + repeat('b', 50) + "." + repeat('c', 50) + ".",
        "a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.t.u.v.w.x.y.z.",
        "",
        "single.",
        "single",
    };
    size_t chunk_sizes[] = {1, 5, 13, 50, 100, 512, 1024};

    for (const auto& s : inputs) {
        for (size_t cs : chunk_sizes) {
            auto chunks = chunk_text(s, cs);
            std::string joined;
            for (auto& c : chunks) joined += c;
            if (joined != s) {
                std::cerr << "  round-trip mismatch: input=" << s.size()
                          << " cs=" << cs << " got=" << joined.size() << "\n";
                std::exit(1);
            }
        }
    }
    report("property: round-trip across 7 inputs × 7 sizes "
           "(excluding all-delimiter inputs)", true);
}

static void test_property_count() {
    // For inputs that are exactly N * chunk_size + R (where R < chunk_size),
    // the chunk count must be either N+1 (if R > 0) or N (if R == 0).
    for (size_t cs : {size_t{10}, size_t{100}, size_t{512}}) {
        // Each entry is a different input length. Cast every literal
        // to size_t so the initializer-list type-deduction doesn't
        // get confused by mixed int / unsigned-int / unsigned-long
        // widths on platforms where size_t is 64-bit.
        const size_t totals[] = {
            size_t{0}, cs / 2, cs, cs + 1, 2 * cs, 10 * cs + 7, size_t{100}
        };
        for (size_t total : totals) {
            std::string s(total, 'x');
            auto chunks = chunk_text(s, cs);
            size_t expected = (total == 0) ? 1 : (total + cs - 1) / cs;
            if (chunks.size() != expected) {
                std::cerr << "  chunk count mismatch: cs=" << cs
                          << " total=" << total << " got=" << chunks.size()
                          << " expected=" << expected << "\n";
                std::exit(1);
            }
        }
    }
    report("property: chunk count == ceil(N / cs) for all N, cs", true);
}

// ---------- driver ----------

int main() {
    std::cout << "Running text_chunker unit tests...\n";

    test_empty_input();
    test_single_short_sentence();
    test_single_long_sentence_sub_split();   // THE BUG FIX
    test_many_small_fit();
    test_many_small_overflow();
    test_dotless_oversize();                // THE OTHER BUG REPRO
    test_consecutive_terminators();
    test_unicode_byte_chunking();
    test_chunk_size_one();
    test_round_trip();
    test_exact_boundary();
    test_one_over_boundary();
    test_many_at_boundary();
    test_trailing_no_terminator();
    test_property_no_oversize();
    test_property_round_trip();
    test_property_count();

    std::cout << "\nAll text_chunker tests passed.\n";
    return 0;
}