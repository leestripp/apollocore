# UI testing (manual)

The repo used to ship Python/Firefox-BiDi smoke tests; they were removed
(the project is C++-only). UI verification is now a quick manual pass —
it takes about a minute.

## Manual check

```bash
# 1. Start the server
./build/src/mcp_server/apollo_mcp_server --port 8765 &

# 2. Open the UI
xdg-open http://localhost:8765/
```

Walk through this checklist:

| Check | What it proves |
|-------|----------------|
| Page title is "ApolloCore" | correct HTML loaded |
| 5 tabs present (Health / Search / Graph / Settings / About) | markup wasn't accidentally truncated |
| Clicking each tab shows its panel | `app.js` router works |
| Health tab populates and auto-refreshes | the `/mcp` round-trip works |
| Health "probe" shows a round-trip latency | the server actually responds |
| Search tab returns "N results in T ms" for a query | the search pipeline runs end-to-end |
| Graph tab renders nodes (once the KB has entities) | graph resource + canvas code work |
| No errors in the browser console (F12) | no JS runtime errors |

`ctest` also runs `test_ui_static`, which verifies the files under
`src/mcp_server/static/` exist, are non-empty, and have the expected
structure — that catches truncated/missing assets without a browser.
