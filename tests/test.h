// Minimal test framework for host tests: TEST(name) registers a function, CHECK* macros count failures.
#pragma once
#include <cmath>
#include <cstdio>
#include <vector>

namespace pctest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

// Number of failed checks so far; the runner compares it before and after each test.
inline int& failureCount() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace pctest

#define PCT_CONCAT_(a, b) a##b
#define PCT_CONCAT(a, b) PCT_CONCAT_(a, b)

#define TEST(name)                                                                       \
    static void PCT_CONCAT(pct_test_, name)();                                           \
    static ::pctest::Registrar PCT_CONCAT(pct_reg_, name)(#name, &PCT_CONCAT(pct_test_, name)); \
    static void PCT_CONCAT(pct_test_, name)()

#define PCT_FAIL_(what)                                                                  \
    do {                                                                                 \
        std::printf("    %s:%d: check failed: %s\n", __FILE__, __LINE__, what);          \
        ++::pctest::failureCount();                                                      \
    } while (0)

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) PCT_FAIL_(#cond);                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        if (!((a) == (b))) PCT_FAIL_(#a " == " #b);                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                            \
    do {                                                                                 \
        if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps)))      \
            PCT_FAIL_("|" #a " - " #b "| <= " #eps);                                     \
    } while (0)
