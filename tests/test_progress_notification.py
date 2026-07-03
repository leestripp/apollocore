#!/usr/bin/env python3
"""Phase D4 — verify that calling a tool via POST /mcp emits
`notifications/progress` frames on the session's SSE channel.

Flow:
  1. Initialize session.
  2. Open GET /mcp SSE connection for the session.
  3. POST /mcp tools/call for that session (a fast tool).
  4. Read the SSE stream and confirm two progress notifications fire:
     one at progress=0 (start) and one at progress=1.0 / total=1.0 (finish).
"""
import json, time, socket, threading
import urllib.request

HOST, PORT = '127.0.0.1', 8765
URL = f'http://{HOST}:{PORT}'

def http(method, path, body=None, headers=None):
    headers = headers or {}
    if body is not None:
        headers.setdefault('Content-Type', 'application/json')
        body = body.encode() if isinstance(body, str) else body
    headers.setdefault('Accept', 'application/json')
    headers.setdefault('Origin', 'http://localhost:3000')
    req = urllib.request.Request(URL + path, data=body, method=method,
                                  headers=headers)
    with urllib.request.urlopen(req, timeout=15) as resp:
        return resp.status, dict(resp.headers), resp.read()

def must_alive():
    with urllib.request.urlopen(URL + '/health', timeout=3) as r:
        assert r.status == 200
    print("  alive ✓")

def parse_sse(raw):
    """Yield (event_name, data_string) tuples from a chunk of SSE bytes."""
    for block in raw.split(b'\r\n\r\n'):
        if not block.strip():
            continue
        name = ''
        data = []
        for line in block.split(b'\r\n'):
            if line.startswith(b'event: '):
                name = line[7:].decode()
            elif line.startswith(b'data: '):
                data.append(line[6:].decode())
        if data:
            yield name or 'message', ''.join(data)

def test_progress_on_tool_call():
    must_alive()
    # 1. Initialize
    _, h, _ = http('POST', '/mcp',
                   body=json.dumps({"jsonrpc": "2.0", "id": 1,
                                    "method": "initialize",
                                    "params": {"protocolVersion": "2025-11-25",
                                               "capabilities": {},
                                               "clientInfo":
                                               {"name": "t", "version": "1"}}}))
    sid = h.get('Mcp-Session-Id') or h.get('mcp-session-id')
    assert sid, f"no Mcp-Session-Id in {h}"
    print(f"  initialized SID={sid[:12]}...")

    # 2. Open SSE for that session.
    s = socket.create_connection((HOST, PORT), timeout=20)
    s.sendall((f"GET /mcp HTTP/1.1\r\n"
               f"Host: {HOST}:{PORT}\r\n"
               f"Accept: text/event-stream\r\n"
               f"Mcp-Session-Id: {sid}\r\n"
               f"MCP-Protocol-Version: 2025-11-25\r\n"
               f"Origin: http://localhost:3000\r\n"
               f"Connection: close\r\n\r\n").encode())

    # Read until end of HTTP headers (CRLF CRLF).
    buf = b''
    s.settimeout(5)
    while b'\r\n\r\n' not in buf:
        chunk = s.recv(4096)
        if not chunk: break
        buf += chunk
    assert buf.startswith(b'HTTP/1.1 200'), f"SSE handshake failed: {buf[:80]!r}"
    print("  SSE opened (200)")
    # Give the writer_loop time to subscribe to the bus. The on_upgrade
    # callback is called after the headers are sent; without a small
    # delay here, the POST can race the subscription and we lose the
    # progress frames.
    time.sleep(0.3)

    # 3. Trigger a tool call (use a real tools/call so we hit the
    #    handle_tools_call path which emits progress).
    status, _, body = http('POST', '/mcp',
         body=json.dumps({"jsonrpc": "2.0", "id": 99,
                          "method": "tools/call",
                          "params": {"name": "apollo_get_stats",
                                     "arguments": {}}}),
         headers={"Mcp-Session-Id": sid,
                  "MCP-Protocol-Version": "2025-11-25",
                  "Origin": "http://localhost:3000"})
    print(f"  tools/call apollo_get_stats → HTTP {status}")

    # 4. Read frames for up to 8 seconds, collecting progress notifications.
    deadline = time.time() + 8
    progress_frames = []
    while time.time() < deadline and len(progress_frames) < 2:
        try:
            s.settimeout(2)
            chunk = s.recv(4096)
            if not chunk:
                break
            for ev_name, data in parse_sse(chunk):
                try:
                    msg = json.loads(data)
                except json.JSONDecodeError:
                    continue
                if msg.get('method') == 'notifications/progress':
                    progress_frames.append(msg)
                    print(f"  ← progress {msg['params']}")
        except socket.timeout:
            continue
    try:
        s.close()
    except Exception:
        pass

    assert len(progress_frames) >= 2, \
        f"expected ≥2 progress frames, got {len(progress_frames)}: {progress_frames}"
    start = progress_frames[0]
    finish = progress_frames[-1]
    assert start['params']['progress'] == 0.0, start
    assert finish['params']['progress'] == 1.0, finish
    assert finish['params'].get('total') == 1.0, finish
    print(f"  D4 progress: start={start['params']}, finish={finish['params']} ✓")
    print("\nALL OK")

if __name__ == '__main__':
    print("Running Phase D4 notifications/progress test:\n")
    test_progress_on_tool_call()