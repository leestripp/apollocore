"""
smoke_settings.py — drive the ApolloCore Web UI through Firefox
and verify:
  1. Settings tab has only Projects + Models (no Server config / Build)
  2. About tab absorbs the moved read-only diagnostics
  3. Models section has Browse buttons and an editable input
  4. No "Apply" button (auto-apply on blur/Enter/picker)
  5. The model inputs are pre-populated with the resolved paths
  6. Status badges show load state
"""
import sys
import time

sys.path.insert(0, '/home/lee/.codemax/skills/firefox/scripts')
from bidi_client import BidiClient

with BidiClient("localhost", 9235) as bc:
    bc.session_new()
    bc.session_subscribe(["log.entryAdded", "browsingContext.load"])
    ctx = bc.context()
    bc.navigate(ctx, "http://127.0.0.1:8765/")
    time.sleep(2)

    # ── Settings tab ─────────────────────────────────────────────
    print("== Settings tab ==")
    bc.script_evaluate(ctx, "document.querySelector('.tab[data-tab=\"settings\"]').click()")
    time.sleep(1)

    def jval(x):
        """Unwrap the BiDi RemoteObject / array-of-objects shape."""
        if isinstance(x, list):
            return [j.get("value") for j in x if isinstance(j, dict)]
        if isinstance(x, dict):
            return x.get("value")
        return x

    cards = jval(bc.script_evaluate(ctx, """
        Array.from(document.querySelectorAll('#tab-settings .card > h3'))
             .map(h => h.textContent)
    """))
    print(f"  cards: {cards}")

    # The required cards: Models and Projects. NOT Server config / Build.
    assert "Models" in cards, f"expected 'Models' card, got {cards}"
    assert "Projects" in cards, f"expected 'Projects' card, got {cards}"
    assert "Server configuration" not in cards, "Server config should be in About, not Settings"
    assert "Build" not in cards, "Build should be in About, not Settings"
    print("  ✓ only Models + Projects (no read-only cruft)")

    # Browse buttons
    embed_browse = bc.script_evaluate(ctx, "!!document.getElementById('cfg-embed-browse')")
    llm_browse = bc.script_evaluate(ctx, "!!document.getElementById('cfg-llm-browse')")
    print(f"  Browse buttons: embed={embed_browse}  llm={llm_browse}")
    assert embed_browse and llm_browse, "Browse buttons missing"

    # No Apply button
    has_apply = bc.script_evaluate(ctx, """
        Array.from(document.querySelectorAll('#tab-settings button'))
             .some(b => b.textContent.trim() === 'Apply')
    """)
    print(f"  Apply button present: {has_apply}  (expect False)")
    assert not has_apply, "Apply button should be gone — auto-apply only"

    # Inputs are pre-populated
    embed_val = bc.script_evaluate(ctx, "document.getElementById('cfg-embed-input').value")
    llm_val = bc.script_evaluate(ctx, "document.getElementById('cfg-llm-input').value")
    print(f"  embed input value: {embed_val!r}")
    print(f"  llm   input value: {llm_val!r}")
    assert "nomic-embed" in embed_val, "embed input should be pre-populated"
    assert "qwen" in llm_val, "llm input should be pre-populated"

    # Status badges
    embed_status = bc.script_evaluate(ctx, "document.getElementById('cfg-embed-status').textContent")
    llm_status   = bc.script_evaluate(ctx, "document.getElementById('cfg-llm-status').textContent")
    print(f"  embed status: {embed_status!r}")
    print(f"  llm   status: {llm_status!r}")
    assert "loaded" in embed_status.lower(), "expected embed to show loaded"
    assert "loaded" in llm_status.lower(), "expected llm to show loaded"

    # Auto-apply: simulate a blur with a bogus path
    print("\n  test auto-apply on bogus path…")
    bc.script_evaluate(ctx, """
        const i = document.getElementById('cfg-llm-input');
        i.value = '/no/such/file.gguf';
        i.dispatchEvent(new Event('blur'));
    """)
    time.sleep(2)
    llm_status_after = bc.script_evaluate(ctx, "document.getElementById('cfg-llm-status').textContent")
    print(f"  llm status after bogus apply: {llm_status_after!r}")
    assert "✗" in llm_status_after or "file not found" in llm_status_after.lower(), \
        f"expected error status after bogus path, got {llm_status_after!r}"

    # ── About tab ───────────────────────────────────────────────
    print("\n== About tab ==")
    bc.script_evaluate(ctx, "document.querySelector('.tab[data-tab=\"about\"]').click()")
    time.sleep(1)
    about_cards = jval(bc.script_evaluate(ctx, """
        Array.from(document.querySelectorAll('#tab-about .card > h3'))
             .map(h => h.textContent)
    """))
    print(f"  cards: {about_cards}")
    assert "Build" in about_cards, "Build should be in About"
    assert "Server (read-only — restart to change)" in about_cards, "Server info should be in About"
    assert "Connect an agent" in about_cards
    assert "What is ApolloCore?" in about_cards
    print("  ✓ Build, Server, Connect, About all present")

    # ── screenshot ─────────────────────────────────────────────
    bc.script_evaluate(ctx, "document.querySelector('.tab[data-tab=\"settings\"]').click()")
    time.sleep(1)
    bc.take_screenshot(ctx, "/tmp/apollo_settings.png")
    print("\n  screenshot: /tmp/apollo_settings.png")

print("\nAll Settings-tab checks passed.")