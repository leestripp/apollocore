#!/usr/bin/env bash
# Run all MCP tests 3 times to confirm stability.
set -e
ROOT="$(dirname "$(readlink -f "$0")")/.."
cd "$ROOT"
for i in 1 2 3; do
    echo "=== run $i ==="
    ./scripts/ensure-running.sh >/dev/null 2>&1
    python3 tests/test_mcp_conformance.py 2>&1 | tail -2
    python3 tests/test_pagination.py 2>&1 | tail -2
    python3 tests/test_sse_streaming.py 2>&1 | tail -2
    python3 tests/test_progress_notification.py 2>&1 | tail -2
    /tmp/venv_test/bin/python -m pytest tests/test_mcp_sdk.py -q 2>&1 | tail -2
done