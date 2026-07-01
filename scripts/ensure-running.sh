#!/usr/bin/env bash
# scripts/ensure-running.sh — start the ApolloCore MCP server if it is
# not already running. Idempotent; safe to run multiple times.
#
# This is the entry point agents and humans should use in any
# environment where the server might not already be up:
#   - distrobox / podman containers (no systemd as PID 1)
#   - CI runners
#   - fresh shells
#
# On a production host, prefer `sudo systemctl start apollo_mcp_server`.
# This script is the no-systemd equivalent.
#
# Usage:
#   ./scripts/ensure-running.sh                     # uses defaults
#   APOLLO_PORT=9000 ./scripts/ensure-running.sh    # override port
#   ./scripts/ensure-running.sh --no-ui              # start without UI
#   APOLLO_PROBE_HOST=10.0.2.2 ./scripts/ensure-running.sh
#
# Exit codes:
#   0  server is now running and responding
#   1  binary not built
#   2  server failed to bind (port in use by something else,
#      OR start.sh returned non-zero)
#   3  server started but did not respond to /health within 30 s
#
# Behaviour:
#   1. Probe http://${APOLLO_PROBE_HOST:-127.0.0.1}:${APOLLO_PORT}/health.
#      If /health returns 200 with `"status":"ok"`, print and exit 0
#      without starting anything.
#   2. Otherwise, run ./scripts/start.sh --detach (passing through any
#      extra args). This itself is idempotent on the port-collision
#      check.
#   3. Poll /health for up to 30 s until the server answers.
#      Print the URL when it does.
#
# Environment overrides:
#   APOLLO_PORT              default 8765
#   APOLLO_PROBE_HOST         default 127.0.0.1
#   APOLLO_HEALTH_TIMEOUT     default 30 (seconds)
#
# When the apollo_mcp_server lives on a different host (e.g. running
# as a systemd service on the Ubuntu host while the agent is in a
# distrobox inside it), use APOLLO_PROBE_HOST=<host-IP>. The script
# detects and skips starting if the host is already serving.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

: "${APOLLO_PORT:=8765}"
: "${APOLLO_PROBE_HOST:=127.0.0.1}"
: "${APOLLO_HEALTH_TIMEOUT:=30}"

URL="http://${APOLLO_PROBE_HOST}:${APOLLO_PORT}"
HEALTH_URL="${URL}/health"

# ── 1. probe ────────────────────────────────────────────────────────
probe() {
    curl -fsS --max-time 2 "$HEALTH_URL" 2>/dev/null \
        | grep -q '"status":"ok"'
}

if probe; then
    echo "ensure-running.sh: server already responding at $URL"
    exit 0
fi

# ── 2. start (if not running) ────────────────────────────────────
echo "ensure-running.sh: no server at $URL; starting via ./scripts/start.sh --detach"
# NB: use the "cmd || { rc=$?; ... }" pattern — `$?` after `if ! cmd`
# is the INVERTED exit (0 if cmd failed), not cmd's actual exit code.
"$SCRIPT_DIR/start.sh" --detach "$@" || {
    rc=$?
    echo "ensure-running.sh: start.sh exited $rc" >&2
    # Try one more probe — the port may already be held by another
    # tool serving /health on the same URL.
    if probe; then
        echo "ensure-running.sh: actually — server IS responding now at $URL"
        exit 0
    fi
    case "$rc" in
        3) exit 3 ;;  # port already in use, no probe → no server
        *) exit $rc ;;
    esac
}

# ── 3. wait for /health ──────────────────────────────────────────
echo "ensure-running.sh: waiting up to ${APOLLO_HEALTH_TIMEOUT}s for $HEALTH_URL to respond..."
deadline=$(( SECONDS + APOLLO_HEALTH_TIMEOUT ))
while (( SECONDS < deadline )); do
    if probe; then
        echo "ensure-running.sh: ✓ server responding at $URL"
        echo "  MCP:    ${URL}/mcp"
        echo "  UI:     ${URL}/"
        echo "  Health: ${HEALTH_URL}"
        exit 0
    fi
    sleep 1
done

echo "ensure-running.sh: server did not respond within ${APOLLO_HEALTH_TIMEOUT}s" >&2
echo "                       log: $PROJECT_ROOT/apollo_mcp_server.log" >&2
exit 3