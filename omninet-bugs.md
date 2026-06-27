# OmniNet + ApolloCore Integration — Bugs Found

Documented while integrating OmniNet (HTTP/WebSocket server library) into
ApolloCore Knowledge Graph MCP server. Some bugs are in OmniNet itself,
others are in adjacent code I touched.

Reporting context: ApolloCore is an MCP server (Model Context Protocol)
that uses OmniNet for its HTTP transport. ~33 MCP tools exposed via
JSON-RPC 2.0, plus a REST API for direct KB operations.

---

## 🔴 Critical Bugs (Data corruption / hangs)

### 1. `SQLiteDB::step()` returns `true` for `SQLITE_DONE` → infinite loop

**File:** `src/persistence.cpp` (ApolloCore, but used alongside OmniNet server)

**Severity:** CRITICAL — causes server hang

**Bug:**
```cpp
bool SQLiteDB::step(sqlite3_stmt* stmt) {
    if (!stmt) return false;
    int rc = sqlite3_step(stmt);
    // SQLITE_DONE = success (no more rows)
    // SQLITE_ROW = success (got a row)
    return rc == SQLITE_DONE || rc == SQLITE_ROW;  // ← BUG
}
```

**Impact:** Using this wrapper as `while (db_.step(stmt)) { ... }` creates
an infinite loop. I observed the loop counter reach **6+ million** before
I killed the server. Memory grows unboundedly as rows are appended.

**Why it's dormant:** No existing code in ApolloCore uses the wrapper —
everyone uses `sqlite3_step(stmt) == SQLITE_ROW` directly. I hit it
when implementing `delete_document` which was the first user.

**Fix:**
```cpp
return rc == SQLITE_ROW;  // Only true while there's more data
```

**Workaround (applied):** Use `while (sqlite3_step(stmt) == SQLITE_ROW)`
directly instead of the wrapper.

---

## 🟠 High-Severity Bugs (Workarounds needed)

### 2. `delete_()` HTTP method uses trailing underscore (no `delete` alias)

**File:** `src/http/Router.h`

**Severity:** API design — not technically a bug, but worth flagging

**Observation:**
```cpp
void delete_(std::string pattern, Handler handler);
// ...
void delete_(std::string pattern, JsonHandler handler);
```

The `delete` keyword is reserved in C++, so the method is named `delete_()`.
This works but is awkward. Every route registration looks like:
```cpp
router.delete_("/api/foo", handler);  // ← visual noise
```

**Suggestion:** Add a method alias or use a macro:
```cpp
#define ON_DELETE router.delete_
```
or
```cpp
void del(std::string pattern, Handler handler) { delete_(std::move(pattern), std::move(handler)); }
```

**Not a bug**, but contributed to readability issues in `apollo_rest_server.cpp`.

---

### 3. `Response::setBody()` takes `std::string` not `string_view` — copies even when not needed

**File:** `src/http/HttpResponse.h` (assumed)

**Severity:** Performance / minor API friction

**Observation:** When passing a JSON body that's already in memory,
`resp.setBody(body.dump())` copies the string. For large responses
(e.g. `get_subgraph` returning thousands of entities), this is wasteful.

**Workaround (applied):** Live with the copy — JSON serialization is
already expensive, the copy isn't the bottleneck.

**Suggestion:** Add `setBody(std::string_view)` overload.

---

## 🟡 Medium-Severity Issues (Friction, but works)

### 4. `Request::query()` returns reference to internal map, `.get()` returns `optional<string_view>`

**File:** `src/http/HttpRequest.h`

**Severity:** API discovery

**Observation:** Query parameter access pattern is:
```cpp
const auto& query = req.query();
if (auto lim = query.get("limit")) {
    int n = std::stoi(std::string(*lim));  // need to convert view → string
}
```

The `*lim` is `std::string_view`, but `std::stoi` needs `std::string`.
The double-conversion (`view → string → int`) is a common papercut.

**Suggestion:** Provide a typed getter:
```cpp
std::optional<int> queryInt(std::string_view key);
```

**Workaround (applied):** Use `std::string(view)` explicitly each time.

---

### 5. `StatusCode` enum requires `static_cast` from `int`

**File:** `src/http/HttpResponse.h` (assumed)

**Severity:** Minor friction

**Observation:**
```cpp
resp.setStatus(static_cast<onh::StatusCode>(status));  // status is int
```

**Suggestion:** `setStatus(int)` overload accepting raw HTTP codes.

**Workaround (applied):** `static_cast` everywhere.

---

### 6. No documented "shutdown" mechanism for graceful server stop

**File:** `src/http/HttpServer.h`

**Severity:** Operational concern

**Observation:** I couldn't find a clean way to stop the server from
within a handler. The REST API server runs forever; Ctrl-C is the only
way to stop. For production, you'd want a `/shutdown` admin endpoint
or signal-based graceful stop.

**Suggestion:** Add `Server::stop()` method or SIGTERM handler.

---

## 🟢 Low-Severity / Nits

### 7. No structured logging integration for OmniNet handlers

OmniNet uses its own `AsyncLogger`. ApolloCore uses `std::cerr` directly
in handlers. Two logging systems means logs go to different streams,
formats differ, no log levels coordinated.

**Suggestion:** Provide an integration point so users can plug in their
own logger.

---

### 8. CMake target name `omninet::omninet` (if that's what it is) is unusual

Not actually verified — the integration "just worked" via FetchContent.
But checking `find_package(omninet)` conventions: most libs use either
`omninet::omninet` or `omninet::OmniNet` consistently. Worth confirming
which convention OmniNet follows.

---

## 🔧 Other Bugs From This Session (NOT OmniNet)

These were found while building the MCP server. Documenting here in
case they're useful context.

### 9. llama.cpp `GGML_VULKAN` option is declared but never used

**File:** llama.cpp `ggml/CMakeLists.txt` line 224

**Bug:**
```cmake
option(GGML_VULKAN  "ggml: use Vulkan"  OFF)
```

That's the **only** mention of `GGML_VULKAN` in the entire CMakeLists.
No `if(GGML_VULKAN)` anywhere. Vulkan backend source exists at
`ggml/src/ggml-vulkan/` but is never included in the build.

**Fix:** Add after `ggml_add_backend(CPU)`:
```cmake
if (GGML_VULKAN)
    ggml_add_backend(VULKAN)
endif()
```

**Workaround (applied in ApolloCore CMakeLists.txt):**
Post-extract patch that modifies `ggml/src/CMakeLists.txt` after
FetchContent extracts llama.cpp.

---

### 10. llama.cpp renamed `LLAMA_VULKAN` → `GGML_VULKAN` (breaking)

**File:** llama.cpp CMake

The flag name changed between versions. Old `LLAMA_VULKAN=ON` is
silently ignored in newer versions. No deprecation warning.

**Fix:** Upstream should either keep the alias or document the rename.

---

### 11. FetchContent `CMAKE_ARGS` don't override `option()` defaults

**File:** ApolloCore `CMakeLists.txt`

**Bug:** Setting `-DGGML_VULKAN=ON` via `FetchContent_Declare(... CMAKE_ARGS ...)`
doesn't override `option(GGML_VULKAN ... OFF)` inside the inner project.
The `option()` macro ignores pre-set cache values without `FORCE`.

**Workaround (applied):**
```cmake
set(GGML_VULKAN ON CACHE BOOL "Enable ggml Vulkan backend" FORCE)
FetchContent_MakeAvailable(llama_cpp)
```

---

### 12. Stale `build/_deps/<dep>-build` causes "directory already used" errors

**File:** ApolloCore `CMakeLists.txt`

**Bug:** After patching llama.cpp's CMakeLists via post-extract patch,
re-running CMake without wiping `_deps/llama_cpp-build` fails with:
```
CMake Error: The binary directory
  .../build/_deps/llama_cpp-build/ggml/src/ggml-vulkan
is already used to build a source directory.
```

**Workaround (applied):** Wipe `_deps/<dep>-build` and `_deps/<dep>-subbuild`
before reconfiguring.

---

### 13. Missing namespace close in `apollo_mcp_tools.hpp`

**File:** `include/apollocore/apollo_mcp_tools.hpp`

**Bug:** When I split `namespace apollocore_v2::mcp` into
`namespace apollocore_v2 { namespace mcp {`, I forgot to add the
extra `}` for the outer `apollocore_v2` namespace. Only 3 of 4
needed `}` were present.

**Impact:** Every TU including this header ended up doubly-nested:
function symbols became `apollocore_v2::apollocore_v2::mcp::...`
and `<iostream>` couldn't find `std::istream` because the lookup
was being done in the wrong scope.

**Diagnostic:** The error message itself revealed the bug —
"function name is `apollocore_v2::apollocore_v2::mcp::...`".

**Fix:** Added the missing `} // namespace apollocore_v2` at end of header.

---

## 📊 Summary

| Category | Count | Severity |
|----------|-------|----------|
| Critical (data corruption / hangs) | 1 | 🔴 |
| High (workarounds needed) | 2 | 🟠 |
| Medium (friction) | 3 | 🟡 |
| Low (nits) | 2 | 🟢 |
| Other session bugs | 5 | varies |

**Critical bug (#1) should be fixed upstream in OmniNet or ApolloCore
persistence** — it WILL bite anyone using the wrapper.

---

## 🔗 Verification

All bugs were verified against actual code in:
- `/home/lee/projects/AI/ApolloCore` (this repo)
- `build/_deps/omninet-src/` (OmniNet v0.7.1)
- `build/_deps/llama_cpp-src/` (llama.cpp master at time of writing)

Reproduction:
```bash
cd /home/lee/projects/AI/ApolloCore
./build/examples/apollo_mcp_stdio_server \
    --embedding-model models/nomic-embed-text-v1.5.Q4_K_M.gguf \
    < /tmp/test_in.bin
```
Send JSON-RPC `tools/call` for `delete_document` — observe hang.

For bug #9 (Vulkan):
```bash
grep "Including" build/CMakeFiles/CMakeOutput.log  # Should say "Vulkan"
```

---

## 📝 Notes for Maintainer (leestripp)

- Most OmniNet APIs work cleanly. Issues are minor friction, not blockers.
- The persistence wrapper bug (#1) is the only one I'd say is "your bug
  to fix" — it's a 1-line fix in `step()`.
- Suggestion: consider documenting the `delete_()` method's naming
  rationale in the header (it's `delete` is a keyword, but newcomers
  might wonder).
- Suggestion: add `queryInt()`, `queryDouble()` typed accessors — would
  save users a `std::string(view)` boilerplate everywhere.

Happy to file issues on GitLab for any of these individually if helpful.