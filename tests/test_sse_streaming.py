"""
test_sse_streaming.py — Phase B1/B2 conformance smoke test.

Verifies the GET /mcp SSE channel:
  * Sends the initial `: connected` comment
  * Sends `: keepalive\r\n\r\n` comments every ~15s

We use raw sockets (urllib respects Content-Length and stops reading
after the initial body). The test holds the connection for ~16s and
asserts at least one keep-alive arrives.

Runs against an already-running apollo_mcp_server on $APOLLO_PORT (default
8765).
"""

import os
import socket
import json
import time
import urllib.request

PORT = int(os.environ.get("APOLLO_PORT", "8765"))
HOST = "127.0.0.1"


def must_alive():
    with urllib.request.urlopen(f"http://{HOST}:{PORT}/health", timeout=3) as r:
        assert r.status == 200


def initialize() -> str:
    req = urllib.request.Request(
        f"http://{HOST}:{PORT}/mcp",
        data=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                         "params": {"protocolVersion": "2025-11-25",
                                    "capabilities": {},
                                    "clientInfo": {"name": "t", "version": "1"}}}
                        ).encode(),
        headers={"Content-Type": "application/json",
                 "Accept": "application/json",
                 "Origin": "http://localhost:3000"})
    with urllib.request.urlopen(req, timeout=10) as r:
        sid = r.headers.get("Mcp-Session-Id")
        assert sid, "no SID echoed"
        return sid


def test_keepalive(sid: str):
    """Hold the SSE connection for ~18s and confirm we get at least one
    keep-alive comment (server keep-alive interval is 15s).

    Server should also send a `: connected` comment in the response body
    right after the headers.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    sock.sendall(
        f"GET /mcp HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Accept: text/event-stream\r\n"
        f"Mcp-Session-Id: {sid}\r\n"
        f"Origin: http://localhost:3000\r\n"
        f"Connection: keep-alive\r\n"
        f"\r\n".encode())
    sock.settimeout(20)
    buf = b""
    saw_connected = False
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
        if b": connected" in buf and not saw_connected:
            saw_connected = True
            print(f"  SSE initial ': connected' -> received ✓")
        if b"keepalive" in buf:
            sock.close()
            print(f"  SSE keep-alive       -> received (total {len(buf)} bytes) ✓")
            assert saw_connected, "keep-alive before ': connected'?"
            return
    sock.close()
    raise AssertionError(
        f"no keep-alive in 20s. saw_connected={saw_connected}, "
        f"buf[:300]={buf[:300]!r}")


def main():
    must_alive()
    sid = initialize()
    print(f"  initialized SID={sid[:8]}...")
    test_keepalive(sid)
    print("\nALL OK")


if __name__ == "__main__":
    main()