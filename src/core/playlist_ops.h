// Pure playlist arithmetic shared by the native playlist panel and the unit tests (`make test`).
// No foobar2000 SDK here.
#pragma once
#include <cstddef>
#include <vector>

namespace pui {

// Drag-to-reorder: the permutation (order[new position] = old index, as
// playlist_manager::reorder_items takes it) that moves the selected items, keeping their relative
// order, to insertion point `at` (0..n, in pre-move indices) — unselected items before `at`, the
// selection, then the remaining unselected items. Empty when nothing would move.
inline std::vector<size_t> drop_order(const std::vector<bool>& selected, size_t at) {
    const size_t n = selected.size();
    if (at > n) at = n;
    std::vector<size_t> order;
    order.reserve(n);
    for (size_t i = 0; i < at; ++i) if (!selected[i]) order.push_back(i);
    for (size_t i = 0; i < n; ++i) if (selected[i]) order.push_back(i);
    for (size_t i = at; i < n; ++i) if (!selected[i]) order.push_back(i);
    for (size_t i = 0; i < n; ++i) if (order[i] != i) return order;
    return {};
}

} // namespace pui
