cmake_minimum_required(VERSION 3.20)

enable_testing()

# v2 tests (ApolloEngine with real embeddings)
add_executable(test_v2
    test_v2.cpp
)
target_include_directories(test_v2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_v2 PRIVATE apollo_core)
add_test(NAME test_v2 COMMAND test_v2 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Real embedding test (requires GGUF model)
add_executable(test_embeddings
    test_embeddings.cpp
)
target_include_directories(test_embeddings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_embeddings PRIVATE apollo_core)
add_test(NAME test_embeddings COMMAND test_embeddings WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Entity extraction tests (can run without LLM model)
add_executable(test_entity_extraction
    test_entity_extraction.cpp
)
target_include_directories(test_entity_extraction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_entity_extraction PRIVATE apollo_core)
add_test(NAME test_entity_extraction COMMAND test_entity_extraction)

# Cross-encoder reranking tests
add_executable(test_cross_encoder
    test_cross_encoder.cpp
)
target_include_directories(test_cross_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_cross_encoder PRIVATE apollo_core)
add_test(NAME test_cross_encoder COMMAND test_cross_encoder)

# Persistence tests (SQLite)
add_executable(test_persistence
    test_persistence.cpp
)
target_include_directories(test_persistence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_persistence PRIVATE apollo_core)
add_test(NAME test_persistence COMMAND test_persistence)

# FBIN binary embedding tests
add_executable(test_fbin
    test_fbin.cpp
)
target_include_directories(test_fbin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_fbin PRIVATE apollo_core)
add_test(NAME test_fbin COMMAND test_fbin)

# Knowledge Base persistence tests
add_executable(test_kb_persistence
    test_kb_persistence.cpp
)
target_include_directories(test_kb_persistence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_kb_persistence PRIVATE apollo_core)
add_test(NAME test_kb_persistence COMMAND test_kb_persistence)

# ApolloCore persistence integration tests
add_executable(test_apollo_persistence
    test_apollo_persistence.cpp
)
target_include_directories(test_apollo_persistence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_apollo_persistence PRIVATE apollo_core)
add_test(NAME test_apollo_persistence COMMAND test_apollo_persistence)

# M3.C Multi-Project tests (no embedding model required)
add_executable(test_multi_project
    test_multi_project.cpp
)
target_include_directories(test_multi_project PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_multi_project PRIVATE apollo_core)
add_test(NAME MultiProjectTests COMMAND test_multi_project)

# M3.B Code Extractor tests (tree-sitter AST → entities/relationships)
add_executable(test_code_extractor
    test_code_extractor.cpp
)
target_include_directories(test_code_extractor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_code_extractor PRIVATE apollo_core)
add_test(NAME CodeExtractorTests COMMAND test_code_extractor)

# HyDE (Hypothetical Document Embeddings) tests
add_executable(test_hyde
    test_hyde.cpp
)
target_include_directories(test_hyde PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_hyde PRIVATE apollo_core)
add_test(NAME test_hyde COMMAND test_hyde)

# Anthropic-style Contextual Retrieval tests
add_executable(test_contextual_retrieval
    test_contextual_retrieval.cpp
)
target_include_directories(test_contextual_retrieval PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_contextual_retrieval PRIVATE apollo_core)
add_test(NAME test_contextual_retrieval COMMAND test_contextual_retrieval)

# M2.7+M2.8 Hybrid Search and Cross-Encoder Reranking tests
add_executable(test_hybrid_search
    test_hybrid_search.cpp
)
target_include_directories(test_hybrid_search PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_hybrid_search PRIVATE apollo_core)
add_test(NAME HybridSearchTests COMMAND test_hybrid_search WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Coverage Holes tests — fills gaps in existing test suites
add_executable(test_coverage_holes
    test_coverage_holes.cpp
)
target_include_directories(test_coverage_holes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_coverage_holes PRIVATE apollo_core)
add_test(NAME CoverageHolesTests COMMAND test_coverage_holes WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
