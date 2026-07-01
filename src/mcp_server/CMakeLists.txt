cmake_minimum_required(VERSION 3.20)

# ============================================================================
# apollo_mcp_server — MCP server executable
# ============================================================================
# A thin C++23 executable that exposes the ApolloCore static library over the
# Model Context Protocol (MCP) using OmniNet as the HTTP/WebSocket transport.
#
# This CMakeLists.txt is wired up by the top-level CMakeLists.txt. We do NOT
# link apollo_core statically into a single archive here — we link against
# the existing static library target so all existing tests keep passing.

find_package(Threads REQUIRED)

add_executable(apollo_mcp_server
    main.cpp
    dispatcher.cpp
    jsonrpc.cpp
    tools.cpp
    resources.cpp
    prompts.cpp
    server_state.cpp
    http_server.cpp
)

target_include_directories(apollo_mcp_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        # Re-use ApolloCore public headers.
        ${CMAKE_SOURCE_DIR}/include
        # nlohmann/json (header-only via apollo_core's transitive dep).
)

target_link_libraries(apollo_mcp_server
    PRIVATE
        # The static library we're exposing.
        apollo_core
        # OmniNet — HTTP + WebSocket + TLS + sessions + admin dashboard.
        OmniNet
        # nlohmann_json is pulled in transitively, link it explicitly to be
        # safe across versions.
        nlohmann_json::nlohmann_json
        # pthreads (OmniNet uses std::thread / std::jthread).
        Threads::Threads
)

target_compile_features(apollo_mcp_server PRIVATE cxx_std_23)

# Match the rest of the project's warning posture.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apollo_mcp_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Web UI — pure static files, served by OmniNet at runtime.
# Bundled next to the source tree (so dev builds get the UI by default) and
# installed under share/apollocore/ui/ so `cmake --install` deploys it.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/static
        DESTINATION share/apollocore/ui
        OPTIONAL  # tolerate empty/missing static/ dir on third-party builds
        FILES_MATCHING PATTERN "*")

# Install alongside the library so `cmake --install` deploys both.
install(TARGETS apollo_mcp_server RUNTIME DESTINATION bin)