cmake_minimum_required(VERSION 3.20)

add_executable(simple_usage simple_usage.cpp)
target_include_directories(simple_usage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(simple_usage PRIVATE apollo_core)

# Full feature demo - showcases all ApolloCore capabilities
add_executable(full_demo full_demo.cpp)
target_include_directories(full_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(full_demo PRIVATE apollo_core)

# OmniNet hello world - demonstrates HTTP server integration (Phase O1)
add_executable(omninet_hello omninet_hello.cpp)
target_include_directories(omninet_hello PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/omninet-src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/omninet-src/src
)
target_link_libraries(omninet_hello PRIVATE apollo_core OmniNet)

# ApolloCore REST Server (Phase R1) - HTTP API for KB operations
add_executable(apollo_rest_server
    apollo_rest_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_rest_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_prompts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_hybrid_search.cpp
)

# ApolloCore MCP stdio Server (Phase M2.14) - JSON-RPC over stdin/stdout
add_executable(apollo_mcp_stdio_server
    apollo_mcp_stdio_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_stdio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_mcp_prompts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/apollo_hybrid_search.cpp
)
target_include_directories(apollo_mcp_stdio_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/nlohmann_json-src/include
    ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(apollo_mcp_stdio_server PRIVATE apollo_core)
target_include_directories(apollo_rest_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/omninet-src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/omninet-src/src
)
target_link_libraries(apollo_rest_server PRIVATE apollo_core OmniNet)
