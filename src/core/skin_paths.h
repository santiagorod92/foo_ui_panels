#pragma once
#include <string>
#include <vector>

namespace pui {

std::string skins_root();
std::string active_skin();
void set_skins_root(const std::string& utf8);
void set_active_skin(const std::string& name);
std::vector<std::string> list_skins(const std::string& root);

std::string resolve_skin_dir();
std::string resolve_skin_dir_for(const std::string& root, const std::string& active);

std::string skin_preview_cache_path(const std::string& skinDir);

struct PrefsBackend;
PrefsBackend& prefs_backend();

std::string layout_skins_dir();

std::string component_dir();

}
