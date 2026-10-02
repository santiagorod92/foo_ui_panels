#include "skin_paths.h"
#include "skin_config.h"
#include "fs_util.h"
#include "../fb2k.h"
#include <algorithm>

namespace pui {

namespace {
// {1F8A6C2E-4D9B-4E71-8C3A-5B2D9E0F6A17} — cfg_string: skins root folder (contains one
// subfolder per skin). Empty = single-skin mode (component's own bundled folder).
const GUID g_skins_root_guid =
    { 0x1f8a6c2e, 0x4d9b, 0x4e71, { 0x8c, 0x3a, 0x5b, 0x2d, 0x9e, 0x0f, 0x6a, 0x17 } };
cfg_var_modern::cfg_string g_skins_root_cfg(g_skins_root_guid, "");

// {2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24} — cfg_string: active skin = subfolder name under the
// root above. Empty (or root empty) = the component's own bundled folder.
const GUID g_active_skin_guid =
    { 0x2a7e4c11, 0x8b3f, 0x4d6a, { 0x9e, 0x52, 0x1c, 0x8f, 0x3a, 0x6b, 0x0d, 0x24 } };
cfg_var_modern::cfg_string g_active_skin_cfg(g_active_skin_guid, "");

// {7B3E1D52-9A4C-4F8B-B1E6-2D5C8A9F0E31} — cfg_string: main script file name override.
const GUID g_main_script_guid =
    { 0x7b3e1d52, 0x9a4c, 0x4f8b, { 0xb1, 0xe6, 0x2d, 0x5c, 0x8a, 0x9f, 0x0e, 0x31 } };
cfg_var_modern::cfg_string g_main_script_cfg(g_main_script_guid, "");
} // namespace

std::string skins_root() { pfc::string8 s = g_skins_root_cfg.get(); return std::string(s.get_ptr(), s.length()); }
std::string active_skin() { pfc::string8 s = g_active_skin_cfg.get(); return std::string(s.get_ptr(), s.length()); }
void set_skins_root(const std::string& utf8) { g_skins_root_cfg.set(utf8.c_str()); }
void set_active_skin(const std::string& name) { g_active_skin_cfg.set(name.c_str()); }

std::vector<std::string> list_skins(const std::string& root) {
    std::vector<std::string> out;
    if (root.empty()) return out;
    std::error_code ec;
    std::filesystem::directory_iterator it(fs_path(root), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const std::string name = fs_utf8(it->path().filename());
        if (it->is_directory(e2) && !name.empty() && name[0] != '.') out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The persisted halves of skin_config.h (kept here so skin_config.cpp stays SDK-free).
std::string main_script_override() {
    pfc::string8 s = g_main_script_cfg.get();
    return std::string(s.get_ptr(), s.length());
}
void set_main_script_override(const std::string& name) { g_main_script_cfg.set(name.c_str()); }

std::string resolve_main_script(const std::string& dir, const SkinConfig& cfg, std::string* why) {
    return resolve_main_script_with(dir, cfg, main_script_override(), why);
}

std::string resolve_skin_dir_for(const std::string& root, const std::string& active) {
    if (!root.empty() && !active.empty()) {
#ifdef _WIN32
        std::string candidate = root + "\\" + active;
#else
        std::string candidate = root + "/" + active;
#endif
        std::error_code ec;
        if (std::filesystem::is_directory(fs_path(candidate), ec)) return candidate;
    }
    return component_dir();
}

std::string resolve_skin_dir() { return resolve_skin_dir_for(skins_root(), active_skin()); }

} // namespace pui
