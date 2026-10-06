// What a right-click on a now-playing cover offers (the Album Art panel, and the Track Display a
// skin draws its cover in): the cover at full size, the image file, the track's folder, a web
// search for the cover. Shared by those panels.
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

// The menu items, with ids from `firstId` on, appended to `menu`. False (nothing added) when
// nothing is playing.
bool add_cover_menu_items(ui::Menu& menu, int firstId);
// Runs the item `id` picked from such a menu; false if it isn't one of them.
bool run_cover_menu_item(SkinEngine* engine, int id, int firstId);

} // namespace pui
