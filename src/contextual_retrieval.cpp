/**
 * ApolloCore - Contextual Retrieval Implementation
 */

#include "apollocore/contextual_retrieval.hpp"
#include "llama.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cctype>

namespace apollocore_v2 {

// ============================================================================
// Llama State (PIMPL)
// ============================================================================

struct ContextGenerator::LlamaState {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    
    ~LlamaState() {
        if (ctx) {
            llama_free(ctx);
            ctx = nullptr;
        }
        if (model) {
            llama_model_free(model);
            model = nullptr;
        }
    }
};

// ============================================================================
// Configuration Factory Functions
// ============================================================================

ContextualRetrievalConfig default_contextual_config() {
    ContextualRetrievalConfig config;
    config.context_before = 1;
    config.context_after = 1;
    config.use_llm_context = false;
    config.max_context_tokens = 200;
    config.chunk_size = 512;
    config.chunk_overlap = 50;
    config.verbose = false;
    return config;
}

ContextualRetrievalConfig high_quality_contextual_config() {
    ContextualRetrievalConfig config = default_contextual_config();
    config.context_before = 2;
    config.context_after = 2;
    config.use_llm_context = true;
    config.max_context_tokens = 300;
    config.verbose = true;
    return config;
}

ContextualRetrievalConfig fast_contextual_config() {
    ContextualRetrievalConfig config = default_contextual_config();
    config.context_before = 1;
    config.context_after = 0;
    config.use_llm_context = false;
    config.chunk_size = 256;
    config.chunk_overlap = 25;
    return config;
}

// ============================================================================
// Utility Functions
// ============================================================================

size_t estimate_tokens(const std::string& text) {
    // Rough approximation: ~4 characters per token
    return text.length() / 4;
}

std::string truncate_to_tokens(const std::string& text, size_t max_tokens) {
    size_t max_chars = max_tokens * 4;
    if (text.length() <= max_chars) {
        return text;
    }
    return text.substr(0, max_chars) + "...";
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ContextGenerator::ContextGenerator(const ContextualRetrievalConfig& config) 
    : config_(config) {
    if (config_.use_llm_context && !config_.llm.model_path.empty()) {
        initialize();
    } else {
        // Still initialized even without LLM (for non-LLM context)
        initialized_ = true;
    }
}

ContextGenerator::~ContextGenerator() = default;

// ============================================================================
// Initialization
// ============================================================================

void ContextGenerator::initialize() {
    llama_backend_init();
    
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;
    
    llm_state_ = std::make_unique<LlamaState>();
    llm_state_->model = llama_model_load_from_file(
        config_.llm.model_path.c_str(), 
        model_params
    );
    
    if (!llm_state_->model) {
        if (config_.verbose) {
            std::cerr << "[ContextualRetrieval] Failed to load model\n";
        }
        llm_state_.reset();
        return;
    }
    
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.llm.n_ctx;
    ctx_params.n_threads = config_.llm.n_threads;
    
    llm_state_->ctx = llama_init_from_model(llm_state_->model, ctx_params);
    
    if (!llm_state_->ctx) {
        if (config_.verbose) {
            std::cerr << "[ContextualRetrieval] Failed to create context\n";
        }
        llama_model_free(llm_state_->model);
        llm_state_.reset();
        return;
    }
    
    initialized_ = true;
    
    if (config_.verbose) {
        std::cerr << "[ContextualRetrieval] Initialized\n";
    }
}

// ============================================================================
// Document Processing
// ============================================================================

std::vector<Chunk> ContextGenerator::split_into_chunks(const Document& doc) {
    std::vector<Chunk> chunks;
    
    // Simple character-based chunking
    // In production, use proper token-based splitting
    size_t chunk_size = config_.chunk_size * 4;  // Convert tokens to chars
    size_t overlap = config_.chunk_overlap * 4;
    
    std::string content = doc.content;
    size_t position = 0;
    size_t chunk_id = 0;
    
    while (position < content.length()) {
        size_t end = std::min(position + chunk_size, content.length());
        
        // Try to break at sentence boundary
        if (end < content.length()) {
            // Look for sentence end
            size_t sentence_end = content.find_last_of(".!?\n", end);
            if (sentence_end != std::string::npos && sentence_end > position + 50) {
                end = sentence_end + 1;
            }
        }
        
        std::string chunk_text = content.substr(position, end - position);
        
        Chunk chunk;
        chunk.id = doc.id + "_chunk_" + std::to_string(chunk_id);
        chunk.content = chunk_text;
        chunk.contextualized = chunk_text;
        chunk.document_id = doc.id;
        chunk.position = chunk_id;
        chunk.start_token = position / 4;
        chunk.end_token = end / 4;
        
        chunks.push_back(chunk);
        
        // Move position with overlap
        if (end >= content.length()) break;
        position = end - overlap;
        chunk_id++;
    }
    
    return chunks;
}

void ContextGenerator::add_document_context(std::vector<Chunk>& chunks, const Document& doc) {
    std::string doc_context = "[Document: " + doc.title + "]";
    
    if (!doc.document_type.empty()) {
        doc_context += " [" + doc.document_type + "]";
    }
    
    // Add metadata as context
    for (const auto& [key, value] : doc.metadata) {
        doc_context += " " + key + ": " + value;
    }
    
    for (auto& chunk : chunks) {
        chunk.context = doc_context;
        chunk.contextualized = chunk.context + "\n\n" + chunk.content;
    }
}

// ============================================================================
// Contextualization
// ============================================================================

Chunk ContextGenerator::contextualize_chunk(const Chunk& chunk,
                                           const std::vector<Chunk>& before,
                                           const std::vector<Chunk>& after) {
    Chunk result = chunk;
    
    // Build surrounding context string
    std::string before_context;
    std::string after_context;
    
    for (size_t i = 0; i < std::min(before.size(), config_.context_before); i++) {
        before_context = before[before.size() - 1 - i].content + "\n" + before_context;
    }
    
    for (size_t i = 0; i < std::min(after.size(), config_.context_after); i++) {
        after_context += after[i].content + "\n";
    }
    
    // Add surrounding context
    std::string context = chunk.context;  // Keep document context
    
    if (!before_context.empty()) {
        context += "\n\n[Previous context]\n" + before_context;
    }
    
    if (!after_context.empty()) {
        context += "\n[Following context]\n" + after_context;
    }
    
    result.context = context;
    
    // If LLM context is enabled, generate better context
    if (config_.use_llm_context && initialized_) {
        std::string llm_context = generate_llm_context(chunk, before, after);
        result.context = llm_context;
    }
    
    result.contextualized = result.context + "\n\n" + chunk.content;
    
    return result;
}

std::string ContextGenerator::create_contextualization_prompt(
    const std::string& chunk,
    const std::string& before_context,
    const std::string& after_context,
    const std::string& document_title
) {
    std::ostringstream prompt;
    prompt << R"(<|im_start|>system
You are a helpful assistant that provides brief contextual summaries.
Given a text chunk and its surrounding context, provide a 1-2 sentence summary
that helps clarify what this chunk is about. Focus on what came before and
what comes after to give proper context.
<|im_end|>
<|im_start|>user
Document: )" << document_title << R"(

Surrounding text before:
)" << (before_context.empty() ? "[none]" : before_context) << R"(

Text chunk to contextualize:
)" << chunk << R"(

Surrounding text after:
)" << (after_context.empty() ? "[none]" : after_context) << R"(

Provide context summary:
<|im_end|>
<|im_start|>assistant
)";
    return prompt.str();
}

std::string ContextGenerator::generate_llm_context(const Chunk& chunk,
                                                  const std::vector<Chunk>& before_chunks,
                                                  const std::vector<Chunk>& after_chunks) {
    if (!initialized_ || !llm_state_) {
        // Return basic context
        std::string basic;
        if (!chunk.context.empty()) {
            basic = chunk.context;
        }
        return basic;
    }
    
    // Build surrounding context
    std::string before_context;
    std::string after_context;
    
    for (size_t i = 0; i < std::min(before_chunks.size(), config_.context_before); i++) {
        before_context = before_chunks[before_chunks.size() - 1 - i].content + "\n" + before_context;
    }
    
    for (size_t i = 0; i < std::min(after_chunks.size(), config_.context_after); i++) {
        after_context += after_chunks[i].content + "\n";
    }
    
    std::string prompt = create_contextualization_prompt(
        chunk.content, before_context, after_context, "Document"
    );
    
    const llama_vocab* vocab = llama_model_get_vocab(llm_state_->model);
    
    // Tokenize
    std::vector<llama_token> tokens(llama_n_ctx(llm_state_->ctx));
    int n_tokens = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.length(),
        tokens.data(), (int)tokens.size(), true, true
    );
    
    if (n_tokens < 0) {
        return "[Context needed]";
    }
    tokens.resize(n_tokens);
    
    // Evaluate prompt
    for (int i = 0; i < n_tokens; i++) {
        llama_batch batch = llama_batch_get_one(&tokens[i], 1);
        llama_decode(llm_state_->ctx, batch);
    }
    
    // Generate response
    std::string response;
    llama_sampler* smpl = llama_sampler_init_greedy();
    
    for (int i = 0; i < (int)config_.max_context_tokens; i++) {
        llama_token new_token = llama_sampler_sample(smpl, llm_state_->ctx, -1);
        
        if (llama_vocab_is_eog(vocab, new_token)) {
            break;
        }
        
        char buf[64];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n > 0) {
            response.append(buf, n);
        }
        
        if (response.find('\n') != std::string::npos) {
            break;
        }
        
        llama_batch batch = llama_batch_get_one(&new_token, 1);
        llama_decode(llm_state_->ctx, batch);
    }
    
    llama_sampler_free(smpl);
    
    if (response.empty()) {
        return "[Context: " + chunk.content.substr(0, 50) + "...]";
    }
    
    return "[Context: " + response + "]";
}

// ============================================================================
// Main Processing
// ============================================================================

std::vector<Chunk> ContextGenerator::process_document(const Document& doc) {
    // Step 1: Split into chunks
    std::vector<Chunk> chunks = split_into_chunks(doc);
    
    if (chunks.empty()) {
        return chunks;
    }
    
    // Step 2: Add document-level context
    add_document_context(chunks, doc);
    
    // Step 3: Add surrounding context to each chunk
    for (size_t i = 0; i < chunks.size(); i++) {
        // Get chunks before
        std::vector<Chunk> before;
        if (i > 0) {
            size_t start = (i >= config_.context_before) ? i - config_.context_before : 0;
            for (size_t j = start; j < i; j++) {
                before.push_back(chunks[j]);
            }
        }
        
        // Get chunks after
        std::vector<Chunk> after;
        size_t end = std::min(chunks.size(), i + config_.context_after + 1);
        for (size_t j = i + 1; j < end; j++) {
            after.push_back(chunks[j]);
        }
        
        // Contextualize
        chunks[i] = contextualize_chunk(chunks[i], before, after);
    }
    
    if (config_.verbose) {
        std::cerr << "[ContextualRetrieval] Processed " << chunks.size() << " chunks\n";
    }
    
    return chunks;
}

} // namespace apollocore_v2
