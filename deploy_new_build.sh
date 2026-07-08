#!/bin/bash
# deploy_new_build.sh — deploy the newly-built apollo_mcp_server binary
# Run once; you enter your sudo password once for all operations.

set -e
cd /home/lee/projects/AI/ApolloCore

echo "=== Deploying apollo_mcp_server build ==="

# 0. Stop the systemd service (if running)
echo "0. Stopping apollo_mcp_server.service..."
systemctl stop apollo_mcp_server.service 2>/dev/null || true

# Also kill any other instances holding the binary open (ports 8765 or 18765)
echo "   Checking for other running instances..."
for pid in $(pgrep -f "apollo_mcp_server"); do
    echo "   Killing PID $pid..."
    kill -9 $pid 2>/dev/null || true
done

# Wait for file handle to release
sleep 2

# 1. Copy the new binary
echo "1. Copying new binary..."
cp build/src/mcp_server/apollo_mcp_server /opt/apollocore/bin/apollo_mcp_server
echo "   Binary updated ($(stat -c%s /opt/apollocore/bin/apollo_mcp_server) bytes)"

# 2. Copy updated llama.cpp static libs
echo "2. Copying updated llama.cpp libs..."
cp build/_deps/llama_cpp-build/src/libllama.a           /opt/apollocore/lib/libllama.a
cp build/_deps/llama_cpp-build/ggml/src/libggml.a       /opt/apollocore/lib/libggml.a
cp build/_deps/llama_cpp-build/ggml/src/libggml-base.a   /opt/apollocore/lib/libggml-base.a
cp build/_deps/llama_cpp-build/ggml/src/libggml-cpu.a    /opt/apollocore/lib/libggml-cpu.a
echo "   libllama, libggml, libggml-base, libggml-cpu updated"

# 3. Copy OmniNet lib
cp build/_deps/omninet-build/libOmniNet.a /opt/apollocore/lib/libOmniNet.a 2>/dev/null && echo "   libOmniNet updated" || echo "   (libOmniNet unchanged)"

# 4. Update library cache
echo "3. Running ldconfig..."
ldconfig /opt/apollocore/lib

# 5. Restart the systemd service
echo "4. Starting apollo_mcp_server.service..."
systemctl start apollo_mcp_server.service
echo "   Service started"

echo ""
echo "=== Deploy complete ==="
