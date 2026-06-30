/**
 * ApolloCore Hybrid Search Implementation (Phase M2.7)
 *
 * Combines vector search + BM25 + graph expansion with RRF.
 */

#include "apollocore/apollo_hybrid_search.hpp"
#include "apollocore/apollo_persistence.hpp"  // For ApolloCore full definition
#include "apollocore/entity_extractor.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace apollocore_v2::search {

/**
 * Graph expansion: find entities in KB that match query terms,
 * then boost chunks that contain those entities.
 *
 * Simple heuristic (Phase 1):
 *   - Tokenize query, look for token matches against entity names
 *   - Boost chunks that mention matched entities
 *   - Score = 1.0 / (1 + distance_in_hops)
 *
 * Future: use LLM to extract query entities, do fuzzy match, traverse.
 */
inline std::unordered_map<std::string, double> graph_expand(
    ApolloCore& kb,
    const std::string& query,
    size_t /*max_hops*/)
{
    std::unordered_map<std::string, double> chunk_boost;

    auto qtokens = BM25Index::tokenize(query);
    std::unordered_set<std::string> qset(qtokens.begin(), qtokens.end());

    // Get all entities in the KB (lazy: only if KB is open)
    if (!kb.is_open()) return chunk_boost;

    auto all_entities = kb.get_entities(10000);
    std::unordered_map<std::string, std::vector<std::string>> entity_to_chunks;

    // Build entity name -> chunks mapping
    for (const auto& e : all_entities) {
        // Check if query contains this entity name (case-insensitive)
        std::string ename_lower = e.name;
        std::transform(ename_lower.begin(), ename_lower.end(),
                      ename_lower.begin(), ::tolower);
        std::string q_lower = query;
        std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);

        if (q_lower.find(ename_lower) != std::string::npos) {
            // Entity matched. Boost chunks for this entity.
            double boost = 1.0;
            chunk_boost[e.id] = boost;  // e.id is "doc_X_chunk_Y_entity_Z"
            // Extract chunk_id from entity id (everything before "_entity_")
            auto pos = e.id.find("_entity_");
            if (pos != std::string::npos) {
                std::string chunk_id = e.id.substr(0, pos);
                // Boost is stronger for direct entity match, weaker for hop away
                chunk_boost[chunk_id] = std::max(chunk_boost[chunk_id], 1.0);
            }
        }
    }

    return chunk_boost;
}

/**
 * Full hybrid search: vector + BM25 + graph + RRF.
 *
 * @param kb ApolloCore instance with loaded embedding model
 * @param params search parameters
 * @return ranked list of HybridResult
 */
std::vector<HybridResult> hybrid_search(
    ApolloCore& kb,
    const HybridSearchParams& params)
{
    if (!kb.is_open()) return {};

    // ========================================================================
    // 1. Vector search (alpha)
    // ========================================================================
    size_t n_candidates = params.top_k * params.candidate_multiplier;
    auto vector_results = kb.query(params.query, n_candidates);

    // Normalize vector scores to [0, 1]
    double v_max = 0.0;
    for (const auto& [_, s] : vector_results) v_max = std::max(v_max, static_cast<double>(s));
    std::unordered_map<std::string, double> vector_scores;
    for (const auto& [id, s] : vector_results) {
        vector_scores[id] = v_max > 0.0 ? static_cast<double>(s) / v_max : 0.0;
    }

    // ========================================================================
    // 2. BM25 search (beta)
    // ========================================================================
    // Build BM25 index from all chunks (could be cached, but rebuild for now)
    BM25Index bm25;
    {
        auto chunks = kb.get_all_chunks();
        std::vector<std::pair<std::string, std::string>> docs;
        docs.reserve(chunks.size());
        for (const auto& [id, content] : chunks) {
            docs.emplace_back(id, content);
        }
        bm25.build(docs);
    }
    auto bm25_results = bm25.search(params.query, n_candidates);

    // Normalize BM25 scores to [0, 1]
    double b_max = 0.0;
    for (const auto& [_, s] : bm25_results) b_max = std::max(b_max, s);
    std::unordered_map<std::string, double> bm25_scores;
    for (const auto& [id, s] : bm25_results) {
        bm25_scores[id] = b_max > 0.0 ? s / b_max : 0.0;
    }

    // ========================================================================
    // 3. Graph expansion (gamma)
    // ========================================================================
    auto graph_scores = graph_expand(kb, params.query, params.graph_max_hops);

    // ========================================================================
    // 4. Fuse using weighted sum (not RRF — we want continuous scores)
    // ========================================================================
    // Collect all chunk IDs
    std::set<std::string> all_ids;
    for (const auto& [id, _] : vector_results) all_ids.insert(id);
    for (const auto& [id, _] : bm25_results) all_ids.insert(id);

    std::vector<HybridResult> fused;
    fused.reserve(all_ids.size());
    for (const auto& id : all_ids) {
        double vs = vector_scores.count(id) ? vector_scores[id] : 0.0;
        double bs = bm25_scores.count(id)  ? bm25_scores[id]  : 0.0;
        double gs = graph_scores.count(id) ? graph_scores[id] : 0.0;

        // Weighted sum
        double combined = params.alpha * vs +
                         params.beta  * bs +
                         params.gamma * gs;

        HybridResult r;
        r.chunk_id = id;
        r.score = combined;
        r.vector_score = vs;
        r.bm25_score = bs;
        r.graph_score = gs;
        fused.push_back(r);
    }

    // Sort by combined score descending
    std::sort(fused.begin(), fused.end(),
              [](const HybridResult& a, const HybridResult& b) {
                  return a.score > b.score;
              });

    if (fused.size() > params.top_k) fused.resize(params.top_k);

    return fused;
}

/**
 * Convert HybridResult to JSON.
 */
nlohmann::json hybrid_result_to_json(
    const HybridResult& r,
    ApolloCore& kb)
{
    // Look up content for the chunk
    auto all_chunks = kb.get_all_chunks();
    std::string content;
    for (const auto& [cid, ctext] : all_chunks) {
        if (cid == r.chunk_id) {
            content = ctext;
            break;
        }
    }
    return {
        {"chunk_id", r.chunk_id},
        {"score", r.score},
        {"vector_score", r.vector_score},
        {"bm25_score", r.bm25_score},
        {"graph_score", r.graph_score},
        {"content", content}
    };
}

} // namespace apollocore_v2::search