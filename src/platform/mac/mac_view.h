// macOS implementation details of the ui:: layer shared between mac_view.mm and mac_main.mm.
#pragma once
#include "../../ui/view.h"
#include <memory>
#include <string>

namespace pui::ui::mac {

// A top-level host for `view` with no parent: its NSView becomes the content of the Panels UI
// layout element (the root canvas). native() is the NSView*.
std::unique_ptr<ViewHost> create_root_view(View* view, const ViewOptions& opts);
// Zoom of a root view (create_root_view): its bounds become frame / zoom, so the view, its
// subviews (the hosted panels) and the events they get all stay in skin units.
void set_root_zoom(ViewHost& root, double zoom);
// The zoom setting as a factor (automatic = 1: Retina is already the canvas' business).
double zoom_factor();

} // namespace pui::ui::mac

namespace pui::mac {

// Live Panels UI canvases (mac_main.mm), for the Preferences page (mac_preferences.mm): re-resolve
// the skin folder and reload it in every one, or set a persisted variable through their engines
// (written straight to the store when none is open).
void reload_skin_everywhere();
void set_pvar_everywhere(const std::string& key, const std::string& value);

} // namespace pui::mac
