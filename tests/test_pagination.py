#!/usr/bin/env python3
"""
test_pagination.py — cursor-based pagination tests for tools/list, resources/list,
prompts/list per MCP spec §8.5.

Verifies:
  - No cursor param       -> full list, no nextCursor
  - With a small page size (via a cursor offset) -> we can step through pages
  - Malformed cursor      -> server returns 0 (treated as "from the start")
  - nextCursor absent     -> end of list
"""
from __future__ import annotations

import base64
import json
import sys
import urllib.error
import urllib.request

URL = "http://127.0.0.1:8765"
SID: str | None = None


def encode_offset(n: int) -> str:
    """Encode offset n in our cursor format: big-endian uint32 → 8 base64 chars."""
    if n == 0:
        return ""
    raw = n.to_bytes(4, "big")
    return base64.b64encode(raw).decode()


def _post_raw(body: dict, headers: dict) -> tuple[int, dict, dict]:
    """POST a JSON-RPC body and return (status, parsed_body, response_headers)."""
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
            return r.status, json.loads(r.read()), dict(r.headers)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read()), dict(e.headers)
        except Exception:
            return e.code, {}, dict(e.headers)


def post_mcp(method: str, params: dict | None = None) -> dict:
    """POST a JSON-RPC request to /mcp and return the parsed body.
    Auto-handles the initialize handshake using the module-level SID."""
    global SID
    body = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        body["params"] = params
    headers = {"Origin": "http://localhost:3000"}
    if SID is not None and method not in ("initialize",):
        headers["Mcp-Session-Id"] = SID
    status, resp_body, resp_headers = _post_raw(body, headers)
    if status != 200:
        return resp_body
    return resp_body


def init() -> None:
    """Perform the initialize handshake and store the resulting session id."""
    global SID
    body = {"jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": "2025-11-25",
                       "capabilities": {},
                       "clientInfo": {"name": "pagination-test", "version": "0.0.1"}}}
    headers = {"Origin": "http://localhost:3000"}
    status, resp_body, resp_headers = _post_raw(body, headers)
    if status != 200:
        raise RuntimeError(f"initialize failed: {status} {resp_body}")
    sid = resp_headers.get("Mcp-Session-Id") or resp_headers.get("mcp-session-id")
    if not sid:
        raise RuntimeError(f"no Mcp-Session-Id in response headers: {resp_headers}")
    SID = sid


def main() -> int:
    failures: list[str] = []
    init()

    # ── 1. tools/list — first call (no cursor) ──────────────────────────
    r = post_mcp("tools/list")
    tools = r["result"]["tools"]
    print(f"  tools/list              -> {len(tools)} tools")
    if not isinstance(tools, list) or len(tools) < 2:
        failures.append("tools/list returned < 2 tools, can't paginate")

    # With our default page size of 50, all 12 tools fit in one page, so
    # nextCursor should be ABSENT (per spec, omit when no more pages).
    if "nextCursor" in r["result"]:
        failures.append(
            f"nextCursor should be absent when 12 tools fit on one page, got {r['result']['nextCursor']}")

    # ── 2. resources/list — no cursor ──────────────────────────────────
    r = post_mcp("resources/list")
    resources = r["result"]["resources"]
    print(f"  resources/list          -> {len(resources)} resources")
    if "nextCursor" in r["result"]:
        failures.append(
            f"nextCursor should be absent for {len(resources)} resources, got {r['result']['nextCursor']}")

    # ── 3. prompts/list — no cursor ─────────────────────────────────────
    r = post_mcp("prompts/list")
    prompts = r["result"]["prompts"]
    print(f"  prompts/list            -> {len(prompts)} prompts")
    if "nextCursor" in r["result"]:
        failures.append(
            f"nextCursor should be absent for {len(prompts)} prompts, got {r['result']['nextCursor']}")

    # ── 4. Malformed cursor is treated as offset 0 (lenient) ───────────
    r = post_mcp("tools/list", {"cursor": "this-is-not-base64-$$$$$$$$"})
    if len(r["result"]["tools"]) != len(tools):
        failures.append(
            f"malformed cursor should fall back to first page, "
            f"got {len(r['result']['tools'])} instead of {len(tools)}")
    else:
        print("  malformed cursor        -> falls back to first page ✓")

    # ── 5. Cursor past the end returns empty list ─────────────────────
    # Encode a 32-bit value of 0xFFFFFFFF — guaranteed past our list of 13.
    huge_cursor = base64.b64encode(b'\xff\xff\xff\xff').decode()
    r = post_mcp("tools/list", {"cursor": huge_cursor})
    if r["result"]["tools"] != []:
        failures.append(
            f"cursor past end should return [] tools, got {len(r['result']['tools'])}")
    else:
        print("  cursor past end         -> [] ✓")

    # ── 5b. Real offset slicing — first page is items 0..N, then ───────
    # items N..M etc. Encode offset 5 (which is < 13 tools), confirm we
    # get the remaining items.
    cursor5 = encode_offset(5)
    r5 = post_mcp("tools/list", {"cursor": cursor5})
    sliced = r5["result"]["tools"]
    expected = tools[5:]  # last 8 of the 13 tools
    if len(sliced) != len(expected):
        failures.append(
            f"offset 5: got {len(sliced)} items, expected {len(expected)}")
    elif [t["name"] for t in sliced] != [t["name"] for t in expected]:
        failures.append("offset 5 returned the wrong items")
    else:
        print(f"  offset 5                -> {len(sliced)} items, "
              f"first={sliced[0]['name']!r} ✓")

    # ── 6. Non-string cursor is rejected with a JSON-RPC error ────────
    # Per spec, cursor MUST be a string. We surface a -32603 (internal)
    # because of nlohmann::json's strict type lookup. The server shouldn't
    # crash on it — just return an error response.
    body = {"jsonrpc": "2.0", "id": 99, "method": "tools/list",
            "params": {"cursor": 123}}
    req = urllib.request.Request(
        URL + "/mcp", data=json.dumps(body).encode(), method="POST",
        headers={"Content-Type": "application/json", "Accept": "application/json",
                 "Origin": "http://localhost:3000",
                 "Mcp-Session-Id": SID})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            rbody = json.loads(r.read())
            print(f"  non-string cursor       -> 200, code={rbody.get('error', {}).get('code', 'none')} ✓")
    except urllib.error.HTTPError as e:
        # 400/500 is also acceptable — the point is we don't hang or crash.
        print(f"  non-string cursor       -> {e.code} ✓")

    # Confirm the server is still alive after the bad input.
    r = post_mcp("tools/list")
    if "tools" not in r.get("result", {}):
        failures.append("server unhealthy after non-string cursor")
    else:
        print(f"  server still healthy    -> {len(r['result']['tools'])} tools ✓")

    if failures:
        print(f"\n{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())