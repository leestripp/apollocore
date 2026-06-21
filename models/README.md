# ApolloCore - Models

This directory stores GGUF models for ApolloCore. Models are **not** committed to git — they are downloaded automatically by CMake, or manually if preferred.

## Required Models

| Model | Purpose | Size | Dimensions |
|-------|---------|------|------------|
| `nomic-embed-text-v1.5.Q4_K_M.gguf` | Text embeddings | ~81 MB | 768 |
| `qwen2.5-0.5b-instruct-q4_k_m.gguf` | Entity extraction (LLM) | ~469 MB | — |

## Automatic Download (Recommended)

CMake will download the models when you configure the build:

```bash
cd /path/to/ApolloCore
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

The download happens **once** — subsequent builds skip files that already exist.

### Skip the download

If you want to manage models yourself:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAPOLLO_SKIP_MODEL_DOWNLOAD=ON
```

## Manual Download

If the automatic download fails (firewall, no internet, etc.) you can fetch the models manually:

```bash
cd models/

# Embedding model (~81 MB)
curl -L -O "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf"

# LLM for entity extraction (~469 MB)
curl -L -O "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"
```

## Using the Models in Code

```cpp
#include <apollocore/apollo_persistence.hpp>

apollocore_v2::ApolloCoreWithPersistence kb;

// Load the embedding model
kb.load_embedding_model("models/nomic-embed-text-v1.5.Q4_K_M.gguf");

// (Optional) Configure entity extraction with the LLM
// kb.set_llm_model("models/qwen2.5-0.5b-instruct-q4_k_m.gguf");
```

## Why these models?

- **Nomic Embed Text v1.5** — Best open-source embedding model at this size. Rivals OpenAI's `text-embedding-ada-002` on MTEB benchmark.
- **Qwen2.5 0.5B Instruct** — Small but capable instruction-tuned LLM. Fast enough for entity extraction on CPU, good enough for JSON output.

## Sources

- [Nomic AI on HuggingFace](https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF)
- [Qwen2.5 on HuggingFace](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF)

## Requirements

- llama.cpp compiled with embedding + LLM support
- GGUF format model files
- ~550 MB disk space for both models
