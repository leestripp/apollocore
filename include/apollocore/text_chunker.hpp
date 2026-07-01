#pragma once

#include <string>
#include <vector>

namespace apollocore {

/**
 * chunk_text — split a document into chunks no larger than `chunk_size`
 * characters.
 *
 * Algorithm
 * ---------
 * 1. Split on '.' (sentence terminator). The resulting sentences are
 *    stored individually.
 * 2. Append sentences into `current_chunk` until adding the next one
 *    would exceed `chunk_size`. At that point flush `current_chunk`
 *    and start a new one.
 * 3. If a SINGLE sentence is longer than `chunk_size`, sub-split it
 *    into pieces of exactly `chunk_size` characters. Without this,
 *    minified JS / base64 / any dot-less content would flow through
 *    as a single oversized chunk and crash the embedder.
 * 4. After the loop, flush any remaining `current_chunk`.
 * 5. If the input is empty, the result has exactly one empty chunk
 *    (so downstream "embedding every chunk" loops still have
 *    something to iterate over).
 *
 * Properties guaranteed by this implementation:
 *
 * - All chunks have size <= chunk_size. (The "dotless oversize
 *   sentence" path is the reason this property holds.)
 * - The input's characters are preserved across chunks in order,
 *   i.e. concatenating all chunks yields the input (with '.' sentinels
 *   re-inserted by step 1).
 * - chunk_text never returns more chunks than chars / (chunk_size/2)
 *   would predict; in the worst case the input has chunk_size chars
 *   per chunk.
 *
 * @param text        The document text. May contain '\n', '\t', UTF-8
 *                    bytes — we only split on '.'.
 * @param chunk_size  Maximum size of a single chunk in characters.
 *                    Caller is expected to pass a value < embedder n_ctx.
 * @return Vector of chunks, each < chunk_size chars. Never empty for
 *         non-empty input. For empty input, returns {""}.
 */
std::vector<std::string> chunk_text(const std::string& text, size_t chunk_size);

} // namespace apollocore