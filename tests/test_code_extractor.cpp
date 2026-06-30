/**
 * ApolloCore Code Extractor Tests (Phase M3.B)
 *
 * Tests the tree-sitter AST → CodeModel extraction.
 * Verifies that C++ functions, classes, namespaces are extracted
 * correctly with their relationships (CALLS, INHERITS).
 */

#undef NDEBUG

#include "apollocore/apollo_code_extractor.hpp"
#include "apollocore/apollo_ts_wrapper.hpp"
#include <cassert>
#include <functional>
#include <iostream>

using namespace apollocore_v2;
namespace ts = apollocore_v2::ts;

const std::string SAMPLE_CPP = R"(
#include <iostream>

namespace app {

class Session {
public:
    Session(const std::string& user) : user_(user) {}
    std::string user() const { return user_; }
private:
    std::string user_;
};

bool authenticate(const std::string& user, const std::string& password) {
    return user_db.has_user(user);
}

Session* login(const std::string& user) {
    auto session = new Session(user);
    sessions_.push_back(session);
    return session;
}

} // namespace app

class Shape {
public:
    virtual double area() const = 0;
};

class Circle : public Shape {
public:
    double area() const override { return 3.14 * r * r; }
private:
    double r;
};

int main() {
    app::login("alice");
    app::authenticate("alice", "secret");
    Circle c;
    return 0;
}
)";

// ============================================================================
// Test 1: Language detection
// ============================================================================
void test_language_detection() {
    std::cout << "Test 1: Language detection... ";
    assert(detect_language("foo.cpp") == "cpp");
    assert(detect_language("foo.cc") == "cpp");
    assert(detect_language("foo.cxx") == "cpp");
    assert(detect_language("foo.hpp") == "cpp");
    assert(detect_language("foo.h") == "cpp");
    assert(detect_language("foo.c") == "cpp");
    assert(detect_language("foo.py") == "python");
    assert(detect_language("foo.pyi") == "python");
    assert(detect_language("foo.js") == "javascript");
    assert(detect_language("foo.md") == "");
    assert(detect_language("foo") == "");
    assert(detect_language("foo.CPP") == "cpp");  // case insensitive
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 2: Extract functions
// ============================================================================
void test_extract_functions() {
    std::cout << "Test 2: Extract functions... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_auth = false, found_login = false, found_main = false;
    for (const auto& e : model.entities) {
        if (e.type != "FUNCTION") continue;
        if (e.name == "app::authenticate") found_auth = true;
        if (e.name == "app::login") found_login = true;
        if (e.name == "main") found_main = true;
    }
    assert(found_auth);
    assert(found_login);
    assert(found_main);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 3: Extract classes with namespace
// ============================================================================
void test_extract_classes() {
    std::cout << "Test 3: Extract classes... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_session = false, found_shape = false, found_circle = false;
    for (const auto& e : model.entities) {
        if (e.type != "CLASS") continue;
        if (e.name == "app::Session") found_session = true;
        if (e.name == "Shape") found_shape = true;
        if (e.name == "Circle") found_circle = true;
    }
    assert(found_session);
    assert(found_shape);
    assert(found_circle);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 4: Extract namespaces
// ============================================================================
void test_extract_namespaces() {
    std::cout << "Test 4: Extract namespaces... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_app = false;
    for (const auto& e : model.entities) {
        if (e.type == "NAMESPACE" && e.name == "app") {
            found_app = true;
        }
    }
    assert(found_app);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 5: Extract INHERITS relationships
// ============================================================================
void test_extract_inherits() {
    std::cout << "Test 5: Extract INHERITS relationships... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_inherits = false;
    for (const auto& r : model.relationships) {
        if (r.type == "INHERITS" && r.source == "Circle" && r.target == "Shape") {
            found_inherits = true;
        }
    }
    assert(found_inherits);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 6: Extract CALLS relationships
// ============================================================================
void test_extract_calls() {
    std::cout << "Test 6: Extract CALLS relationships... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    // main() calls app::login and app::authenticate
    bool main_calls_login = false, main_calls_auth = false;
    for (const auto& r : model.relationships) {
        if (r.type != "CALLS") continue;
        if (r.source == "main" && r.target == "app::login") main_calls_login = true;
        if (r.source == "main" && r.target == "app::authenticate") main_calls_auth = true;
    }
    assert(main_calls_login);
    assert(main_calls_auth);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 7: Class methods (members)
// ============================================================================
void test_extract_class_methods() {
    std::cout << "Test 7: Extract class methods... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_session_ctor = false, found_session_user = false;
    for (const auto& e : model.entities) {
        if (e.type != "FUNCTION") continue;
        // Methods should be qualified: ClassName::method
        if (e.name == "app::Session::Session") found_session_ctor = true;
        if (e.name == "app::Session::user") found_session_user = true;
    }
    assert(found_session_ctor);
    assert(found_session_user);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 8: Includes (IMPORTS)
// ============================================================================
void test_extract_imports() {
    std::cout << "Test 8: Extract #include IMPORTS... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");

    bool found_iostream = false;
    for (const auto& r : model.relationships) {
        if (r.type == "IMPORTS" && r.target.find("iostream") != std::string::npos) {
            found_iostream = true;
        }
    }
    assert(found_iostream);
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 9: File path preserved
// ============================================================================
void test_file_path_preserved() {
    std::cout << "Test 9: file_path preserved in model... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "src/foo.cpp");
    assert(model.file_path == "src/foo.cpp");
    assert(model.language == "cpp");
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 10: to_json works
// ============================================================================
void test_to_json() {
    std::cout << "Test 10: to_json converts CodeModel to JSON... ";
    CodeModel model = extract_cpp(SAMPLE_CPP, "test.cpp");
    auto j = to_json(model);

    assert(j["file_path"] == "test.cpp");
    assert(j["language"] == "cpp");
    assert(j["entities"].is_array());
    assert(j["relationships"].is_array());
    assert(j["entities"].size() >= 5);  // At least 5 entities
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 11: Empty source produces empty model
// ============================================================================
void test_empty_source() {
    std::cout << "Test 11: Empty source... ";
    CodeModel model = extract_cpp("", "empty.cpp");
    assert(model.entities.empty());
    assert(model.relationships.empty());
    assert(model.language == "cpp");
    std::cout << "PASSED\n";
}

// ============================================================================
// Test 12: Malformed source doesn't crash
// ============================================================================
void test_malformed_source() {
    std::cout << "Test 12: Malformed source handled... ";
    CodeModel model = extract_cpp("class { int x ; } }}}", "bad.cpp");
    // Should not throw, just may extract less
    assert(model.language == "cpp");
    std::cout << "PASSED\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "===========================================\n";
    std::cout << "ApolloCore M3.B Code Extractor Tests\n";
    std::cout << "===========================================\n\n";

    test_language_detection();
    test_extract_functions();
    test_extract_classes();
    test_extract_namespaces();
    test_extract_inherits();
    test_extract_calls();
    test_extract_class_methods();
    test_extract_imports();
    test_file_path_preserved();
    test_to_json();
    test_empty_source();
    test_malformed_source();

    std::cout << "\n===========================================\n";
    std::cout << "ALL M3.B TESTS PASSED ✅\n";
    std::cout << "===========================================\n";

    return 0;
}