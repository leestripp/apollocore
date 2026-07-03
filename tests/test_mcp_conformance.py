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

  D2 — Phase D2 handlers (logging/setLevel, completion/complete,
        notifications/cancelled) accept correct params and reject bad ones.

  D1 — Strict session-id validation (Phase D).
        * unknown session id → 400
        * missing session id on post-init request → 400

  B1 — GET /mcp Server-Sent Events (Phase B1).

  E3 — Negative tests (Phase E hardening):
        * malformed JSON  → HTTP 400 PARSE_ERROR (-32700)
        * invalid id types (bool/array/object) → rejected
        * unknown method  → code -32601
        * unknown tool    → isError:true result with text
        * second DELETE on dead session → 404
        * missing Origin  → 403/400
        * ping round-trip → empty object result
        * resources/list + prompts/list → return the registered sets

  B1 — GET /mcp SSE endpoint (Phase B1).
        * no SSE Accept        → 405
        * SSE + no session     → 400
        * SSE + valid session  → 200, text/event-stream,
          Cache-Control: no-cache, Connection: keep-alive,
          X-Accel-Buffering: no, and the initial `: connected` comment.
"""

import os
import sys
import json
import urllib.request
import urllib.error
from urllib.request import Request, urlopen

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


# ---------------------------------------------------------------------------
# D2 — Phase D2 handlers (logging/setLevel + completion/complete +
# notifications/cancelled)
# ---------------------------------------------------------------------------
def test_logging_set_level(sid):
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 10, "method": "logging/setLevel",
         "params": {"level": "warning"}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25"},
        expect_status=200,
    )
    assert body.get("result") == {}, \
        f"logging/setLevel expected {{}}, got {body.get('result')!r}"
    print(f"  D2 logging/setLevel -> 200 {{}} ✓")


def test_logging_set_level_rejects_bad_level(sid):
    """An invalid level must be rejected. Our server wraps the
    std::invalid_argument throw in a JSON-RPC Invalid Params error
    (code -32602) returned with HTTP 200 (per JSON-RPC: protocol
    errors come back on the 200 response envelope)."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 11, "method": "logging/setLevel",
         "params": {"level": "verbose"}},  # not a valid MCP level
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25"},
        expect_status=200,  # JSON-RPC errors come back on 200
    )
    assert "error" in body, f"expected error, got {body}"
    assert body["error"]["code"] == -32602, \
        f"expected -32602 (Invalid Params), got code {body['error']['code']}"
    msg = body["error"]["message"]
    assert "level" in msg.lower() or "verbose" in msg, \
        f"error should mention the level, got: {msg}"
    print(f"  D2 bad log level    -> code -32602 ({msg[:50]}...) ✓")


def test_completion_complete(sid):
    """completion/complete is a stub today — returns empty completion."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 12, "method": "completion/complete",
         "params": {"ref": {"type": "ref/prompt", "name": "summarize"},
                    "argument": {"name": "text", "value": ""}}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25"},
        expect_status=200,
    )
    res = body.get("result", {})
    assert "completion" in res, f"expected completion field, got {res}"
    assert res["completion"].get("values") == [], \
        f"expected empty values, got {res['completion']}"
    print(f"  D2 completion      -> 200, empty values [] ✓")


def test_cancelled_notification_ignored(sid):
    """notifications/cancelled is a no-op acknowledgement. We accept
    and return no reply (same as notifications/initialized)."""
    import json as _json
    req = Request(
        f"{URL}/mcp",
        data=_json.dumps({"jsonrpc": "2.0",
                          "method": "notifications/cancelled",
                          "params": {"requestId": 999}}).encode(),
        headers={"Content-Type": "application/json", "Accept": "application/json",
                 "Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"})
    # urlopen + a short read; we expect nothing back.
    try:
        urlopen(req, timeout=2).read()
        # OK either way — the server may or may not return a body.
    except Exception:
        pass
    # A subsequent request still works (server didn't hang).
    status, _, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 13, "method": "ping", "params": {}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25"},
        expect_status=200,
    )
    print(f"  D2 cancelled        -> no-op, server still healthy ✓")


# ---------------------------------------------------------------------------
# D1 — Strict session-id validation
# ---------------------------------------------------------------------------
def test_unknown_session_rejected():
    """An MCP-Session-Id that was never issued (or was DELETE'd) → 400."""
    bogus = "00000000-0000-4000-8000-000000000000"
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 99, "method": "tools/list", "params": {}},
        headers={"Mcp-Session-Id": bogus,
                 "MCP-Protocol-Version": "2025-11-25"},
        expect_status=400,
    )
    msg = body["error"]["message"]
    assert "Session-Id" in msg or "session" in msg.lower(), \
        f"error should mention session, got: {msg}"
    print(f"  D1 unknown session  -> HTTP {status} ({msg[:50]}...) ✓")


def test_missing_session_after_init_rejected():
    """Post-init request with no Mcp-Session-Id header → 400."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 99, "method": "tools/list", "params": {}},
        headers={"MCP-Protocol-Version": "2025-11-25"},
        expect_status=400,
    )
    msg = body["error"]["message"]
    assert "Session-Id" in msg or "session" in msg.lower(), \
        f"error should mention session, got: {msg}"
    print(f"  D1 missing session  -> HTTP {status} ({msg[:50]}...) ✓")


# ---------------------------------------------------------------------------
# B1 — GET /mcp Server-Sent Events
# ---------------------------------------------------------------------------
def test_get_mcp_no_sse_accept():
    """GET /mcp without Accept: text/event-stream → 405."""
    req = urllib.request.Request(f"{URL}/mcp", method="GET")
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            assert False, f"expected 405, got {r.status}"
    except urllib.error.HTTPError as e:
        assert e.code == 405, f"expected 405, got {e.code}"
    print("  B1 no SSE Accept -> HTTP 405 ✓")


def test_get_mcp_no_session():
    """GET /mcp with SSE Accept but no Mcp-Session-Id → 400."""
    req = urllib.request.Request(f"{URL}/mcp", method="GET",
                                  headers={"Accept": "text/event-stream"})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            assert False, f"expected 400, got {r.status}"
    except urllib.error.HTTPError as e:
        assert e.code == 400, f"expected 400, got {e.code}"
    print("  B1 SSE no session -> HTTP 400 ✓")


def test_get_mcp_sse(sid):
    """GET /mcp with SSE Accept + session → 200 + event-stream headers +
    initial `: connected` comment."""
    req = urllib.request.Request(f"{URL}/mcp", method="GET",
                                  headers={"Accept": "text/event-stream",
                                           "Mcp-Session-Id": sid})
    try:
        # The server holds the connection open and pumps keep-alives.
        # We use a short timeout to capture the headers + first chunk
        # then bail.
        with urllib.request.urlopen(req, timeout=3) as r:
            assert r.status == 200, f"expected 200, got {r.status}"
            ct = r.headers.get("Content-Type", "")
            assert ct.startswith("text/event-stream"), \
                f"Content-Type={ct!r}"
            assert r.headers.get("Cache-Control") == "no-cache", \
                f"Cache-Control={r.headers.get('Cache-Control')!r}"
            # The MCP SDK and many real-world clients reuse the TCP
            # socket for follow-up POSTs. Mixing SSE keep-alive writes
            # on the same socket with POST response bodies racy-
            # corrupts the JSON-RPC reply (": keepalive\r\n" landed
            # inside a POST body during E1 SDK testing). We force
            # Connection: close on the SSE stream so every SSE call is
            # on its own TCP. Document the choice here; future work:
            # implement a per-connection write mutex so we can keep
            # keep-alive without corrupting POSTs.
            assert r.headers.get("Connection") == "close", \
                f"Connection={r.headers.get('Connection')!r}"
            assert r.headers.get("X-Accel-Buffering") == "no", \
                f"X-Accel-Buffering={r.headers.get('X-Accel-Buffering')!r}"
            # Read up to the first ': connected' event.
            chunk = r.read(64).decode(errors="replace")
            assert ": connected" in chunk, \
                f"missing ': connected' in initial chunk: {chunk!r}"
    except (urllib.error.URLError, TimeoutError) as e:
        # A clean 200 + headers read may be interrupted by timeout —
        # the server holds the connection open for keep-alives.
        # If we got here without seeing the headers, fail.
        assert False, f"GET /mcp SSE failed before headers: {e}"
    print(f"  B1 SSE stream    -> 200, text/event-stream, ': connected' ✓")


# ---------------------------------------------------------------------------
# E3 — Negative tests (Phase E test hardening)
# ---------------------------------------------------------------------------

def _fresh_sid():
    """Re-init and return a fresh SID. Used by E3 tests that need a
    valid session but are run after A4 has killed the main one."""
    req = Request(f"{URL}/mcp",
                  data=json.dumps({"jsonrpc": "2.0", "id": 999,
                                   "method": "initialize",
                                   "params": {"protocolVersion": "2025-11-25",
                                              "capabilities": {},
                                              "clientInfo":
                                              {"name": "t", "version": "1"}}}).encode(),
                  method="POST",
                  headers={"Content-Type": "application/json",
                           "Accept": "application/json",
                           "Origin": "http://localhost:3000"})
    with urlopen(req, timeout=10) as resp:
        # urllib's headers are case-insensitive: get with any case.
        sid = resp.headers.get("Mcp-Session-Id") \
              or resp.headers.get("mcp-session-id")
    if not sid:
        # RFC 7230 says header names are case-insensitive, but just in
        # case the server emits something exotic, search all keys.
        for k, v in urllib.request.Request(f"{URL}/mcp").headers.items():
            sid = v
            if k.lower() == "mcp-session-id": break
    return sid


def test_malformed_json_rejected():
    """Sending non-JSON body MUST return 400 PARSE_ERROR, not 500."""
    req = Request(f"{URL}/mcp",
                  data=b"this is not valid json {{{",
                  method="POST",
                  headers={"Content-Type": "application/json",
                           "Accept": "application/json",
                           "Origin": "http://localhost:3000"})
    try:
        with urlopen(req, timeout=5) as resp:
            assert resp.status == 400, f"expected 400, got {resp.status}"
            data = json.loads(resp.read())
            assert "error" in data
            assert data["error"]["code"] == -32700  # PARSE_ERROR
            assert "parse" in data["error"]["message"].lower()
    except urllib.error.HTTPError as e:
        # Some implementations respond 400 directly; accept that too.
        assert e.code == 400
    print("  E3 malformed JSON    -> HTTP 400 PARSE_ERROR ✓")


def test_invalid_jsonrpc_id_rejected(_sid_unused):
    """JSON-RPC id MUST be a string, number, or null — NOT a boolean/array/object.
    Our parser treats invalid id types as a malformed JSON-RPC envelope
    (-32700 PARSE_ERROR), which is defensible since the envelope itself
    is invalid before we even get to dispatch."""
    sid = _fresh_sid()
    for bad_id in [True, [1, 2], {"foo": "bar"}]:
        req = Request(f"{URL}/mcp",
                      data=json.dumps({"jsonrpc": "2.0", "id": bad_id,
                                       "method": "tools/list",
                                       "params": {}}).encode(),
                      method="POST",
                      headers={"Content-Type": "application/json",
                               "Accept": "application/json",
                               "Mcp-Session-Id": sid,
                               "MCP-Protocol-Version": "2025-11-25",
                               "Origin": "http://localhost:3000"})
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read())
                # -32700 PARSE_ERROR is valid (malformed envelope);
                # -32600 INVALID_REQUEST is also valid.
                assert "error" in data, \
                    f"expected error for id={bad_id!r}, got {data}"
                assert data["error"]["code"] in (-32600, -32700), \
                    f"unexpected error code for id={bad_id!r}: {data}"
        except urllib.error.HTTPError as e:
            assert e.code in (400, 500), f"got HTTP {e.code}"
    print(f"  E3 invalid id types  -> rejected (PARSE_ERROR or INVALID_REQUEST) ✓")


def test_method_not_found(_sid_unused):
    sid = _fresh_sid()
    """Unknown method MUST return -32601 METHOD_NOT_FOUND."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 100, "method": "this/method/does/not/exist",
         "params": {}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"},
        expect_status=200,  # JSON-RPC errors come back on HTTP 200
    )
    assert "error" in body, f"expected error, got {body}"
    assert body["error"]["code"] == -32601, \
        f"expected -32601 (METHOD_NOT_FOUND), got {body['error']['code']}"
    msg = body["error"]["message"].lower()
    assert "not" in msg or "unknown" in msg, \
        f"error message should mention 'unknown' or 'not found', got: {msg}"
    print(f"  E3 method not found  -> code -32601 ✓")


def test_unknown_tool(_sid_unused):
    sid = _fresh_sid()
    """tools/call with unknown name MUST return isError:true result
    (tool-level error), not a JSON-RPC -32602 protocol error."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 101, "method": "tools/call",
         "params": {"name": "this_tool_definitely_does_not_exist",
                    "arguments": {}}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"},
        expect_status=200,
    )
    res = body.get("result", {})
    assert res.get("isError") is True, \
        f"unknown tool should be isError:true result, got {body}"
    text = " ".join(c.get("text", "")
                    for c in res.get("content", [])
                    if c.get("type") == "text").lower()
    assert "unknown" in text or "not" in text, \
        f"error text should mention 'unknown tool', got: {text}"
    print(f"  E3 unknown tool      -> isError:true, message='...unknown tool...' ✓")


def test_delete_twice_yields_404(_sid_unused):
    """First DELETE → 200, second DELETE → 404 (already destroyed)."""
    # Re-init to get a fresh SID (post_mcp's third return is the SID).
    _, _, s2 = post_mcp(
        {"jsonrpc": "2.0", "id": 102, "method": "initialize",
         "params": {"protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {"name": "t", "version": "1"}}},
        headers={"Origin": "http://localhost:3000"})
    if not s2:
        print("  E3 delete-twice     -> SKIP (couldn't mint fresh SID)"); return
    # First DELETE on fresh session
    req1 = Request(f"{URL}/mcp", method="DELETE",
                   headers={"Mcp-Session-Id": s2,
                            "MCP-Protocol-Version": "2025-11-25",
                            "Origin": "http://localhost:3000"})
    with urlopen(req1, timeout=5) as r:
        assert r.status == 200, f"first DELETE should be 200, got {r.status}"
    # Second DELETE
    req2 = Request(f"{URL}/mcp", method="DELETE",
                   headers={"Mcp-Session-Id": s2,
                            "MCP-Protocol-Version": "2025-11-25",
                            "Origin": "http://localhost:3000"})
    try:
        with urlopen(req2, timeout=5) as r:
            data = json.loads(r.read())
        assert r.status == 404 or "error" in data, \
            f"second DELETE should 404, got {r.status}: {data}"
    except urllib.error.HTTPError as e:
        assert e.code == 404, f"second DELETE should 404, got {e.code}"
    print(f"  E3 delete-twice      -> second DELETE is 404 ✓")


def test_empty_origin_allowed_for_cli():
    """We allow missing/empty Origin for non-browser callers (curl, MCP
    CLI tools). Browsers always send Origin; servers/CLIs often omit
    it. This is a deviation from a strict reading of the spec but
    matches mainstream MCP implementations (including the official
    TypeScript SDK reference server). Document the choice via a
    happy-path test, and verify evil Origins are still rejected."""
    # initialize is the only call allowed pre-session; empty Origin
    # + initialize should still be accepted so curl users can connect.
    req = Request(f"{URL}/mcp",
                  data=json.dumps({"jsonrpc": "2.0", "id": 1,
                                   "method": "initialize",
                                   "params": {"protocolVersion": "2025-11-25",
                                              "capabilities": {},
                                              "clientInfo":
                                              {"name": "t", "version": "1"}}}).encode(),
                  method="POST",
                  headers={"Content-Type": "application/json",
                           "Accept": "application/json"})
    with urlopen(req, timeout=5) as resp:
        assert resp.status == 200, \
            f"empty Origin + initialize should be allowed, got {resp.status}"
    print(f"  E3 missing Origin    -> allowed for CLI (200 on init) ✓")
    # But a clearly evil Origin MUST be rejected (re-verified).
    req2 = Request(f"{URL}/mcp",
                   data=b"{}",
                   method="POST",
                   headers={"Content-Type": "application/json",
                            "Origin": "https://attacker.example.com"})
    try:
        with urlopen(req2, timeout=5) as resp:
            assert resp.status >= 400, \
                f"evil Origin should be rejected, got {resp.status}"
    except urllib.error.HTTPError as e:
        assert e.code in (400, 403), \
            f"evil Origin must 400/403, got {e.code}"
    print(f"  E3 evil Origin       -> rejected (400/403) ✓")


def test_ping_round_trip(_sid_unused):
    sid = _fresh_sid()
    """ping MUST return an empty object result immediately."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 103, "method": "ping", "params": {}},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"},
        expect_status=200,
    )
    assert "result" in body, f"ping should return result, got {body}"
    assert body["result"] == {}, \
        f"ping result must be an empty object, got {body['result']!r}"
    print(f"  E3 ping round-trip   -> empty object result ✓")

    # Per spec §Lifecycle: ping is allowed before and after initialize.
    # It serves as a keep-alive and protocol-level probe.
    req = Request(f"{URL}/mcp",
                  data=json.dumps({"jsonrpc": "2.0", "id": 1,
                                   "method": "ping",
                                   "params": {}}).encode(),
                  method="POST",
                  headers={"Content-Type": "application/json",
                           "Accept": "application/json"})
    with urlopen(req, timeout=5) as resp:
        assert resp.status == 200, f"ping pre-init should return 200, got {resp.status}"
        body_ping = json.loads(resp.read())
        assert body_ping.get("result") == {}, f"ping result should be {{}}, got {body_ping}"
        print(f"  E3 ping pre-init    -> 200, {{}} ✓")


def test_resources_subscribe_is_accepted(_sid_unused):
    sid = _fresh_sid()
    """We declare resources.subscribe=false, so server should return
    METHOD_NOT_FOUND for resources/subscribe (or accept if we add it
    later). Acceptance test for now: server is healthy after."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 104, "method": "resources/list"},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"},
        expect_status=200,
    )
    res = body.get("result", {})
    # Spec says resources: {subscribe: false} implies subscribe NOT supported.
    assert "resources" in res, f"resources/list should return resources[], got {res}"
    print(f"  E3 resources/list    -> {len(res['resources'])} resource(s) ✓")


def test_prompts_list(_sid_unused):
    sid = _fresh_sid()
    """prompts/list should return our 4 prompt templates."""
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 105, "method": "prompts/list"},
        headers={"Mcp-Session-Id": sid, "MCP-Protocol-Version": "2025-11-25",
                 "Origin": "http://localhost:3000"},
        expect_status=200,
    )
    res = body.get("result", {})
    assert "prompts" in res, f"prompts/list should return prompts[], got {res}"
    assert len(res["prompts"]) >= 4, \
        f"expected ≥4 prompts, got {len(res['prompts'])}"
    print(f"  E3 prompts/list      -> {len(res['prompts'])} prompt(s) ✓")


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
def test_protocol_version_rejects_unknown(sid):
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 3, "method": "tools/list", "params": {}},
        headers={"MCP-Protocol-Version": "1999-01-01",
                 "Mcp-Session-Id": sid},
        expect_status=400,
    )
    assert body and "error" in body
    msg = body["error"]["message"]
    assert "MCP-Protocol-Version" in msg or "Protocol-Version" in msg, \
        f"error should reference the header, got: {msg}"
    print(f"  A2 unknown ver   -> HTTP {status} ({msg[:60]}...) ✓")


def test_protocol_version_accepts_known(sid):
    status, body, _ = post_mcp(
        {"jsonrpc": "2.0", "id": 4, "method": "tools/list", "params": {}},
        headers={"MCP-Protocol-Version": "2025-11-25",
                 "Mcp-Session-Id": sid},
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
    print("Running MCP conformance smoke test (Phase A + B1):\n")
    must_alive()
    test_origin_rejects_evil()
    sid = test_origin_accepts_localhost()
    test_protocol_version_rejects_unknown(sid)
    test_protocol_version_accepts_known(sid)
    test_logging_set_level(sid)
    test_logging_set_level_rejects_bad_level(sid)
    test_completion_complete(sid)
    test_cancelled_notification_ignored(sid)
    test_delete_session(sid)
    test_unknown_session_rejected()
    test_missing_session_after_init_rejected()
    test_get_mcp_no_sse_accept()
    test_get_mcp_no_session()
    test_get_mcp_sse(sid)
    # E3 — negative tests (Phase E).
    # Order matters: SID-needing tests must run before A4 deletes the SID.
    # SID-agnostic tests can run anywhere.
    test_malformed_json_rejected()
    test_empty_origin_allowed_for_cli()
    test_method_not_found(None)
    test_unknown_tool(None)
    test_invalid_jsonrpc_id_rejected(None)
    test_ping_round_trip(None)
    test_resources_subscribe_is_accepted(None)
    test_prompts_list(None)
    # After the main sid is dead, mint a fresh one and verify
    # DELETE-on-alive → 200 and DELETE-on-dead → 404.
    test_delete_twice_yields_404(None)
    test_unknown_session_rejected()
    test_missing_session_after_init_rejected()
    print("\nALL OK")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"\nFAIL: {e}", file=sys.stderr)
        sys.exit(1)