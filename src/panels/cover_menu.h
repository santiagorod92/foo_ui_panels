#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

bool add_cover_menu_items(ui::Menu& menu, int firstId);
bool run_cover_menu_item(SkinEngine* engine, int id, int firstId);

}
