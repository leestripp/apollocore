#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <optional>
#include <algorithm>
#include <queue>
#include <random>
#include <numeric>
#include <cmath>

namespace apollocore {

using NodeID = std::string;
using ChunkID = std::string;
using EmbeddingVector = std::vector<float>;

struct ChunkLink { ChunkID chunk_id; std::string link_type; float weight{1.0f}; };
struct TreeLink { NodeID from_id, to_id; std::string link_type, label; };
using Metadata = std::unordered_map<std::string, std::string>;
struct ChunkConfig { size_t chunk_size{512}; size_t chunk_overlap{64}; bool sentence_aware{true}; size_t min_chunk_size{50}; };
struct QueryOptions { size_t max_results{10}; float similarity_threshold{0.0f}; };
struct QueryResultItem { std::string node_id, title, content, chunk_content, result_type; float score{0.0f}; Metadata metadata; };
struct QueryResults { std::vector<QueryResultItem> items; size_t total_hits{0}; };
struct EmbeddingResult { EmbeddingVector vector; std::string model_name; size_t dimensions{0}; };

using NodeHandle = std::shared_ptr<class KnowledgeNode>;
using ChunkHandle = std::shared_ptr<class RAGChunk>;

class KnowledgeNode {
public:
    NodeID id; std::string title, content;
    std::vector<ChunkLink> chunk_links; Metadata metadata;
    std::chrono::system_clock::time_point created_at, updated_at;
    std::weak_ptr<KnowledgeNode> parent;
    std::vector<std::shared_ptr<KnowledgeNode>> children;
    KnowledgeNode() : created_at(std::chrono::system_clock::now()), updated_at(created_at) {}
    KnowledgeNode(NodeID id_, std::string_view title_, std::string_view content_) : id(std::move(id_)), title(title_), content(content_), created_at(std::chrono::system_clock::now()), updated_at(created_at) {}
    bool is_root() const { return parent.expired(); }
};

class HashEmbedding {
public:
    HashEmbedding(size_t dims = 384) : dims_(dims) {}
    EmbeddingResult embed(std::string_view text) {
        EmbeddingVector vec(dims_, 0.0f);
        auto tokens = tokenize(text);
        for (auto& t : tokens) {
            uint64_t h = 42;
            for (char c : t) h = h * 31 + static_cast<unsigned char>(c);
            for (size_t i = 0; i < dims_; ++i) {
                uint64_t seed = h ^ (i * 0x9e3779b97f4a7c15ULL);
                vec[i] += (float)((seed * 1103515245ULL + 12345ULL) % (1ULL << 31)) / (float)(1ULL << 31) * 2 - 1;
            }
        }
        float norm = 0; for (float v : vec) norm += v * v; norm = std::sqrt(norm);
        if (norm > 1e-10f) for (float& v : vec) v /= norm;
        return {std::move(vec), "Hash", dims_};
    }
    size_t dimensions() const { return dims_; }
private:
    size_t dims_;
    std::vector<std::string> tokenize(std::string_view text) const {
        std::vector<std::string> tokens; std::string cur;
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c))) cur += std::tolower(static_cast<unsigned char>(c));
            else if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        }
        if (!cur.empty()) tokens.push_back(cur);
        return tokens;
    }
};

class VectorStore {
public:
    bool add_vector(ChunkID id, EmbeddingVector vec, Metadata meta = {}) {
        if (vec.empty()) return false;
        if (dims_ == 0) dims_ = vec.size();
        vectors_[id] = std::move(vec);
        metadata_[id] = std::move(meta);
        return true;
    }
    std::optional<EmbeddingVector> get_vector(ChunkID id) const {
        auto it = vectors_.find(id);
        return it != vectors_.end() ? std::optional(it->second) : std::nullopt;
    }
    struct SearchResult { ChunkID id; float similarity; Metadata metadata; };
    std::vector<SearchResult> search(const EmbeddingVector& query, size_t top_k, float min_sim = 0.0f) const {
        if (query.empty() || vectors_.empty()) return {};
        std::vector<SearchResult> results;
        for (auto& [id, vec] : vectors_) {
            float sim = 0.0f, nq = 0.0f, nv = 0.0f;
            for (size_t i = 0; i < query.size() && i < vec.size(); ++i) { sim += query[i] * vec[i]; nq += query[i] * query[i]; nv += vec[i] * vec[i]; }
            sim /= (std::sqrt(nq) * std::sqrt(nv) + 1e-10f);
            if (sim >= min_sim) {
                auto mit = metadata_.find(id);
                results.push_back({id, sim, mit != metadata_.end() ? mit->second : Metadata{}});
            }
        }
        std::sort(results.begin(), results.end(), [](auto& a, auto& b) { return a.similarity > b.similarity; });
        if (results.size() > top_k) results.resize(top_k);
        return results;
    }
    size_t size() const { return vectors_.size(); }
    size_t dimensions() const { return dims_; }
    void clear() { vectors_.clear(); metadata_.clear(); dims_ = 0; }
    bool remove_vector(ChunkID id) { auto it = vectors_.find(id); if (it == vectors_.end()) return false; vectors_.erase(it); metadata_.erase(id); return true; }
private:
    std::unordered_map<ChunkID, EmbeddingVector> vectors_;
    std::unordered_map<ChunkID, Metadata> metadata_;
    size_t dims_{0};
};

class RAGChunk {
public:
    ChunkID id; std::string content; NodeID source_node_id; EmbeddingVector embedding;
    size_t chunk_index{0}; Metadata metadata; std::chrono::system_clock::time_point created_at;
    RAGChunk() : created_at(std::chrono::system_clock::now()) {}
    RAGChunk(ChunkID id_, std::string_view content_, NodeID source_, size_t idx) 
        : id(std::move(id_)), content(content_), source_node_id(source_), chunk_index(idx), created_at(std::chrono::system_clock::now()) {}
    size_t word_count() const { size_t count = 0; bool in = false; for (char c : content) { if (std::isalnum(static_cast<unsigned char>(c))) { if (!in) { ++count; in = true; } } else in = false; } return count; }
    bool has_embedding() const { return !embedding.empty(); }
};

class KnowledgeTree {
public:
    KnowledgeTree();
    NodeHandle create_node(std::string_view title, std::string_view content = {}, NodeHandle parent = nullptr);
    bool delete_node(NodeHandle node);
    NodeHandle get_node(NodeID id) const;
    NodeHandle get_root() const;
    bool move_node(NodeHandle node, NodeHandle new_parent);
    std::vector<NodeHandle> get_children(NodeHandle node) const;
    std::vector<NodeHandle> get_ancestors(NodeHandle node) const;
    std::vector<NodeHandle> get_descendants(NodeHandle node) const;
    bool set_content(NodeHandle node, std::string_view content);
    std::string_view get_content(NodeHandle node) const;
    bool link_chunk(NodeHandle node, ChunkID chunk_id, std::string_view type = "related");
    std::vector<ChunkID> get_linked_chunks(NodeHandle node) const;
    void traverse_bfs(std::function<void(NodeHandle)> callback) const;
    size_t node_count() const;
    void clear();
private:
    NodeHandle root_;
    std::unordered_map<NodeID, NodeHandle> nodes_;
    size_t node_counter_{0};
    NodeID generate_id();
    void collect_descendants(NodeHandle node, std::vector<NodeHandle>& result) const;
};

class ApolloCore {
public:
    ApolloCore();
    NodeHandle create_node(std::string_view title, std::string_view content = {});
    NodeHandle create_node(std::string_view title, std::string_view content, NodeHandle parent);
    bool delete_node(NodeHandle node, bool delete_children = true);
    NodeHandle get_node(NodeID id) const;
    NodeHandle get_root() const;
    std::vector<NodeHandle> get_children(NodeHandle node) const;
    std::vector<NodeHandle> get_ancestors(NodeHandle node) const;
    std::vector<NodeHandle> get_descendants(NodeHandle node) const;
    std::string_view get_content(NodeHandle node) const;
    bool set_content(NodeHandle node, std::string_view content);
    ChunkHandle add_chunk(std::string_view content, NodeHandle parent_node);
    std::vector<ChunkHandle> add_document(std::string_view text, NodeHandle parent_node, const ChunkConfig& config = {});
    std::vector<ChunkHandle> search_chunks(std::string_view query, size_t top_k = 10);
    QueryResults query(std::string_view query_text, const QueryOptions& options = {});
    struct Stats { size_t node_count{0}; size_t chunk_count{0}; };
    Stats get_stats() const;
    void clear();
private:
    ChunkID gen_chunk_id();
    std::vector<std::string> split_sentences(std::string_view text) const;
    KnowledgeTree tree_;
    HashEmbedding embedding_;
    VectorStore vector_store_;
    std::unordered_map<ChunkID, ChunkHandle> chunks_;
    std::unordered_map<NodeID, std::vector<ChunkID>> node_to_chunks_;
    size_t chunk_counter_{0};
};

// Implementations
inline KnowledgeTree::KnowledgeTree() {
    root_ = std::make_shared<KnowledgeNode>("root", "Root", "Root of knowledge tree");
    nodes_["root"] = root_;
}
inline NodeID KnowledgeTree::generate_id() { return "node_" + std::to_string(++node_counter_); }
inline NodeHandle KnowledgeTree::create_node(std::string_view title, std::string_view content, NodeHandle parent) {
    auto node = std::make_shared<KnowledgeNode>(generate_id(), title, content);
    if (parent) { node->parent = parent; parent->children.push_back(node); }
    nodes_[node->id] = node;
    return node;
}
inline bool KnowledgeTree::delete_node(NodeHandle node) {
    if (!node || node == root_) return false;
    for (auto& child : node->children) delete_node(child);
    if (auto p = node->parent.lock()) p->children.erase(std::remove(p->children.begin(), p->children.end(), node), p->children.end());
    nodes_.erase(node->id);
    return true;
}
inline NodeHandle KnowledgeTree::get_node(NodeID id) const { auto it = nodes_.find(id); return it != nodes_.end() ? it->second : nullptr; }
inline NodeHandle KnowledgeTree::get_root() const { return root_; }
inline bool KnowledgeTree::move_node(NodeHandle node, NodeHandle new_parent) {
    if (!node || !new_parent || node == root_) return false;
    if (auto p = node->parent.lock()) p->children.erase(std::remove(p->children.begin(), p->children.end(), node), p->children.end());
    node->parent = new_parent;
    new_parent->children.push_back(node);
    return true;
}
inline std::vector<NodeHandle> KnowledgeTree::get_children(NodeHandle node) const { return node ? node->children : std::vector<NodeHandle>{}; }
inline std::vector<NodeHandle> KnowledgeTree::get_ancestors(NodeHandle node) const {
    std::vector<NodeHandle> result;
    while (auto p = node->parent.lock()) { result.push_back(p); node = p; }
    std::reverse(result.begin(), result.end());
    return result;
}
inline void KnowledgeTree::collect_descendants(NodeHandle node, std::vector<NodeHandle>& result) const {
    for (auto& child : node->children) { result.push_back(child); collect_descendants(child, result); }
}
inline std::vector<NodeHandle> KnowledgeTree::get_descendants(NodeHandle node) const {
    std::vector<NodeHandle> result;
    if (node) collect_descendants(node, result);
    return result;
}
inline bool KnowledgeTree::set_content(NodeHandle node, std::string_view content) {
    if (!node) return false;
    node->content = content;
    node->updated_at = std::chrono::system_clock::now();
    return true;
}
inline std::string_view KnowledgeTree::get_content(NodeHandle node) const { return node ? node->content : std::string_view{}; }
inline bool KnowledgeTree::link_chunk(NodeHandle node, ChunkID chunk_id, std::string_view type) {
    if (!node) return false;
    node->chunk_links.push_back({std::move(chunk_id), std::string(type), 1.0f});
    return true;
}
inline std::vector<ChunkID> KnowledgeTree::get_linked_chunks(NodeHandle node) const {
    if (!node) return {};
    std::vector<ChunkID> result;
    for (auto& link : node->chunk_links) result.push_back(link.chunk_id);
    return result;
}
inline void KnowledgeTree::traverse_bfs(std::function<void(NodeHandle)> callback) const {
    if (!callback) return;
    std::queue<NodeHandle> q;
    q.push(root_);
    while (!q.empty()) { auto n = q.front(); q.pop(); callback(n); for (auto& c : n->children) q.push(c); }
}
inline size_t KnowledgeTree::node_count() const { return nodes_.size(); }
inline void KnowledgeTree::clear() {
    nodes_.clear(); node_counter_ = 0;
    root_ = std::make_shared<KnowledgeNode>("root", "Root", "Root of knowledge tree");
    nodes_["root"] = root_;
}

inline ApolloCore::ApolloCore() {}
inline ChunkID ApolloCore::gen_chunk_id() { return "chunk_" + std::to_string(++chunk_counter_); }
inline std::vector<std::string> ApolloCore::split_sentences(std::string_view text) const {
    std::vector<std::string> sents; std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i]; cur += c;
        if ((c == '.' || c == '!' || c == '?') && (i + 1 >= text.size() || std::isspace(static_cast<unsigned char>(text[i + 1])))) {
            if (!cur.empty()) { sents.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) sents.push_back(cur);
    return sents;
}
inline NodeHandle ApolloCore::create_node(std::string_view title, std::string_view content) { return tree_.create_node(title, content, tree_.get_root()); }
inline NodeHandle ApolloCore::create_node(std::string_view title, std::string_view content, NodeHandle parent) { return tree_.create_node(title, content, parent ? parent : tree_.get_root()); }
inline bool ApolloCore::delete_node(NodeHandle node, bool delete_children) {
    if (!node) return false;
    if (delete_children) for (auto& d : tree_.get_descendants(node)) { for (auto& cid : tree_.get_linked_chunks(d)) { vector_store_.remove_vector(cid); chunks_.erase(cid); } }
    return tree_.delete_node(node);
}
inline NodeHandle ApolloCore::get_node(NodeID id) const { return tree_.get_node(id); }
inline NodeHandle ApolloCore::get_root() const { return tree_.get_root(); }
inline std::vector<NodeHandle> ApolloCore::get_children(NodeHandle node) const { return tree_.get_children(node); }
inline std::vector<NodeHandle> ApolloCore::get_ancestors(NodeHandle node) const { return tree_.get_ancestors(node); }
inline std::vector<NodeHandle> ApolloCore::get_descendants(NodeHandle node) const { return tree_.get_descendants(node); }
inline std::string_view ApolloCore::get_content(NodeHandle node) const { return tree_.get_content(node); }
inline bool ApolloCore::set_content(NodeHandle node, std::string_view content) { return tree_.set_content(node, content); }
inline ChunkHandle ApolloCore::add_chunk(std::string_view content, NodeHandle parent_node) {
    NodeID pid = parent_node ? parent_node->id : "";
    auto chunk = std::make_shared<RAGChunk>(gen_chunk_id(), content, pid, 0);
    chunk->embedding = embedding_.embed(content).vector;
    vector_store_.add_vector(chunk->id, chunk->embedding);
    chunks_[chunk->id] = chunk;
    if (!pid.empty()) { node_to_chunks_[pid].push_back(chunk->id); tree_.link_chunk(parent_node, chunk->id); }
    return chunk;
}
inline std::vector<ChunkHandle> ApolloCore::add_document(std::string_view text, NodeHandle parent_node, const ChunkConfig& config) {
    NodeID pid = parent_node ? parent_node->id : "";
    std::vector<ChunkHandle> result; std::string current;
    auto sentences = split_sentences(text);
    for (auto& s : sentences) {
        if (current.size() + s.size() <= config.chunk_size) { if (!current.empty()) current += " "; current += s; }
        else { if (!current.empty()) {
            auto chunk = std::make_shared<RAGChunk>(gen_chunk_id(), current, pid, result.size());
            chunk->embedding = embedding_.embed(current).vector;
            vector_store_.add_vector(chunk->id, chunk->embedding);
            chunks_[chunk->id] = chunk;
            if (!pid.empty()) node_to_chunks_[pid].push_back(chunk->id);
            result.push_back(chunk);
        } current = s; }
    }
    if (!current.empty()) {
        auto chunk = std::make_shared<RAGChunk>(gen_chunk_id(), current, pid, result.size());
        chunk->embedding = embedding_.embed(current).vector;
        vector_store_.add_vector(chunk->id, chunk->embedding);
        chunks_[chunk->id] = chunk;
        if (!pid.empty()) node_to_chunks_[pid].push_back(chunk->id);
        result.push_back(chunk);
    }
    return result;
}
inline std::vector<ChunkHandle> ApolloCore::search_chunks(std::string_view query, size_t top_k) {
    auto qemb = embedding_.embed(query);
    auto results = vector_store_.search(qemb.vector, top_k);
    std::vector<ChunkHandle> found;
    for (auto& r : results) { auto it = chunks_.find(r.id); if (it != chunks_.end()) found.push_back(it->second); }
    return found;
}
inline QueryResults ApolloCore::query(std::string_view query_text, const QueryOptions& options) {
    QueryResults results;
    auto chunks = search_chunks(std::string(query_text), options.max_results);
    for (auto& chunk : chunks) {
        QueryResultItem item;
        item.node_id = chunk->source_node_id;
        item.chunk_content = chunk->content;
        item.score = 0.8f;
        item.result_type = "chunk";
        if (auto node = tree_.get_node(chunk->source_node_id)) { item.title = node->title; item.content = node->content; }
        results.items.push_back(item);
    }
    results.total_hits = results.items.size();
    return results;
}
inline ApolloCore::Stats ApolloCore::get_stats() const { return {tree_.node_count(), chunks_.size()}; }
inline void ApolloCore::clear() { tree_.clear(); chunks_.clear(); node_to_chunks_.clear(); vector_store_.clear(); chunk_counter_ = 0; }

} // namespace apollocore
