# =============================================================================
# ApolloCore - Model Setup
# =============================================================================
# Downloads required GGUF models from HuggingFace on first CMake configure.
# Models are NOT committed to git (see .gitignore).
#
# Models downloaded:
#   1. nomic-embed-text-v1.5-Q4_K_M.gguf  (~81MB) - Text embeddings
#   2. qwen2.5-0.5b-instruct-q4_k_m.gguf  (~469MB) - LLM for entity extraction
#
# Models are downloaded to ${CMAKE_CURRENT_SOURCE_DIR}
# =============================================================================

set(MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Model URLs and filenames
set(EMBEDDING_MODEL_NAME "nomic-embed-text-v1.5.Q4_K_M.gguf")
set(EMBEDDING_MODEL_URL  "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf")

set(LLM_MODEL_NAME        "qwen2.5-0.5b-instruct-q4_k_m.gguf")
set(LLM_MODEL_URL         "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf")

# =============================================================================
# Helper function: download_model
# Args:
#   _url      - URL to download from
#   _filename - Output filename
# =============================================================================
function(download_model _url _filename)
    set(_path "${MODELS_DIR}/${_filename}")
    
    if(EXISTS "${_path}")
        file(SIZE "${_path}" _size)
        message(STATUS "  ✓ ${_filename} (${_size} bytes) - already exists")
        return()
    endif()
    
    message(STATUS "  ↓ Downloading ${_filename}...")
    message(STATUS "    URL: ${_url}")
    message(STATUS "    Destination: ${_path}")
    
    # DOWNLOAD with progress shown
    file(DOWNLOAD
        "${_url}"
        "${_path}"
        SHOW_PROGRESS
        STATUS  _download_status
        LOG     _download_log
    )
    
    list(GET _download_status 0 _status_code)
    if(NOT _status_code EQUAL 0)
        list(GET _download_status 1 _error_msg)
        message(WARNING "  ✗ Failed to download ${_filename}")
        message(WARNING "    Status: ${_download_status}")
        message(WARNING "    Error:  ${_error_msg}")
        message(WARNING "    You can manually download with:")
        message(WARNING "      curl -L -o \"${_path}\" \"${_url}\"")
        return()
    endif()
    
    file(SIZE "${_path}" _size)
    message(STATUS "  ✓ Downloaded ${_filename} (${_size} bytes)")
endfunction()

# =============================================================================
# Download all models (skip if APOLLO_SKIP_MODEL_DOWNLOAD=ON)
# =============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS "ApolloCore - Model Setup")
message(STATUS "========================================")

if(APOLLO_SKIP_MODEL_DOWNLOAD)
    message(STATUS "APOLLO_SKIP_MODEL_DOWNLOAD=ON - skipping downloads")
    message(STATUS "Ensure these models exist in ${MODELS_DIR}:")
    message(STATUS "  - ${EMBEDDING_MODEL_NAME}")
    message(STATUS "  - ${LLM_MODEL_NAME}")
else()
    message(STATUS "Downloading required models to ${MODELS_DIR}...")
    message(STATUS "")
    
    download_model("${EMBEDDING_MODEL_URL}" "${EMBEDDING_MODEL_NAME}")
    download_model("${LLM_MODEL_URL}"        "${LLM_MODEL_NAME}")
    
    message(STATUS "")
    message(STATUS "Model setup complete.")
endif()

message(STATUS "")
message(STATUS "Models directory: ${MODELS_DIR}")
message(STATUS "========================================")
message(STATUS "")

# =============================================================================
# Export variables for use in main CMakeLists.txt
# =============================================================================
set(APOLLO_EMBEDDING_MODEL "${MODELS_DIR}/${EMBEDDING_MODEL_NAME}"
    CACHE FILEPATH "Path to ApolloCore embedding model")
set(APOLLO_LLM_MODEL "${MODELS_DIR}/${LLM_MODEL_NAME}"
    CACHE FILEPATH "Path to ApolloCore LLM model")