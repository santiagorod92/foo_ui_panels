// A clickable region recorded by the skin script ($button/$textbutton/$imagebutton).
// In its own header so both SkinEngine and the native panels (TrackDisplay) can hold a
// list of them without a circular include.
#pragma once
#include <string>
#include <vector>

namespace pui {

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action; // e.g. "Playback/Random", "PVAR:SET:mini.panels:2", "WINDOWSIZE:..."
    std::string tooltip; // TOOLTIP:"text" / TOOLTIP,text argument; empty = none

    // "Selected" state — one tab of a radio group stays lit while its panel is showing, so the
    // user can tell which one is active without hovering. A group is detected generically: a
    // pvar that two or more buttons in the SAME frame set to *different* values (fooAvA's left
    // `PVAR:SET:showPanel:1..5` and right `PVAR:SET:showPane:1..4` panel tabs) — see
    // SkinEngine::apply_selected_buttons, which fills `selected` in and does the relighting.
    // pvarKey/pvarValue are the PVAR:SET:<key>:<value> action split apart; litImage is the
    // button's themed (2nd) image, drawn back over the normal one while selected, at the size
    // it was drawn with (litW/litH, 0 = the image's natural size). Empty litImage = the button
    // has no themed image, so only the outline is added.
    std::string pvarKey, pvarValue, litImage;
    int litW = 0, litH = 0;
    bool selected = false;
};

// A $panel(name,type,x,y,w,h) placement recorded by the skin script. Also its own header:
// TrackDisplay needs it for the sub-panels its OWN per-panel script can declare (e.g. Display.txt's
// mini.playlist/miniinfo cover overlay) — see SkinEngine::host_child_panel.
struct Placement {
    std::string name, type;
    int x = 0, y = 0, w = 0, h = 0;
};

// Point-in-button test, shared by click dispatch and hover-state tracking.
inline bool button_hit(const Button& b, int x, int y) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// Tooltip of the button under (x,y): the last one drawn (topmost) that has one, else "".
inline std::string tooltip_at(const std::vector<Button>& list, int x, int y) {
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (!it->tooltip.empty() && button_hit(*it, x, y)) return it->tooltip;
    return {};
}

} // namespace pui
