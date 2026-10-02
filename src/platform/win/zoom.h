// The zoom on Windows: one factor for every Panels UI window (main canvas, panel hosts, popups),
// from the zoom setting — automatic = the main window's monitor DPI / 96. Views and the engine
// work in skin units; the hosts convert to device pixels with to_device()/to_logical()
// (ui_logic.h) and paint through a GdiCanvas created with this scale.
#pragma once
#include "win_sdk.h"

namespace pui::win {

double zoom();               // any thread (the render-thread panels read it)
void refresh_zoom(HWND main); // UI thread: re-read the setting / the window's DPI
UINT window_dpi(HWND w);     // the monitor DPI the window is on (96 if unknown)

} // namespace pui::win
