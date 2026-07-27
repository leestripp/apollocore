cmake_minimum_required(VERSION 3.20)
project(ApolloCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
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
        -DGGML_VULKAN=OFF         # Vulkan disabled (SPIRV-Headers not available on this system)
        -DLLAMA_BUILD_EXAMPLES=OFF
        -DLLAMA_BUILD_TESTS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

# Download and build llama.cpp
# Note: Set GGML_VULKAN explicitly here because option() in inner ggml project
# would otherwise override our -DGGML_VULKAN=OFF from CMAKE_ARGS.
set(GGML_VULKAN OFF CACHE BOOL "Enable ggml Vulkan backend" FORCE)
FetchContent_MakeAvailable(llama_cpp)

# Patch the fetched llama.cpp ggml/src/CMakeLists.txt, in pure CMake
# (idempotent, and keeps the project free of any Python dependency):
#
#   1. Propagate GGML_USE_CPU to ggml-base so ggml-backend-reg.cpp can
#      #ifdef GGML_USE_CPU and register the CPU backend. Without this,
#      llama_model_load_from_file() fails with "no backends are loaded"
#      because ggml_backend_reg_count() == 0.
#   2. The GGML_VULKAN option is declared in ggml/CMakeLists.txt but never
#      used, so the Vulkan backend is never built. Strip every bare
#      `ggml_add_backend(Vulkan|VULKAN)` activation (an unconditional one
#      would collide with ours → CMake error "binary directory already
#      used") and insert exactly one `if (GGML_VULKAN)`-guarded activation.
set(_ggml_src_cmake "${llama_cpp_SOURCE_DIR}/ggml/src/CMakeLists.txt")
if(NOT EXISTS "${_ggml_src_cmake}")
    message(FATAL_ERROR "llama.cpp ggml/src/CMakeLists.txt not found at ${_ggml_src_cmake}")
endif()
file(READ "${_ggml_src_cmake}" _ggml_cmake)

set(_cpu_patch_block
"# ApolloCore patch: propagate GGML_USE_CPU to ggml-base
# (needed so ggml-backend-reg.cpp can #ifdef on GGML_USE_CPU)
if (GGML_CPU)
    target_compile_definitions(ggml-base PUBLIC GGML_USE_CPU)
endif()
")
set(_vulkan_patch_block
"# Patch: enable Vulkan backend (GGML_VULKAN option was declared but never used)
if (GGML_VULKAN)
    ggml_add_backend(VULKAN)
endif()
")

# Strip our own previous insertions and any bare Vulkan activations so
# re-running configure stays idempotent.
string(REPLACE "${_cpu_patch_block}" "" _ggml_cmake "${_ggml_cmake}")
string(REPLACE "${_vulkan_patch_block}" "" _ggml_cmake "${_ggml_cmake}")
string(REGEX REPLACE "\n[ \t]*ggml_add_backend\\([Vv][Uu][Ll][Kk][Aa][Nn]\\)[ \t]*\r?\n" "\n" _ggml_cmake "${_ggml_cmake}")

# Insert both patches right after the first ggml_add_backend(CPU).
string(FIND "${_ggml_cmake}" "ggml_add_backend(CPU)" _cpu_pos)
if(_cpu_pos EQUAL -1)
    message(FATAL_ERROR
        "ggml_add_backend(CPU) not found in ${_ggml_src_cmake} — llama.cpp "
        "upstream layout changed; update the ApolloCore patch block in CMakeLists.txt")
endif()
string(LENGTH "ggml_add_backend(CPU)" _anchor_len)
math(EXPR _insert_pos "${_cpu_pos} + ${_anchor_len}")
string(SUBSTRING "${_ggml_cmake}" 0 ${_insert_pos} _ggml_before)
string(SUBSTRING "${_ggml_cmake}" ${_insert_pos} -1 _ggml_after)
set(_ggml_cmake "${_ggml_before}\n${_cpu_patch_block}\n${_vulkan_patch_block}${_ggml_after}")
file(WRITE "${_ggml_src_cmake}" "${_ggml_cmake}")
message(STATUS "Patched llama.cpp ggml backends (GGML_USE_CPU propagation + Vulkan activation)")

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

    # tree-sitter-cpp's CMakeLists regenerates src/parser.c with the
    # tree-sitter CLI whenever grammar.json looks newer — which is always on
    # a fresh clone — and fails hard when the CLI is not installed. The repo
    # ships a pre-generated parser.c and the CLI is not a build dependency
    # of ours, so after populating we guard the regeneration rule behind
    # `if(TREE_SITTER_CLI)` before the subproject is added. Hard error if
    # upstream restructures the file and the patch anchor disappears
    # (better than a confusing TREE_SITTER_CLI-NOTFOUND build error).
    FetchContent_GetProperties(tree-sitter-cpp)
    if(NOT tree-sitter-cpp_POPULATED)
        FetchContent_Populate(tree-sitter-cpp)
        file(READ "${tree-sitter-cpp_SOURCE_DIR}/CMakeLists.txt" _tsc_cmake)
        string(REGEX REPLACE
            "(add_custom_command\\(OUTPUT [^\n]*parser\\.c\")"
            "if(TREE_SITTER_CLI)\n\\1"
            _tsc_cmake "${_tsc_cmake}")
        string(REGEX REPLACE
            "(COMMENT \"Generating parser\\.c\"\\))"
            "\\1\nendif()"
            _tsc_cmake "${_tsc_cmake}")
        if(NOT _tsc_cmake MATCHES "if\\(TREE_SITTER_CLI\\)")
            message(FATAL_ERROR
                "Could not patch ${tree-sitter-cpp_SOURCE_DIR}/CMakeLists.txt "
                "(parser.c regeneration guard) — tree-sitter-cpp upstream layout changed")
        endif()
        file(WRITE "${tree-sitter-cpp_SOURCE_DIR}/CMakeLists.txt" "${_tsc_cmake}")
    endif()
    add_subdirectory(${tree-sitter-cpp_SOURCE_DIR} ${tree-sitter-cpp_BINARY_DIR})
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
# Coverage (gcov — GCC only, no lcov/gcovr)
# ============================================================================
# -DAPOLLO_COVERAGE=ON instruments everything defined from this point on
# (apollo_core, apollo_mcp_server, tests, examples) with gcov counters.
# Dependencies are deliberately NOT instrumented — they are all added via
# FetchContent above, and we only measure our own code. Then:
#   cmake --build build -j$(nproc) && cmake --build build --target coverage
option(APOLLO_COVERAGE "Instrument project targets with gcov" OFF)
if(APOLLO_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(FATAL_ERROR "APOLLO_COVERAGE requires GCC (gcov)")
    endif()
    # -O0 so line attribution isn't mangled by optimization; the coverage
    # build is for measuring, not for running fast.
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
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
    src/text_chunker.cpp
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
        ${CMAKE_BINARY_DIR}/bin/libggml-cpu.so  # CPU backend shared lib (GGML_BACKEND_SHARED=ON builds to CMAKE_BINARY_DIR/bin/)
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

# Note: the apollo_core static library target exists purely so the MCP server
# (and the CTest targets) can link against the RAG/KG code in one place. It is
# NOT installed or exported — `apollo_mcp_server` is the only public artifact.

# ============================================================================
# MCP Server — ApolloCore's ONLY public artifact
# ============================================================================
# ApolloCore is delivered exclusively as the `apollo_mcp_server` executable
# below. It speaks MCP (Model Context Protocol) 2025-11-25 over Streamable
# HTTP transport (JSON-RPC 2.0 over `POST /mcp`), served by OmniNet.
#
# There is no opt-in flag, no alternative mode, no library for downstream
# consumers to link. The static-library target above is an internal
# organization primitive only — it is not installed or exported.
add_subdirectory(src/mcp_server)

# ============================================================================
# Tests
# ============================================================================
enable_testing()
add_subdirectory(tests)

if(APOLLO_COVERAGE)
    # `cmake --build build --target coverage`: run the suite, then print a
    # per-file line-coverage summary for project sources (deps excluded).
    add_custom_target(coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                -DBUILD_DIR=${CMAKE_BINARY_DIR}
                -P ${CMAKE_SOURCE_DIR}/cmake/coverage_report.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
        COMMENT "Running tests and summarizing gcov coverage")
endif()

# ============================================================================
# Examples
# ============================================================================
add_subdirectory(examples)

# ============================================================================
# Model Download
# ============================================================================
add_subdirectory(models)
