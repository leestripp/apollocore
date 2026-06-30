/**
 * ApolloCore Code Extractor — Implementation (Phase M3.B)
 *
 * Walks tree-sitter C++ and Python ASTs to extract code entities and
 * relationships. Designed for high-quality auto-extraction from real code.
 */

#include "apollocore/apollo_code_extractor.hpp"
#include "apollocore/apollo_ts_wrapper.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

namespace apollocore_v2 {

namespace {

// Helper: get text of a node from source
std::string node_text(const ts::TSNode& node, std::string_view source) {
    return std::string(node.text(source));
}

// Helper: get line number of a node (0-based, convert to 1-based)
size_t node_start_line(const ts::TSNode& node) {
    return node.start_row() + 1;
}

size_t node_end_line(const ts::TSNode& node) {
    return node.end_row() + 1;
}

// Helper: find first named child of a type
ts::TSNode find_child_of_type(const ts::TSNode& parent, std::string_view type) {
    return parent.first_child_of_type(type).value_or(ts::TSNode());
}

// Helper: get name from function_declarator / field_declaration
std::string extract_function_name(const ts::TSNode& func_def,
                                  std::string_view source) {
    // Tree-sitter C++ has function_definition with structure:
    //   - type (return type)
    //   - function_declarator (the name + params)
    //     - identifier or field_identifier (the name)
    //
    // Direct child "function_declarator" works for both top-level and
    // method (inside field_declaration) function definitions.
    auto fn_decl = find_child_of_type(func_def, "function_declarator");
    if (!fn_decl.is_null()) {
        // function_declarator contains identifier or field_identifier
        auto id = find_child_of_type(fn_decl, "identifier");
        if (id.is_null()) {
            id = find_child_of_type(fn_decl, "field_identifier");
        }
        if (!id.is_null()) {
            return node_text(id, source);
        }
        // No identifier inside (rare)
        return "";
    }

    // Fallback: declarator wrapper (older tree-sitter or unusual cases)
    auto declarator = find_child_of_type(func_def, "declarator");
    if (declarator.is_null()) {
        declarator = find_child_of_type(func_def, "field_declarator");
    }
    if (declarator.is_null()) {
        return "";
    }

    auto name_node = find_child_of_type(declarator, "function_declarator");
    if (name_node.is_null()) {
        name_node = find_child_of_type(declarator, "identifier");
        if (name_node.is_null()) {
            name_node = find_child_of_type(declarator, "field_identifier");
        }
    }
    if (!name_node.is_null()) {
        return node_text(name_node, source);
    }
    return "";
}

// Get name from class_specifier / struct_specifier
std::string extract_class_name(const ts::TSNode& class_spec,
                               std::string_view source) {
    auto name_node = find_child_of_type(class_spec, "type_identifier");
    if (name_node.is_null()) {
        name_node = find_child_of_type(class_spec, "identifier");
    }
    if (name_node.is_null()) {
        // Anonymous class/struct — skip
        return "";
    }
    return node_text(name_node, source);
}

// Get namespace name
std::string extract_namespace_name(const ts::TSNode& ns_def,
                                    std::string_view source) {
    // In tree-sitter C++, namespace uses "namespace_identifier" type
    auto name_node = find_child_of_type(ns_def, "namespace_identifier");
    if (name_node.is_null()) {
        name_node = find_child_of_type(ns_def, "type_identifier");
    }
    if (name_node.is_null()) {
        name_node = find_child_of_type(ns_def, "identifier");
    }
    if (name_node.is_null()) {
        name_node = find_child_of_type(ns_def, "nested_identifier");
    }
    if (name_node.is_null()) {
        return "";
    }
    return node_text(name_node, source);
}

// Extract base class names (for INHERITS relationship)
std::vector<std::string> extract_base_classes(const ts::TSNode& class_spec,
                                              std::string_view source) {
    std::vector<std::string> bases;
    auto bases_list = class_spec.first_child_of_type("base_class_clause");
    if (bases_list.has_value()) {
        // Each child is a type_identifier or template_type
        for (uint32_t i = 0; i < bases_list->child_count(); ++i) {
            auto child = bases_list->child(i);
            if (child.is_null()) continue;
            auto t = child.type();
            if (t == "type_identifier" || t == "identifier" ||
                t == "template_type") {
                bases.push_back(node_text(child, source));
            }
        }
    }
    return bases;
}

// Find all function calls in a node's subtree
void find_calls_in_subtree(const ts::TSNode& root,
                            std::string_view source,
                            std::vector<std::pair<std::string, size_t>>& calls) {
    if (root.is_null()) return;

    // Walk children iteratively to avoid recursion depth issues
    std::vector<ts::TSNode> stack = {root};
    size_t node_count = 0;
    const size_t MAX_NODES = 50000;  // safety limit

    while (!stack.empty() && node_count < MAX_NODES) {
        auto node = stack.back();
        stack.pop_back();
        node_count++;

        if (node.is_null()) continue;

        // Skip if this is a function declarator (defining the function, not calling)
        // We want call_expression
        if (node.type() == "call_expression") {
            // call_expression: function ( arguments )
            // First child is the function being called
            auto fn_node = node.child(0);
            if (!fn_node.is_null()) {
                std::string fn_name = node_text(fn_node, source);
                // Skip calls to "this", "self", etc.
                if (fn_name != "this" && fn_name != "self") {
                    calls.emplace_back(fn_name, node_start_line(node));
                }
            }
        }

        // Push children
        for (uint32_t i = 0; i < node.child_count(); ++i) {
            stack.push_back(node.child(i));
        }
    }
}

// Build qualified name with namespace/class prefix
std::string qualified_name(const std::string& parent, const std::string& name) {
    if (parent.empty()) return name;
    return parent + "::" + name;
}

// Make a safe chunk_id from file path and entity name
std::string make_chunk_id(const std::string& file_path, const std::string& name) {
    std::string id = file_path;
    // Replace path separators with underscores
    std::replace(id.begin(), id.end(), '/', '_');
    std::replace(id.begin(), id.end(), '\\', '_');
    return id + "_entity_" + name;
}

// ============================================================================
// C++ Extraction
// ============================================================================

void extract_cpp_recursive(const ts::TSNode& node,
                           std::string_view source,
                           const std::string& file_path,
                           const std::string& current_namespace,
                           CodeModel& model) {
    if (node.is_null()) return;

    std::string type(node.type());

    if (type == "function_definition") {
        std::string name = extract_function_name(node, source);
        if (!name.empty()) {
            std::string full_name = qualified_name(current_namespace, name);
            std::string signature = node_text(node, source);
            // Trim signature to first line for storage
            auto newline_pos = signature.find('\n');
            if (newline_pos != std::string::npos && newline_pos < 200) {
                signature = signature.substr(0, newline_pos);
            } else if (signature.size() > 200) {
                signature = signature.substr(0, 200) + "...";
            }

            CodeEntity entity;
            entity.name = full_name;
            entity.type = "FUNCTION";
            entity.file_path = file_path;
            entity.chunk_id = make_chunk_id(file_path, full_name);
            entity.parent = current_namespace;
            entity.start_line = node_start_line(node);
            entity.end_line = node_end_line(node);
            entity.signature = signature;
            model.entities.push_back(entity);

            // Find calls within the function body
            std::vector<std::pair<std::string, size_t>> calls;
            find_calls_in_subtree(node, source, calls);
            for (const auto& [fn_name, line] : calls) {
                // Filter operator calls and trivial names
                if (fn_name.find('.') != std::string::npos) continue;  // method calls
                if (fn_name.find("operator") == 0) continue;  // operators
                CodeRelationship rel;
                rel.source = full_name;
                rel.target = fn_name;
                rel.type = "CALLS";
                rel.line = line;
                model.relationships.push_back(rel);
            }
        }
        // Don't recurse into function body for more entities
        return;
    }

    if (type == "class_specifier" || type == "struct_specifier") {
        std::string name = extract_class_name(node, source);
        if (!name.empty()) {
            std::string full_name = qualified_name(current_namespace, name);
            CodeEntity entity;
            entity.name = full_name;
            entity.type = (type == "struct_specifier") ? "STRUCT" : "CLASS";
            entity.file_path = file_path;
            entity.chunk_id = make_chunk_id(file_path, full_name);
            entity.parent = current_namespace;
            entity.start_line = node_start_line(node);
            entity.end_line = node_end_line(node);
            entity.signature = node_text(node, source).substr(0, 100);
            model.entities.push_back(entity);

            // INHERITS relationships
            auto bases = extract_base_classes(node, source);
            for (const auto& base : bases) {
                CodeRelationship rel;
                rel.source = full_name;
                rel.target = base;
                rel.type = "INHERITS";
                rel.line = node_start_line(node);
                model.relationships.push_back(rel);
            }

            // Recurse into class body with class as parent. This naturally
            // finds function_definitions (constructors, methods) at any depth.
            for (uint32_t i = 0; i < node.child_count(); ++i) {
                extract_cpp_recursive(node.child(i), source, file_path,
                                     full_name, model);
            }
            return;
        }
    }

    if (type == "namespace_definition") {
        std::string name = extract_namespace_name(node, source);
        if (!name.empty() && name != "{anonymous}") {
            CodeEntity entity;
            entity.name = qualified_name(current_namespace, name);
            entity.type = "NAMESPACE";
            entity.file_path = file_path;
            entity.chunk_id = make_chunk_id(file_path, entity.name);
            entity.parent = current_namespace;
            entity.start_line = node_start_line(node);
            entity.end_line = node_end_line(node);
            model.entities.push_back(entity);

            std::string new_namespace = entity.name;
            // Recurse with new namespace
            for (uint32_t i = 0; i < node.child_count(); ++i) {
                extract_cpp_recursive(node.child(i), source, file_path,
                                     new_namespace, model);
            }
            return;
        }
    }

    if (type == "preproc_include") {
        // Extract #include path
        auto path_node = find_child_of_type(node, "string_literal");
        if (path_node.is_null()) {
            path_node = find_child_of_type(node, "system_lib_string");
        }
        if (!path_node.is_null()) {
            std::string path = node_text(path_node, source);
            // Strip quotes/brackets
            if (path.size() >= 2) {
                path = path.substr(1, path.size() - 2);
            }
            // Extract just the filename for the relationship target
            auto slash_pos = path.find_last_of("/\\");
            std::string target = (slash_pos != std::string::npos)
                ? path.substr(slash_pos + 1)
                : path;
            // Strip extension
            auto dot_pos = target.find_last_of('.');
            if (dot_pos != std::string::npos) {
                target = target.substr(0, dot_pos);
            }

            CodeRelationship rel;
            rel.source = file_path;
            rel.target = target;
            rel.type = "IMPORTS";
            rel.line = node_start_line(node);
            model.relationships.push_back(rel);
        }
    }

    if (type == "template_declaration") {
        // Templates wrap function_definition/class_specifier — recurse into them
        for (uint32_t i = 0; i < node.child_count(); ++i) {
            extract_cpp_recursive(node.child(i), source, file_path,
                                 current_namespace, model);
        }
        return;
    }

    // Recurse into children for other node types
    for (uint32_t i = 0; i < node.child_count(); ++i) {
        extract_cpp_recursive(node.child(i), source, file_path,
                             current_namespace, model);
    }
}

// ============================================================================
// Python Extraction (simpler — no templates, no namespaces)
// ============================================================================

#ifdef APOLLOCORE_ENABLE_PYTHON
void extract_python_recursive(const ts::TSNode& node,
                              std::string_view source,
                              const std::string& file_path,
                              const std::string& current_class,
                              CodeModel& model) {
    if (node.is_null()) return;

    std::string type(node.type());

    if (type == "function_definition") {
        std::string name;
        // Get function name from identifier
        for (uint32_t i = 0; i < node.child_count(); ++i) {
            auto child = node.child(i);
            if (!child.is_null() && child.type() == "identifier") {
                name = node_text(child, source);
                break;
            }
        }

        if (!name.empty()) {
            std::string full_name = current_class.empty()
                ? name
                : current_class + "." + name;

            CodeEntity entity;
            entity.name = full_name;
            entity.type = "FUNCTION";
            entity.file_path = file_path;
            entity.chunk_id = make_chunk_id(file_path, full_name);
            entity.parent = current_class;
            entity.start_line = node_start_line(node);
            entity.end_line = node_end_line(node);
            entity.signature = "def " + full_name;
            model.entities.push_back(entity);

            // Find calls
            std::vector<std::pair<std::string, size_t>> calls;
            find_calls_in_subtree(node, source, calls);
            for (const auto& [fn_name, line] : calls) {
                if (fn_name.find('.') == std::string::npos) {
                    CodeRelationship rel;
                    rel.source = full_name;
                    rel.target = fn_name;
                    rel.type = "CALLS";
                    rel.line = line;
                    model.relationships.push_back(rel);
                }
            }
        }
        return;
    }

    if (type == "class_definition") {
        std::string name;
        for (uint32_t i = 0; i < node.child_count(); ++i) {
            auto child = node.child(i);
            if (!child.is_null() && child.type() == "identifier") {
                name = node_text(child, source);
                break;
            }
        }
        if (!name.empty()) {
            CodeEntity entity;
            entity.name = name;
            entity.type = "CLASS";
            entity.file_path = file_path;
            entity.chunk_id = make_chunk_id(file_path, name);
            entity.parent = current_class;
            entity.start_line = node_start_line(node);
            entity.end_line = node_end_line(node);
            entity.signature = "class " + name;
            model.entities.push_back(entity);

            // Recurse for methods (use name as current_class)
            for (uint32_t i = 0; i < node.child_count(); ++i) {
                extract_python_recursive(node.child(i), source, file_path,
                                        name, model);
            }
            return;
        }
    }

    if (type == "import_statement" || type == "import_from_statement") {
        // Extract imported module name
        for (uint32_t i = 0; i < node.child_count(); ++i) {
            auto child = node.child(i);
            if (!child.is_null()) {
                std::string child_type(child.type());
                if (child_type == "dotted_name" || child_type == "identifier" ||
                    child_type == "aliased_import") {
                    std::string target = node_text(child, source);
                    CodeRelationship rel;
                    rel.source = file_path;
                    rel.target = target;
                    rel.type = "IMPORTS";
                    rel.line = node_start_line(node);
                    model.relationships.push_back(rel);
                    break;
                }
            }
        }
        return;
    }

    for (uint32_t i = 0; i < node.child_count(); ++i) {
        extract_python_recursive(node.child(i), source, file_path,
                                current_class, model);
    }
}

#endif // APOLLOCORE_ENABLE_PYTHON

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::string detect_language(const std::string& file_path) {
    auto dot = file_path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = file_path.substr(dot);
    // Lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".cxx" ||
        ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h" ||
        ext == ".c") {
        return "cpp";
    }
    if (ext == ".py" || ext == ".pyi") return "python";
    if (ext == ".js" || ext == ".jsx" || ext == ".mjs") return "javascript";
    return "";
}

CodeModel extract_cpp(const std::string& source, const std::string& file_path) {
    CodeModel model;
    model.file_path = file_path;
    model.language = "cpp";

    try {
        ts::TSParser parser;
        if (!parser.set_cpp_language()) {
            std::cerr << "[CodeExtractor] Failed to set C++ language\n";
            return model;
        }

        auto tree = parser.parse_string(source);
        if (tree.is_null()) {
            return model;
        }

        auto root = tree.root_node();
        extract_cpp_recursive(root, source, file_path, "", model);
    } catch (const std::exception& e) {
        std::cerr << "[CodeExtractor] C++ extraction error: " << e.what() << "\n";
    }

    return model;
}

CodeModel extract_python(const std::string& /*source*/, const std::string& file_path) {
#ifdef APOLLOCORE_ENABLE_PYTHON
    CodeModel model;
    model.file_path = file_path;
    model.language = "python";

    try {
        ts::TSParser parser;
        if (!parser.set_python_language()) {
            std::cerr << "[CodeExtractor] Failed to set Python language\n";
            return model;
        }

        auto tree = parser.parse_string(source);
        if (tree.is_null()) {
            return model;
        }

        auto root = tree.root_node();
        extract_python_recursive(root, source, file_path, "", model);
    } catch (const std::exception& e) {
        std::cerr << "[CodeExtractor] Python extraction error: " << e.what() << "\n";
    }

    return model;
#else
    CodeModel empty;
    empty.file_path = file_path;
    empty.language = "python";
    return empty;
#endif
}

CodeModel extract_code(const std::string& source, const std::string& file_path) {
    std::string lang = detect_language(file_path);
    if (lang == "cpp") return extract_cpp(source, file_path);
    if (lang == "python") return extract_python(source, file_path);
    // Unsupported language — return empty model
    CodeModel empty;
    empty.file_path = file_path;
    empty.language = lang;
    return empty;
}

nlohmann::json to_json(const CodeModel& model) {
    nlohmann::json j;
    j["file_path"] = model.file_path;
    j["language"] = model.language;
    j["entities"] = nlohmann::json::array();
    for (const auto& e : model.entities) {
        j["entities"].push_back({
            {"name", e.name},
            {"type", e.type},
            {"chunk_id", e.chunk_id},
            {"parent", e.parent},
            {"start_line", e.start_line},
            {"end_line", e.end_line},
            {"signature", e.signature}
        });
    }
    j["relationships"] = nlohmann::json::array();
    for (const auto& r : model.relationships) {
        j["relationships"].push_back({
            {"source", r.source},
            {"target", r.target},
            {"type", r.type},
            {"line", r.line}
        });
    }
    return j;
}

} // namespace apollocore_v2