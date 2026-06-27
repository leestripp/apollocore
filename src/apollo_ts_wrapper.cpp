/**
 * ApolloCore Tree-sitter C++ Wrapper — Implementation (Phase M3.B)
 *
 * Thin RAII wrappers around the tree-sitter C API.
 */

#include "apollocore/apollo_ts_wrapper.hpp"

#include <tree_sitter/api.h>

// Grammar symbols (extern "C" to avoid name mangling)
extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_python();
}

namespace apollocore_v2::ts {

// ============================================================================
// TSNode
// ============================================================================

std::string_view TSNode::type() const {
    const char* t = ts_node_type(node_);
    return t ? std::string_view(t) : std::string_view();
}

bool TSNode::is_named() const {
    return ts_node_is_named(node_);
}

bool TSNode::is_missing() const {
    return ts_node_is_missing(node_);
}

bool TSNode::is_extra() const {
    return ts_node_is_extra(node_);
}

std::string TSNode::to_sexp() const {
    char* s = ts_node_string(node_);
    if (!s) return {};
    std::string result(s);
    free(s);
    return result;
}

TSNode::Range TSNode::range() const {
    TSPoint start = ts_node_start_point(node_);
    TSPoint end = ts_node_end_point(node_);
    Range r;
    r.start_byte = ts_node_start_byte(node_);
    r.end_byte = ts_node_end_byte(node_);
    r.start_row = start.row;
    r.start_col = start.column;
    r.end_row = end.row;
    r.end_col = end.column;
    return r;
}

std::string_view TSNode::text(std::string_view source) const {
    uint32_t start = ts_node_start_byte(node_);
    uint32_t end = ts_node_end_byte(node_);
    if (start >= source.size() || end > source.size() || start > end) {
        return {};
    }
    return source.substr(start, end - start);
}

uint32_t TSNode::child_count() const {
    return ts_node_child_count(node_);
}

TSNode TSNode::child(uint32_t i) const {
    return TSNode(ts_node_child(node_, i));
}

TSNode TSNode::named_child(uint32_t i) const {
    return TSNode(ts_node_named_child(node_, i));
}

TSNode TSNode::child_by_field_name(std::string_view field_name) const {
    // tree-sitter API takes a C string + length
    return TSNode(ts_node_child_by_field_name(
        node_,
        field_name.data(),
        static_cast<uint32_t>(field_name.size())
    ));
}

std::optional<TSNode> TSNode::first_child_of_type(std::string_view type) const {
    auto children = find_children_of_type(type);
    if (children.empty()) return std::nullopt;
    return children[0];
}

std::vector<TSNode> TSNode::find_children_of_type(std::string_view type) const {
    std::vector<TSNode> results;
    uint32_t n = child_count();
    for (uint32_t i = 0; i < n; ++i) {
        TSNode c = child(i);
        if (c.type() == type) {
            results.push_back(c);
        } else if (c.is_named()) {
            // Recurse into named children
            auto sub = c.find_children_of_type(type);
            for (auto& s : sub) results.push_back(s);
        }
    }
    return results;
}

std::vector<TSNode> TSNode::named_children() const {
    std::vector<TSNode> results;
    uint32_t n = ts_node_named_child_count(node_);
    for (uint32_t i = 0; i < n; ++i) {
        results.push_back(TSNode(ts_node_named_child(node_, i)));
    }
    return results;
}

bool TSNode::operator==(TSNode other) const {
    return ts_node_eq(node_, other.node_);
}

// ============================================================================
// TSTree
// ============================================================================

TSTree::~TSTree() {
    if (tree_) ts_tree_delete(tree_);
}

TSTree::TSTree(TSTree&& other) noexcept : tree_(other.tree_) {
    other.tree_ = nullptr;
}

TSTree& TSTree::operator=(TSTree&& other) noexcept {
    if (this != &other) {
        if (tree_) ts_tree_delete(tree_);
        tree_ = other.tree_;
        other.tree_ = nullptr;
    }
    return *this;
}

TSNode TSTree::root_node() const {
    return TSNode(ts_tree_root_node(tree_));
}

std::string_view TSTree::source() const {
    // Note: tree-sitter doesn't store the source, just byte ranges.
    // Caller must keep their source string alive.
    // We return an empty view here as a safety signal.
    return {};
}

bool TSTree::has_error() const {
    return ts_node_has_error(root_node().node_);
}

// ============================================================================
// TSParser
// ============================================================================

TSParser::TSParser() : parser_(ts_parser_new()) {}

TSParser::~TSParser() {
    if (parser_) ts_parser_delete(parser_);
}

TSParser::TSParser(TSParser&& other) noexcept : parser_(other.parser_) {
    other.parser_ = nullptr;
}

TSParser& TSParser::operator=(TSParser&& other) noexcept {
    if (this != &other) {
        if (parser_) ts_parser_delete(parser_);
        parser_ = other.parser_;
        other.parser_ = nullptr;
    }
    return *this;
}

bool TSParser::set_cpp_language() {
    return ts_parser_set_language(parser_, tree_sitter_cpp());
}

bool TSParser::set_python_language() {
    return ts_parser_set_language(parser_, tree_sitter_python());
}

TSTree TSParser::parse_string(std::string_view source) {
    TSTree result(ts_parser_parse_string(
        parser_,
        nullptr,  // no old tree
        source.data(),
        static_cast<uint32_t>(source.size())
    ));
    return result;
}

} // namespace apollocore_v2::ts