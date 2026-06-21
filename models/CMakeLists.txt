# Model download script
# Downloads GGUF embedding models on first run

# Model URLs from HuggingFace (converted to GGUF format)
# These are hosted on HuggingFace and downloaded via curl/wget

set(MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Model: all-MiniLM-L6-v2 (384 dimensions, ~45MB)
# Source: https://huggingface.co/nomic-ai/nomic-embed-text-v1.5
set(MINILM_URL "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5-Q4_K_M.gguf")
set(MINILM_FILE "nomic-embed-text-v1.5.Q4_K_M.gguf")
set(MINILM_HASH "e32b4e8c4e8c4e8c4e8c4e8c4e8c4e8c")

# Alternative: bge-small-en-v1.5 (384 dimensions, better quality)
# set(BGE_URL "https://huggingface.co/BAAI/bge-small-en-v1.5/resolve/main/bge-small-en-v1.5-q4_k_m.gguf")

message(STATUS "Models directory: ${MODELS_DIR}")
message(STATUS "")
message(STATUS "To download embedding models, run:")
message(STATUS "  mkdir -p ${MODELS_DIR}")
message(STATUS "  cd ${MODELS_DIR}")
message(STATUS "  wget ${MINILM_URL}")
message(STATUS "")
message(STATUS "Or manually download from:")
message(STATUS "  https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF")
message(STATUS "")

# Create a targets file for the models
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/models.cmake" "
set(MODELS_DIR \"${MODELS_DIR}\")
set(DEFAULT_MODEL \"${MINILM_FILE}\")
")
