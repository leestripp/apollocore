/**
 * ApolloCore MCP Prompts — Built-in implementations (Phase M2.B)
 *
 * Six useful prompt templates that AI clients can invoke:
 *
 *   1. summarize_kb        — Get a high-level overview of the KB
 *   2. explain_entity      — Explain an entity with its relationships
 *   3. compare_documents   — Compare two documents
 *   4. find_similar        — Find documents similar to a query
 *   5. knowledge_qa        — Answer a question using KB context
 *   6. graph_explore       — Explore the knowledge graph from a seed
 *
 * Each prompt is a multi-message conversation template that the AI
 * client (Claude, etc.) sees as if the user typed these messages.
 */

#include "apollocore/apollo_mcp_prompts.hpp"
#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_hybrid_search.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

namespace apollocore_v2::mcp {

using json = nlohmann::json;

namespace {

// Trim a string to N characters with ellipsis
std::string trim_to(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n) + "...";
}

// ============================================================================
// 1. summarize_kb — high-level KB overview
// ============================================================================

std::vector<PromptMessage> render_summarize_kb(ApolloCore& kb, const json& args) {
    auto stats = kb.get_stats();
    auto entities = kb.get_entities(10000);
    auto rels = kb.get_relationships(10000);

    // Group entities by type
    std::unordered_map<EntityType, int> type_counts;
    for (const auto& e : entities) {
        type_counts[e.type]++;
    }

    // Top 10 most connected entities (by rel count)
    std::unordered_map<std::string, int> degree;
    for (const auto& r : rels) {
        degree[r.source]++;
        degree[r.target]++;
    }
    std::vector<std::pair<std::string, int>> sorted(degree.begin(), degree.end());
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b) { return a.second > b.second; });

    std::ostringstream summary;
    summary << "This knowledge base contains:\n\n";
    summary << "**Counts:**\n";
    summary << "- " << stats.document_count << " documents\n";
    summary << "- " << stats.chunk_count << " chunks\n";
    summary << "- " << stats.entity_count << " entities\n";
    summary << "- " << stats.relationship_count << " relationships\n\n";

    if (!type_counts.empty()) {
        summary << "**Entity types:**\n";
        for (const auto& [type, count] : type_counts) {
            summary << "- " << entity_type_to_string(type) << ": " << count << "\n";
        }
        summary << "\n";
    }

    if (!sorted.empty()) {
        summary << "**Most connected entities (top 10):**\n";
        for (size_t i = 0; i < std::min(size_t(10), sorted.size()); ++i) {
            summary << "- " << sorted[i].first << " (" << sorted[i].second << " connections)\n";
        }
    }

    std::string user_msg = "Please provide a high-level summary of this knowledge base:\n\n";
    user_msg += summary.str();
    user_msg += "\n\nIdentify the main topics, key entities, and any notable patterns.";

    return {
        {"user", user_msg}
    };
}

// ============================================================================
// 2. explain_entity — entity with its relationships
// ============================================================================

std::vector<PromptMessage> render_explain_entity(ApolloCore& kb, const json& args) {
    std::string entity_name = prompt_str_arg(args, "entity_name");
    int max_rels = prompt_int_arg(args, "max_relationships", 20);

    if (entity_name.empty()) {
        throw McpError(ErrorCode::InvalidParams,
            "explain_entity requires 'entity_name'");
    }

    auto entities = kb.get_entities(100000);
    auto rels = kb.get_relationships(100000);

    // Find entity by name (substring match)
    std::string n_lower = entity_name;
    std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
    const Entity* found = nullptr;
    for (const auto& e : entities) {
        std::string ename_lower = e.name;
        std::transform(ename_lower.begin(), ename_lower.end(),
                      ename_lower.begin(), ::tolower);
        if (ename_lower.find(n_lower) != std::string::npos) {
            found = &e;
            break;
        }
    }

    if (!found) {
        return {
            {"user", "I asked about the entity \"" + entity_name +
                     "\" but it doesn't exist in the knowledge base. " +
                     "Available entities include: " +
                     (entities.empty() ? "none" : "see the list") +
                     ". Please suggest alternatives or create a new KB entry."}
        };
    }

    // Find all relationships involving this entity
    std::vector<std::string> outgoing, incoming;
    for (const auto& r : rels) {
        std::string type_str = relation_type_to_string(r.type);
        if (r.source == found->name) outgoing.push_back(type_str + " -> " + r.target);
        if (r.target == found->name) incoming.push_back(r.source + " -> " + type_str);
    }

    std::ostringstream ctx;
    ctx << "Entity: **" << found->name << "**\n";
    ctx << "Type: " << entity_type_to_string(found->type) << "\n";
    if (!found->description.empty()) {
        ctx << "Description: " << found->description << "\n";
    }
    ctx << "\n";

    if (!outgoing.empty()) {
        ctx << "**Outgoing relationships (" << outgoing.size() << "):**\n";
        for (size_t i = 0; i < std::min(size_t(max_rels), outgoing.size()); ++i) {
            ctx << "- " << outgoing[i] << "\n";
        }
    }
    if (!incoming.empty()) {
        ctx << "\n**Incoming relationships (" << incoming.size() << "):**\n";
        for (size_t i = 0; i < std::min(size_t(max_rels), incoming.size()); ++i) {
            ctx << "- " << incoming[i] << "\n";
        }
    }

    return {
        {"user", "Please explain the entity \"" + found->name + "\" based on this knowledge graph context:\n\n" + ctx.str() +
                 "\n\nInclude its type, what it's connected to, and any patterns you see."}
    };
}

// ============================================================================
// 3. compare_documents — side-by-side comparison
// ============================================================================

std::vector<PromptMessage> render_compare_documents(ApolloCore& kb, const json& args) {
    std::string doc_a = prompt_str_arg(args, "doc_a");
    std::string doc_b = prompt_str_arg(args, "doc_b");

    if (doc_a.empty() || doc_b.empty()) {
        throw McpError(ErrorCode::InvalidParams,
            "compare_documents requires 'doc_a' and 'doc_b'");
    }

    // Naive: use search to find documents by title prefix.
    // For a real impl, we'd have list_documents() with full content.
    auto docs = kb.get_all_documents();
    const std::string* content_a = nullptr;
    const std::string* content_b = nullptr;
    for (const auto& [id, content] : docs) {
        if (id == doc_a) content_a = &content;
        if (id == doc_b) content_b = &content;
    }

    std::ostringstream ctx;
    ctx << "**Document A** (" << doc_a << "):\n";
    ctx << (content_a ? trim_to(*content_a, 2000) : "[not found]") << "\n\n";
    ctx << "**Document B** (" << doc_b << "):\n";
    ctx << (content_b ? trim_to(*content_b, 2000) : "[not found]") << "\n\n";

    return {
        {"user", "Please compare these two documents:\n\n" + ctx.str() +
                 "\n\nIdentify similarities, differences, key themes, and any contradictions."}
    };
}

// ============================================================================
// 4. find_similar — discover related documents
// ============================================================================

std::vector<PromptMessage> render_find_similar(ApolloCore& kb, const json& args) {
    std::string topic = prompt_str_arg(args, "topic");
    int top_k = prompt_int_arg(args, "top_k", 5);
    if (top_k <= 0 || top_k > 50) top_k = 5;

    if (topic.empty()) {
        throw McpError(ErrorCode::InvalidParams,
            "find_similar requires 'topic'");
    }

    // Use hybrid search for best results
    apollocore_v2::search::HybridSearchParams params;
    params.query = topic;
    params.top_k = static_cast<size_t>(top_k);

    std::vector<apollocore_v2::search::HybridResult> results;
    try {
        results = apollocore_v2::search::hybrid_search(kb, params);
    } catch (...) {
        // Fall back to vector search if hybrid not available
        auto fallback = kb.query(topic, top_k);
        std::ostringstream ctx;
        ctx << "Topic: " << topic << "\n\n";
        ctx << "Results (vector search only):\n";
        for (const auto& [id, score] : fallback) {
            ctx << "- chunk " << id << " (score: " << score << ")\n";
        }
        return {
            {"user", "Find documents related to \"" + topic + "\". Results:\n\n" + ctx.str() +
                     "\n\nSummarize what's in this KB about that topic."}
        };
    }

    // Build a chunk_id -> text map for content display
    auto chunks = kb.get_all_chunks();
    std::unordered_map<std::string, std::string> chunk_texts;
    for (const auto& [cid, text] : chunks) chunk_texts[cid] = text;

    std::ostringstream ctx;
    ctx << "Topic: " << topic << "\n\n";
    ctx << "Top " << results.size() << " matches (hybrid search):\n\n";
    for (size_t i = 0; i < results.size(); ++i) {
        ctx << "**Match " << (i+1) << "** (combined score: "
            << results[i].score << ")\n";
        ctx << "Vector: " << results[i].vector_score
            << " | BM25: " << results[i].bm25_score
            << " | Graph: " << results[i].graph_score << "\n";
        auto it = chunk_texts.find(results[i].chunk_id);
        if (it != chunk_texts.end()) {
            ctx << "> " << trim_to(it->second, 200) << "\n";
        }
        ctx << "\n";
    }

    return {
        {"user", "Here are the most relevant chunks in the knowledge base for the topic \"" + topic + "\":\n\n" +
                 ctx.str() + "\n\nPlease synthesize this into a coherent answer."}
    };
}

// ============================================================================
// 5. knowledge_qa — answer a question with KB context
// ============================================================================

std::vector<PromptMessage> render_knowledge_qa(ApolloCore& kb, const json& args) {
    std::string question = prompt_str_arg(args, "question");
    int top_k = prompt_int_arg(args, "top_k", 8);
    if (top_k <= 0 || top_k > 30) top_k = 8;

    if (question.empty()) {
        throw McpError(ErrorCode::InvalidParams,
            "knowledge_qa requires 'question'");
    }

    apollocore_v2::search::HybridSearchParams params;
    params.query = question;
    params.top_k = static_cast<size_t>(top_k);

    // Build a chunk_id -> text map
    auto chunks = kb.get_all_chunks();
    std::unordered_map<std::string, std::string> chunk_texts;
    for (const auto& [cid, text] : chunks) chunk_texts[cid] = text;

    std::ostringstream ctx;
    try {
        auto results = apollocore_v2::search::hybrid_search(kb, params);
        ctx << "Question: " << question << "\n\n";
        ctx << "Context from knowledge base (" << results.size() << " chunks):\n\n";
        for (size_t i = 0; i < results.size(); ++i) {
            auto it = chunk_texts.find(results[i].chunk_id);
            std::string text = (it != chunk_texts.end()) ? it->second : "";
            ctx << "[" << (i+1) << "] " << trim_to(text, 400) << "\n\n";
        }
    } catch (...) {
        auto fallback = kb.query(question, top_k);
        ctx << "Question: " << question << "\n\n";
        ctx << "Context (vector search fallback):\n\n";
        for (const auto& [id, score] : fallback) {
            ctx << "- chunk " << id << " (score " << score << ")\n";
        }
    }

    return {
        {"user", "Answer this question using only the provided context. "
                 "If the answer isn't in the context, say so.\n\n" + ctx.str() +
                 "\n\nQuestion: " + question}
    };
}

// ============================================================================
// 6. graph_explore — start a graph exploration from a seed entity
// ============================================================================

std::vector<PromptMessage> render_graph_explore(ApolloCore& kb, const json& args) {
    std::string entity = prompt_str_arg(args, "entity");
    int hops = prompt_int_arg(args, "hops", 2);
    if (hops <= 0 || hops > 4) hops = 2;

    if (entity.empty()) {
        throw McpError(ErrorCode::InvalidParams,
            "graph_explore requires 'entity'");
    }

    // Use get_neighbors
    auto entities = kb.get_entities(100000);
    auto rels = kb.get_relationships(100000);

    std::string n_lower = entity;
    std::transform(n_lower.begin(), n_lower.end(), n_lower.begin(), ::tolower);
    std::string start_name;
    for (const auto& e : entities) {
        std::string ename_lower = e.name;
        std::transform(ename_lower.begin(), ename_lower.end(),
                      ename_lower.begin(), ::tolower);
        if (ename_lower.find(n_lower) != std::string::npos) {
            start_name = e.name;
            break;
        }
    }

    if (start_name.empty()) {
        return {
            {"user", "Entity \"" + entity + "\" not found. Please choose a different starting point."}
        };
    }

    // Build adjacency (undirected)
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& r : rels) {
        adj[r.source].push_back(r.target + " (" + relation_type_to_string(r.type) + ")");
        adj[r.target].push_back(r.source + " (" + relation_type_to_string(r.type) + ")");
    }

    // BFS up to N hops
    std::unordered_set<std::string> visited;
    std::queue<std::pair<std::string, int>> q;
    q.push({start_name, 0});
    visited.insert(start_name);

    std::ostringstream ctx;
    ctx << "Starting from: **" << start_name << "**\n";
    ctx << "Exploring " << hops << " hop" << (hops == 1 ? "" : "s") << ":\n\n";

    while (!q.empty()) {
        auto [cur, depth] = q.front();
        q.pop();
        ctx << "- " << cur;
        if (depth > 0) ctx << " (depth " << depth << ")";
        ctx << "\n";
        if (depth >= hops) continue;
        auto it = adj.find(cur);
        if (it == adj.end()) continue;
        for (const auto& nb : it->second) {
            // Extract just the name from "name (rel)"
            size_t paren = nb.find(" (");
            std::string name = nb.substr(0, paren);
            if (!visited.count(name)) {
                visited.insert(name);
                q.push({name, depth + 1});
            }
        }
    }

    return {
        {"user", "Explore the knowledge graph starting from \"" + start_name + "\". "
                 "Here are the entities within " + std::to_string(hops) + " hops:\n\n" +
                 ctx.str() + "\n\nWhat patterns or interesting connections do you see?"}
    };
}

} // namespace

void register_all_prompts(PromptRegistry& registry, ApolloCore& kb) {
    registry.register_prompt({
        "summarize_kb",
        "Get a high-level overview of the knowledge base: document count, "
        "entity types, most connected entities, and key topics.",
        {},
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_summarize_kb(kb, args);
        }
    });

    registry.register_prompt({
        "explain_entity",
        "Explain an entity using its description and relationships from the KB. "
        "Useful for 'tell me about X' queries.",
        {
            {"entity_name", "Entity name to explain (substring match)", true},
            {"max_relationships", "Max relationships to include (default 20)", false}
        },
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_explain_entity(kb, args);
        }
    });

    registry.register_prompt({
        "compare_documents",
        "Compare two documents side-by-side. Identifies similarities, "
        "differences, key themes, and contradictions.",
        {
            {"doc_a", "First document ID (e.g. doc_1)", true},
            {"doc_b", "Second document ID (e.g. doc_2)", true}
        },
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_compare_documents(kb, args);
        }
    });

    registry.register_prompt({
        "find_similar",
        "Find documents/chunks in the KB related to a topic using hybrid search.",
        {
            {"topic", "Topic to search for", true},
            {"top_k", "Number of results (default 5, max 50)", false}
        },
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_find_similar(kb, args);
        }
    });

    registry.register_prompt({
        "knowledge_qa",
        "Answer a question using only the KB context. Returns 'I don't know' "
        "if the answer isn't in the context.",
        {
            {"question", "Question to answer", true},
            {"top_k", "Number of context chunks (default 8)", false}
        },
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_knowledge_qa(kb, args);
        }
    });

    registry.register_prompt({
        "graph_explore",
        "Explore the knowledge graph from a seed entity up to N hops. "
        "Useful for discovering connections.",
        {
            {"entity", "Starting entity name (substring match)", true},
            {"hops", "Max hops to explore (default 2, max 4)", false}
        },
        [&kb](const json& args) -> std::vector<PromptMessage> {
            return render_graph_explore(kb, args);
        }
    });
}

} // namespace apollocore_v2::mcp