#!/usr/bin/env bash
# scripts/install.sh — install ApolloCore as a systemd service on
# Ubuntu 26.04 (or any distro with systemd v255+).
#
# What this does:
#   1. Validates sudo + build artifacts.
#   2. Creates the `apollocore` system user + group.
#   3. Creates the runtime directories:
#        /opt/apollocore/bin/
#        /opt/apollocore/share/apollocore/ui/   (HTML/CSS/JS)
#        /opt/apollocore/models/               (GGUF files)
#        /var/lib/apollocore/data/             (KB SQLite + FBIN)
#        /var/log/apollocore/                 (service logs)
#        /var/cache/apollocore/                (HTTP client cache)
#   4. Installs the binary + UI via `cmake --install`.
#   5. Copies the systemd unit + env file into /etc/.
#   6. daemon-reload + enable --now.
#
# Re-running this script is safe: it overwrites in place and restarts
# the service.
#
# Environment variables:
#   APOLLO_INSTALL_PREFIX       default /opt/apollocore
#   APOLLO_DATA_DIR             default /var/lib/apollocore/data
#   APOLLO_NO_SYSTEMD=1         skip systemd enable/start (manual start.sh instead)
#   APOLLO_SKIP_DOWNLOAD=1      don't auto-fetch GGUF models (offline install)
#
# Exit codes:
#   0  installed (or already installed)
#   1  not root (no sudo)
#   2  build artifacts missing — run cmake first
#   3  user-create failed
#   4  directory-create failed
#   5  install failed
#   6  systemd unit install failed

set -euo pipefail

# ─────────────────────────── paths ───────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

: "${APOLLO_INSTALL_PREFIX:=/opt/apollocore}"
: "${APOLLO_DATA_DIR:=/var/lib/apollocore/data}"
: "${APOLLO_NO_SYSTEMD:=0}"
: "${APOLLO_SKIP_DOWNLOAD:=0}"

BIN_SRC="$PROJECT_ROOT/build/src/mcp_server/apollo_mcp_server"
UI_SRC="$PROJECT_ROOT/src/mcp_server/static"
UNIT_SRC="$PROJECT_ROOT/packaging/apollo_mcp_server.service"
ENV_SRC="$PROJECT_ROOT/packaging/apollocore.env"

# ─────────────────────────── root check ────────────────────────
if [[ $EUID -ne 0 ]] && ! sudo -n true 2>/dev/null; then
    echo "install.sh: this script needs sudo. Re-run as root or set up passwordless sudo." >&2
    exit 1
fi
run() {
    # Run as root via sudo if we're not already root.
    if [[ $EUID -eq 0 ]]; then "$@"
    else sudo "$@"
    fi
}

# ─────────────────────────── preflight ──────────────────────────
if [[ ! -x "$BIN_SRC" ]]; then
    echo "install.sh: binary not found at $BIN_SRC" >&2
    echo "  build it first: cmake --build build --target apollo_mcp_server" >&2
    exit 2
fi
if [[ ! -d "$UI_SRC" ]]; then
    echo "install.sh: UI directory not found at $UI_SRC" >&2
    exit 2
fi
if [[ ! -f "$UNIT_SRC" ]]; then
    echo "install.sh: systemd unit not found at $UNIT_SRC" >&2
    exit 2
fi
if [[ ! -f "$ENV_SRC" ]]; then
    echo "install.sh: env file not found at $ENV_SRC" >&2
    exit 2
fi

# ─────────────────────────── user ──────────────────────────────
if ! id apollocore >/dev/null 2>&1; then
    echo "install.sh: creating apollocore system user"
    run useradd --system --user-group --home-dir "$APOLLO_INSTALL_PREFIX" \
        --shell /usr/sbin/nologin --comment "ApolloCore MCP service" \
        apollocore || { echo "install.sh: useradd failed" >&2; exit 3; }
fi

# ─────────────────────────── directories ────────────────────────
echo "install.sh: creating directories under $APOLLO_INSTALL_PREFIX and /var/lib /var/log /var/cache"
run mkdir -p \
    "$APOLLO_INSTALL_PREFIX/bin" \
    "$APOLLO_INSTALL_PREFIX/models" \
    "$APOLLO_DATA_DIR" \
    /var/log/apollocore \
    /var/cache/apollocore || { echo "install.sh: mkdir failed" >&2; exit 4; }

# ─────────────────────────── install binary + UI ───────────────
echo "install.sh: installing binary + UI to $APOLLO_INSTALL_PREFIX"
run cmake --install "$PROJECT_ROOT/build" \
    --prefix "$APOLLO_INSTALL_PREFIX" --component apollo_mcp_server \
    2>/dev/null || run cmake --install "$PROJECT_ROOT/build" \
    --prefix "$APOLLO_INSTALL_PREFIX" || {
        echo "install.sh: cmake --install failed; falling back to manual copy" >&2
        run install -m 0755 "$BIN_SRC" "$APOLLO_INSTALL_PREFIX/bin/apollo_mcp_server"
        run cp -r "$UI_SRC" "$APOLLO_INSTALL_PREFIX/share/apollocore/ui"
    }

# ─────────────────────────── systemd ───────────────────────────
# /etc/default/ may not exist on all distros; ensure it does.
run mkdir -p /etc/default

# ─────────────────────────── env + unit ────────────────────────
echo "install.sh: installing systemd unit + env file"
run install -m 0644 "$ENV_SRC"  /etc/default/apollocore
run install -m 0644 "$UNIT_SRC" /etc/systemd/system/apollo_mcp_server.service || {
    echo "install.sh: unit install failed" >&2; exit 6; }

# ─────────────────────────── model fetch ───────────────────────
if [[ "$APOLLO_SKIP_DOWNLOAD" != "1" ]]; then
    if [[ ! -f "$APOLLO_INSTALL_PREFIX/models/nomic-embed-text-v1.5.Q4_K_M.gguf" ]]; then
        echo "install.sh: downloading nomic-embed-text-v1.5.Q4_K_M.gguf (~84 MB)"
        run mkdir -p "$APOLLO_INSTALL_PREFIX/models"
        run curl -fSL --retry 3 -o \
            "$APOLLO_INSTALL_PREFIX/models/nomic-embed-text-v1.5.Q4_K_M.gguf" \
            "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/nomic-embed-text-v1.5.Q4_K_M.gguf" \
            || echo "install.sh: warning — embedding model download failed; install manually" >&2
    fi
    if [[ ! -f "$APOLLO_INSTALL_PREFIX/models/qwen2.5-0.5b-instruct-q4_k_m.gguf" ]]; then
        echo "install.sh: downloading qwen2.5-0.5b-instruct-q4_k_m.gguf (~469 MB)"
        run curl -fSL --retry 3 -o \
            "$APOLLO_INSTALL_PREFIX/models/qwen2.5-0.5b-instruct-q4_k_m.gguf" \
            "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf" \
            || echo "install.sh: warning — LLM model download failed; install manually" >&2
    fi
fi

# ─────────────────────────── ownership ────────────────────────
echo "install.sh: setting ownership"
run chown -R apollocore:apollocore \
    "$APOLLO_INSTALL_PREFIX" \
    "$APOLLO_DATA_DIR" \
    /var/log/apollocore \
    /var/cache/apollocore

# ─────────────────────────── systemd ───────────────────────────
if [[ "$APOLLO_NO_SYSTEMD" == "1" ]] || ! command -v systemctl >/dev/null 2>&1; then
    echo
    echo "install.sh: done. Start manually with:"
    echo "  $APOLLO_INSTALL_PREFIX/bin/apollo_mcp_server --help"
    exit 0
fi

echo "install.sh: enabling + starting apollo_mcp_server.service"
run systemctl daemon-reload
run systemctl enable apollo_mcp_server.service
if ! run systemctl restart apollo_mcp_server.service; then
    echo "install.sh: systemctl restart failed; check 'journalctl -u apollo_mcp_server'" >&2
    exit 6
fi

echo
echo "install.sh: ✔ apollo_mcp_server installed and running"
echo "  binary:  $APOLLO_INSTALL_PREFIX/bin/apollo_mcp_server"
echo "  ui:      http://$(hostname -I | awk '{print $1}'):${APOLLO_PORT:-8765}/"
echo "  mcp:     http://$(hostname -I | awk '{print $1}'):${APOLLO_PORT:-8765}/mcp"
echo "  logs:    journalctl -u apollo_mcp_server -f"
echo "  status:  systemctl status apollo_mcp_server"