// The Preferences page's identity, shared by the Windows and macOS pages and by what opens it.
#pragma once
#include "../fb2k.h"

namespace pui {

const GUID& prefs_page_guid();
// Opens foobar2000's Preferences on the Panels UI page.
void show_preferences_page();
// The layout wizard in the player window, brought to the front (the Preferences page's button).
// False when no Panels UI player window is open.
bool open_layout_wizard();

} // namespace pui
