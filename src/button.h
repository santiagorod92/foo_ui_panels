// A clickable region recorded by the skin script ($button/$textbutton/$imagebutton).
// In its own header so both SkinEngine and the native panels (TrackDisplay) can hold a
// list of them without a circular include.
#pragma once
#include <string>

namespace pui {

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action; // e.g. "Playback/Random", "PVAR:SET:mini.panels:2", "WINDOWSIZE:..."
};

} // namespace pui
