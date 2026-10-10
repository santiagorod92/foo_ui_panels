#pragma once
#include "../../ui/view.h"
#include <memory>
#include <string>

namespace pui::ui::mac {

std::unique_ptr<ViewHost> create_root_view(View* view, const ViewOptions& opts);
void set_root_zoom(ViewHost& root, double zoom);
double zoom_factor();

}
