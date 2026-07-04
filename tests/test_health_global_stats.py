"""Health-tab smoke test: verify the new apollo://stats + apollo_get_stats
shape after a multi-project KB is populated.

Spins up a 3-project KB via MCP, ingests text under each, then asserts:
  * global totals are returned (no longer filtered by current_project_)
  * the projects[] array has one entry per project with sane counts
"""
import json, os, sys, time, urllib.request

PORT = int(os.environ.get("APOLLO_PORT", "8767"))
HOST = "127.0.0.1"
URL  = f"http://{HOST}:{PORT}"

LONG = ("ApolloCore is a C++23 MCP server. " * 30)  # > 512 chars => multiple chunks

def http_post_mcp(body, sid=None, timeout=60):
    h = {"Content-Type":"application/json",
         "Accept":"application/json, text/event-stream",
         "Origin": f"http://{HOST}"}
    if sid: h["Mcp-Session-Id"] = sid
    req = urllib.request.Request(
        URL + "/mcp", method="POST",
        data=json.dumps(body).encode(),
        headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.headers.get("Mcp-Session-Id"), json.loads(resp.read())

def http_get(path, timeout=10):
    req = urllib.request.Request(URL + path, method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())

# Wait for server to be ready
for _ in range(60):
    try:
        http_get("/health")
        break
    except Exception:
        time.sleep(1)
else:
    sys.exit("server never came up")

# 1. initialize
sid, init = http_post_mcp(
    {"jsonrpc":"2.0","id":1,"method":"initialize",
     "params":{"protocolVersion":"2025-11-25","capabilities":{},
               "clientInfo":{"name":"health-smoke","version":"1"}}})
assert sid, "no session id"
print(f"  init             -> sid={sid[:8]}... PASS")

# 2. create the KB
_, c = http_post_mcp(
    {"jsonrpc":"2.0","id":2,"method":"tools/call",
     "params":{"name":"apollo_create_kb",
               "arguments":{"path":"/tmp/health_smoke/kb","name":"smoke"}}},
    sid=sid)
ctext = c["result"]["content"][0]["text"]
print(f"  create_kb        -> {ctext[:80]} PASS")

# 2b. Register the projects directly via SQLite. There is no
#     apollo_register_project tool exposed via MCP — project registration
#     is normally an internal/server-side op. So we INSERT the rows into
#     the KB's projects table directly to set up the smoke test.
import sqlite3
con = sqlite3.connect("/tmp/health_smoke/kb/apollo.db")
for pname in ("alpha", "beta", "gamma"):
    con.execute(
        "INSERT OR REPLACE INTO projects "
        "(id, name, description, path, is_shared, owner, metadata) "
        "VALUES (?, ?, ?, '', 0, 'smoke', '{}');",
        (pname, pname, f"smoke project {pname}"))
con.commit()
con.close()
print("  register 3 projects via SQLite (no apollo_register_project tool)")

# 3. switch + ingest a doc into each
for pname in ("alpha", "beta", "gamma"):
    _, sp = http_post_mcp(
        {"jsonrpc":"2.0","id":3,"method":"tools/call",
         "params":{"name":"apollo_set_project","arguments":{"project":pname}}},
        sid=sid)
    st = sp.get("result",{}).get("content",[{}])[0].get("text","")
    print(f"  set_project      -> {pname} {st[:60]}")

    _, ad = http_post_mcp(
        {"jsonrpc":"2.0","id":4,"method":"tools/call",
         "params":{"name":"apollo_add_document",
                   "arguments":{"text": LONG + " project=" + pname,
                                "doc_id": "doc-" + pname}}},
        sid=sid)
    ad_text = ad["result"]["content"][0]["text"]
    print(f"  add_document     -> {pname} {ad_text}")

time.sleep(1)

# 4. read apollo://stats
_, rs = http_post_mcp(
    {"jsonrpc":"2.0","id":100,"method":"resources/read",
     "params":{"uri":"apollo://stats"}}, sid=sid)
stats = json.loads(rs["result"]["contents"][0]["text"])
print("\n--- apollo://stats ---")
print(json.dumps(stats, indent=2))

# 5. assertions
assert "documents" in stats and "chunks" in stats
assert "projects" in stats and isinstance(stats["projects"], list), \
    "projects[] missing or wrong type"
assert stats["chunks"] >= 6, f"expected >=6 chunks, got {stats['chunks']}"
assert stats["documents"] >= 3, f"expected >=3 docs, got {stats['documents']}"

names = {p["name"] for p in stats["projects"]}
print("\nprojects in KB:", sorted(names))
# 'default' should NOT have new chunks (we never wrote to it)
# alpha/beta/gamma each got a doc -> each should have >=1 chunk
for pname in ("alpha","beta","gamma"):
    rec = next((p for p in stats["projects"] if p["name"] == pname), None)
    assert rec, f"missing project {pname} in breakdown"
    assert rec["chunks"] > 0, f"{pname} should have chunks, got {rec}"
    assert rec["documents"] >= 1, f"{pname} should have docs, got {rec}"

# 6. verify apollo_get_stats tool also has the new shape
_, gs = http_post_mcp(
    {"jsonrpc":"2.0","id":101,"method":"tools/call",
     "params":{"name":"apollo_get_stats","arguments":{}}}, sid=sid)
gstats = gs["result"].get("structuredContent") or json.loads(gs["result"]["content"][0]["text"])
assert "projects" in gstats, f"apollo_get_stats missing projects[]: {gstats}"
assert gstats["chunks"] == stats["chunks"]

# 7. CRITICAL: verify the OLD filtered behaviour is gone.
#    Switching current_project used to shrink the stats — it was a bug.
#    Now totals should be invariant to set_current_project (per the user's
#    model: projects are tags, not isolation).
_, sp = http_post_mcp(
    {"jsonrpc":"2.0","id":102,"method":"tools/call",
     "params":{"name":"apollo_set_project","arguments":{"project":"default"}}},
    sid=sid)
_, gs2 = http_post_mcp(
    {"jsonrpc":"2.0","id":103,"method":"tools/call",
     "params":{"name":"apollo_get_stats","arguments":{}}}, sid=sid)
g2 = gs2["result"].get("structuredContent") or json.loads(gs2["result"]["content"][0]["text"])
assert g2["chunks"] == stats["chunks"], \
    f"REGRESSION: switching to 'default' changed chunks from {stats['chunks']} to {g2['chunks']} — stats are still filtered!"
assert g2["documents"] == stats["documents"], \
    f"REGRESSION: docs filtered after project switch: {stats['documents']} -> {g2['documents']}"

# 8. Each non-empty project should have at least one chunk > 0.
nonzero = [p for p in stats["projects"] if p["chunks"] > 0]
print(f"\nprojects with chunks: {sorted([p['name'] for p in nonzero])}")
assert len(nonzero) >= 3, f"expected >=3 projects with chunks, got {[p['name'] for p in nonzero]}"

print("\nALL OK")
print("  - apollo://stats and apollo_get_stats both expose global totals + projects[]")
print(f"  - global counts ({stats['chunks']} chunks, {stats['documents']} docs) DO NOT shrink on set_project")
print(f"  - per-project breakdown: alpha={stats['projects'][0]['chunks']}, beta={stats['projects'][1]['chunks']}, gamma={stats['projects'][3]['chunks']} chunks")