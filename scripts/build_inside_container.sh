#!/bin/bash
set -e
cd /workspace

echo "=== Configuring ApolloCore ==="
cmake -S /workspace -B /workspace/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 \
    -DCMAKE_C_COMPILER=/usr/bin/gcc-15 \
    -DTREE_SITTER_CLI=/usr/local/cargo/bin/tree-sitter \
    -DAPOLLO_SKIP_MODEL_DOWNLOAD=ON \
    -DGGML_VULKAN=OFF

echo "=== Build apollo_mcp_server ==="
cmake --build /workspace/build -j$(nproc) --target apollo_mcp_server

echo "=== Verify binary ==="
ls -lh /workspace/build/src/mcp_server/apollo_mcp_server
/workspace/build/src/mcp_server/apollo_mcp_server --help
