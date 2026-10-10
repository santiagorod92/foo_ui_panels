#pragma once
#include <cstddef>
#include <vector>

namespace pui {

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

}
