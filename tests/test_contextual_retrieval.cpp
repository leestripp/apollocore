/**
 * ApolloCore - Contextual Retrieval Test
 */

#include "apollocore/contextual_retrieval.hpp"
#include <iostream>
#include <cassert>

using namespace apollocore_v2;

void test_config() {
    std::cout << "Test: ContextualRetrievalConfig Factory Functions... ";
    
    auto default_cfg = default_contextual_config();
    assert(default_cfg.context_before == 1);
    assert(default_cfg.context_after == 1);
    assert(default_cfg.use_llm_context == false);
    assert(default_cfg.chunk_size == 512);
    
    auto high_cfg = high_quality_contextual_config();
    assert(high_cfg.context_before == 2);
    assert(high_cfg.context_after == 2);
    assert(high_cfg.use_llm_context == true);
    
    auto fast_cfg = fast_contextual_config();
    assert(fast_cfg.context_before == 1);
    assert(fast_cfg.context_after == 0);
    assert(fast_cfg.use_llm_context == false);
    
    std::cout << "PASSED\n";
}

void test_chunk() {
    std::cout << "Test: Chunk Structure... ";
    
    Chunk c1;
    assert(c1.id.empty());
    assert(c1.content.empty());
    assert(c1.contextualized.empty());
    
    Chunk c2("chunk_1", "This is test content");
    assert(c2.id == "chunk_1");
    assert(c2.content == "This is test content");
    assert(c2.contextualized == "This is test content");
    
    std::cout << "PASSED\n";
}

void test_document() {
    std::cout << "Test: Document Structure... ";
    
    Document doc1;
    assert(doc1.id.empty());
    assert(doc1.title.empty());
    
    Document doc2("doc_1", "Test Title", "This is the content");
    assert(doc2.id == "doc_1");
    assert(doc2.title == "Test Title");
    assert(doc2.content == "This is the content");
    
    // Add metadata
    doc2.metadata["author"] = "John Doe";
    doc2.metadata["year"] = "2024";
    assert(doc2.metadata["author"] == "John Doe");
    
    std::cout << "PASSED\n";
}

void test_split_into_chunks() {
    std::cout << "Test: Split Into Chunks... ";
    
    ContextualRetrievalConfig config = fast_contextual_config();
    config.chunk_size = 50;  // Small chunks for testing
    config.chunk_overlap = 10;
    
    ContextGenerator generator(config);
    
    Document doc("test", "Test Document", 
        "This is the first sentence. This is the second sentence. "
        "This is the third sentence. This is the fourth sentence. "
        "This is the fifth sentence.");
    
    auto chunks = generator.split_into_chunks(doc);
    
    assert(!chunks.empty());
    assert(chunks[0].document_id == "test");
    assert(chunks[0].position == 0);
    
    std::cout << "PASSED (created " << chunks.size() << " chunks)\n";
}

void test_document_context() {
    std::cout << "Test: Document-Level Context... ";
    
    ContextualRetrievalConfig config;
    ContextGenerator generator(config);
    
    Document doc("doc_1", "Einstein's Biography", "Content here...");
    doc.document_type = "biography";
    doc.metadata["author"] = "Jane Smith";
    
    auto chunks = generator.split_into_chunks(doc);
    
    // Add document context
    generator.add_document_context(chunks, doc);
    
    assert(!chunks.empty());
    assert(chunks[0].context.find("Einstein's Biography") != std::string::npos);
    assert(chunks[0].context.find("biography") != std::string::npos);
    assert(chunks[0].contextualized.find("Einstein's Biography") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_contextualize_chunk() {
    std::cout << "Test: Chunk Contextualization... ";
    
    ContextualRetrievalConfig config;
    config.context_before = 1;
    config.context_after = 1;
    ContextGenerator generator(config);
    
    Chunk target("target", "The theory was revolutionary.");
    std::vector<Chunk> before = {Chunk("b1", "Previous paragraph about physics.")};
    std::vector<Chunk> after = {Chunk("a1", "This changed modern science.")};
    
    auto result = generator.contextualize_chunk(target, before, after);
    
    assert(!result.contextualized.empty());
    assert(result.contextualized.find("The theory was revolutionary.") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_process_document() {
    std::cout << "Test: Full Document Processing... ";
    
    ContextualRetrievalConfig config;
    config.context_before = 1;
    config.context_after = 1;
    config.verbose = false;
    
    ContextGenerator generator(config);
    
    Document doc;
    doc.id = "science_doc";
    doc.title = "Introduction to Quantum Mechanics";
    doc.content = 
        "Quantum mechanics is a fundamental theory in physics. "
        "It provides a description of the physical properties of nature at the atomic scale. "
        "The theory was developed in the early 20th century. "
        "It forms the basis for understanding atomic structure and chemical bonding.";
    doc.document_type = "educational";
    
    auto chunks = generator.process_document(doc);
    
    assert(!chunks.empty());
    
    // All chunks should have contextualized content
    for (const auto& chunk : chunks) {
        assert(!chunk.contextualized.empty());
        assert(chunk.contextualized.find(chunk.content) != std::string::npos);
        (void)chunk;
    }
    
    std::cout << "PASSED (processed " << chunks.size() << " chunks)\n";
}

void test_contextual_retrieval_result() {
    std::cout << "Test: ContextualRetrievalResult... ";
    
    ContextualRetrievalResult result;
    assert(result.total_chunks() == 0);
    
    Chunk c1("1", "Content 1");
    Chunk c2("2", "Content 2");
    result.chunks.push_back(c1);
    result.chunks.push_back(c2);
    
    assert(result.total_chunks() == 2);
    
    std::cout << "PASSED\n";
}

void test_token_utilities() {
    std::cout << "Test: Token Utility Functions... ";
    
    std::string short_text = "Short text";
    std::string long_text = "This is a much longer piece of text that should be truncated when we call truncate_to_tokens.";
    
    size_t short_tokens = estimate_tokens(short_text);
    size_t long_tokens = estimate_tokens(long_text);
    
    assert(short_tokens > 0);
    assert(long_tokens > short_tokens);
    (void)short_tokens;
    (void)long_tokens;
    
    std::string truncated = truncate_to_tokens(long_text, 10);
    assert(truncated.length() <= 50);  // Should be much shorter
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - Contextual Retrieval Tests            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    test_config();
    test_chunk();
    test_document();
    test_split_into_chunks();
    test_document_context();
    test_contextualize_chunk();
    test_process_document();
    test_contextual_retrieval_result();
    test_token_utilities();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All Contextual Retrieval Tests PASSED               ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Configuration presets: PASSED\n";
    std::cout << "   • Chunk structure: PASSED\n";
    std::cout << "   • Document structure: PASSED\n";
    std::cout << "   • Chunk splitting: PASSED\n";
    std::cout << "   • Document context: PASSED\n";
    std::cout << "   • Chunk contextualization: PASSED\n";
    std::cout << "   • Full document processing: PASSED\n";
    std::cout << "   • Result structure: PASSED\n";
    std::cout << "   • Token utilities: PASSED\n";
    std::cout << "\n";
    std::cout << "📦 Usage:\n";
    std::cout << "   1. Create ContextGenerator with config\n";
    std::cout << "   2. Call process_document() with a Document\n";
    std::cout << "   3. Each chunk now has contextualized content\n";
    std::cout << "   4. Use contextualized content for embeddings\n";
    std::cout << "\n";
    
    return 0;
}
