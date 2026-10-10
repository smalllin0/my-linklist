#pragma once
#include <cstdio>
#include <cstdlib>

// ======================================================
// 简单测试框架宏
// ======================================================

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("Failed: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        return false; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("Failed: %s (expected %d, got %d) at %s:%d\n", \
               msg, (int)(b), (int)(a), __FILE__, __LINE__); \
        return false; \
    } \
} while (0)

#define TEST_ASSERT_TRUE(cond) TEST_ASSERT((cond), #cond)
#define TEST_ASSERT_FALSE(cond) TEST_ASSERT(!(cond), "!" #cond)

// ======================================================
// 测试用例注册
// ======================================================

using TestFn = bool (*)();
struct TestCase {
    const char* name;
    TestFn      fn;
};

inline int run_all_tests(const TestCase* cases, size_t count) {
    int failed = 0;
    for (size_t i = 0; i < count; ++i) {
        printf("[Run ] %s\n", cases[i].name);
        if (cases[i].fn()) {
            printf("[Pass] %s\n", cases[i].name);
        } else {
            printf("[Fail] %s\n", cases[i].name);
            ++failed;
        }
    }
    printf("\n=== %zu tests, %d failed ===\n", count, failed);
    return failed == 0 ? 0 : 1;
}

#define TEST_SUITE(name, cases) \
int main() { \
    return run_all_tests(cases, sizeof(cases) / sizeof(cases[0])); \
}
