// Skin-folder resolution + its persisted settings. The Preferences page writes the override;
// the UI's initial load and the Preferences page both resolve it the same way.
#pragma once
#include <string>
#include <vector>

namespace pui {

// Skins root folder (one subfolder per skin; "" = single-skin mode) and the active skin name.
std::string skins_root();
std::string active_skin();
void set_skins_root(const std::string& utf8);
void set_active_skin(const std::string& name);
// The skins under `root`: its subfolders (no hidden ones), sorted. Empty for root "".
std::vector<std::string> list_skins(const std::string& root);

// The skin folder (main script, foo_ui_panels.ini, panel scripts, art): <root>/<active> if both
// are set and the folder exists, else the component's own folder (next to the binary).
std::string resolve_skin_dir();
// Same, for not-yet-saved root/active values (the Preferences page's pending edits).
std::string resolve_skin_dir_for(const std::string& root, const std::string& active);

// Where the engine keeps the picture of a skin it showed (Preferences' skin picker uses it when
// the skin has no preview of its own): a PNG under the profile folder. "" if unavailable.
std::string skin_preview_cache_path(const std::string& skinDir);

// The Preferences pages' storage (prefs_store.cpp).
struct PrefsBackend;
PrefsBackend& prefs_backend();

// Folder the component binary lives in (UTF-8, no trailing separator). Platform-supplied.
std::string component_dir();

} // namespace pui
