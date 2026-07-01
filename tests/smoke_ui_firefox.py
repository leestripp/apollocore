"""
test_ui_with_firefox.py — drive the ApolloCore Web UI through Firefox
and report:
  1. Page loads (no JS errors)
  2. CSS validation: any "Unknown property" or parse failures
  3. Each tab renders: switch to #search, #graph, #settings, #about
  4. UI successfully calls POST /mcp (Health tab auto-refreshes)
"""
import sys
import time
import json

sys.path.insert(0, '/home/lee/.codemax/skills/firefox/scripts')
from bidi_client import BidiClient, log_collector, css_warnings

URL = "http://127.0.0.1:8765/"
TABS = ["health", "search", "graph", "settings", "about"]

js_log = []
css_log = []
network_log = []

with BidiClient("localhost", 9225) as bc:
    bc.session_new()
    bc.session_subscribe([
        "log.entryAdded",
        "browsingContext.load",
        "network.responseCompleted",
    ])
    ctx = bc.context()

    # ── Phase 1: load the page ──────────────────────────────
    print(f"== navigate to {URL} ==")
    bc.navigate(ctx, URL)
    time.sleep(2)  # let JS modules load

    msgs = log_collector(bc)
    for m in msgs:
        if m.get("method") != "log.entryAdded":
            continue
        e = m["params"]["entry"]
        src = e.get("source", "?")
        lvl = e.get("level", "?")
        txt = e.get("text", "")
        if src in ("console", "javascript", "pageerror") or lvl in ("error", "warning"):
            js_log.append((lvl, src, txt))

    css_log = css_warnings(log_collector(bc))

    # ── Phase 2: read the page title and tab count ──────────
    title = bc.script_evaluate(ctx, "document.title")
    n_tabs = bc.script_evaluate(ctx, "document.querySelectorAll('.tab').length")
    n_panels = bc.script_evaluate(ctx, "document.querySelectorAll('.tab-panel').length")
    print(f"  title:    {title}")
    print(f"  tab btns: {n_tabs}  (expect 5)")
    print(f"  panels:   {n_panels}  (expect 5)")

    # ── Phase 3: click each tab and check the panel becomes visible ──
    print("\n== click each tab ==")
    for t in TABS:
        bc.script_evaluate(ctx, f"""
            (() => {{
                const btn = document.querySelector('.tab[data-tab="{t}"]');
                if (btn) btn.click();
            }})();
        """)
        time.sleep(0.8)  # let any tab-specific JS run

        visible_id = bc.script_evaluate(ctx, f"""
            (() => {{
                const e = document.getElementById('tab-{t}');
                return e && !e.hidden ? e.id : null;
            }})();
        """)
        active = bc.script_evaluate(ctx, f"""
            document.querySelector('.tab.active')?.dataset.tab || null
        """)
        ok = (visible_id == f"tab-{t}") and (active == t)
        print(f"  {t:10s}  panel visible={visible_id!r}  active={active!r}  {'✓' if ok else '✗'}")

    # ── Phase 4: Health tab probe ─────────────────────────────
    print("\n== Health tab — run the round-trip probe ==")
    bc.script_evaluate(ctx, """
        document.querySelector('.tab[data-tab="health"]').click();
    """)
    time.sleep(0.5)
    bc.script_evaluate(ctx, "document.getElementById('health-probe-btn').click()")
    time.sleep(2)
    rtt = bc.script_evaluate(ctx, """
        document.getElementById('health-probe-rtt').textContent
    """)
    print(f"  /mcp round-trip latency: {rtt}")

    # ── Phase 5: Search tab — does the form exist and accept input? ──
    print("\n== Search tab — type a query, submit, check results ==")
    bc.script_evaluate(ctx, "document.querySelector('.tab[data-tab=\"search\"]').click()")
    time.sleep(0.4)
    bc.script_evaluate(ctx, "document.getElementById('search-query').focus()")
    bc.script_evaluate(ctx, "document.getElementById('search-query').value = 'hello'")
    bc.script_evaluate(ctx, "document.getElementById('search-form').dispatchEvent(new Event('submit', {cancelable: true, bubbles: true}))")
    time.sleep(2)
    status = bc.script_evaluate(ctx, "document.getElementById('search-status').textContent")
    print(f"  status: {status!r}")

    # ── Phase 6: take screenshot via canvas ─────────────────
    # BiDi has a screencast capability, but it's complex; instead read
    # out the body text length to confirm a real page rendered.
    body_text_len = bc.script_evaluate(ctx, "document.body.innerText.length")
    print(f"\n== rendered body innerText length: {body_text_len}")

    # ── Phase 6.5: take screenshot ─────────────────────────
    print("\n== screenshot ==")
    shot_path = "/tmp/apollo_ui.png"
    bc.take_screenshot(ctx, path=shot_path)
    print(f"  saved: {shot_path}")

    # ── Network calls ─────────────────────────────────────────
    msgs = log_collector(bc)
    mcp_calls = [m for m in msgs if "mcp" in json.dumps(m).lower()]
    print(f"== network events: {len(msgs)} total ==")

# ── Report JS / CSS issues ────────────────────────────────────
print("\n== JS console messages ==")
if js_log:
    for lvl, src, txt in js_log[:20]:
        print(f"  [{lvl}] {src}: {txt[:200]}")
    if len(js_log) > 20:
        print(f"  ... +{len(js_log) - 20} more")
else:
    print("  (none)")

print("\n== CSS validation ==")
if css_log:
    for src, lvl, txt in css_log:
        print(f"  [{lvl}] {txt[:200]}")
else:
    print("  (no warnings)")

# Exit code: 1 if any JS error
errors = [e for e in js_log if e[0] == "error"]
sys.exit(1 if errors else 0)