# UI testing with Firefox (BiDi)

How to drive the Web UI through a real browser and what the gotchas
are. Read this before your first browser test — the BiDi transport
has some surprising setup requirements on Firefox 151+.

## TL;DR — run the smoke test

```bash
# 1. Start the server
APOLLO_PORT=8765 ./scripts/start.sh --detach

# 2. Start Firefox with BiDi enabled (use the skill's launcher script)
FIREFOX_DEBUGGER_PORT=9222 \
    /home/lee/.codemax/skills/firefox/scripts/start-firefox-debug.sh

# 3. Drive the UI
python3 tests/smoke_ui_firefox.py
# -> visits /, clicks each tab, runs the Health probe,
#    submits a Search query, takes a screenshot
```

Expected output:

```
== navigate to http://127.0.0.1:8765/ ==
  title:    ApolloCore
  tab btns: 5  (expect 5)
  panels:   5  (expect 5)

== click each tab ==
  health      ✓
  search      ✓
  graph       ✓
  settings    ✓
  about       ✓

== Health tab — run the round-trip probe ==
  /mcp round-trip latency: 5.0 ms

== Search tab — type a query, submit, check results ==
  status: '0 results in 7 ms'

== CSS validation ==
  (no warnings)
```

## What the smoke test catches

| What | Why it matters |
|------|----------------|
| Page title is "ApolloCore" | correct HTML loaded |
| 5 `.tab` buttons present | markup wasn't accidentally truncated |
| 5 `.tab-panel` elements present | panels for each tab exist in DOM |
| All 5 tab clicks hide/show correct panels | `app.js` router works |
| Health `/mcp` round-trip < a few ms | the server actually responds |
| Search shows "N results in T ms" | the search pipeline runs end-to-end |
| No CSS warnings | no "crossed-out" rules in DevTools |
| No `console.error` messages | no JS runtime errors |

## Gotchas you'll hit

### 1. **Without `user.js` prefs in the profile, BiDi never binds**

This burned me twice in one session. Firefox 151 will start, the
process appears, but `--remote-debugging-port=N` is silently ignored
on a fresh profile. Fix: pre-write BiDi prefs to `user.js` before
launch:

```js
user_pref("devtools.debugger.remote-enabled", true);
user_pref("devtools.chrome.enabled", true);
user_pref("devtools.debugger.prompt-connection", false);
user_pref("devtools.debugger.force-local", false);
user_pref("remote.enabled", true);
user_pref("marionette.enabled", true);
```

The skill's `start-firefox-debug.sh` does this. **Always use that
script — don't roll your own firefox command line.**

### 2. The user's normal Firefox blocks BiDi on the same port

If the user has Firefox running with `--dbus-service /usr/bin/firefox`,
a second instance with `--remote-debugging-port=N` will launch but
the BiDi listener won't bind (the launcher process holds the DBus
session). Use the skill's `--profile /tmp/fx-bidi-agent` and the
right flags to get a clean second instance.

If you accidentally kill the user's firefox while cleaning up (e.g.
`pkill -9 firefox` matches everything called "firefox"), restart it
with:

```bash
firefox --dbus-service /usr/bin/firefox &
```

### 3. `css_warnings` and `console_messages` are functions, not methods

The skill's helpers take a list of events:

```python
from bidi_client import log_collector, css_warnings

# Drain the queue first, then filter
events = log_collector(bc)
warnings = css_warnings(events)
```

Calling `bc.css_warnings(bc)` will raise `TypeError: 'BidiClient' is not iterable`.

### 4. The event is `log.entryAdded`, not `network.requestWillBeSent`

Firefox's BiDi uses different network event names than Chrome DevTools
Protocol. Stick to these:

```python
bc.session_subscribe(["log.entryAdded", "browsingContext.load", "network.responseCompleted"])
```

Subscribing to `network.requestWillBeSent` (a CDP-ism) returns
`invalid argument`. Same for some other common CDP event names.

### 5. `script_evaluate` returns a single value, not a tuple

```python
result = bc.script_evaluate(ctx, "document.title")
# result == "ApolloCore", not ("ApolloCore",)

# WRONG: result, = bc.script_evaluate(ctx, "...")
# CORRECT:
result = bc.script_evaluate(ctx, "...")
```

### 6. The body of the BiDi message is async

The skill's `log_collector` correctly drains the queue and returns
events collected over a window of seconds. Always `time.sleep(N)`
*before* draining, because navigation events arrive with a delay.

### 7. `script_evaluate` IIFEs are your friend

Pass a self-contained IIFE if you need multi-statement JS:

```python
bc.script_evaluate(ctx, """
    (() => {
        const btn = document.querySelector('.tab[data-tab="search"]');
        if (btn) btn.click();
    })();
""")
```

A naked `document.querySelector(...).click()` also works; IIFEs just
let you do error handling.

## Manual test (no Python)

If you want to look at the UI by hand, start the server and firefox:

```bash
APOLLO_PORT=8765 ./scripts/start.sh --detach
xdg-open http://localhost:8765/
```

Five tabs, JSON-RPC-backed, all chrome.

## Folder layout

```
~/.codemax/skills/firefox/
├── SKILL.md                  ← the loaded skill description (above)
└── scripts/
    ├── bidi_client.py        ← zero-dep Python BiDi client
    └── start-firefox-debug.sh ← the only safe way to launch BiDi Firefox

tests/smoke_ui_firefox.py      ← smoke test that drives the UI through BiDi
```