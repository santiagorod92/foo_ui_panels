// foobar2000's Dark Mode for our own plain Win32 windows (the Preferences page). The SDK's
// helpers expect real dialogs: libPPUI's CDarkModeHooks needs ATL/WTL (not in our cross-compile
// toolchain), and the core's fb2k::CCoreDarkModeHooks leaves custom window classes half-light.
// So this is the small subset we need: the core's dark/light state, a palette, the system's
// dark visual styles per control class, and a self-painted tab strip (the stock tab control has
// no dark style).
#pragma once
#include "win_sdk.h"

namespace pui::win::dark {

// What foobar2000 shows: Preferences › Display › Dark mode (Auto follows Windows' app theme).
// Main thread only.
bool enabled();

// Colours (and brushes, owned here) for one mode.
struct Palette {
    COLORREF bg, text, grayText, field, fieldText, border, tabSel;
    HBRUSH bgBrush, fieldBrush;
};
const Palette& palette(bool dark);

// Give a control (by its window class) the system's dark or default visual style.
void theme_control(HWND ctl, bool dark);
// theme_control on every direct child of `parent`.
void theme_children(HWND parent, bool dark);

// Paint a tab control in the palette instead of the stock (light-only) look while `*dark` is
// true; reads the flag on each paint, so toggling it needs only an invalidate.
void subclass_tab(HWND tab, const bool* dark);

// WM_CTLCOLOR* answer for dark mode: colours `dc`, returns the background brush.
LRESULT ctl_color(UINT msg, HDC dc, bool grayText = false);

} // namespace pui::win::dark
