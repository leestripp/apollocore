cmake_minimum_required(VERSION 3.20)
project(ApolloCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include FetchContent module
include(FetchContent)

# ============================================================================
# Dependencies
# ============================================================================

# nlohmann/json for serialization
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# llama.cpp for embeddings and GPU acceleration
# Using Vulkan for AMD GPU support
FetchContent_Declare(llama_cpp
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git
    GIT_TAG master
    CMAKE_ARGS
        -DGGML_VULKAN=ON          # Vulkan GPU backend (was LLAMA_VULKAN in older versions)
        -DLLAMA_BUILD_EXAMPLES=OFF
        -DLLAMA_BUILD_TESTS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# Download and build llama.cpp
# Note: Set GGML_VULKAN explicitly here because option() in inner ggml project
# would otherwise override our -DGGML_VULKAN=ON from CMAKE_ARGS.
set(GGML_VULKAN ON CACHE BOOL "Enable ggml Vulkan backend" FORCE)
FetchContent_MakeAvailable(llama_cpp)

# Patch: GGML_VULKAN option is declared in ggml/CMakeLists.txt but never used,
# so the Vulkan backend is never built. Add it after CPU backend.
file(READ "${llama_cpp_SOURCE_DIR}/ggml/src/CMakeLists.txt" _ggml_src)
string(REPLACE
    "ggml_add_backend(CPU)"
    "ggml_add_backend(CPU)\n\n# Patch: enable Vulkan backend (GGML_VULKAN option was declared but never used)\nif (GGML_VULKAN)\n    ggml_add_backend(VULKAN)\nendif()"
    _ggml_src_patched
    "${_ggml_src}")
file(WRITE "${llama_cpp_SOURCE_DIR}/ggml/src/CMakeLists.txt" "${_ggml_src_patched}")
message(STATUS "Patched llama.cpp to enable Vulkan backend")

# ============================================================================
# Tree-sitter — proper AST parsing for code (M3.B)
# ============================================================================
# MIT-licensed, modern (v0.26.9, May 2026), used by Neovim/Helix/GitHub.
# Used for auto-extracting code-aware entities (FUNCTION, CLASS, etc.)
# from ingested source files. Replaces regex hacks.
#
# Core is a C library (linked statically). The C++ grammar is a separate
# repo providing the tree_sitter_cpp() entry point.
option(TREE_SITTER_SKIP_FETCH "Use system tree-sitter instead of FetchContent" OFF)
if(NOT TREE_SITTER_SKIP_FETCH)
    # Build tree-sitter as static libraries (BUILD_SHARED_LIBS=OFF)
    # so we can statically link them into apollo_core without
    # having to ship .so files at runtime.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tree-sitter
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
        GIT_TAG        v0.26.9
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(
        tree-sitter-cpp
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tree-sitter)
    FetchContent_MakeAvailable(tree-sitter-cpp)
else()
    find_package(tree-sitter QUIET)
    if(NOT tree-sitter_FOUND)
        message(FATAL_ERROR "TREE_SITTER_SKIP_FETCH=ON but find_package failed")
    endif()
endif()


# Vulkan detection (for AMD GPU)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan found: ${Vulkan_VERSION}")
    message(STATUS "  SDK: ${Vulkan_INCLUDE_DIR}")
else()
    message(STATUS "Vulkan not found - will use CPU fallback")
endif()

# SQLite for persistence
find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3 found: ${SQLite3_VERSION}")

# ============================================================================
# OmniNet — MCP server transport (Streamable HTTP over TLS+WS)
# ============================================================================
# OmniNet (https://gitlab.com/leestripp/omninet) provides HTTP+WebSocket+TLS
# in C++23. We use it for the MCP server's Streamable HTTP transport.
#
# It pulls in OpenSSL 3, sqlite-vec (optional), and nlohmann/json (we already
# declare it above — CMake deduplicates). First build takes ~5+ minutes.
#
# NOTE: A specific tag is preferred for reproducibility. The repo currently
# has no public stable tag — we pin to `main` and document this. Upgrade to a
# tagged release as soon as one ships.
option(OMNINET_SKIP_FETCH "Use system OmniNet instead of FetchContent" OFF)
if(NOT OMNINET_SKIP_FETCH)
    FetchContent_Declare(
        omninet
        GIT_REPOSITORY https://gitlab.com/leestripp/omninet.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(omninet)
else()
    find_package(OmniNet QUIET)
    if(NOT OmniNet_FOUND)
        message(FATAL_ERROR "OMNINET_SKIP_FETCH=ON but find_package failed")
    endif()
endif()

# ============================================================================
# Library
# ============================================================================

add_library(apollo_core STATIC)
target_sources(apollo_core PRIVATE
    src/apollocore.cpp
    src/embedding_model.cpp
    src/entity_extractor.cpp
    src/cross_encoder.cpp
    src/hyde.cpp
    src/contextual_retrieval.cpp
    src/persistence.cpp
    src/fbin.cpp
    src/kb_persistence.cpp
    src/apollo_persistence.cpp
    src/apollo_hybrid_search.cpp   # Phase M2.7: vector + BM25 + graph fusion
    src/apollo_ts_wrapper.cpp   # Phase M3.B: tree-sitter C++ wrapper
    src/apollo_code_extractor.cpp   # Phase M3.B: AST → entities/relationships
)
target_include_directories(apollo_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Tree-sitter (Phase M3.B) - PUBLIC so consumers see the headers
        ${tree-sitter_SOURCE_DIR}/lib/include
        ${tree-sitter-cpp_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${llama_cpp_SOURCE_DIR}/include
)
target_link_libraries(apollo_core
    PUBLIC
        nlohmann_json::nlohmann_json
        llama
        SQLite::SQLite3
    PRIVATE
        # Tree-sitter (Phase M3.B) - static linking
        tree-sitter
        tree-sitter-cpp
)
target_compile_features(apollo_core PUBLIC cxx_std_23)

# Platform-specific linking
if(Vulkan_FOUND)
    target_link_libraries(apollo_core PRIVATE Vulkan::Vulkan)
endif()

# Install
install(TARGETS apollo_core ARCHIVE DESTINATION lib)
install(DIRECTORY include/apollocore DESTINATION include)

# ============================================================================
# MCP Server (new — Phase 4 of the refactor plan)
# ============================================================================
# A thin executable that exposes ApolloCore over MCP via OmniNet's HTTP server.
# Streams JSON-RPC 2.0 over Streamable HTTP transport (MCP spec 2025-11-25).
#
# The MCP server is OPT-IN: when APOLLO_BUILD_MCP_SERVER=ON (default), the
# executable `apollo_mcp_server` is built. This lets downstream consumers
# continue to link only the static lib if they want.
option(APOLLO_BUILD_MCP_SERVER "Build the ApolloCore MCP server executable" ON)
if(APOLLO_BUILD_MCP_SERVER)
    add_subdirectory(src/mcp_server)
endif()

# ============================================================================
# Tests
# ============================================================================
enable_testing()
add_subdirectory(tests)

# ============================================================================
# Examples
# ============================================================================
add_subdirectory(examples)

# ============================================================================
# Model Download
# ============================================================================
add_subdirectory(models)
