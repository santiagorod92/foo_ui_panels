// What every platform ViewHost (WinViewHost, MacViewHost) does with a view, in one place: how
// it is started, painted and fed input. The hosts only translate native events into these calls,
// so the behaviour a panel sees is the same on both platforms.
#pragma once
#include "view.h"

namespace pui::ui {

// Hooks the view to its host, then lets it start (timers, callbacks).
inline void start_view(View& v, ViewHost& h) {
    v.attach_host(&h);
    v.on_attached();
}

// The view, then the keyboard-focus ring when it is on and the view holds the focus.
inline void paint_view(View& v, gfx::Canvas& cv, bool ring_shown) {
    v.paint(cv);
    if (ring_shown) draw_focus_ring(cv);
}

// A left or right press moves the keyboard focus to the view — clicking a panel is what says
// "its keys go here" (playlist Ctrl+A / Delete) — and hides the focus ring, since the mouse is
// in use. Middle clicks leave the focus alone.
inline bool press_takes_focus(MouseButton b) { return b != MouseButton::Middle; }

// A key press: the view first; an unused Tab / Shift+Tab then moves the focus on
// (ViewOptions::on_tab). False = nobody took it: the host passes it on (keyboard shortcuts on
// Windows, the responder chain on macOS).
inline bool dispatch_key(View& v, bool live, const ViewOptions& opts, int key, unsigned mods) {
    if (live && v.on_key_down(key, mods)) return true;
    if (key == kKeyTab && opts.on_tab) {
        opts.on_tab((mods & kShift) != 0);
        return true;
    }
    return false;
}

} // namespace pui::ui
