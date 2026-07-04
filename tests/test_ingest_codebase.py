"""
test_ingest_codebase.py — regression test for the crash fixed by
the chunker sub-split + embedder truncation patch. Exercises
ingest_codebase against a synthetic corpus with:

  - small file (well under chunk_size)
  - "no-dot.txt" — 5000 chars of 'x' with NO sentence terminator,
    which is the worst case for the old sentence-split chunker
  - "big.md" — ~3 MB of normal text, realistic code-doc load

Asserts:
  - The call returns without the server dying
  - total_chunks > 0
  - files_processed ≥ 3
  - no errors
  - no-dot.txt made it into doc_ids (proves the sub-split worked)

If any of these fail, the server likely crashed.
"""
import os
import sys
import time
import json
import shutil
import tempfile
import urllib.request
import urllib.error

PORT = int(os.environ.get("APOLLO_PORT", "8765"))
URL = f"http://127.0.0.1:{PORT}"


def post_mcp(method, params, id_=1):
    body = json.dumps({"jsonrpc": "2.0", "id": id_, "method": method, "params": params}).encode()
    req = urllib.request.Request(
        f"{URL}/mcp",
        data=body,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read())


def tools_call(name, arguments):
    r = post_mcp("tools/call", {"name": name, "arguments": arguments})
    if "error" in r:
        raise RuntimeError(f"MCP error: {r['error']}")
    res = r["result"]
    if res.get("isError"):
        raise RuntimeError(f"Tool {name} returned error: {res['content']}")
    return res["content"][0]["text"]


def must_alive():
    try:
        with urllib.request.urlopen(f"{URL}/health", timeout=5) as resp:
            body = resp.read().decode()
        assert '"status":"ok"' in body, f"unhealthy: {body}"
    except (urllib.error.URLError, AssertionError) as e:
        raise AssertionError(f"server is NOT running ({e})")


def main():
    src = tempfile.mkdtemp(prefix="ingest-test-")
    try:
        # Small file (well under chunk_size)
        with open(os.path.join(src, "small.md"), "w") as f:
            f.write("Hello world. " * 100)

        # No sentence terminator — 5000 chars of 'x'. The old chunker
        # would push the entire thing as one chunk, blowing past
        # chunk_size and crashing the embedder.
        with open(os.path.join(src, "no-dot.md"), "w") as f:
            f.write("x" * 5000)

        # ~600 KB of normal text — realistic code-doc load. Under the
        # default 1MB per-file cap in ingest_directory_local.
        with open(os.path.join(src, "big.md"), "w") as f:
            f.write("# Big doc\n\n")
            sentence = ("Lorem ipsum dolor sit amet, consectetur adipiscing elit. " * 5)
            for _ in range(1500):
                f.write(sentence + " ")

        must_alive()

        # KB is auto-opened at startup — no create step needed.
        t0 = time.time()
        report_text = tools_call(
            "apollo_ingest_codebase",
            {"path": src, "project": "ingest-regression"},
        )
        elapsed = time.time() - t0

        must_alive()

        idx = report_text.find("Report: ")
        assert idx > 0, f"no report in response: {report_text[:200]}"
        report = json.loads(report_text[idx + len("Report: "):])

        n_proc   = report["files_processed"]
        n_skip   = report["files_skipped"]
        n_chunks = report["total_chunks"]
        errors   = report["errors"]

        print(f"  elapsed:        {elapsed:.1f}s")
        print(f"  files_processed:{n_proc}")
        print(f"  files_skipped:  {n_skip}")
        print(f"  total_chunks:   {n_chunks}")
        print(f"  errors:         {errors}")
        print(f"  doc_ids:        {report['doc_ids']}")

        assert n_proc >= 3,   f"expected >=3 files processed, got {n_proc}"
        assert n_chunks > 0,  f"expected >0 chunks, got {n_chunks}"
        assert len(errors) == 0, f"unexpected errors: {errors}"
        assert "/no-dot.md" in str(report["doc_ids"]), \
            "no-dot.md should have been ingested (sub-split path)"
        assert "/big.md" in str(report["doc_ids"]), \
            "big.md should have been ingested"

        print("\n  OK ingest_codebase handled all three torture cases")
        print("  OK server stayed alive throughout")
    finally:
        shutil.rmtree(src, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
        print("\nALL OK")
    except Exception as e:
        print(f"\nFAIL: {e}", file=sys.stderr)
        sys.exit(1)