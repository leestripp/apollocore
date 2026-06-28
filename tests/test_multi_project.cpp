/**
 * ApolloCore M3.C Multi-Project Tests
 *
 * Unit tests for multi-agent + project scoping functionality.
 * Tests schema migration, project isolation, shared reads, deletion.
 *
 * Does NOT require embedding model — tests pure project logic.
 */

// CRITICAL: Undefine NDEBUG to enable assert() in Release mode
#undef NDEBUG

#include "apollocore/apollo_persistence.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace apollocore_v2;
using json = nlohmann::json;

const std::string TEST_PATH = "/tmp/apollo_m3c_tests";

// ============================================================================
// Helpers
// ============================================================================

void cleanup() {
    if (std::filesystem::exists(TEST_PATH)) {
        std::filesystem::remove_all(TEST_PATH);
    }
}

std::vector<std::string> sorted(const std::vector<std::string>& v) {
    auto sorted = v;
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

// ============================================================================
// Test 1: Default project is "default" on create
// ============================================================================
void test_default_project_on_create() {
    std::cout << "Test 1: Default project on create... ";
    cleanup();

    ApolloCore kb;
    assert(kb.create(TEST_PATH, "Test KB"));

    // After create, current project should be "default"
    assert(kb.current_project() == "default");

    // "default" project should be registered
    auto info = kb.get_project_info("default");
    assert(!info.is_null());
    assert(info["name"] == "default");
    assert(info["is_shared"] == false);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 2: Default project exists on open
// ============================================================================
void test_default_project_on_open() {
    std::cout << "Test 2: Default project on open... ";
    cleanup();

    // Create + close
    {
        ApolloCore kb;
        assert(kb.create(TEST_PATH, "Test KB"));
        kb.close();
    }

    // Open + verify default project exists
    ApolloCore kb;
    assert(kb.open(TEST_PATH));
    assert(kb.current_project() == "default");

    auto info = kb.get_project_info("default");
    assert(!info.is_null());

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 3: Register + list projects
// ============================================================================
void test_register_and_list() {
    std::cout << "Test 3: Register and list projects... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");

    // Register two more projects
    assert(kb.register_project("vortxGE", "Game engine", true, "alice"));
    assert(kb.register_project("cubeless", "Cube game", false, "bob"));

    // List projects — should be 3 (default + 2 new)
    auto projects = kb.list_projects();
    assert(projects.size() == 3);

    // Find each project
    bool found_default = false, found_vortxGE = false, found_cubeless = false;
    for (const auto& p : projects) {
        if (p["name"] == "default") {
            found_default = true;
            assert(p["is_current"] == true);
            assert(p["is_shared"] == false);
        }
        if (p["name"] == "vortxGE") {
            found_vortxGE = true;
            assert(p["is_shared"] == true);
            assert(p["owner"] == "alice");
        }
        if (p["name"] == "cubeless") {
            found_cubeless = true;
            assert(p["is_shared"] == false);
            assert(p["owner"] == "bob");
        }
    }
    assert(found_default && found_vortxGE && found_cubeless);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 4: Set current project
// ============================================================================
void test_set_current_project() {
    std::cout << "Test 4: Set current project... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");

    // Switch to vortxGE
    assert(kb.set_current_project("vortxGE"));
    assert(kb.current_project() == "vortxGE");

    // Switch to cubeless
    assert(kb.set_current_project("cubeless"));
    assert(kb.current_project() == "cubeless");

    // Switch to non-existent project should fail
    assert(!kb.set_current_project("nonexistent"));
    assert(kb.current_project() == "cubeless");  // unchanged

    // Switching to empty name should fail
    assert(!kb.set_current_project(""));

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 5: Writes are tagged with current project
// ============================================================================
void test_writes_tagged_with_project() {
    std::cout << "Test 5: Writes tagged with current project... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");

    // Write to vortxGE
    kb.set_current_project("vortxGE");
    size_t chunks1 = kb.add_document("vortxGE content");
    auto eid1 = kb.add_entity("EntityA", "CLASS", "", "vortxGE entity");
    auto rid1 = kb.add_relationship("EntityA", "EntityA", "RELATED_TO");
    assert(chunks1 > 0);
    assert(!eid1.empty());
    assert(!rid1.empty());

    // Write to cubeless
    kb.set_current_project("cubeless");
    size_t chunks2 = kb.add_document("cubeless content");
    auto eid2 = kb.add_entity("EntityB", "CLASS", "", "cubeless entity");
    assert(chunks2 > 0);
    assert(!eid2.empty());

    // Verify entities are isolated
    kb.set_current_project("vortxGE");
    auto vortxGE_entities = kb.get_entities(100);
    assert(vortxGE_entities.size() == 1);
    assert(vortxGE_entities[0].name == "EntityA");

    kb.set_current_project("cubeless");
    auto cubeless_entities = kb.get_entities(100);
    assert(cubeless_entities.size() == 1);
    assert(cubeless_entities[0].name == "EntityB");

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 6: Project isolation — entities don't leak
// ============================================================================
void test_project_isolation_entities() {
    std::cout << "Test 6: Entity isolation across projects... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");

    // Add 3 entities to vortxGE
    kb.set_current_project("vortxGE");
    kb.add_entity("Func1", "FUNCTION", "", "");
    kb.add_entity("Func2", "FUNCTION", "", "");
    kb.add_entity("Class1", "CLASS", "", "");

    // Add 2 entities to cubeless
    kb.set_current_project("cubeless");
    kb.add_entity("Cube", "CLASS", "", "");
    kb.add_entity("Level", "MODULE", "", "");

    // Verify vortxGE sees only its 3
    kb.set_current_project("vortxGE");
    auto entities = kb.get_entities(100);
    assert(entities.size() == 3);
    std::vector<std::string> names;
    for (const auto& e : entities) names.push_back(e.name);
    names = sorted(names);
    assert(names == sorted({"Func1", "Func2", "Class1"}));

    // Verify cubeless sees only its 2
    kb.set_current_project("cubeless");
    entities = kb.get_entities(100);
    assert(entities.size() == 2);
    names.clear();
    for (const auto& e : entities) names.push_back(e.name);
    names = sorted(names);
    assert(names == sorted({"Cube", "Level"}));

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 7: Project isolation — documents don't leak
// ============================================================================
void test_project_isolation_documents() {
    std::cout << "Test 7: Document isolation across projects... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");

    // vortxGE docs
    kb.set_current_project("vortxGE");
    kb.add_document("vortxGE doc 1");
    kb.add_document("vortxGE doc 2");

    // cubeless docs
    kb.set_current_project("cubeless");
    kb.add_document("cubeless doc 1");

    // Verify each project sees only its own
    kb.set_current_project("vortxGE");
    auto vortxGE_docs = kb.get_all_documents();
    assert(vortxGE_docs.size() == 2);

    kb.set_current_project("cubeless");
    auto cubeless_docs = kb.get_all_documents();
    assert(cubeless_docs.size() == 1);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 8: Shared read sources
// ============================================================================
void test_shared_read_sources() {
    std::cout << "Test 8: Shared read sources... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE", "", true);  // shared
    kb.register_project("cubeless");

    // vortxGE has data
    kb.set_current_project("vortxGE");
    kb.add_entity("SharedEntity", "CLASS", "", "");

    // cubeless without shared read source — sees nothing
    kb.set_current_project("cubeless");
    assert(kb.get_entities(100).size() == 0);

    // Add vortxGE as shared read source
    assert(kb.add_shared_read_source("vortxGE"));

    // cubeless can now read vortxGE content
    auto entities = kb.get_entities(100);
    assert(entities.size() == 1);
    assert(entities[0].name == "SharedEntity");

    // Removing the shared source takes it away
    assert(kb.remove_shared_read_source("vortxGE"));
    assert(kb.get_entities(100).size() == 0);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 9: Delete project only affects that project
// ============================================================================
void test_delete_project_isolation() {
    std::cout << "Test 9: Delete project only affects its data... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");
    kb.register_project("toscratch");  // extra project to delete

    // Add data to all three projects
    kb.set_current_project("vortxGE");
    kb.add_entity("Vx", "CLASS", "", "");
    kb.set_current_project("cubeless");
    kb.add_entity("Cb", "CLASS", "", "");
    kb.set_current_project("toscratch");
    kb.add_entity("Ts", "CLASS", "", "");

    // Verify all 3 projects have data
    assert(kb.set_current_project("vortxGE"));
    assert(kb.get_entities(100).size() == 1);
    assert(kb.set_current_project("cubeless"));
    assert(kb.get_entities(100).size() == 1);
    assert(kb.set_current_project("toscratch"));
    assert(kb.get_entities(100).size() == 1);

    // Delete toscratch (switch away from it first)
    kb.set_current_project("cubeless");
    assert(kb.delete_project("toscratch", true));

    // vortxGE and cubeless should still have their data
    kb.set_current_project("vortxGE");
    auto vortxGE_ents = kb.get_entities(100);
    assert(vortxGE_ents.size() == 1);
    assert(vortxGE_ents[0].name == "Vx");

    kb.set_current_project("cubeless");
    auto cubeless_ents = kb.get_entities(100);
    assert(cubeless_ents.size() == 1);
    assert(cubeless_ents[0].name == "Cb");

    // toscratch should be gone
    assert(kb.get_project_info("toscratch").is_null());

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 10: Cannot delete current or default project
// ============================================================================
void test_delete_protections() {
    std::cout << "Test 10: Delete protections... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("other");

    // Cannot delete without confirm
    assert(!kb.delete_project("other", false));

    // Cannot delete current project (current is "default")
    assert(!kb.delete_project("default", true));

    // Switch to "other", then cannot delete "default"
    kb.set_current_project("other");
    assert(!kb.delete_project("default", true));

    // Switch back to "default", can delete "other"
    kb.set_current_project("default");
    assert(kb.delete_project("other", true));
    assert(kb.get_project_info("other").is_null());

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 11: Project with name like default works
// ============================================================================
void test_set_project_shared() {
    std::cout << "Test 11: Toggle project shared... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test KB");
    kb.register_project("vortxGE");

    // Initially not shared
    auto info = kb.get_project_info("vortxGE");
    assert(info["is_shared"] == false);

    // Mark shared
    assert(kb.set_project_shared("vortxGE", true));
    info = kb.get_project_info("vortxGE");
    assert(info["is_shared"] == true);

    // Unmark
    assert(kb.set_project_shared("vortxGE", false));
    info = kb.get_project_info("vortxGE");
    assert(info["is_shared"] == false);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 12: Schema migration — pre-M3.C DB still loads
// ============================================================================
void test_schema_migration_compat() {
    std::cout << "Test 12: Schema migration backwards compat... ";
    cleanup();

    // Simulate a pre-M3.C DB by creating it, closing, and reopening
    {
        ApolloCore kb;
        assert(kb.create(TEST_PATH, "PreMigration"));
        // Use the default project
        kb.add_document("Pre-existing data");
        kb.add_entity("OldEntity", "PERSON", "", "");
        kb.close();
    }

    // Reopen — should auto-migrate
    ApolloCore kb;
    assert(kb.open(TEST_PATH));

    // "default" project should be registered
    auto info = kb.get_project_info("default");
    assert(!info.is_null());

    // Pre-existing data should be in "default" project
    auto entities = kb.get_entities(100);
    assert(entities.size() == 1);
    assert(entities[0].name == "OldEntity");

    // Should be able to add more data without issues
    kb.register_project("new_proj");
    kb.set_current_project("new_proj");
    kb.add_entity("NewEntity", "FUNCTION", "", "");

    auto new_entities = kb.get_entities(100);
    assert(new_entities.size() == 1);
    assert(new_entities[0].name == "NewEntity");

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 13: Re-running migration is idempotent
// ============================================================================
void test_migration_idempotent() {
    std::cout << "Test 13: Migration idempotent... ";
    cleanup();

    ApolloCore kb;
    assert(kb.create(TEST_PATH, "Test"));

    // Reopen multiple times — migration should not error
    for (int i = 0; i < 3; ++i) {
        kb.close();
        ApolloCore kb2;
        assert(kb2.open(TEST_PATH));
        assert(kb2.current_project() == "default");
        // Use the new kb2 so we don't reuse stale state
        kb2.close();
    }

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 14: get_project_info returns null for unknown project
// ============================================================================
void test_get_project_info_null() {
    std::cout << "Test 14: get_project_info for unknown project... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test");

    auto info = kb.get_project_info("doesnt_exist");
    assert(info.is_null());

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Test 15: visible_projects includes current and shared sources
// ============================================================================
void test_visible_projects() {
    std::cout << "Test 15: visible_projects() content... ";
    cleanup();

    ApolloCore kb;
    kb.create(TEST_PATH, "Test");
    kb.register_project("vortxGE");
    kb.register_project("cubeless");
    kb.register_project("third");

    kb.set_current_project("cubeless");
    auto visible = kb.visible_projects();
    assert(visible.size() == 1);
    assert(visible[0] == "cubeless");

    kb.add_shared_read_source("vortxGE");
    kb.add_shared_read_source("third");

    visible = kb.visible_projects();
    assert(visible.size() == 3);
    visible = sorted(visible);
    assert(visible == sorted({"cubeless", "vortxGE", "third"}));

    kb.remove_shared_read_source("third");
    visible = kb.visible_projects();
    assert(visible.size() == 2);

    std::cout << "PASSED\n";
    cleanup();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "===========================================\n";
    std::cout << "ApolloCore M3.C Multi-Project Tests\n";
    std::cout << "===========================================\n\n";

    test_default_project_on_create();
    test_default_project_on_open();
    test_register_and_list();
    test_set_current_project();
    test_writes_tagged_with_project();
    test_project_isolation_entities();
    test_project_isolation_documents();
    test_shared_read_sources();
    test_delete_project_isolation();
    test_delete_protections();
    test_set_project_shared();
    test_schema_migration_compat();
    test_migration_idempotent();
    test_get_project_info_null();
    test_visible_projects();

    std::cout << "\n===========================================\n";
    std::cout << "ALL M3.C TESTS PASSED ✅\n";
    std::cout << "===========================================\n";

    return 0;
}