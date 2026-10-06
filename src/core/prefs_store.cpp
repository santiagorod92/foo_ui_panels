// PrefsBackend on foobar2000's configuration: where the Preferences pages' settings are stored and
// how an applied change reaches the open players. Also the skin preview cache the engine fills.
#include "prefs_model.h"
#include "prefs_store.h"
#include "skin_engine.h"
#include "skin_paths.h"
#include "ui_settings.h"
#include "fs_util.h"
#include <functional>

namespace pui {

std::string skin_preview_cache_path(const std::string& skinDir) {
    pfc::string8 native;
    try { filesystem::g_get_native_path(core_api::get_profile_path(), native); } catch (...) { return {}; }
    if (native.is_empty()) return {};
    std::string name = fs_utf8(fs_path(skinDir).filename());
    for (auto& c : name) if (!isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    char hash[20];
    snprintf(hash, sizeof hash, "%08x", (unsigned)(std::hash<std::string>{}(skinDir) & 0xffffffffu));
    return std::string(native.get_ptr()) + kPathSep + "foo_ui_panels-previews" + kPathSep + name + "-" + hash + ".png";
}

namespace {

class StoreBackend : public PrefsBackend {
public:
    PrefsSettings load() override {
        PrefsSettings s;
        s.root = skins_root();
        s.active = active_skin();
        s.main = main_script_override();
        s.zoom = zoom_setting();
        s.onTop = always_on_top();
        s.pvars = load_all_pvars();
        return s;
    }

    void store(const PrefsSettings& now, const PrefsSettings& before) override {
        set_skins_root(now.root);
        set_active_skin(now.active);
        set_main_script_override(now.main);
        const bool skinChanged = now.root != before.root || now.active != before.active || now.main != before.main;
        // The live engines hold their own copy of the store and would write it back over ours:
        // push the edits through them (set_pvar persists), or straight to the store if none is open.
        if (serialize_pvars(now.pvars) != serialize_pvars(before.pvars)) {
            const PvarMap stored = load_all_pvars();
            for (SkinEngine* e : SkinEngine::live()) {
                for (auto& [k, v] : now.pvars) if (e->get_pvar(k) != v) e->set_pvar(k, v);
                for (auto& [k, v] : stored) if (!now.pvars.count(k)) e->set_pvar(k, "");
            }
            save_all_pvars(now.pvars);
        }
        set_zoom_setting(now.zoom);
        set_always_on_top(now.onTop);
        if (skinChanged) SkinEngine::reload_all();
        apply_view_settings(); // zoom, always on top; repaints (new pvars show too)
    }

    std::vector<std::string> list_skins(const std::string& root) override { return pui::list_skins(root); }
    std::string skin_dir(const std::string& root, const std::string& active) override {
        return resolve_skin_dir_for(root, active);
    }
    std::string cached_preview(const std::string& skinDir) override {
        const std::string p = skin_preview_cache_path(skinDir);
        std::error_code ec;
        return !p.empty() && std::filesystem::is_regular_file(fs_path(p), ec) ? p : std::string();
    }
};

} // namespace

const GUID& prefs_page_guid() {
    // {9C1D9F3A-2B7E-4A6C-9F0D-7E3C5A8B1D40}
    static const GUID g = { 0x9c1d9f3a, 0x2b7e, 0x4a6c, { 0x9f, 0x0d, 0x7e, 0x3c, 0x5a, 0x8b, 0x1d, 0x40 } };
    return g;
}

void show_preferences_page() { ui_control::get()->show_preferences(prefs_page_guid()); }

PrefsBackend& prefs_backend() {
    static StoreBackend b;
    return b;
}

} // namespace pui
