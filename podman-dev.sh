#!/bin/bash
# podman-dev.sh — Open an interactive shell inside the ApolloCore dev container
#
# This mounts the ApolloCore source tree so you can build and test from inside
# the container while editing files on the host with your normal editor.
#
# Usage:
#   ./podman-dev.sh                    # interactive shell
#   ./podman-dev.sh --build           # run the build script
#   ./podman-dev.sh --ctest           # run tests
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${APOLLOCORE_IMAGE:-apollocore/dev:latest}"

# Source mounts
SRC_MOUNT="${SCRIPT_DIR}:/workspace:rw"
MODEL_MOUNT="/opt/apollocore/models:/opt/apollocore/models:ro"
DATA_MOUNT="/var/lib/apollocore/data:/var/lib/apollocore/data:rw"

case "${1:-}" in
    --build)
        exec podman run --rm \
            -v "$SRC_MOUNT" \
            -v "$MODEL_MOUNT" \
            -v "$DATA_MOUNT" \
            -w /workspace \
            "$IMAGE" \
            bash -c "cmake -S /workspace -B /workspace/container_build \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 \
                -DCMAKE_C_COMPILER=/usr/bin/gcc-15 \
                -DTREE_SITTER_CLI=/usr/local/cargo/bin/tree-sitter \
                -DAPOLLO_SKIP_MODEL_DOWNLOAD=ON \
                -DGGML_VULKAN=OFF \
            && cmake --build /workspace/container_build -j\$(nproc) --target apollo_mcp_server"
        ;;

    --ctest)
        exec podman run --rm \
            -v "$SRC_MOUNT" \
            -v "$MODEL_MOUNT" \
            -v "$DATA_MOUNT" \
            -w /workspace \
            "$IMAGE" \
            bash -c "cd /workspace/container_build && ctest -E test_ingest_codebase --output-on-failure"
        ;;

    --)
        shift
        exec podman run -it --rm \
            -v "$SRC_MOUNT" \
            -v "$MODEL_MOUNT" \
            -v "$DATA_MOUNT" \
            -w /workspace \
            "$IMAGE" \
            bash
        ;;

    *)
        exec podman run -it --rm \
            -v "$SRC_MOUNT" \
            -v "$MODEL_MOUNT" \
            -v "$DATA_MOUNT" \
            -w /workspace \
            "$IMAGE" \
            bash
        ;;
esac
