#pragma once
#include <string>
#include <vector>

namespace pui {

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action;
    std::string tooltip;

    std::string pvarKey, pvarValue, litImage;
    int litW = 0, litH = 0;
    bool selected = false;
};

struct Placement {
    std::string name, type;
    int x = 0, y = 0, w = 0, h = 0;
};

inline bool button_hit(const Button& b, int x, int y) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

inline std::string tooltip_at(const std::vector<Button>& list, int x, int y) {
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (!it->tooltip.empty() && button_hit(*it, x, y)) return it->tooltip;
    return {};
}

}
