/**
 * ApolloCore Tree-sitter C++ Wrapper (Phase M3.B - Step 2)
 *
 * Thin RAII wrappers around the tree-sitter C API for safe, ergonomic
 * use from C++. All resources are managed automatically.
 *
 * Usage:
 *
 *   TSParser parser;
 *   parser.set_cpp_language();
 *
 *   std::string src = "namespace app { void foo() {} }";
 *   auto tree = parser.parse_string(src);
 *   TSNode root = tree.root_node();
 *
 *   for (auto child : root.children()) {
 *       std::cout << child.type() << "\n";
 *   }
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Include tree-sitter C API for proper TSNode/TSParser/TSTree types
extern "C" {
    #include <tree_sitter/api.h>
}

namespace apollocore_v2::ts {

// ============================================================================
// TSNode — wraps a tree-sitter node (cheap to copy, just holds a TSNode value)
// ============================================================================
// IMPORTANT: TSNode is defined as a complete type (not forward-declared) because
// it holds a TSNode struct as a member and needs to know its size/layout.
class TSNode {
public:
    TSNode() = default;
    explicit TSNode(::TSNode node) : node_(node) {}

    // Type tag (e.g. "function_definition", "class_specifier")
    std::string_view type() const;

    // Is this node "named" (i.e. not anonymous like ";")?
    bool is_named() const;

    // Is this a missing/error node?
    bool is_missing() const;

    // Is this an extra node (e.g. from error recovery)?
    bool is_extra() const;

    // Tree-sitter sexp representation
    std::string to_sexp() const;

    // Source range
    struct Range {
        uint32_t start_byte = 0, end_byte = 0;
        uint32_t start_row = 0, start_col = 0;
        uint32_t end_row = 0, end_col = 0;
    };
    Range range() const;

    // Sliced source text (caller must keep `source` alive!)
    std::string_view text(std::string_view source) const;

    // Children
    uint32_t child_count() const;
    TSNode child(uint32_t i) const;
    TSNode named_child(uint32_t i) const;
    TSNode child_by_field_name(std::string_view field_name) const;

    // Find first child of a given type (recursive search, depth-first)
    // Returns std::nullopt if not found
    std::optional<TSNode> first_child_of_type(std::string_view type) const;

    // Iterate over named children of a specific type
    std::vector<TSNode> find_children_of_type(std::string_view type) const;

    // Iterate over all named children
    std::vector<TSNode> named_children() const;

    // Equality
    bool operator==(TSNode other) const;
    bool operator!=(TSNode other) const { return !(*this == other); }

public:
    ::TSNode node_{};  // TSNode is a small POD, public for direct C API interop
};

// ============================================================================
// TSTree — owns a parsed tree. Move-only.
// ============================================================================
class TSTree {
public:
    TSTree() = default;
    explicit TSTree(::TSTree* tree) : tree_(tree) {}

    ~TSTree();
    TSTree(TSTree&& other) noexcept;
    TSTree& operator=(TSTree&& other) noexcept;
    TSTree(const TSTree&) = delete;
    TSTree& operator=(const TSTree&) = delete;

    // Root node
    TSNode root_node() const;

    // Source (the original text passed to parse_string)
    std::string_view source() const;

    // Has a parse error?
    bool has_error() const;

private:
    ::TSTree* tree_ = nullptr;
};

// ============================================================================
// TSParser — owns the parser. Move-only.
// ============================================================================
class TSParser {
public:
    TSParser();
    ~TSParser();
    TSParser(TSParser&& other) noexcept;
    TSParser& operator=(TSParser&& other) noexcept;
    TSParser(const TSParser&) = delete;
    TSParser& operator=(const TSParser&) = delete;

    // Set language to C++ (returns true on success)
    bool set_cpp_language();

    // Set language to Python (returns true on success)
    bool set_python_language();

    // Parse a string. Caller owns the source string for the lifetime of the tree.
    TSTree parse_string(std::string_view source);

private:
    ::TSParser* parser_ = nullptr;
};

} // namespace apollocore_v2::ts