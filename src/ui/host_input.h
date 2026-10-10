#pragma once
#include "view.h"

namespace pui::ui {

inline void start_view(View& v, ViewHost& h) {
    v.attach_host(&h);
    v.on_attached();
}

inline void paint_view(View& v, gfx::Canvas& cv, bool ring_shown) {
    v.paint(cv);
    if (ring_shown) draw_focus_ring(cv);
}

inline bool press_takes_focus(MouseButton b) { return b != MouseButton::Middle; }

inline bool dispatch_key(View& v, bool live, const ViewOptions& opts, int key, unsigned mods) {
    if (live && v.on_key_down(key, mods)) return true;
    if (key == kKeyTab && opts.on_tab) {
        opts.on_tab((mods & kShift) != 0);
        return true;
    }
    return false;
}

}
