#define CATCH_CONFIG_MAIN

#include <iostream>
#include <cassert>
#include <cmath>

// Simple test framework
namespace test {
    
struct TestCase {
    const char* name;
    bool (*func)();
};

static TestCase g_tests[256];
static int g_test_count = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

void register_test(const char* name, bool (*func)()) {
    if (g_test_count < 256) {
        g_tests[g_test_count++] = {name, func};
    }
}

#define TEST_CASE(name) \
    static bool test_##name(); \
    static void register_##name() { \
        test::register_test(#name, test_##name); \
    } \
    bool test_##name()

#define RUN_TESTS() \
    int main() { \
        std::cout << "Running ApolloCore Tests\n"; \
        std::cout << "========================\n\n"; \
        \
        for (int i = 0; i < g_test_count; ++i) { \
            std::cout << "Running: " << g_tests[i].name << "... "; \
            try { \
                if (g_tests[i].func()) { \
                    std::cout << "PASSED\n"; \
                    g_tests_passed++; \
                } else { \
                    std::cout << "FAILED\n"; \
                    g_tests_failed++; \
                } \
            } catch (const std::exception& e) { \
                std::cout << "EXCEPTION: " << e.what() << "\n"; \
                g_tests_failed++; \
            } catch (...) { \
                std::cout << "UNKNOWN EXCEPTION\n"; \
                g_tests_failed++; \
            } \
        } \
        \
        std::cout << "\n========================\n"; \
        std::cout << "Results: " << g_tests_passed << " passed, " << g_tests_failed << " failed\n"; \
        return g_tests_failed > 0 ? 1 : 0; \
    }

#define ASSERT_TRUE(x) \
    if (!(x)) { \
        std::cerr << "Assertion failed: " << #x << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::cerr << "Assertion failed: " << #a << " == " << #b << " (" << (a) << " != " << (b) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_NE(a, b) \
    if ((a) == (b)) { \
        std::cerr << "Assertion failed: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_FLOAT_EQ(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::cerr << "Assertion failed: " << #a << " ~= " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_STREQ(a, b) \
    if (std::string(a) != std::string(b)) { \
        std::cerr << "Assertion failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_GT(a, b) \
    if (!((a) > (b))) { \
        std::cerr << "Assertion failed: " << #a << " > " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_GE(a, b) \
    if (!((a) >= (b))) { \
        std::cerr << "Assertion failed: " << #a << " >= " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

#define ASSERT_LT(a, b) \
    if (!((a) < (b))) { \
        std::cerr << "Assertion failed: " << #a << " < " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    }

}

