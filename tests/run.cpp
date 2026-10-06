#include "test.h"
#include <cstdlib>

int main() {
    int failedCases = 0;
    const bool verbose = std::getenv("T_VERBOSE") != nullptr; // names each case first (finds a hang)
    for (auto& c : t::cases()) {
        if (verbose) { std::fprintf(stderr, "... %s\n", c.name); std::fflush(stderr); }
        const int before = t::failures();
        c.fn();
        if (t::failures() != before) { ++failedCases; std::fprintf(stderr, "FAIL %s\n", c.name); }
    }
    std::printf("%zu tests, %d failed\n", t::cases().size(), failedCases);
    return failedCases ? 1 : 0;
}
