#!/usr/bin/env bash
# stop.sh — stop the apollo_mcp_server started by start.sh.
#
# Reads the PID from .apollo_mcp_server.pid in the project root and
# sends SIGTERM, then SIGKILL after 5 s if it hasn't exited.
#
# Use this in dev when start.sh was launched with --detach.
# On a real systemd host use: sudo systemctl stop apollo_mcp_server
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PID_FILE="$PROJECT_ROOT/.apollo_mcp_server.pid"

if [[ ! -f "$PID_FILE" ]]; then
    echo "stop.sh: no .apollo_mcp_server.pid found; nothing to stop" >&2
    # Fall back: pgrep by binary name.
    if command -v pgrep >/dev/null 2>&1; then
            if pids=$(pgrep -f "$PROJECT_ROOT/build/src/mcp_server/apollo_mcp_server" || true); [[ -n "$pids" ]]; then
            echo "stop.sh: but found running processes:" >&2
            echo "$pids" >&2
            kill -TERM $pids 2>/dev/null || true
            sleep 1
            kill -KILL $pids 2>/dev/null || true
        fi
    fi
    exit 0
fi

PID="$(cat "$PID_FILE")"
if ! kill -0 "$PID" 2>/dev/null; then
    echo "stop.sh: pid $PID not running; cleaning stale pidfile"
    rm -f "$PID_FILE"
    exit 0
fi

echo "stop.sh: sending SIGTERM to pid $PID"
kill -TERM "$PID"

# Wait up to 5 s.
for _ in $(seq 1 50); do
    if ! kill -0 "$PID" 2>/dev/null; then
        echo "stop.sh: stopped"
        rm -f "$PID_FILE"
        exit 0
    fi
    sleep 0.1
done

echo "stop.sh: still alive after 5 s; sending SIGKILL"
kill -KILL "$PID" 2>/dev/null || true
rm -f "$PID_FILE"
exit 0