// Player-level settings that aren't per skin: zoom and always-on-top. Persisted in the
// foobar2000 configuration; read by both platforms, the Preferences pages and the
// View > Panels UI menu.
#pragma once

namespace pui {

// Zoom percentage, 0 = automatic (Windows: the system scale; macOS: 100%). See ui_logic.h.
int zoom_setting();
void set_zoom_setting(int pct);
// Zoom in (dir > 0) / out from the zoom in effect (automatic counts as what it resolved to),
// or back to automatic (dir == 0); applied right away.
void step_zoom(int dir);
// The zoom in effect on the player window, in percent (100 when none is open).
int effective_zoom_percent();

// foobar2000's own "always on top" (View > Always on Top), applied to the Panels UI windows
// whenever it changes, from wherever (the core's command, Preferences, a skin button).
bool always_on_top();
void set_always_on_top(bool on);

// Whether panels mark script problems on the skin itself (SkinEngine::draw_problem_marker).
bool show_script_problems();
void set_show_script_problems(bool on); // repaints

// Applies the stored zoom / always-on-top to every open Panels UI window (main thread).
void apply_view_settings();

} // namespace pui
