#include <cstdio>

#include "test.h"

int main() {
    int failedTests = 0;
    for (const auto& t : pctest::registry()) {
        const int before = pctest::failureCount();
        std::printf("RUN  %s\n", t.name);
        t.fn();
        const bool ok = pctest::failureCount() == before;
        if (!ok) ++failedTests;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", t.name);
    }
    std::printf("%d tests, %d failed\n", static_cast<int>(pctest::registry().size()), failedTests);
    return failedTests == 0 ? 0 : 1;
}
