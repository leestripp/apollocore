# Project Refactor Plan: Single Global KB — No Isolation

> **Status:** Writing plan (this file). Execute after reading.
> **Trigger:** User reported that search returns 0 results because `current_project_`
> defaulted to `"default"` but all ingested docs went to named projects.
> **Root cause:** The "remove KB-lifecycle tools" pass (2026-07-05) eliminated the
> `apollo_set_project` tool but left the `current_project_` state machine intact.
> Every ingest wrote to `current_project_`; every read filtered by it.
> **Fix:** Eliminate `current_project_` / `shared_read_sources_` entirely.
> Projects are write-time provenance tags only; reads always span all data.

---

## Step 1 — Audit `ApolloCore` class header

**File:** `include/apollocore/apollo_persistence.hpp`

**Action:** Read and note every function that takes or returns `current_project_`
or `shared_read_sources_`. The goal is to remove both from the public API.

Expected removals from the class declaration:
- `std::string current_project_` member
- `std::vector<std::string> shared_read_sources_` member
- `std::string current_project() const` — getter (no longer needed)
- `bool set_current_project(const std::string&)` — remove entirely
- `bool add_shared_read_source(const std::string&)` — remove
- `bool remove_shared_read_source(const std::string&)` — remove
- `bool set_project_shared(const std::string&, bool)` — remove (no longer meaningful)
- `std::vector<std::string> visible_projects() const` — remove (after Step 4)

Functions to **keep but change signatures:**
- `std::string add_entity(name, type, chunk_id, description, confidence, project = "default")`
  — add `project` parameter (was using `current_project_`)
- `std::string add_relationship(src, tgt, type, description, confidence, project = "default")`
  — add `project` parameter
- `size_t extract_entities(confidence, skip_extracted, limit, project = "default")`
  — add `project` parameter (for provenance tagging of new entities)

Functions **unchanged** (just remove the filtering):
- `std::vector<std::pair<std::string, float>> query(...)` — always all projects
- `std::vector<HybridResult> query_hybrid(...)` — always all projects
- `std::vector<RerankedResult> query_with_rerank(...)` — always all projects
- `std::vector<RerankedResult> query_full(...)` — always all projects
- `std::vector<Entity> get_entities_for_chunk(...)` — already all chunks, entity `project_id` is metadata
- `std::vector<Relationship> get_relationships(...)` — already all relationships
- `std::vector<Chunk> get_all_chunks()` — already all chunks

Functions **unchanged** (informational/management only):
- `bool register_project(name, description, owner)` — keep, creates the project tag record
- `bool delete_project(name, confirm)` — keep, bulk-deletes everything with that project_id
- `nlohmann::json list_projects() const` — keep, returns all project metadata
- `nlohmann::json get_project_info(name) const` — keep, returns one project info
- `nlohmann::json get_stats() const` — keep, global counts
- `std::vector<ProjectStats> get_project_breakdown() const` — keep, per-project breakdown

---

## Step 2 — Rewrite `src/apollo_persistence.cpp`

### 2a. Remove members + initializers

In constructor and `create()`/`open()`:
```cpp
// REMOVE these two lines from every constructor / create() / open():
current_project_ = "default";
shared_read_sources_.clear();
```

### 2b. Remove `visible_projects()` implementation

Delete the entire function. Every read operation that called it
will be changed to read without filtering (Step 2d).

### 2c. Change write-time functions to take `project` parameter

All three write paths set `project_id` on data. After this change,
the project comes from the caller, not from mutable state.

**`add_entity(name, type, chunk_id, desc, confidence, project)`:**
- Change signature: add `project` parameter with default `"default"`
- Update the `kb_persistence_->save_entity()` call to pass `project` instead of `current_project_`
- 1 caller in tools.cpp → update call site

**`add_relationship(src, tgt, type, desc, confidence, project)`:**
- Change signature: add `project` parameter with default `"default"`
- Update the `kb_persistence_->save_relationship()` call to pass `project`
- 1 caller in tools.cpp → update call site

**`add_document(text, doc_id, chunk_size, project)` — existing overload:**
- Change signature: add `project` parameter with default `"default"`
- Update `save_entity()` call inside `add_document()` (line 805) to pass `project`
- Update `save_relationship()` call inside `add_document()` (line 831) to pass `project`
- Update `chunks_.push_back()` (line 419) → remove `current_project_` from the Chunk struct; either remove the project field from Chunk or leave it as metadata only
  - **Note:** The `Chunk` struct in the header has a `project_id` field. After the refactor this field is write-time provenance metadata only. Update `Chunk` struct if needed.

**`extract_entities(confidence, skip_extracted, limit, project)`:**
- Add `project` parameter with default `"default"`
- Update the `add_entity()` and `add_relationship()` calls inside to pass `project`
- The existing code also calls `save_entity()` directly (line 805) and `save_relationship()` directly (line 831) — update those to pass `project`
- 1 caller in tools.cpp → update call site

### 2d. Remove `visible_projects()` callers from read operations

Delete `auto visible = visible_projects();` and replace every subsequent
filtering `for` loop with a direct call to the non-filtering version.

**Search operations** (`query()`, `query_hybrid()`, `query_with_rerank()`, `query_full()`):
- The `visible_projects()` call and the `visible_set.count(...)` check are removed
- All chunks are searched unconditionally
- Result: vector search always spans all projects

**`get_entities_for_chunk(chunk_id)`:**
- Currently calls `kb_persistence_->load_entities_for_chunk(chunk_id, visible_set)`
- Change to: `kb_persistence_->load_entities_for_chunk(chunk_id)` (no filter)
- Result: always returns all entities for a chunk

**`get_entities(limit)`** (if it exists):
- Currently filters by `visible_projects()` — remove the filter
- Result: always returns all entities

**`get_relationships(limit)`:**
- Currently calls `load_relationships_for_projects(visible, limit)` — change to
  `load_all_relationships(limit)`
- Result: always returns all relationships

**`get_neighbors(entity_name, hops)`** (if it exists):
- Currently calls `visible_projects()` — remove the filter
- Result: neighbors span all projects

### 2e. Remove deprecated member functions

Delete from `apollo_persistence.cpp`:
```cpp
// DELETE these completely:
bool ApolloCore::set_current_project(const std::string&) { ... }
bool ApolloCore::add_shared_read_source(const std::string&) { ... }
bool ApolloCore::remove_shared_read_source(const std::string&) { ... }
bool ApolloCore::set_project_shared(const std::string&, bool) { ... }
std::vector<std::string> ApolloCore::visible_projects() const { ... }
```

### 2f. Update `list_projects()` and `get_project_info()`

Remove `"is_current"` field from the JSON output — it no longer has meaning.
```cpp
// In list_projects():
// OLD: {"is_current", p.name == current_project_}
// NEW: remove this field entirely

// In get_project_info():
// OLD: {"is_current", p.name == current_project_}
// NEW: remove this field entirely
```

### 2g. Update `delete_project()`

The `delete_project()` currently checks `if (name == current_project_)`.
Remove that guard — any project can be deleted regardless.
```cpp
// OLD:
// if (name == current_project_) return false; // can't delete current
// NEW: remove that guard
```

---

## Step 3 — Update `tools.cpp`

### 3a. `apollo_add_document` — remove save/restore dance

**Before:**
```cpp
const std::string saved_project = state.core().current_project();
if (!project.empty()) state.core().set_current_project(project);
try { added = state.core().add_document(text, doc_id, chunk_size); }
catch { state.core().set_current_project(saved_project); throw; }
state.core().set_current_project(saved_project);
```

**After:**
```cpp
// project is already extracted from args (or defaults to "default")
// register it if it doesn't exist (for provenance display in list_projects)
if (!project.empty()) {
    (void)state.core().register_project(project, "Ingested via apollo_add_document");
}
// Pass project directly — no state mutation, no save/restore:
size_t added = state.core().add_document(text, doc_id, chunk_size, project);
```

**Also:** Remove `current_project()` getter call (function no longer exists).

### 3b. `apollo_ingest_codebase` — remove `set_current_project` call

**Before:**
```cpp
state.core().register_project(project, ...);
state.core().set_current_project(project);  // REMOVE THIS LINE
auto report = ingest_directory_local(state.core(), path, extensions);
```

**After:**
```cpp
state.core().register_project(project, ...);
// No set_current_project() — project is passed through the call stack
auto report = ingest_directory_local(state.core(), path, extensions, project); // Step 3c
```

**Also:** Remove `apollo_set_config` tool if it calls `set_current_project` (it doesn't — skip).

### 3c. `ingest_directory_local()` — pass `project` through to `add_document`

The local helper `ingest_directory_local()` (defined inside `tools.cpp`)
calls `core.add_document()` on each file. Update its signature:
```cpp
// OLD:
nlohmann::json ingest_directory_local(ApolloCore& core, const std::string& path, ...);
// NEW:
nlohmann::json ingest_directory_local(ApolloCore& core, const std::string& path,
                                   const std::string& project, ...);
// Every call to core.add_document(...) becomes core.add_document(..., project);
```

### 3d. `apollo_extract_entities` — add project parameter

```cpp
// In apollo_extract_entities tool handler:
// OLD:
size_t n = state.core().extract_entities(0.5f, skip, limit);
// NEW:
const std::string project = args.value("project", "default");
size_t n = state.core().extract_entities(0.5f, skip, limit, project);
```

### 3e. `apollo_get_neighbors` — verify all-project scope

Already correct after Step 2d. `query_hybrid()` no longer filters by project.
Just add a comment explaining this.

### 3f. `apollo_list_projects` — confirm no `is_current`

Already handled by Step 2f (the `p.erase("is_current")` line).
Confirm it's still there.

### 3g. Tool schemas — update descriptions

In the JSON Schema for each tool, update the `description` field for `project`:

**`apollo_add_document` schema:**
```json
// OLD:
"project": {"description": "Tag for this chunk batch (creates the project ...). Default: default."}
// NEW:
"project": {"description": "Provenance tag for this chunk batch. Projects are metadata only — they do NOT isolate data. All searches span every project. Default: default."}
```

**`apollo_ingest_codebase` schema:**
```json
// ADD to schema:
"project": {"type": "string", "description": "Provenance tag for ingested files. Projects are metadata only — they do NOT isolate data."}
// Note: already in args but verify schema is updated
```

**`apollo_extract_entities` schema — ADD project param:**
```json
"project": {"type": "string", "description": "Provenance tag for extracted entities. Projects are metadata only. Default: default."}
```

---

## Step 4 — Update `kb_persistence.cpp` if needed

Check whether `save_entity()` / `save_relationship()` take a `project_id` parameter.
If they already do (they do — we saw `current_project_` passed as last arg),
the persistence layer needs no changes — it just stores whatever it receives.

Check:
- `load_entities_for_chunk(chunk_id)` — should have NO project filter overload
- `load_all_entities()` — should exist and return everything
- `load_relationships_for_projects(visible, limit)` — replace with `load_all_relationships(limit)` if it exists

If these don't exist, add them.

---

## Step 5 — Update `include/apollocore/apollo_persistence.hpp`

After the class changes in Step 1, update the header to match:
- Remove `current_project_`, `shared_read_sources_` members
- Remove `current_project()`, `set_current_project()`, `add_shared_read_source()`, `remove_shared_read_source()`, `set_project_shared()`, `visible_projects()`
- Update signatures of `add_entity()`, `add_relationship()`, `extract_entities()` to add `project` parameter

---

## Step 6 — Update `tests/test_multi_project.cpp`

This test suite tests multi-project isolation and `visible_projects()`.
**All those tests are now wrong** — replace with tests that verify:

1. `add_document` with `project="alpha"` → `query()` returns that chunk (verify no filter)
2. `add_document` with `project="beta"` → `query()` also returns that chunk (no project filter)
3. `register_project()` creates a project record with no data → `list_projects()` includes it
4. `delete_project("alpha")` removes all alpha-tagged chunks → `query()` returns 0 for alpha chunks
5. `extract_entities(text, "gamma")` stores entities tagged "gamma" → `get_entities()` returns them

Remove:
- `test_visible_projects()` — concept removed
- Any test that sets `current_project_` directly (use `add_document(..., project)` instead)
- Any test asserting that `query()` filters by project

---

## Step 7 — Build + smoke test

```bash
# Build
cmake --build build -j8 --target apollo_mcp_server 2>&1 | grep -v "_deps/" | grep -E "error:|warning:"

# Quick smoke test: ingest two docs in different projects, search returns both
# (run against a fresh temp KB to avoid pollution)

# Run ctest
ctest --test-dir build -E test_ingest_codebase
```

Expected results:
- All C++ files compile with zero new errors
- `ctest` passes
- Search returns chunks from ALL projects (not just the "current" one)
- `apollo_list_projects` shows each project with correct counts
- `apollo_get_stats` shows global totals

---

## Step 8 — Update `CodeMax.md`

After completing the refactor:
1. Update the "Active Plan" section with this refactor as done
2. Update tool descriptions in the same section to reflect that project is provenance-only
3. Add test results to the test summary
4. Add a new section "Architecture: Projects as Provenance Tags" explaining the design

---

## Expected Outcome

After this refactor:

| Operation | Projects affect it? |
|---|---|
| `apollo_query_kb` (vector/hybrid/rerank/full) | **No** — always all data |
| `apollo_search_symbols` | **No** — always all data |
| `apollo_get_neighbors` | **No** — always all data |
| `apollo_extract_entities` | Tags entities with the `project` you pass |
| `apollo_add_document` | Tags chunks with the `project` you pass |
| `apollo_ingest_codebase` | Tags all ingested chunks with the `project` you pass |
| `apollo_list_projects` | Lists all project tags (informational) |
| `apollo_get_stats` | Shows global totals + per-project breakdown (informational) |
| `apollo_delete_project` | Bulk-deletes all data with that project tag |

**Core invariant:** Two AI agents in different "projects" can each ingest their own data, but when either searches, they see everything — the knowledge base is genuinely shared.
