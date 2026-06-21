# ApolloCore - Embedding Models

This directory stores GGUF embedding models for ApolloCore v2.

## Recommended Models

### Nomic Embed Text v1.5 (RECOMMENDED)
- **Context**: 8,192 tokens
- **Dimensions**: 768
- **Quality**: Excellent, rivals OpenAI ada-002
- **Quantized (Q4_K_M)**: ~84MB, great quality/size ratio
- **Full**: ~274MB

**Download:**
```bash
# Quantized (recommended for most users)
wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5-Q4_K_M.gguf \
    -O nomic-embed-text-v1.5-Q4_K_M.gguf

# Full precision
wget https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.gguf \
    -O nomic-embed-text-v1.5.gguf
```

### All-MiniLM-L6-v2
- **Context**: 256 tokens
- **Dimensions**: 384
- **Size**: ~90MB (GGUF)
- **Speed**: Very fast
- **Best for**: Short texts, embeddings without GPU

**Download:**
```bash
wget https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/onnx/ggml-model-q4_0.gguf \
    -O all-MiniLM-L6-v2.gguf
```

### GTE-Qwen2-1.5B-Instruct
- **Context**: 8,192 tokens
- **Dimensions**: 1536
- **Quality**: High quality
- **Size**: ~3GB
- **Best for**: Maximum quality

**Download:**
```bash
wget https://huggingface.co/qqnlp/gte-Qwen2-1.5B-instruct-GGUF/resolve/main/ggml-model-q4_k_m.gguf \
    -O gte-Qwen2-1.5B-instruct.gguf
```

## Loading a Model

In your code:
```cpp
#include "apollocore/apollocore_v2.hpp"

apollocore_v2::ApolloCore kb;

// Load the model
kb.load_embedding_model("models/nomic-embed-text-v1.5-Q4_K_M.gguf");

// Now all embeddings use the real model
auto chunks = kb.add_document("Your text here", node);
auto results = kb.query("Your query");
```

## Model Sources

- [Nomic AI on HuggingFace](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF)
- [Sentence Transformers on HuggingFace](https://huggingface.co/sentence-transformers)
- [GTE on HuggingFace](https://huggingface.co/qqnlp/gte-Qwen2-1.5B-instruct-GGUF)

## Requirements

- llama.cpp compiled with embedding support
- GGUF format model file
- Vulkan SDK for GPU acceleration (optional)
