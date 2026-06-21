/**
 * ApolloCore - ONNX Runtime Embedding Model
 * 
 * Uses ONNX Runtime for running embedding models.
 * Alternative to llama.cpp for embeddings.
 */

#include "apollocore/apollocore.hpp"

#include <onnxruntime_cxx_api.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace apollocore {

// ============================================================================
// ONNX Embedding Model
// ============================================================================

class OnnxEmbeddingModel {
public:
    struct Impl {
        Ort::Env env;
        Ort::Session session;
        Ort::AllocatorWithDefaultOptions allocator;
        
        std::vector<const char*> input_names;
        std::vector<const char*> output_names;
        
        int64_t max_seq_length = 256;
        
        ~Impl() {}
    };
    
    std::unique_ptr<Impl> pimpl;
    size_t dimensions_{384};
    std::string model_path_;
    bool is_loaded_ = false;
    
public:
    OnnxEmbeddingModel();
    explicit OnnxEmbeddingModel(const std::string& model_path);
    ~OnnxEmbeddingModel();
    
    bool load(const std::string& path);
    bool is_loaded() const { return is_loaded_; }
    size_t dimensions() const { return dimensions_; }
    std::string model_name() const { return model_path_; }
    
    // Embed a single text
    EmbeddingResult embed(std::string_view text);
    
    // Embed multiple texts (batch processing)
    std::vector<EmbeddingResult> embed_batch(const std::vector<std::string>& texts);
    
private:
    std::vector<int64_t> tokenize(std::string_view text);
    EmbeddingVector run_inference(const std::vector<int64_t>& input_ids, const std::vector<int64_t>& attention_mask);
    void normalize(EmbeddingVector& vec);
};

OnnxEmbeddingModel::OnnxEmbeddingModel() : pimpl(std::make_unique<Impl>()) {
    pimpl->env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "OnnxEmbedding");
}

OnnxEmbeddingModel::OnnxEmbeddingModel(const std::string& model_path) : pimpl(std::make_unique<Impl>()) {
    pimpl->env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "OnnxEmbedding");
    load(model_path);
}

OnnxEmbeddingModel::~OnnxEmbeddingModel() = default;

bool OnnxEmbeddingModel::load(const std::string& path) {
    model_path_ = path;
    
    try {
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetIntraOpNumThreads(4);
        
        pimpl->session = Ort::Session(pimpl->env, path.c_str(), session_options);
        
        // Get input/output names
        size_t num_input_nodes = pimpl->session.GetInputCount();
        size_t num_output_nodes = pimpl->session.GetOutputCount();
        
        printf("ONNX Model loaded: %s\n", path.c_str());
        printf("  Input nodes: %zu\n", num_input_nodes);
        printf("  Output nodes: %zu\n", num_output_nodes);
        
        // Get first input name (usually "input_ids")
        if (num_input_nodes > 0) {
            auto input_name = pimpl->session.GetInputNameAllocated(0, pimpl->allocator);
            pimpl->input_names.push_back(input_name.get());
            printf("  Input name: %s\n", pimpl->input_names[0]);
        }
        
        // Get first output name (usually "last_hidden_state" or "embeddings")
        if (num_output_nodes > 0) {
            auto output_name = pimpl->session.GetOutputNameAllocated(0, pimpl->allocator);
            pimpl->output_names.push_back(output_name.get());
            printf("  Output name: %s\n", pimpl->output_names[0]);
        }
        
        is_loaded_ = true;
        return true;
        
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "Failed to load ONNX model: %s\n", e.what());
        return false;
    }
}

// Simple BERT tokenizer
std::vector<int64_t> OnnxEmbeddingModel::tokenize(std::string_view text) {
    // Vocab - this is a simplified version
    // In production, load the actual vocab file
    static std::unordered_map<std::string, int64_t> vocab = {
        {"[CLS]", 101}, {"[SEP]", 102}, {"[PAD]", 0}, {"[UNK]", 100},
        {"the", 1996}, {"a", 1997}, {"an", 2030}, {"is", 2003}, {"are", 2024},
        {"and", 1998}, {"of", 1999}, {"in", 1997}, {"to", 2000}, {"for", 2005},
    };
    
    std::vector<int64_t> tokens;
    tokens.push_back(101); // [CLS]
    
    std::string current_word;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current_word += std::tolower(static_cast<unsigned char>(c));
        } else if (!current_word.empty()) {
            auto it = vocab.find(current_word);
            if (it != vocab.end()) {
                tokens.push_back(it->second);
            } else {
                tokens.push_back(100); // [UNK]
            }
            current_word.clear();
        }
    }
    if (!current_word.empty()) {
        auto it = vocab.find(current_word);
        if (it != vocab.end()) {
            tokens.push_back(it->second);
        } else {
            tokens.push_back(100); // [UNK]
        }
    }
    
    tokens.push_back(102); // [SEP]
    
    // Pad to max_seq_length
    while (tokens.size() < (size_t)pimpl->max_seq_length) {
        tokens.push_back(0); // [PAD]
    }
    if (tokens.size() > (size_t)pimpl->max_seq_length) {
        tokens.resize(pimpl->max_seq_length);
    }
    
    return tokens;
}

EmbeddingVector OnnxEmbeddingModel::run_inference(
    const std::vector<int64_t>& input_ids,
    const std::vector<int64_t>& attention_mask) {
    
    if (!is_loaded_) return EmbeddingVector(dimensions_, 0.0f);
    
    try {
        // Prepare input tensor
        std::array<int64_t, 2> input_shape = {1, (int64_t)input_ids.size()};
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        
        auto input_tensor = Ort::Value::CreateTensor<int64_t>(
            mem_info,
            const_cast<int64_t*>(input_ids.data()),
            input_ids.size(),
            input_shape.data(),
            2
        );
        
        auto attention_tensor = Ort::Value::CreateTensor<int64_t>(
            mem_info,
            const_cast<int64_t*>(attention_mask.data()),
            attention_mask.size(),
            input_shape.data(),
            2
        );
        
        std::vector<Ort::Value> input_tensors;
        input_tensors.push_back(std::move(input_tensor));
        input_tensors.push_back(std::move(attention_tensor));
        
        // Run inference
        auto output_tensors = pimpl->session.Run(
            Ort::RunOptions{nullptr},
            pimpl->input_names.data(),
            input_tensors.data(),
            input_tensors.size(),
            pimpl->output_names.data(),
            pimpl->output_names.size()
        );
        
        // Get embeddings (shape: [1, seq_len, hidden_dim])
        auto& output = output_tensors[0];
        float* embeddings = output.GetTensorMutableData<float>();
        
        // Get output shape
        auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
        
        // Mean pool over sequence dimension (skip [CLS] and [SEP])
        size_t hidden_dim = shape[2];
        dimensions_ = hidden_dim;
        
        EmbeddingVector result(hidden_dim, 0.0f);
        int count = 0;
        for (size_t i = 1; i < shape[1] - 1; ++i) { // Skip CLS and SEP
            for (size_t j = 0; j < hidden_dim; ++j) {
                result[j] += embeddings[i * hidden_dim + j];
            }
            count++;
        }
        
        if (count > 0) {
            for (float& v : result) v /= count;
        }
        
        normalize(result);
        return result;
        
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "ONNX inference failed: %s\n", e.what());
        return EmbeddingVector(dimensions_, 0.0f);
    }
}

void OnnxEmbeddingModel::normalize(EmbeddingVector& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-10f) {
        for (float& v : vec) v /= norm;
    }
}

EmbeddingResult OnnxEmbeddingModel::embed(std::string_view text) {
    if (!is_loaded_) {
        fprintf(stderr, "Warning: Model not loaded, returning zero embedding\n");
        return {EmbeddingVector(dimensions_, 0.0f), "none", dimensions_};
    }
    
    auto tokens = tokenize(text);
    std::vector<int64_t> attention_mask(tokens.size(), 1);
    
    // Set attention mask for padding to 0
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == 0) attention_mask[i] = 0; // [PAD]
    }
    
    auto vec = run_inference(tokens, attention_mask);
    return {std::move(vec), model_path_, dimensions_};
}

std::vector<EmbeddingResult> OnnxEmbeddingModel::embed_batch(const std::vector<std::string>& texts) {
    std::vector<EmbeddingResult> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(embed(text));
    }
    return results;
}

// ============================================================================
// SentencePiece Tokenizer (for ONNX model)
// ============================================================================

class SentencePieceTokenizer {
public:
    void load(const std::string& vocab_path) {
        // In production, load actual SentencePiece model
        // For now, use simple character-based tokenization
        (void)vocab_path;
    }
    
    std::vector<int64_t> encode(std::string_view text) {
        std::vector<int64_t> tokens;
        tokens.push_back(1); // [CLS]
        
        for (unsigned char c : text) {
            if (c < 128) {
                tokens.push_back(c + 100); // Simple mapping
            }
        }
        
        tokens.push_back(2); // [SEP]
        return tokens;
    }
    
    std::string decode(const std::vector<int64_t>& tokens) {
        std::string text;
        for (int64_t t : tokens) {
            if (t > 100 && t < 228) {
                text += static_cast<char>(t - 100);
            }
        }
        return text;
    }
};

// ============================================================================
// Factory function to get embedding model
// ============================================================================

std::unique_ptr<OnnxEmbeddingModel> create_embedding_model(const std::string& model_path) {
    auto model = std::make_unique<OnnxEmbeddingModel>(model_path);
    if (model->is_loaded()) {
        return model;
    }
    return nullptr;
}

} // namespace apollocore
