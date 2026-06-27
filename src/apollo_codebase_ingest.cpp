/**
 * ApolloCore Codebase Ingestion — Implementation
 *
 * Walks a directory tree, reads source files, adds them as documents.
 * Each file's path becomes its document ID (with sanitization).
 *
 * Example:
 *   /home/lee/proj/src/foo.cpp → doc_id = "src/foo.cpp"
 *   /home/lee/proj/include/bar.hpp → doc_id = "include/bar.hpp"
 */

#include "apollocore/apollo_codebase_ingest.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace apollocore_v2 {

namespace fs = std::filesystem;

namespace {

// Sanitize a file path to a safe doc_id
std::string path_to_doc_id(const fs::path& base, const fs::path& file) {
    fs::path rel = fs::relative(file, base);
    std::string s = rel.generic_string();

    // Replace path separators with underscores for flat ID
    // Actually keep them — they're meaningful and unique
    // Just remove leading "./" if any
    if (s.size() >= 2 && s[0] == '.' && s[1] == '/') {
        s = s.substr(2);
    }
    return s;
}

// Check if file has one of the desired extensions
bool has_extension(const fs::path& file, const std::vector<std::string>& exts) {
    if (exts.empty()) return true;  // no filter
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (const auto& e : exts) {
        std::string e_lower = e;
        std::transform(e_lower.begin(), e_lower.end(), e_lower.begin(), ::tolower);
        if (ext == e_lower) return true;
        // Also match without leading dot for convenience
        if (ext == e_lower.substr(1)) return true;
    }
    return false;
}

// Read file contents safely
std::string read_file(const fs::path& file, size_t max_size) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "";

    // Check size first
    auto size = fs::file_size(file);
    if (size > max_size) return "";  // caller will check size

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

nlohmann::json ingest_directory(
    ApolloCore& kb,
    const std::string& path,
    const std::vector<std::string>& extensions,
    size_t max_file_size,
    bool recursive)
{
    nlohmann::json result = {
        {"files_processed", 0},
        {"files_skipped", 0},
        {"total_chunks", 0},
        {"errors", nlohmann::json::array()},
        {"doc_ids", nlohmann::json::array()}
    };

    fs::path base(path);
    if (!fs::exists(base)) {
        result["errors"].push_back("Path does not exist: " + path);
        return result;
    }
    if (!fs::is_directory(base)) {
        result["errors"].push_back("Not a directory: " + path);
        return result;
    }

    // Skip common build/vcs directories
    std::set<std::string> skip_dirs = {
        ".git", ".svn", ".hg",
        "build", "build-debug", "build-release",
        "cmake-build-debug", "cmake-build-release",
        "node_modules", "__pycache__",
        ".venv", "venv", "env",
        "target", "dist", "out",
        ".cache", ".idea", ".vscode"
    };

    auto exts = extensions.empty() ? default_code_extensions() : extensions;

    try {
        auto walk = [&](auto& self, const fs::path& dir) -> void {
            std::error_code ec;
            for (auto it = fs::directory_iterator(dir, ec);
                 !ec && it != fs::end(it);
                 it.increment(ec))
            {
                if (ec) break;
                const auto& entry = *it;

                std::string name = entry.path().filename().string();
                if (entry.is_directory()) {
                    if (!recursive) continue;
                    if (skip_dirs.count(name)) continue;
                    self(self, entry.path());
                } else if (entry.is_regular_file()) {
                    if (!has_extension(entry.path(), exts)) {
                        result["files_skipped"] = result["files_skipped"].get<int>() + 1;
                        continue;
                    }
                    auto size = fs::file_size(entry.path(), ec);
                    if (ec || size > max_file_size) {
                        result["files_skipped"] = result["files_skipped"].get<int>() + 1;
                        if (size > max_file_size) {
                            result["errors"].push_back(
                                "Skipped (too large): " + entry.path().string() +
                                " (" + std::to_string(size) + " bytes)");
                        }
                        continue;
                    }

                    std::string content = read_file(entry.path(), max_file_size);
                    if (content.empty()) {
                        result["files_skipped"] = result["files_skipped"].get<int>() + 1;
                        result["errors"].push_back("Could not read: " + entry.path().string());
                        continue;
                    }

                    // Add as document
                    std::string doc_id = path_to_doc_id(base, entry.path());
                    try {
                        size_t chunks = kb.add_document(content);
                        result["files_processed"] = result["files_processed"].get<int>() + 1;
                        result["total_chunks"] = result["total_chunks"].get<int>() + chunks;
                        result["doc_ids"].push_back(doc_id);
                    } catch (const std::exception& e) {
                        result["errors"].push_back(
                            "Failed to add " + entry.path().string() + ": " + e.what());
                    }
                }
            }
        };

        walk(walk, base);
    } catch (const std::exception& e) {
        result["errors"].push_back(std::string("Walk error: ") + e.what());
    }

    return result;
}

} // namespace apollocore_v2