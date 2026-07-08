#!/bin/bash
# podman-run.sh — Run ApolloCore inside a Podman container
#
# Prerequisites:
#   1. Build the image first:
#        podman build -f Containerfile -t apollocore/runtime:latest .
#
#   2. Or build the dev image (includes build tools + source mount):
#        podman build -f Containerfile -t apollocore/runtime:latest .   # runtime
#        podman build -f Containerfile.dev -t apollocore/dev:latest .   # dev
#
# Usage:
#   ./podman-run.sh                  # Run server in background
#   ./podman-run.sh --dev            # Interactive dev shell (source mount)
#   ./podman-run.sh --tail           # Tail server logs
#   ./podman-run.sh --stop           # Stop the container
#
set -e

IMAGE="${APOLLOCORE_IMAGE:-apollocore/runtime:latest}"
CONTAINER="apollocore-server"

# ── volume mounts ────────────────────────────────────────────────────────────
# Models are at /opt/apollocore/models on the host → container
MODELS_SRC="${APOLLOCORE_MODELS:-/opt/apollocore/models}"
DATA_SRC="${APOLLOCORE_DATA:-/var/lib/apollocore/data}"
LOGS_SRC="${APOLLOCORE_LOGS:-/var/log/apollocore}"

# ── helpers ────────────────────────────────────────────────────────────────────
stop() {
    echo "[stop] Stopping and removing container..."
    podman stop "$CONTAINER" 2>/dev/null || true
    podman rm   "$CONTAINER" 2>/dev/null || true
    echo "[stop] Done."
}

tail_logs() {
    podman exec "$CONTAINER" tail -f /var/log/apollocore/apollo_mcp_server.log
}

status() {
    podman inspect "$CONTAINER" --format '{{.State.Status}}' 2>/dev/null || echo "not running"
}

# ── parse args ───────────────────────────────────────────────────────────────
case "${1:-}" in
    --dev)
        exec podman run -it --rm \
            --hostname apollo-dev \
            -p 127.0.0.1:8765:8765 \
            -v "$MODELS_SRC:/opt/apollocore/models" \
            -v "$DATA_SRC:/var/lib/apollocore/data" \
            -v "$LOGS_SRC:/var/log/apollocore" \
            -v "$(pwd):/workspace" \
            -w /workspace \
            "$IMAGE" \
            bash
        ;;

    --tail)
        tail_logs
        ;;

    --stop)
        stop
        ;;

    --status)
        status
        ;;

    --)
        shift
        exec podman run "$@"
        ;;

    *)
        stop
        echo "[run] Building image (one-time, may take 15 min)..."
        podman build \
            -f Containerfile \
            -t apollocore/runtime:latest \
            .

        echo "[run] Starting ApolloCore server..."
        podman run -d \
            --name "$CONTAINER" \
            -p 127.0.0.1:8765:8765 \
            -v "$MODELS_SRC:/opt/apollocore/models" \
            -v "$DATA_SRC:/var/lib/apollocore/data" \
            -v "$LOGS_SRC:/var/log/apollocore" \
            --restart=on-failure \
            "$IMAGE"

        echo "[run] Waiting for server to start..."
        sleep 8

        echo ""
        echo "=== ApolloCore Server ==="
        echo "  MCP HTTP  : http://127.0.0.1:8765/mcp"
        echo "  Health   : http://127.0.0.1:8765/health"
        echo "  Config   : http://127.0.0.1:8765/api/config"
        echo ""
        curl -s http://127.0.0.1:8765/api/config | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(f'  embed_loaded: {d.get(\"embed_loaded\")}')
print(f'  llm_loaded : {d.get(\"llm_loaded\")}')
"
        echo ""
        echo "  Logs: podman logs $CONTAINER"
        echo "  Shell: podman exec -it $CONTAINER bash"
        echo "  Stop : ./podman-run.sh --stop"
        ;;
esac
