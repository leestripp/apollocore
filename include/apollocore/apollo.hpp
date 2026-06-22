#pragma once

/**
 * ApolloCore - The Unified Knowledge Base Library
 * 
 * This is the single header you need to include to use ApolloCore.
 * It provides the `ApolloCore` class with everything you need:
 * 
 *   - Local LLM/embedding inference (via llama.cpp)
 *   - Vector search + BM25 keyword search (hybrid)
 *   - Knowledge graph (entities + relationships)
 *   - Persistent storage (SQLite + binary embeddings)
 *   - Auto-persistence on document add
 * 
 * Usage:
 * 
 *   #include <apollocore/apollo.hpp>
 *   
 *   apollocore_v2::ApolloCore kb;
 *   kb.load_embedding_model("models/embeddings.gguf");
 *   
 *   kb.create("data/my_kb", "My Knowledge Base");
 *   kb.add_document("Some text to add...");
 *   
 *   auto results = kb.query("search query", 5);
 *   for (const auto& [content, score] : results) {
 *       std::cout << score << ": " << content << "\n";
 *   }
 * 
 * For more advanced use cases (NodeHandle, ChunkHandle, BM25 tuning,
 * knowledge graph internals), include the individual module headers
 * directly.
 */

#include "apollocore/apollo_persistence.hpp"

// Re-export the main namespace for convenience
namespace apollocore {

// Bring the unified ApolloCore class into the short namespace too
using ApolloCore = apollocore_v2::ApolloCore;

} // namespace apollocore
