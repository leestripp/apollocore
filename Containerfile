# =============================================================================
# ApolloCore Podman Image — production runtime
# =============================================================================
#
# Build:
#   podman build -f Containerfile -t apollocore/runtime:latest .
#
# This is a multi-stage build:
#   Stage 1 (builder): runs the full cmake build, produces the binary + libs
#   Stage 2 (runtime): copies binary + libs, creates tiny production image
#
# Why multi-stage?
#   The dev image is ~5GB (full build toolchain). The runtime image is ~200MB
#   (binary + libs only). Separate images let you rebuild the dev image when
#   source changes, without touching the production image.
#
# Run:
#   podman run -d --name apollocore \
#     -p 127.0.0.1:8765:8765 \
#     -v /opt/apollocore/models:/opt/apollocore/models \
#     -v /var/lib/apollocore/data:/var/lib/apollocore/data \
#     -v /var/log/apollocore:/var/log/apollocore \
#     apollocore/runtime:latest
#
# Or via Quadlet systemd service (recommended for persistent servers):
#   # Install Quadlet unit:
#   mkdir -p ~/.config/containers/systemd
#   cp apollocore.container ~/.config/containers/systemd/
#   systemctl --user daemon-reload
#   systemctl --user enable --now apollocore
#
# =============================================================================

# ─── Stage 1: builder ────────────────────────────────────────────────────────
# Use the dev image as a base — apt install + cargo are already cached.
# Rebuild only the ApolloCore source (on source change, only this layer rebuilds).
FROM apollocore/dev:latest AS builder

WORKDIR /build

# Copy source and build
COPY . /build/

RUN cmake -S /build -B /build/container_build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++-15 \
        -DCMAKE_C_COMPILER=/usr/bin/gcc-15 \
        -DTREE_SITTER_CLI=/usr/local/cargo/bin/tree-sitter \
        -DAPOLLO_SKIP_MODEL_DOWNLOAD=ON \
        -DGGML_VULKAN=OFF \
    && cmake --build /build/container_build -j$(nproc) --target apollo_mcp_server

# ─── Stage 2: runtime ────────────────────────────────────────────────────────
FROM docker.io/library/ubuntu:26.04 AS runtime

# Runtime libraries only (no build tools, no compilers, ~50MB)
RUN apt-get update && apt-get install -y --no-install-recommends \
        libopenblas0 \
        libgomp1 \
        libssl3t64 \
        libsqlite3-0 \
        zlib1g \
        curl \
    && \
    apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# ── Run as root (container is already isolated by Podman) ─────────────────────
# ApolloCore does not need root privileges at runtime.
# Running as root inside an unprivileged container is safe (no capability/CAP_SYS_ADMIN).
# If you prefer a non-root user, change the USER below to any existing user
# (e.g. USER ubuntu) or add the following to the RUN step above:
#   useradd --uid 9999 --gid 9999 --shell /bin/bash -m -d /home/apollocore apollocore
# and replace 'root' with 'apollocore' below.

WORKDIR /opt/apollocore

# ── Copy binary ────────────────────────────────────────────────────────────────
COPY --from=builder /build/container_build/src/mcp_server/apollo_mcp_server /opt/apollocore/bin/
COPY --from=builder /build/container_build/bin/*.so*                              /opt/apollocore/lib/

# ── Copy static libs (optional — kept for reference / future relinking) ──────────
# NOTE: llama.cpp builds with GGML_BACKEND_SHARED=ON, so .a files are NOT produced.
# The runtime only needs the .so files copied above. These COPY lines would fail.
# Uncomment only if you build with GGML_BACKEND_SHARED=OFF.
# COPY --from=builder /build/container_build/_deps/llama_cpp-build/src/libllama.a           /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/llama_cpp-build/ggml/src/libggml.a       /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/llama_cpp-build/ggml/src/libggml-base.a /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/llama_cpp-build/ggml/src/libggml-cpu.a  /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/omninet-build/libOmniNet.a              /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/tree-sitter-build/libtree-sitter.a       /opt/apollocore/lib/
# COPY --from=builder /build/container_build/_deps/tree-sitter-cpp-build/libtree-sitter-cpp.a /opt/apollocore/lib/

# ── ld.so.conf so the dynamic linker finds our libs ────────────────────────────
RUN echo "/opt/apollocore/lib" > /etc/ld.so.conf.d/apollocore.conf && ldconfig 2>/dev/null || true

# ── directories for volume mounts (populated by host) ───────────────────────────
RUN mkdir -p /opt/apollocore/models && \
    mkdir -p /var/lib/apollocore/data && \
    mkdir -p /var/log/apollocore

RUN chmod 755 /opt/apollocore/bin/apollo_mcp_server

EXPOSE 8765

# ── run as non-root user ───────────────────────────────────────────────────────
USER root
WORKDIR /var/lib/apollocore/data

ENV LD_LIBRARY_PATH=/opt/apollocore/lib
ENV APOLLO_HOST=0.0.0.0
ENV APOLLO_PORT=8765
ENV APOLLO_DATA_DIR=/var/lib/apollocore/data
ENV APOLLO_MODELS_DIR=/opt/apollocore/models

ENTRYPOINT ["/opt/apollocore/bin/apollo_mcp_server"]
CMD ["--host", "0.0.0.0", "--port", "8765", "--data-dir", "/var/lib/apollocore/data", "--embed-model", "/opt/apollocore/models/nomic-embed-text-v1.5.Q4_K_M.gguf", "--llm-model", "/opt/apollocore/models/qwen2.5-0.5b-instruct-q4_k_m.gguf"]
