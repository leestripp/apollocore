#!/usr/bin/env python3
"""
test_auth.py — Bearer token authentication tests for the MCP server.

Validates Phase A3 (APOLLO_AUTH_TOKEN) by:
  1. Setting APOLLO_AUTH_TOKEN=s3cr3t in the environment
  2. Restarting the server
  3. Verifying that:
       - No token         → 401 Unauthorized
       - Wrong token      → 401 Unauthorized
       - Right token      → 200 OK
       - Case-insensitive "bearer" prefix → 200 OK

The test starts its own server instance on port 8766 so it doesn't collide
with the production server on 8765. The server is killed cleanly at the
end.

Requires:
  - /home/lee/projects/AI/ApolloCore/build/src/mcp_server/apollo_mcp_server
  - python3 + urllib (stdlib only)
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Optional

URL = "http://127.0.0.1:8766"
PORT = 8766
TOKEN = "s3cr3t-token-xyz"
BINARY = "/home/lee/projects/AI/ApolloCore/build/src/mcp_server/apollo_mcp_server"
LOG_FILE = "/tmp/apollo_auth_test.log"


def wait_for_server(url: str, timeout_s: int = 45) -> bool:
    """Poll /health until the server answers, or timeout."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as r:
                if r.status == 200:
                    return True
        except Exception:
            time.sleep(0.5)
    return False


def post_mcp(headers: dict, body: dict) -> tuple[int, dict]:
    """POST a JSON-RPC body to /mcp with the given headers.

    Returns (http_status, parsed_body).
    """
    req = urllib.request.Request(
        URL + "/mcp",
        data=json.dumps(body).encode(),
        method="POST",
        headers={"Content-Type": "application/json",
                 "Accept": "application/json",
                 **headers},
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read())
        except Exception:
            return e.code, {}


def main() -> int:
    failures: list[str] = []

    # ---- 1. Start server with APOLLO_AUTH_TOKEN set ----
    print(f"[test] launching server on :{PORT} with APOLLO_AUTH_TOKEN={TOKEN!r}")
    env = os.environ.copy()
    env["APOLLO_AUTH_TOKEN"] = TOKEN
    log = open(LOG_FILE, "w")
    proc = subprocess.Popen(
        [BINARY, "--port", str(PORT)],
        env=env, stdout=log, stderr=log,
        preexec_fn=os.setsid,
    )
    try:
        if not wait_for_server(URL + "/health", 45):
            print(f"[FAIL] server didn't come up. Log:\n{open(LOG_FILE).read()}")
            return 2

        # ---- 2. initialize request — no token, expect 401 ----
        status, body = post_mcp(
            headers={"Origin": "http://localhost:3000"},
            body={"jsonrpc": "2.0", "id": 1, "method": "initialize",
                  "params": {"protocolVersion": "2025-11-25",
                             "capabilities": {},
                             "clientInfo": {"name": "auth-test", "version": "0.0.1"}}},
        )
        if status == 401:
            print("  no token        -> 401 ✓")
        else:
            failures.append(f"no token expected 401, got {status}")
            print(f"  no token        -> {status} ✗ (expected 401)")

        # ---- 3. initialize request — wrong token, expect 401 ----
        status, body = post_mcp(
            headers={"Origin": "http://localhost:3000",
                     "Authorization": "Bearer wrong-token"},
            body={"jsonrpc": "2.0", "id": 1, "method": "initialize",
                  "params": {"protocolVersion": "2025-11-25",
                             "capabilities": {},
                             "clientInfo": {"name": "auth-test", "version": "0.0.1"}}},
        )
        if status == 401:
            print("  wrong token     -> 401 ✓")
        else:
            failures.append(f"wrong token expected 401, got {status}")
            print(f"  wrong token     -> {status} ✗ (expected 401)")

        # ---- 4. initialize request — right token, expect 200 + result ----
        # Per spec, the SID is returned via the Mcp-Session-Id HTTP header,
        # not in the JSON body. We just assert the handshake succeeds.
        status, body = post_mcp(
            headers={"Origin": "http://localhost:3000",
                     "Authorization": f"Bearer {TOKEN}"},
            body={"jsonrpc": "2.0", "id": 1, "method": "initialize",
                  "params": {"protocolVersion": "2025-11-25",
                             "capabilities": {},
                             "clientInfo": {"name": "auth-test", "version": "0.0.1"}}},
        )
        if status == 200 and "result" in body:
            print(f"  right token     -> 200, handshake OK ✓")
        else:
            failures.append(f"right token expected 200+result, got {status}: {body}")
            print(f"  right token     -> {status} ✗ (expected 200)")

        # ---- 5. Case-insensitive "bearer" prefix (RFC 6750) ----
        status, body = post_mcp(
            headers={"Origin": "http://localhost:3000",
                     "Authorization": f"bearer {TOKEN}"},
            body={"jsonrpc": "2.0", "id": 1, "method": "ping", "params": {}},
        )
        if status == 200:
            print("  lowercase bearer-> 200 ✓ (RFC 6750 case-insensitive)")
        else:
            failures.append(f"lowercase 'bearer' expected 200, got {status}")
            print(f"  lowercase bearer-> {status} ✗ (expected 200)")

    finally:
        # ---- Tear down ----
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception:
                pass
        log.close()

    # ---- Report ----
    if failures:
        print(f"\n{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
