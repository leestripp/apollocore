"""Health-tab smoke test: verify apollo://stats + apollo_get_stats
shape after a multi-project KB is populated.

Ingests text under three project tags, then asserts:
  * global totals are returned (no project filter)
  * the projects[] array has one entry per project with sane counts
  * projects are created automatically when ingesting with a new name
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

# 2. KB is auto-opened at startup — no create/open step needed.
#    Projects are auto-registered when ingesting with a new name.
#    Ingest one doc per project tag.
for i, pname in enumerate(("alpha", "beta", "gamma"), start=2):
    _, ad = http_post_mcp(
        {"jsonrpc":"2.0","id":i,"method":"tools/call",
         "params":{"name":"apollo_add_document",
                   "arguments":{"text": LONG + " project=" + pname,
                                "doc_id": "doc-" + pname,
                                "project": pname}}},
        sid=sid)
    ad_text = ad.get("result",{}).get("content",[{}])[0].get("text","(no text)")
    print(f"  add_document({pname}) -> {ad_text[:60]} PASS")

time.sleep(1)

# 3. read apollo://stats
_, rs = http_post_mcp(
    {"jsonrpc":"2.0","id":100,"method":"resources/read",
     "params":{"uri":"apollo://stats"}}, sid=sid)
stats = json.loads(rs["result"]["contents"][0]["text"])
print("\n--- apollo://stats ---")
print(json.dumps(stats, indent=2))

# 4. assertions
assert "documents" in stats and "chunks" in stats
assert "projects" in stats and isinstance(stats["projects"], list), \
    "projects[] missing or wrong type"
assert stats["chunks"] >= 6, f"expected >=6 chunks, got {stats['chunks']}"
assert stats["documents"] >= 3, f"expected >=3 docs, got {stats['documents']}"

names = {p["name"] for p in stats["projects"]}
print("\nprojects in KB:", sorted(names))
# alpha/beta/gamma each got a doc -> each should have >=1 chunk
for pname in ("alpha","beta","gamma"):
    rec = next((p for p in stats["projects"] if p["name"] == pname), None)
    assert rec, f"missing project {pname} in breakdown"
    assert rec["chunks"] > 0, f"{pname} should have chunks, got {rec}"
    assert rec["documents"] >= 1, f"{pname} should have docs, got {rec}"

# 5. verify apollo_get_stats tool also has the new shape
_, gs = http_post_mcp(
    {"jsonrpc":"2.0","id":101,"method":"tools/call",
     "params":{"name":"apollo_get_stats","arguments":{}}}, sid=sid)
gstats = gs["result"].get("structuredContent") or json.loads(gs["result"]["content"][0]["text"])
assert "projects" in gstats, f"apollo_get_stats missing projects[]: {gstats}"
assert gstats["chunks"] == stats["chunks"]

# 6. global totals are invariant — there is no project filter, so
#    stats are always global. (apollo_set_project is gone.)
nonzero = [p for p in stats["projects"] if p["chunks"] > 0]
print(f"\nprojects with chunks: {sorted([p['name'] for p in nonzero])}")
assert len(nonzero) >= 3, f"expected >=3 projects with chunks, got {[p['name'] for p in nonzero]}"

print("\nALL OK")
print("  - apollo://stats and apollo_get_stats both expose global totals + projects[]")
print(f"  - global counts ({stats['chunks']} chunks, {stats['documents']} docs) are always global")
print(f"  - per-project breakdown: alpha={next(p['chunks'] for p in stats['projects'] if p['name']=='alpha')}, beta={next(p['chunks'] for p in stats['projects'] if p['name']=='beta')}, gamma={next(p['chunks'] for p in stats['projects'] if p['name']=='gamma')} chunks")
