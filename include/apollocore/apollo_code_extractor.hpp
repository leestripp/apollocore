/**
 * ApolloCore Code Extractor (Phase M3.B)
 *
 * Parses source code with tree-sitter and extracts code-aware entities
 * (FUNCTION, CLASS, FILE, NAMESPACE) and relationships (CALLS, INHERITS,
 * DEFINES).
 *
 * Designed for me (CodeMax) to ingest codebases and get a populated
 * knowledge graph automatically — no manual add_entity calls needed.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace apollocore_v2 {

/**
 * A code entity extracted from source (function, class, etc.).
 */
struct CodeEntity {
    std::string name;
    std::string type;          // "FUNCTION", "CLASS", "NAMESPACE", etc.
    std::string file_path;     // Source file
    std::string chunk_id;      // Suggested chunk_id for storage
    std::string parent;        // Enclosing class/namespace (empty if top-level)
    size_t start_line = 0;
    size_t end_line = 0;
    std::string signature;     // For display
};

/**
 * A code relationship extracted from source.
 */
struct CodeRelationship {
    std::string source;
    std::string target;
    std::string type;          // "CALLS", "INHERITS", "DEFINES", etc.
    size_t line = 0;
};

/**
 * Complete extracted code model from one source file.
 */
struct CodeModel {
    std::vector<CodeEntity> entities;
    std::vector<CodeRelationship> relationships;
    std::string language;      // "cpp", "python", etc.
    std::string file_path;
};

/**
 * Detect language from file extension.
 * @return "cpp", "python", "javascript", or "" if unsupported
 */
std::string detect_language(const std::string& file_path);

/**
 * Extract code entities and relationships from C++ source.
 */
CodeModel extract_cpp(const std::string& source, const std::string& file_path);

/**
 * Extract code entities and relationships from Python source.
 */
CodeModel extract_python(const std::string& source, const std::string& file_path);

/**
 * Extract from any supported language (auto-detects from file_path).
 * @return CodeModel with empty entities if language unsupported
 */
CodeModel extract_code(const std::string& source, const std::string& file_path);

/**
 * Convert a CodeModel to JSON (for MCP responses / debug).
 */
nlohmann::json to_json(const CodeModel& model);

} // namespace apollocore_v2