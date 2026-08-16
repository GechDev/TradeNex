#pragma once

#if defined(TRADENEX_USE_GTEST) && __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#else

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

namespace tradenex::test {

struct TestFailure {
    std::string message;
};

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, std::function<void()> func) {
        test_registry().push_back({suite, name, std::move(func)});
    }
};

inline int& total_assertions() {
    static int count = 0;
    return count;
}

inline int& failed_assertions() {
    static int count = 0;
    return count;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

template <typename A, typename B>
void check_equal(const A& a, const B& b, const char* expr_a, const char* expr_b, const char* file, int line, bool fatal) {
    ++total_assertions();
    if (!(a == b)) {
        ++failed_assertions();
        std::ostringstream os;
        os << file << ":" << line << ": Assertion failed: (" << expr_a << " == " << expr_b << ")";
        if constexpr (requires(std::ostream& os) { os << a << b; }) {
            os << " [Expected: " << b << ", Actual: " << a << "]";
        }
        std::cerr << "  [FAIL] " << os.str() << "\n";
        if (fatal) {
            throw TestFailure{os.str()};
        }
    }
}

template <typename A, typename B>
void check_not_equal(const A& a, const B& b, const char* expr_a, const char* expr_b, const char* file, int line, bool fatal) {
    ++total_assertions();
    if (a == b) {
        ++failed_assertions();
        std::ostringstream os;
        os << file << ":" << line << ": Assertion failed: (" << expr_a << " != " << expr_b << ")";
        std::cerr << "  [FAIL] " << os.str() << "\n";
        if (fatal) {
            throw TestFailure{os.str()};
        }
    }
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

inline void check_boolean(bool condition, const char* expr, const char* file, int line, bool expected, bool fatal) {
    ++total_assertions();
    if (condition != expected) {
        ++failed_assertions();
        std::ostringstream os;
        os << file << ":" << line << ": Assertion failed: (" << expr << " is " << (expected ? "true" : "false") << ")";
        std::cerr << "  [FAIL] " << os.str() << "\n";
        if (fatal) {
            throw TestFailure{os.str()};
        }
    }
}

inline void check_near(double a, double b, double tol, const char* expr_a, const char* expr_b, const char* file, int line, bool fatal) {
    ++total_assertions();
    if (std::fabs(a - b) > tol) {
        ++failed_assertions();
        std::ostringstream os;
        os << file << ":" << line << ": Assertion failed: |" << expr_a << " - " << expr_b << "| <= " << tol
           << " [Diff: " << std::fabs(a - b) << "]";
        std::cerr << "  [FAIL] " << os.str() << "\n";
        if (fatal) {
            throw TestFailure{os.str()};
        }
    }
}

inline int run_all_tests(int argc = 0, char** argv = nullptr) {
    std::string filter;
    if (argc > 1 && argv && argv[1]) {
        filter = argv[1];
    }

    int passed = 0;
    int failed = 0;
    const auto& tests = test_registry();

    std::cout << "[==========] Running " << tests.size() << " tests from TradeNex test suite.\n";

    for (const auto& test : tests) {
        std::string full_name = test.suite + "." + test.name;
        if (!filter.empty() && full_name.find(filter) == std::string::npos) {
            continue;
        }

        std::cout << "[ RUN      ] " << full_name << "\n";
        int prev_failures = failed_assertions();
        try {
            test.func();
            if (failed_assertions() == prev_failures) {
                std::cout << "[       OK ] " << full_name << "\n";
                ++passed;
            } else {
                std::cout << "[  FAILED  ] " << full_name << "\n";
                ++failed;
            }
        } catch (const TestFailure& e) {
            std::cout << "[  FAILED  ] " << full_name << " (fatal assertion)\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cerr << "  [EXCEPTION] " << e.what() << "\n";
            std::cout << "[  FAILED  ] " << full_name << "\n";
            ++failed;
        }
    }

    std::cout << "[==========] " << (passed + failed) << " tests ran.\n";
    std::cout << "[  PASSED  ] " << passed << " tests.\n";
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests.\n";
        return 1;
    }
    return 0;
}

}  // namespace tradenex::test

#define TEST(suite, name) \
    static void test_##suite##_##name(); \
    static ::tradenex::test::TestRegistrar registrar_##suite##_##name(#suite, #name, test_##suite##_##name); \
    static void test_##suite##_##name()

#define EXPECT_EQ(a, b) ::tradenex::test::check_equal((a), (b), #a, #b, __FILE__, __LINE__, false)
#define EXPECT_NE(a, b) ::tradenex::test::check_not_equal((a), (b), #a, #b, __FILE__, __LINE__, false)
#define EXPECT_TRUE(c)  ::tradenex::test::check_boolean((c), #c, __FILE__, __LINE__, true, false)
#define EXPECT_FALSE(c) ::tradenex::test::check_boolean((c), #c, __FILE__, __LINE__, false, false)
#define EXPECT_NEAR(a, b, tol) ::tradenex::test::check_near((a), (b), (tol), #a, #b, __FILE__, __LINE__, false)

#define ASSERT_EQ(a, b) ::tradenex::test::check_equal((a), (b), #a, #b, __FILE__, __LINE__, true)
#define ASSERT_NE(a, b) ::tradenex::test::check_not_equal((a), (b), #a, #b, __FILE__, __LINE__, true)
#define ASSERT_TRUE(c)  ::tradenex::test::check_boolean((c), #c, __FILE__, __LINE__, true, true)
#define ASSERT_FALSE(c) ::tradenex::test::check_boolean((c), #c, __FILE__, __LINE__, false, true)

#endif
