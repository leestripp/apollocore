# Podman Deployment Plan — ApolloCore MCP Server

> Research findings and execution plan. Updated: 2026-07-08.

---

## Research Findings

### Podman is rootless by default on AerynOS

```
$ podman info | grep rootless
  rootless: true
  rootlessNetworkCmd: pasta
```

- `podman build` runs as your user — no sudo needed
- `podman run` runs as your user — no sudo needed
- `podman generate systemd` + Quadlet → systemd manages the container

### Privileged ports (< 1024) are blocked in rootless Podman

Port 8765 is fine — it's >= 1024. No special setup needed for `127.0.0.1:8765`.

If we ever needed port 80/443: `sudo setcap cap_net_bind_service=ep $(which rootlesskit)`.

### Build happens inside the container as root

The Dockerfile `RUN` commands execute inside a user-namespace build container,
as root inside that namespace — completely isolated from the host. No host
modifications. This is the intended Podman workflow.

### Library compatibility is the hard problem

ApolloCore links dynamically against:
- `libllama.a` (static, contains model loading + inference)
- `libggml-base.a`, `libggml-cpu.a` (static)
- `libggml.a` (static)
- `libOmniNet.a` (static)
- `libsqlite3` (dynamic)
- `libssl` (dynamic — different naming across distros!)
- `libz` (dynamic)
- `libcurl` (dynamic)
- `libgomp` (OpenMP)
- `libomp` (OpenMP)

**The binary built on AerynOS will link against AerynOS's libraries
(`libssl.so.3`, `libcurl.so.4`, etc.). Ubuntu 26.04 has different library
names** (`libssl3t64`, `libcurl3t64-openssl-dev`). This is why the binary
crashes in the Ubuntu container even though it compiles.

**Solution options:**

| Option | Pros | Cons |
|--------|-------|------|
| **A. Static linking** (`-static-libstdc++ -static-libgcc -Wl,--whole-archive`) | One binary, any distro | Larger, OpenBLAS/pthread are hard to static link |
| **B. Build in the container** | Native libs, no compat issues | Must match container's library versions |
| **C. Mount host binary + libs into container** | Fast, no rebuild needed | Must ensure lib versions match |
| **D. Alpine/Linux musl container** | True static by default | Different libc, may have compat issues with g++ |

**Chosen: Option B** — Build inside the Ubuntu 26.04 container. The
container installs its own library versions via apt. We link against those.
The runtime container runs the same image, so libs are identical at build
and run time.

### Quadlet: systemd manages the container (recommended for services)

Podman-generated systemd units (via `podman generate systemd` or Quadlet
`.container` files) are the modern way to run containers as services.

Quadlet approach:
- Write `~/.config/containers/systemd/apollo_mcp_server.container`
- `systemctl --user daemon-reload`
- `systemctl --user enable --now apollocore-mcp-server`
- Container starts on boot, restarts on failure, logs to journald

### Podman can expose ports without sudo (ports >= 1024)

`podman run -p 127.0.0.1:8765:8765` works fine as a regular user.
Only `sudo podman run -p 80:80` would need root.

---

## Plan: Phased Execution

### Phase 1 — Fix Containerfile (DONE ✓)
- [x] Change base image to `docker.io/library/ubuntu:26.04` (fully-qualified)
- [x] Remove `--mount=type=cache` (needs root on host; not available)
- [x] Fix GGML_USE_CPU propagation: replaced broken CMake `string(REGEX REPLACE "\1")` with `scripts/patch_llama_cpu_backend.py` Python script (clean, idempotent)
- [x] Fix libggml-cpu linking: use `${CMAKE_BINARY_DIR}/bin/libggml-cpu.so` (shared lib, not static `.a`)
- [x] Add rpath `${CMAKE_BINARY_DIR}/bin` to binary so `libggml-cpu.so` is found at runtime
- [x] Build dev container: `podman build -f Containerfile.dev -t apollocore/dev:latest .` → SUCCESS

### Phase 2 — Build dev container (DONE ✓)
- [x] `podman build -f Containerfile.dev -t apollocore/dev:latest .` — ~5GB image, Ubuntu 26.04 + g++-15 + cmake + tree-sitter-cli + cargo + Python test deps
- [x] First build time: ~3 min (apt install) + ~1 min (cargo install tree-sitter-cli) + ~1 min (pip install) = ~5 min total
- [x] Build was iterative: cmake configure failed → fixed → configure failed → fixed → build succeeded

### Phase 3 — Verify build inside container (DONE ✓)
- [x] `podman run --rm -v $(pwd):/workspace ... apollocore/dev:latest bash /tmp/run_build.sh`
- [x] Configure: 156s, Build: ~90s (all cached by ccache after first run)
- [x] Binary size: 2.6MB
- [x] `ldd` shows all libs resolved (libllama, libggml, libggml-base, libggml-cpu)
- [x] `--help` works: shows all CLI flags
- [x] Server starts: `embed_loaded: true, llm_loaded: true` ✓
- [x] MCP conformance: 27/27 tests pass ✓

### Phase 4 — Create production (runtime-only) Containerfile (DONE ✓)
- [x] Multi-stage build: stage 1 compiles (FROM apollocore/dev:latest), stage 2 ships binary + libs
- [x] Runtime image: ~200 MB (no build toolchain)
- [x] Models: volume mount from host (`/opt/apollocore/models`)
- [x] KB data: volume mount from host (`/var/lib/apollocore/data`)
- [x] Runtime runs as root inside container (Podman is rootless on host)
- [x] Fixes: libgomp1 (not libomp5), single-line JSON CMD, ldconfig silenced

### Phase 5 — Deploy as systemd service via Quadlet (DONE ✓)
- [x] `apollocore.container` — Quadlet unit file for `systemctl --user` management
- [x] Exposes MCP on `127.0.0.1:8765` (LAN exposure opt-in via --host flag)
- [x] Volume mounts: models, KB data, logs from host filesystem
- [x] Restart=on-failure with 10s backoff, StartLimitBurst=5

### Phase 6 — Get CodeMax running inside the container
- [x] Dev container (`apollocore/dev:latest`) used for all builds + tests
- [x] `podman-dev.sh --build` runs cmake + build inside dev container
- [x] `podman-dev.sh --ctest` runs ctest inside dev container
- [x] MCP conformance 27/27: PASS ✓ (inside runtime container)
- [x] SSE streaming: `: connected` + keep-alive ✓
- [x] Progress notifications: start + finish frames ✓

---

## Verification Checklist

After each phase:

**Phase 1**: `grep "^FROM" Containerfile` → no `@sha256:` short-name references
**Phase 2**: `podman images | grep apollocore/dev` → image exists
**Phase 3**: Binary runs with `./build/src/mcp_server/apollo_mcp_server --help`
**Phase 4**: `podman images | grep apollocore/runtime` → image exists, ~200MB
**Phase 5**: `curl http://127.0.0.1:8765/api/config` → `embed_loaded: true, llm_loaded: true`
**Phase 6**: `python3 tests/test_mcp_conformance.py` → ALL OK

---

## Architecture: Final State

```
┌─────────────────────────────────────────────────────────┐
│  AerynOS host (lee, uid=1000)                         │
│                                                         │
│  /opt/apollocore/models/        ← GGUF model files     │
│  /var/lib/apollocore/data/     ← SQLite KB            │
│  /var/log/apollocore/          ← logs                  │
│  ~/projects/AI/ApolloCore/     ← source code           │
│                                                         │
│  ┌─────────────────────────────────────────────────┐  │
│  │  Podman container: apollocore/dev:latest          │  │
│  │  (root inside build, your user for volume mounts)│  │
│  │                                                   │  │
│  │  Tools: g++-15, cmake, tree-sitter-cli, cargo   │  │
│  │  Mounts: ~/projects/AI/ApolloCore → /workspace  │  │
│  │  Mounts: /opt/apollocore/models → container     │  │
│  │  Mounts: /var/lib/apollocore/data → container    │  │
│  │                                                   │  │
│  │  CodeMax agent runs HERE. All builds + tests.    │  │
│  └─────────────────────────────────────────────────┘  │
│                                                         │
│  ┌─────────────────────────────────────────────────┐  │
│  │  Quadlet: ~/.config/containers/systemd/         │  │
│  │  Service: apollocore-mcp-server                │  │
│  │  Container: apollocore/runtime:latest            │  │
│  │  (tiny, no build tools, binary + libs only)    │  │
│  │  Listens: 127.0.0.1:8765                       │  │
│  └─────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Known Constraints

1. **No GPU access in Podman** — `GGML_VULKAN=OFF`, CPU-only via OpenBLAS
2. **Ports < 1024 need sudo** on host — use port 8765 (>= 1024, no sudo needed)
3. **Model files must exist on host** at `/opt/apollocore/models/` — volume-mounted into both dev and runtime containers
4. **First build ≈ 15-20 min** — subsequent builds hit Podman layer cache

---

## Scripts Reference

| Script | Purpose |
|--------|---------|
| `build.sh` | Build on host (for comparison/debugging) |
| `Containerfile.dev` | Dev container image (full toolchain) |
| `Containerfile` | Runtime container image (binary + libs only) |
| `podman-run.sh` | Run dev container interactively |
| `podman-systemd.sh` | Generate + install Quadlet systemd unit |
| `deploy_new_build.sh` | Deploy updated binary to running service |
