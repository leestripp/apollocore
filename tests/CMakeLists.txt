cmake_minimum_required(VERSION 3.20)

# v1 tests
add_executable(run_apollo_tests 
    test_main.cpp
    test_apollocore.cpp
)
target_include_directories(run_apollo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(run_apollo_tests PRIVATE apollo_core)
enable_testing()
add_test(NAME ApolloCoreTests COMMAND run_apollo_tests)

# v2 tests (with real embeddings)
add_executable(test_v2
    test_v2.cpp
)
target_include_directories(test_v2 PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_v2 PRIVATE apollo_core)

# Real embedding test (requires GGUF model)
add_executable(test_embeddings
    test_embeddings.cpp
)
target_include_directories(test_embeddings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_embeddings PRIVATE apollo_core)

# Entity extraction tests (can run without LLM model)
add_executable(test_entity_extraction
    test_entity_extraction.cpp
)
target_include_directories(test_entity_extraction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_entity_extraction PRIVATE apollo_core)

# LLM Entity extraction integration test (requires GGUF model)
add_executable(test_llm_extraction
    test_llm_extraction.cpp
)
target_include_directories(test_llm_extraction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_llm_extraction PRIVATE apollo_core)

# Cross-encoder reranking tests
add_executable(test_cross_encoder
    test_cross_encoder.cpp
)
target_include_directories(test_cross_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_cross_encoder PRIVATE apollo_core)

# HyDE query enhancement tests
add_executable(test_hyde
    test_hyde.cpp
)
target_include_directories(test_hyde PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_hyde PRIVATE apollo_core)

# Contextual retrieval tests
add_executable(test_contextual_retrieval
    test_contextual_retrieval.cpp
)
target_include_directories(test_contextual_retrieval PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_contextual_retrieval PRIVATE apollo_core)

# Persistence tests
add_executable(test_persistence
    test_persistence.cpp
)
target_include_directories(test_persistence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_persistence PRIVATE apollo_core)

# FBIN binary embedding tests
add_executable(test_fbin
    test_fbin.cpp
)
target_include_directories(test_fbin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(test_fbin PRIVATE apollo_core)

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

# Entity extraction flow tests (Phase G1) - requires both models
add_executable(test_entity_extraction_flow
    test_entity_extraction_flow.cpp
)
target_include_directories(test_entity_extraction_flow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(test_entity_extraction_flow PRIVATE apollo_core)

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
