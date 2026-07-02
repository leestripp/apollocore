"""
test_mcp_conformance.py — live HTTP-level conformance smoke test for the
MCP security guards added in Phase A.

Runs against an already-running apollo_mcp_server on $APOLLO_PORT (default
8765). Asserts:

  A1 — Origin header validation (DNS-rebind defense)
        * evil.example.com → 403
        * localhost      → 200

  A2 — MCP-Protocol-Version header
        * valid version     → 200
        * unknown version   → 400
        * missing (post-init) → 200 (pre-init we already tested in
          test_mcp_dispatcher.py)

  A3 — Mcp-Session-Id is round-tripped after initialize.

  A4 — DELETE /mcp terminates the session.
        * first DELETE → 200
        * second DELETE → 404 (spec §Session-Management)
"""

import os
import sys
import json
import urllib.request
import urllib.error

PORT = int(os.environ.get("APOLLO_PORT", "8765"))
URL = f"http://127.0.0.1:{PORT}"


def post_mcp(body, *, headers=None, expect_status=None, expect_id=None):
    """POST /mcp with the given JSON body and headers. Returns (status, json_or_None)."""
    data = json.dumps(body).encode()
    h = {"Content-Type": "application/json", "Accept": "application/json"}
    if headers:
        h.update(headers)
    req = urllib.request.Request(f"{URL}/mcp", data=data, method="POST", headers=h)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            status = resp.status
            raw = resp.read().decode()
            sid = resp.headers.get("Mcp-Session-Id")
    except urllib.error.HTTPError as e:
        status = e.code
        raw = e.read().decode()
        sid = e.headers.get("Mcp-Session-Id") if e.headers else None
    if expect_status is not None:
        assert status == expect_status, \
            f"expected status {expect_status}, got {status}: {raw}"
    parsed = None
    if raw:
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            pass
    if expect_id is not None and parsed is not None:
        assert parsed.get("id") == expect_id, \
            f"expected id={expect_id}, got {parsed.get('id')}: {parsed}"
    return status, parsed, sid


def delete_mcp(sid):
    """DELETE /mcp with the given session id. Returns status code."""
    req = urllib.request.Request(f"{URL}/mcp", method="DELETE",
                                  headers={"Mcp-Session-Id": sid})
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status
    except urllib.error.HTTPError as e:
        return e.code


def must_alive():
    """Sanity: server is responding at all."""
    with urllib.request.urlopen(f"{URL}/health", timeout=3) as resp:
        assert resp.status == 200


# ---------------------------------------------------------------------------
# A1 — Origin validation
# ---------------------------------------------------------------------------
def test_origin_rejects_evil():
    must_alive()
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {"name": "t", "version": "1"}}},
        headers={"Origin": "https://evil.example.com"},
        expect_status=403,
    )
    assert body and "error" in body, f"expected JSON-RPC error body, got {body}"
    # Spec: error body MUST have no id (parse-error style).
    assert body.get("id") is None, f"expected null id on Origin rejection, got {body}"
    assert "Origin" in body["error"]["message"], \
        f"error should mention Origin, got: {body['error']['message']}"
    print(f"  A1 evil origin  -> HTTP {status} ({body['error']['code']}) ✓")


def test_origin_accepts_localhost():
    status, body, sid = post_mcp(
        {"jsonrpc": "2.0", "id": 2, "method": "initialize",
         "params": {"protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {"name": "t", "version": "1"}}},
        headers={"Origin": "http://localhost:3000"},
        expect_status=200,
    )
    assert body and "result" in body, f"expected result, got {body}"
    assert sid, f"server should echo Mcp-Session-Id, got empty"
    print(f"  A1 localhost    -> HTTP {status}, SID={sid[:8]}... ✓")
    return sid


# ---------------------------------------------------------------------------
# A2 — MCP-Protocol-Version header
# ---------------------------------------------------------------------------
def test_protocol_version_rejects_unknown():
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}},
        headers={"MCP-Protocol-Version": "1999-01-01"},
        expect_status=400,
    )
    assert body and "error" in body
    msg = body["error"]["message"]
    assert "MCP-Protocol-Version" in msg or "Protocol-Version" in msg, \
        f"error should reference the header, got: {msg}"
    print(f"  A2 unknown ver   -> HTTP {status} ({msg[:60]}...) ✓")


def test_protocol_version_accepts_known():
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 4, "method": "tools/list", "params": {}},
        headers={"MCP-Protocol-Version": "2025-11-25"},
        expect_status=200,
    )
    assert body and "result" in body
    assert "tools" in body["result"]
    print(f"  A2 2025-11-25    -> HTTP {status}, {len(body['result']['tools'])} tools ✓")


# ---------------------------------------------------------------------------
# A4 — DELETE /mcp
# ---------------------------------------------------------------------------
def test_delete_session(sid):
    s1 = delete_mcp(sid)
    assert s1 == 200, f"first DELETE should return 200, got {s1}"
    s2 = delete_mcp(sid)
    assert s2 == 404, f"second DELETE should return 404 (gone), got {s2}"
    print(f"  A4 DELETE       -> 200 then 404 ✓")


def main():
    print("Running MCP conformance smoke test (Phase A):\n")
    must_alive()
    test_origin_rejects_evil()
    sid = test_origin_accepts_localhost()
    test_protocol_version_rejects_unknown()
    test_protocol_version_accepts_known()
    test_delete_session(sid)
    print("\nALL OK")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"\nFAIL: {e}", file=sys.stderr)
        sys.exit(1)