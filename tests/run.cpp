#include "test.h"

int main() {
    int failedCases = 0;
    for (auto& c : t::cases()) {
        const int before = t::failures();
        c.fn();
        if (t::failures() != before) { ++failedCases; std::fprintf(stderr, "FAIL %s\n", c.name); }
    }
    std::printf("%zu tests, %d failed\n", t::cases().size(), failedCases);
    return failedCases ? 1 : 0;
}
