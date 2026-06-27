/**
 * ApolloCore Codebase Ingestion (Phase M2.C)
 *
 * Walks a directory and ingests source files into the KB.
 * Each file becomes a "document" with a path-based ID.
 *
 * Designed for me (CodeMax) to ingest codebases.
 */

#pragma once

#include "apollocore/apollo_persistence.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace apollocore_v2 {

/**
 * Ingest a directory of source files into the KB.
 *
 * @param kb ApolloCore instance
 * @param path Directory path to walk (relative or absolute)
 * @param extensions File extensions to include (e.g. {".cpp", ".hpp"})
 * @param max_file_size Skip files larger than this (bytes, default 1MB)
 * @param recursive Walk subdirectories (default true)
 * @return JSON: {files_processed, files_skipped, total_chunks, errors, doc_ids}
 */
nlohmann::json ingest_directory(
    ApolloCore& kb,
    const std::string& path,
    const std::vector<std::string>& extensions,
    size_t max_file_size = 1'000'000,
    bool recursive = true);

/**
 * Default code file extensions.
 */
inline std::vector<std::string> default_code_extensions() {
    return {
        ".cpp", ".cc", ".cxx", ".c",
        ".hpp", ".hh", ".hxx", ".h",
        ".py", ".pyi",
        ".js", ".jsx", ".mjs", ".cjs",
        ".ts", ".tsx", ".d.ts",
        ".go",
        ".rs",
        ".java", ".kt", ".scala",
        ".rb", ".php",
        ".swift", ".m", ".mm",
        ".sh", ".bash", ".zsh",
        ".md", ".rst", ".txt"
    };
}

} // namespace apollocore_v2