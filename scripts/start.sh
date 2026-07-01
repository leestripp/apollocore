#!/usr/bin/env bash
# start.sh — start the ApolloCore MCP server manually (foreground).
#
# This is the dev fallback for environments without systemd (distrobox,
# CI containers, anything not running systemd as PID 1).
#
# On a real Ubuntu 26.04 host use systemd instead:
#
#   sudo systemctl start apollo_mcp_server   # uses packaging/apollo_mcp_server.service
#
# Usage:
#   ./start.sh                 # defaults: --port 8765 --data-dir data --no-ui
#   ./start.sh --port 9000     # pass-through extra args to apollo_mcp_server
#   ./start.sh --detach        # run in background, write pid to .apollo_mcp_server.pid
#   APOLLO_PORT=9000 ./start.sh
#
# Environment variables (all optional):
#   APOLLO_PORT             default 8765
#   APOLLO_HOST             default 0.0.0.0
#   APOLLO_DATA_DIR         default ./data
#   APOLLO_EMBED_MODEL      default ./models/nomic-embed-text-v1.5.Q4_K_M.gguf
#   APOLLO_LLM_MODEL        default ./models/qwen2.5-0.5b-instruct-q4_k_m.gguf
#   APOLLO_UI               default 1 (set to 0 / --no-ui to disable)
#   APOLLO_VERBOSE          default 0 (set to 1 / -v for verbose logging)
#
# Exit codes:
#   0  server started cleanly (foreground: Ctrl-C; detach: wrote pid file)
#        OR already running (when --status / --check)
#   1  binary not found (build it first: cmake --build build --target apollo_mcp_server)
#        OR not running (when --status / --check)
#   2  data dir could not be created
#   3  port already in use
#
# --status / --check:
#   exit 0 if the server is reachable at APOLLO_HOST:APOLLO_PORT (/health 200),
#   exit 1 otherwise. Useful in shell conditionals:
#       ./scripts/start.sh --status || ./scripts/start.sh --detach
set -euo pipefail

# ────────────────────────────────────────────────────────────────────
# Resolve paths relative to this script's location so it works from
# any CWD. The script lives in scripts/, the binary is in build/.
# ────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

BIN="$PROJECT_ROOT/build/src/mcp_server/apollo_mcp_server"
if [[ ! -x "$BIN" ]]; then
    echo "start.sh: binary not found or not executable: $BIN" >&2
    echo "  build it with:  cmake --build build --target apollo_mcp_server" >&2
    exit 1
fi

# ────────────────────────────────────────────────────────────────────
# ────────────────────────────────────────────────────────────────────
# Track which paths the user *actually* set BEFORE we fill defaults.
# We only forward --embed-model / --llm-model to the binary when the
# user picked the path themselves — otherwise the binary sees no flag
# and falls back to <data-dir>/config.json (which is how the Settings
# page persists model choices across restarts).
#
# `${VAR+set}` is "set" if VAR is defined (even if empty), "unset"
# otherwise. Snapshot now, then fill defaults, then translate the
# "set"/"unset" markers to bash booleans.
EMBED_USER_SET="${APOLLO_EMBED_MODEL+set}"
LLM_USER_SET="${APOLLO_LLM_MODEL+set}"

# Apply environment defaults if caller did not set them.
: "${APOLLO_PORT:=8765}"
: "${APOLLO_HOST:=0.0.0.0}"
: "${APOLLO_DATA_DIR:=$PROJECT_ROOT/data}"
: "${APOLLO_EMBED_MODEL:=$PROJECT_ROOT/models/nomic-embed-text-v1.5.Q4_K_M.gguf}"
: "${APOLLO_LLM_MODEL:=$PROJECT_ROOT/models/qwen2.5-0.5b-instruct-q4_k_m.gguf}"
: "${APOLLO_UI:=1}"
: "${APOLLO_VERBOSE:=0}"

[[ "$EMBED_USER_SET" == "set" ]] && USER_SET_EMBED=1 || USER_SET_EMBED=0
[[ "$LLM_USER_SET"   == "set" ]] && USER_SET_LLM=1   || USER_SET_LLM=0

# Allow --no-ui via flag too (we still translate it for convenience).
ARGS=()
DETACH=0
CHECK_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --detach|-d) DETACH=1 ;;
        --no-ui)      APOLLO_UI=0 ;;
        --status|--check|--is-running)
            CHECK_ONLY=1 ;;
        *)            ARGS+=("$arg") ;;
    esac
done

# --status short-circuit: probe and exit.
if [[ "$CHECK_ONLY" == "1" ]]; then
    HOST_FOR_PROBE=${APOLLO_PROBE_HOST:-127.0.0.1}
    if curl -fsS --max-time 2 "http://${HOST_FOR_PROBE}:${APOLLO_PORT}/health" \
            | grep -q '"status":"ok"'; then
        echo "start.sh: APOLLO MCP server already running at ${HOST_FOR_PROBE}:${APOLLO_PORT}"
        exit 0
    fi
    echo "start.sh: no server reachable at ${HOST_FOR_PROBE}:${APOLLO_PORT}" >&2
    exit 1
fi

# Build CLI args from environment.
ARGS+=(--host "$APOLLO_HOST")
ARGS+=(--port "$APOLLO_PORT")
ARGS+=(--data-dir "$APOLLO_DATA_DIR")
# Forward --embed-model / --llm-model ONLY when the user actually chose
# the path themselves (env var or --flag on the command line). When the
# user didn't override, leave the flag OFF — the binary's parse_args then
# uses its placeholder default, and main() reads <data-dir>/config.json
# (written by the Settings page) to override the placeholder.
if (( USER_SET_EMBED )); then ARGS+=(--embed-model "$APOLLO_EMBED_MODEL"); fi
if (( USER_SET_LLM    )); then ARGS+=(--llm-model   "$APOLLO_LLM_MODEL");   fi
[[ "$APOLLO_UI" == "0" ]]      && ARGS+=(--no-ui)
[[ "$APOLLO_VERBOSE" == "1" ]] && ARGS+=(--verbose)

# ────────────────────────────────────────────────────────────────────
# Prepare runtime directories.
# ────────────────────────────────────────────────────────────────────
mkdir -p "$APOLLO_DATA_DIR" "$(dirname "$APOLLO_EMBED_MODEL")" \
         "$(dirname "$APOLLO_LLM_MODEL")" || {
    echo "start.sh: failed to create data/model directories" >&2
    exit 2
}

# Quick port-collision check (best-effort; race-prone but catches the
# common case of "already running").
if command -v ss >/dev/null 2>&1; then
    if ss -ltn "sport = :$APOLLO_PORT" 2>/dev/null | grep -q ":$APOLLO_PORT\b"; then
        echo "start.sh: port $APOLLO_PORT already in use" >&2
        exit 3
    fi
fi

# ────────────────────────────────────────────────────────────────────
# Run.
# ────────────────────────────────────────────────────────────────────
PID_FILE="$PROJECT_ROOT/.apollo_mcp_server.pid"
LOG_FILE="$PROJECT_ROOT/apollo_mcp_server.log"

echo "start.sh: launching apollo_mcp_server"
echo "  host:        $APOLLO_HOST:$APOLLO_PORT"
echo "  data-dir:    $APOLLO_DATA_DIR"
echo "  embed-model: ${APOLLO_EMBED_MODEL} ($( [[ -f $APOLLO_EMBED_MODEL ]] && echo present || echo MISSING ))"
echo "  llm-model:   ${APOLLO_LLM_MODEL}   ($( [[ -f $APOLLO_LLM_MODEL ]] && echo present || echo optional ))"
echo "  ui:          $( [[ $APOLLO_UI == 1 ]] && echo enabled || echo disabled )"
echo

if [[ "$DETACH" == "1" ]]; then
    nohup "$BIN" "${ARGS[@]}" >"$LOG_FILE" 2>&1 &
    echo $! > "$PID_FILE"
    echo "start.sh: detached, pid=$!  log=$LOG_FILE"
    echo "  stop with:  ./scripts/stop.sh"
    exit 0
fi

# Foreground: trap signals and pass them through.
trap 'echo; echo "start.sh: stopping..."; kill -TERM "$BIN_PID" 2>/dev/null || true; wait "$BIN_PID" 2>/dev/null || true; exit 0' INT TERM

"$BIN" "${ARGS[@]}" &
BIN_PID=$!
echo $BIN_PID > "$PID_FILE"
wait "$BIN_PID"
RC=$?
rm -f "$PID_FILE"
exit "$RC"