"""
test_stdio_conformance.py -- Phase C conformance smoke test.

Spawns apollo_mcp_server in --transport stdio mode. Per spec:
  * One JSON-RPC envelope per line.
  * Server reads from stdin, writes responses to stdout.
  * Server MUST log to stderr only (we assert no JSON-RPC on stderr).

Strategy: send all 4 messages via communicate(), read all of stdout
at once after the server exits. Avoids threading/buffering issues.
"""
import json, os, subprocess, sys
from pathlib import Path

BIN  = os.environ.get(
    "APOLLO_BIN",
    str(Path(__file__).resolve().parent.parent
        / "build" / "src" / "mcp_server" / "apollo_mcp_server"))
DATA = "./data"


def req(method, id_=None, params=None):
    obj = {"jsonrpc": "2.0", "method": method}
    if id_ is not None:
        obj["id"] = id_
    if params:
        obj["params"] = params
    return json.dumps(obj)


def main():
    if not os.path.isfile(BIN):
        sys.exit(f"binary not found: {BIN}")

    # 4 messages: initialize (id=1), notifications/initialized (no id),
    #             tools/list (id=2), ping (id=3).
    msgs = [
        req("initialize", 1, {"protocolVersion": "2025-11-25",
                               "capabilities": {},
                               "clientInfo": {"name": "test", "version": "1"}}),
        req("notifications/initialized"),           # notification, no id
        req("tools/list", 2),
        req("ping", 3),
    ]
    stdin_data = "\n".join(msgs + [""]).encode()

    proc = subprocess.Popen(
        [BIN, "--transport", "stdio", "--no-ui", "--data-dir", DATA],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = proc.communicate(input=stdin_data, timeout=90)
    except subprocess.TimeoutExpired:
        proc.kill()
        sys.exit("FAIL: server did not respond within 90s")

    assert proc.returncode == 0, f"server rc={proc.returncode}"

    # Parse all stdout lines; split JSON vs. non-JSON.
    all_lines = stdout.decode(errors="replace").splitlines()
    json_lines = [ln for ln in all_lines if ln.lstrip().startswith("{")]

    # notifications/initialized has no id → no response → it is NOT in json_lines.
    # Only 3 responses expected: initialize, tools/list, ping.
    assert len(json_lines) == 3, \
        f"expected 3 JSON lines (init, tools/list, ping), got {len(json_lines)}: {json_lines}"

    # Validate initialize.
    r = json.loads(json_lines[0])
    assert r.get("id") == 1 and "result" in r, f"bad init: {r}"
    print(f"  initialize       -> serverInfo={r['result']['serverInfo']['name']} "
          f"v{r['result']['serverInfo']['version']}, "
          f"protocol={r['result']['protocolVersion']} -- PASS")

    # Validate tools/list.
    r = json.loads(json_lines[1])
    assert r.get("id") == 2 and "result" in r, f"bad tools/list: {r}"
    tools = r["result"]["tools"]
    assert len(tools) >= 12, f"expected >=12 tools, got {len(tools)}"
    print(f"  tools/list       -> {len(tools)} tools -- PASS")

    # Validate ping.
    r = json.loads(json_lines[2])
    assert r.get("id") == 3 and r.get("result") == {}, f"bad ping: {r}"
    print("  ping             -> {} -- PASS")

    # Verify no JSON on stderr.
    stderr_text = stderr.decode(errors="replace")
    leaked = [ln for ln in stderr_text.splitlines()
              if ln.lstrip().startswith("{")]
    assert not leaked, f"JSON-RPC leaked to stderr: {leaked[:2]}"
    print(f"  stderr clean     -> {len(stderr_text.splitlines())} log lines, "
          f"no JSON leak -- PASS")
    print("\nALL OK")


if __name__ == "__main__":
    main()
