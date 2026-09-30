// macOS implementation details of the ui:: layer shared between mac_view.mm and mac_main.mm.
#pragma once
#include "../../ui/view.h"
#include <memory>

namespace pui::ui::mac {

// A top-level host for `view` with no parent: its NSView becomes the content of the Panels UI
// layout element (the root canvas). native() is the NSView*.
std::unique_ptr<ViewHost> create_root_view(View* view, const ViewOptions& opts);

} // namespace pui::ui::mac
