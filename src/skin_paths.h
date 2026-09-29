// Shared skin-folder resolution — the Preferences page (preferences.cpp) writes the override,
// main.cpp (initial load) and preferences.cpp (page contents) both need to resolve it the same way.
#pragma once
#include <string>

namespace pui {

// The skin folder (fooava.txt + panels/ + images/): the Preferences-page override if the user
// set one, else the component's own folder (historical default — same dir as the DLL).
std::string resolve_skin_dir();

} // namespace pui
