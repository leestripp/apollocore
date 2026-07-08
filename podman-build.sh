#!/bin/bash
# podman-build.sh — Build the ApolloCore Podman image
#
# Usage:
#   ./podman-build.sh [--dev]    # --dev includes build toolchain
#
# What this produces:
#   IMAGE: apollocore/runtime:latest   (or apollocore/dev:latest with --dev)
#
# Volumes needed at runtime:
#   - models:  /opt/apollocore/models   (GGUF files — mounted from host)
#   - data:    /var/lib/apollocore/data (KB SQLite — mounted from host)
#   - logs:    /var/log/apollocore      (optional — for debugging)
#
# Ports:
#   8765/tcp (MCP HTTP)
#
# Examples:
#   # Build lean production image
#   ./podman-build.sh
#
#   # Build dev image (includes g++, cmake, git, tree-sitter-cli)
#   ./podman-build.sh --dev
#
#   # Run the server
#   podman run -d --name apollocore \
#     -p 127.0.0.1:8765:8765 \
#     -v /opt/apollocore/models:/opt/apollocore/models \
#     -v /var/lib/apollocore/data:/var/lib/apollocore/data \
#     -v /var/log/apollocore:/var/log/apollocore \
#     apollocore/runtime:latest
#
#   # Run with shell (for CodeMax to work inside)
#   podman run -it --rm \
#     -p 127.0.0.1:8765:8765 \
#     -v /opt/apollocore/models:/opt/apollocore/models \
#     -v /var/lib/apollocore/data:/var/lib/apollocore/data \
#     -v /var/log/apollocore:/var/log/apollocore \
#     -v /path/to/ApolloCore:/workspace \
#     apollocore/dev:latest \
#     bash
