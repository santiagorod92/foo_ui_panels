#include "test.h"
#include "core/playlist_ops.h"

using namespace pui;
using V = std::vector<size_t>;

TEST("drop_order: one item down, up, and to the ends") {
    const std::vector<bool> sel = { false, true, false, false };
    CHECK(drop_order(sel, 4) == (V{ 0, 2, 3, 1 }));
    CHECK(drop_order(sel, 3) == (V{ 0, 2, 1, 3 }));
    CHECK(drop_order(sel, 0) == (V{ 1, 0, 2, 3 }));
}

TEST("drop_order: dropping onto itself moves nothing") {
    const std::vector<bool> sel = { false, true, false, false };
    CHECK(drop_order(sel, 1).empty());
    CHECK(drop_order(sel, 2).empty());
}

TEST("drop_order: a scattered selection stays in order and gathers") {
    const std::vector<bool> sel = { true, false, true, false };
    CHECK(drop_order(sel, 4) == (V{ 1, 3, 0, 2 }));
    CHECK(drop_order(sel, 2) == (V{ 1, 0, 2, 3 }));
}

TEST("drop_order: out-of-range insertion point clamps; empty lists") {
    CHECK(drop_order({ true, false }, 99) == (V{ 1, 0 }));
    CHECK(drop_order({}, 0).empty());
    CHECK(drop_order({ false, false }, 1).empty());
}
