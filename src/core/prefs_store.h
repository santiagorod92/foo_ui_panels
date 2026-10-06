// The Preferences page's identity, shared by the Windows and macOS pages and by what opens it.
#pragma once
#include "../fb2k.h"

namespace pui {

const GUID& prefs_page_guid();
// Opens foobar2000's Preferences on the Panels UI page.
void show_preferences_page();

} // namespace pui
