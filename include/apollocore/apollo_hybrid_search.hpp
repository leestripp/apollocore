/**
 * ApolloCore Hybrid Search (Phase M2.7)
 *
 * THE killer feature combining three signals:
 *   1. Vector search (semantic similarity)
 *   2. BM25 keyword search (lexical match)
 *   3. Graph expansion (entity-based structural match)
 * Combined with Reciprocal Rank Fusion (RRF).
 *
 * Plus optional re-ranking via MCP sampling.
 */

#pragma once

#include "apollocore/apollo_persistence.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace apollocore_v2::search {

// ============================================================================
// BM25 Implementation
// ============================================================================

struct BM25Index {
    // Per-term document frequency: df[term] = number of docs containing term
    std::unordered_map<std::string, size_t> df;
    // Per-doc term frequency: tf[doc_id][term] = count
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> tf;
    // Per-doc length (in tokens)
    std::unordered_map<std::string, size_t> doc_len;
    // Total docs
    size_t total_docs = 0;
    // Average doc length
    double avg_doc_len = 0.0;
    // BM25 params (Okapi BM25 standard defaults)
    double k1 = 1.2;
    double b  = 0.75;

    /**
     * Tokenize text: lowercase, split on non-alphanumeric, drop stop words.
     */
    static std::vector<std::string> tokenize(const std::string& text) {
        static const std::unordered_set<std::string> stop = {
            "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
            "have", "has", "had", "do", "does", "did", "will", "would", "could",
            "should", "may", "might", "must", "can", "of", "in", "to", "for",
            "on", "with", "at", "by", "from", "as", "and", "or", "but", "if",
            "this", "that", "these", "those", "it", "its", "i", "you", "he",
            "she", "they", "we", "me", "him", "her", "them", "us", "my", "your",
            "his", "their", "our"
        };
        std::vector<std::string> tokens;
        std::string cur;
        for (char c : text) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                cur += std::tolower(static_cast<unsigned char>(c));
            } else if (!cur.empty()) {
                if (!stop.count(cur)) tokens.push_back(cur);
                cur.clear();
            }
        }
        if (!cur.empty() && !stop.count(cur)) tokens.push_back(cur);
        return tokens;
    }

    /**
     * Build BM25 index from a list of (doc_id, text) pairs.
     */
    void build(const std::vector<std::pair<std::string, std::string>>& docs) {
        df.clear();
        tf.clear();
        doc_len.clear();
        total_docs = docs.size();
        size_t total_len = 0;

        for (const auto& [id, text] : docs) {
            auto tokens = tokenize(text);
            doc_len[id] = tokens.size();
            total_len += tokens.size();

            // Count term frequencies for this doc
            std::unordered_set<std::string> seen;  // for df
            for (const auto& term : tokens) {
                tf[id][term]++;
                if (seen.insert(term).second) df[term]++;
            }
        }
        avg_doc_len = total_docs ? static_cast<double>(total_len) / total_docs : 0.0;
    }

    /**
     * IDF (Inverse Document Frequency) for a term.
     * Uses the BM25+ variant: log((N - df + 0.5) / (df + 0.5) + 1)
     */
    double idf(const std::string& term) const {
        size_t n = df.count(term) ? df.at(term) : 0;
        return std::log((static_cast<double>(total_docs) - n + 0.5) /
                        (n + 0.5) + 1.0);
    }

    /**
     * Score a doc against a query. Returns BM25 score (higher = better).
     */
    double score(const std::string& doc_id, const std::vector<std::string>& qterms) const {
        auto it = tf.find(doc_id);
        if (it == tf.end()) return 0.0;
        const auto& doc_tf = it->second;

        size_t dl = doc_len.count(doc_id) ? doc_len.at(doc_id) : 0;
        if (dl == 0 || avg_doc_len == 0.0) return 0.0;

        double s = 0.0;
        for (const auto& term : qterms) {
            auto tf_it = doc_tf.find(term);
            if (tf_it == doc_tf.end()) continue;
            double f = static_cast<double>(tf_it->second);
            double idf_val = idf(term);
            double norm = 1.0 - b + b * (static_cast<double>(dl) / avg_doc_len);
            s += idf_val * (f * (k1 + 1.0)) / (f + k1 * norm);
        }
        return s;
    }

    /**
     * Search: return top N (doc_id, score) sorted by score descending.
     */
    std::vector<std::pair<std::string, double>> search(
        const std::string& query, size_t top_n) const
    {
        auto qterms = tokenize(query);
        std::vector<std::pair<std::string, double>> results;
        results.reserve(total_docs);
        for (const auto& [id, _] : tf) {
            double s = score(id, qterms);
            if (s > 0.0) results.emplace_back(id, s);
        }
        std::sort(results.begin(), results.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        if (results.size() > top_n) results.resize(top_n);
        return results;
    }
};

// ============================================================================
// Reciprocal Rank Fusion (RRF)
// ============================================================================

/**
 * Combine multiple ranked lists into one using RRF.
 *   score(d) = Σ 1 / (k + rank_i(d))
 * Default k = 60 (standard from Cormack et al., 2009).
 */
inline std::vector<std::pair<std::string, double>> rrf_fuse(
    const std::vector<std::vector<std::pair<std::string, double>>>& ranked_lists,
    double k = 60.0)
{
    std::unordered_map<std::string, double> fused;
    for (const auto& list : ranked_lists) {
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& [id, _] = list[i];
            fused[id] += 1.0 / (k + static_cast<double>(i + 1));
        }
    }
    std::vector<std::pair<std::string, double>> out;
    out.reserve(fused.size());
    for (const auto& [id, s] : fused) out.emplace_back(id, s);
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

// ============================================================================
// Hybrid Search Result
// ============================================================================

struct HybridResult {
    std::string chunk_id;
    double score;
    double vector_score = 0.0;
    double bm25_score = 0.0;
    double graph_score = 0.0;
};

struct HybridSearchParams {
    std::string query;
    size_t      top_k = 5;
    double      alpha = 0.5;   // vector weight
    double      beta  = 0.3;   // bm25 weight
    double      gamma = 0.2;   // graph weight
    size_t      candidate_multiplier = 5;  // fetch top_k * N from each source

    // Graph expansion options
    size_t      graph_max_hops = 2;
};

/**
 * Full hybrid search: vector + BM25 + graph expansion, fused with weighted sum.
 * Returns ranked list of HybridResult, sorted by combined score descending.
 */
std::vector<HybridResult> hybrid_search(
    ApolloCore& kb,
    const HybridSearchParams& params);

/**
 * Convert a HybridResult to JSON, including chunk content lookup.
 */
nlohmann::json hybrid_result_to_json(
    const HybridResult& r,
    ApolloCore& kb);

} // namespace apollocore_v2::search