#!/bin/bash
# build.sh — Build apollo_mcp_server on the host (not in a container)
#
# Usage:
#   ./build.sh              # default: Release, 8 jobs
#   ./build.sh --dev       # Development: keep build dir, verbose
#   ./build.sh --clean     # Clean rebuild
#
# Requirements:
#   - g++-15, cmake, make
#   - tree-sitter-cli >= 0.22.6  (cargo install tree-sitter-cli --version 0.22.6)
#   - Libraries: libssl-dev, libsqlite3-dev, zlib1g-dev, libomp-dev, libopenblas-dev
#
# Build output:
#   build/src/mcp_server/apollo_mcp_server   ← the binary
#
# ─────────────────────────────────────────────────────────────────────────────

set -e
cd "$(dirname "$0")"

CLEAN=0
JOBS=$(nproc)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --dev)   CMAKE_FLAGS+=" -DCMAKE_BUILD_TYPE=Debug" ;;
    esac
    shift
done

if [[ "$CLEAN" == 1 ]]; then
    echo "[build] Cleaning..."
    rm -rf build
fi

# ── Check tools ──────────────────────────────────────────────────────────────────
check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo "[build] ERROR: '$1' not found. Install with: $2"
        exit 1
    fi
}

check_tool cmake   "apt install cmake"
check_tool g++-15  "apt install g++-15"

# tree-sitter-cli
if ! command -v tree-sitter &>/dev/null; then
    echo "[build] WARNING: tree-sitter-cli not found."
    echo "          Parser.c will be pre-generated (if exists in tree-sitter cache)."
    echo "          Install: cargo install tree-sitter-cli --version 0.22.6"
    TREE_SITTER_CLI=""
else
    TREE_SITTER_CLI="$(command -v tree-sitter)"
fi

# ── Configure ────────────────────────────────────────────────────────────────────
CMAKE_ARGS=(
    -S . -B build
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_CXX_COMPILER=/usr/bin/g++
    -DCMAKE_C_COMPILER=/usr/bin/gcc
    -DAPOLLO_SKIP_MODEL_DOWNLOAD=ON
    -DGGML_VULKAN=OFF
)

if [[ -n "$TREE_SITTER_CLI" ]]; then
    CMAKE_ARGS+=(-DTREE_SITTER_CLI="$TREE_SITTER_CLI")
fi

# Add any extra flags from env or args
CMAKE_ARGS+=(${CMAKE_FLAGS:-})

echo "[build] Configuring..."
cmake "${CMAKE_ARGS[@]}"

# ── Build ────────────────────────────────────────────────────────────────────────
echo "[build] Building apollo_mcp_server ($JOBS jobs)..."
cmake --build build -j"$JOBS" --target apollo_mcp_server

# ── Verify ────────────────────────────────────────────────────────────────────────
BINARY="build/src/mcp_server/apollo_mcp_server"
if [[ -x "$BINARY" ]]; then
    echo ""
    echo "=== Build successful ==="
    echo "  Binary : $BINARY"
    echo "  Size   : $(du -h "$BINARY" | cut -f1)"
    echo ""
    echo "  To run:"
    echo "    ./$BINARY --help"
    echo ""
    echo "  To deploy:"
    echo "    ./deploy_new_build.sh    # stops service, copies binary, restarts"
    echo ""
else
    echo "[build] ERROR: binary not found or not executable: $BINARY"
    exit 1
fi
