cmake_minimum_required(VERSION 3.20)

add_executable(simple_usage simple_usage.cpp)
target_include_directories(simple_usage PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/llama_cpp-src/include
)
target_link_libraries(simple_usage PRIVATE apollo_core)
