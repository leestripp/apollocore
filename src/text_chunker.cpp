#include "apollocore/text_chunker.hpp"

#include <sstream>

namespace apollocore {

std::vector<std::string> chunk_text(const std::string& text, size_t chunk_size) {
    std::vector<std::string> chunks;
    std::string current;

    std::istringstream iss(text);
    std::string sentence;
    while (std::getline(iss, sentence, '.')) {
        if (!sentence.empty()) sentence += ".";

        // Sub-split oversize sentences. Without this, a single huge
        // "sentence" (minified JS, base64, dotless text) bypasses the
        // chunker entirely and crashes the embedder downstream.
        while (sentence.size() > chunk_size) {
            std::string piece = sentence.substr(0, chunk_size);
            sentence = sentence.substr(chunk_size);
            if (!current.empty()) {
                chunks.push_back(std::move(current));
                current.clear();
            }
            chunks.push_back(std::move(piece));
        }

        if (current.size() + sentence.size() > chunk_size) {
            if (!current.empty()) {
                chunks.push_back(std::move(current));
            }
            current = std::move(sentence);
        } else {
            current += sentence;
        }
    }
    if (!current.empty()) {
        chunks.push_back(std::move(current));
    }

    // Fold trailing punctuation-only residue into the previous chunk
    // when there's room. Without this, a dot-terminated input like
    // "x"*1500+"." produces [500, 500, 500, 1] — a useless 1-char
    // trailing chunk that wastes an embedding round-trip and pollutes
    // search results.
    //
    // Tolerance is proportional to chunk_size so the fold doesn't
    // create oversized chunks for tiny chunk_sizes. cap = max(16,
    // chunk_size / 8). For chunk_size=512 that's max(16, 64) = 64
    // chars of headroom; for chunk_size=1 it's still 16 (so chunk
    // sizes of 1 can fold a 16-char punctuation burst into a single
    // chunk and stay sane).
    auto is_punct_only = [](const std::string& s) {
        return !s.empty() && s.find_first_not_of(".!?;") == std::string::npos;
    };
    const size_t fold_tolerance = chunk_size / 8 + 16;
    while (chunks.size() >= 2 && is_punct_only(chunks.back())) {
        const size_t last = chunks.back().size();
        const size_t prev = chunks[chunks.size() - 2].size();
        if (prev + last <= chunk_size + fold_tolerance) {
            chunks[chunks.size() - 2] += std::move(chunks.back());
            chunks.pop_back();
        } else {
            break;
        }
    }

    // If the input didn't end with '.', the chunker has added a
    // synthetic '.' to the last sentence (because std::getline
    // doesn't tell us whether the input ended on the delimiter or
    // ran out of input). Strip it so the round-trip is exact.
    if (!text.empty() && text.back() != '.' && !chunks.empty()
        && !chunks.back().empty() && chunks.back().back() == '.') {
        chunks.back().pop_back();
    }

    // Empty input → single empty chunk (keeps downstream
    // "embed every chunk" loops consistent).
    if (chunks.empty()) {
        chunks.emplace_back("");
    }
    return chunks;
}

} // namespace apollocore