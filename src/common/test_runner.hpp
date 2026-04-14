#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <functional>

namespace test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& fail_count() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int run_all() {
    int passed = 0;
    fail_count() = 0;
    for (auto& tc : registry()) {
        printf("  %-50s", tc.name);
        fflush(stdout);
        tc.fn();
        printf("OK\n");
        passed++;
    }
    printf("\n%d passed, %d failed\n", passed, fail_count());
    return fail_count();
}

} // namespace test

#define TEST(name)                                                          \
    static void test_##name();                                              \
    static ::test::Registrar reg_##name(#name, test_##name);                \
    static void test_##name()

#define ASSERT_TRUE(expr)                                                   \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "\n  FAIL: %s:%d: ASSERT_TRUE(%s)\n",           \
                    __FILE__, __LINE__, #expr);                              \
            ::test::fail_count()++;                                          \
            return;                                                         \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(a, b)                                                     \
    do {                                                                    \
        if ((a) != (b)) {                                                   \
            fprintf(stderr, "\n  FAIL: %s:%d: ASSERT_EQ(%s, %s)\n",         \
                    __FILE__, __LINE__, #a, #b);                             \
            fprintf(stderr, "    lhs=%lld rhs=%lld\n",                      \
                    (long long)(a), (long long)(b));                         \
            ::test::fail_count()++;                                          \
            return;                                                         \
        }                                                                   \
    } while (0)

#define ASSERT_NE(a, b)                                                     \
    do {                                                                    \
        if ((a) == (b)) {                                                   \
            fprintf(stderr, "\n  FAIL: %s:%d: ASSERT_NE(%s, %s)\n",         \
                    __FILE__, __LINE__, #a, #b);                             \
            ::test::fail_count()++;                                          \
            return;                                                         \
        }                                                                   \
    } while (0)
