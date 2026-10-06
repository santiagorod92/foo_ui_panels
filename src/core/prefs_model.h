// The Preferences page's logic, shared by both platforms: what is being edited (skin selection,
// main script, zoom, always-on-top, persistent variables, font/accent overrides), what changed
// since the last apply, and applying it. The Windows page (Win32 controls) and the macOS page
// (AppKit) are only views over a PrefsModel — a setting added here shows up on both.
// SDK-free (unit-tested); storage and making changes live go through PrefsBackend
// (prefs_store.cpp implements it on foobar2000's configuration).
#pragma once
#include "../gfx/canvas.h"
#include "pvars.h"
#include <string>
#include <utility>
#include <vector>

namespace pui {

struct PrefsSettings {
    std::string root;    // skins root folder ("" = the single skin next to the component)
    std::string active;  // skin subfolder under root
    std::string main;    // main-script override, a file name in the skin folder ("" = automatic)
    int zoom = 0;        // percent, 0 = automatic
    bool onTop = false;
    PvarMap pvars;       // the whole persistent store, incl. the reserved "_prefs_*" overrides
    bool operator==(const PrefsSettings& o) const {
        return root == o.root && active == o.active && main == o.main && zoom == o.zoom && onTop == o.onTop &&
               serialize_pvars(pvars) == serialize_pvars(o.pvars);
    }
};

struct PrefsBackend {
    virtual ~PrefsBackend() = default;
    virtual PrefsSettings load() = 0;
    // Persist `now` and make it live: open players reload a changed skin, take changed pvars,
    // zoom and always-on-top. `before` is what was stored.
    virtual void store(const PrefsSettings& now, const PrefsSettings& before) = 0;
    virtual std::vector<std::string> list_skins(const std::string& root) = 0;
    // The folder <root>/<active> if it exists, else the component's own.
    virtual std::string skin_dir(const std::string& root, const std::string& active) = 0;
    // A preview the engine saved of a skin it showed (see SkinEngine), "" if none.
    virtual std::string cached_preview(const std::string& skinDir) { (void)skinDir; return {}; }
};

class PrefsModel {
public:
    // Reserved pvars (hidden from the variables list): the overrides.
    static constexpr const char* kFontFaceKey = "_prefs_font_face";
    static constexpr const char* kFontSizeKey = "_prefs_font_size";
    static constexpr const char* kAccentKey = "_prefs_accent_color";
    static bool is_reserved(const std::string& key) { return key.rfind("_prefs_", 0) == 0; }

    explicit PrefsModel(PrefsBackend& b) : m_b(b) {}

    // Reads the stored settings (and the main script's text) as both pending and applied.
    void load();
    bool changed() const;
    // Stores the pending settings (and writes the script if edited); they become the applied ones.
    void apply();
    // Pending settings back to defaults: no skins root, automatic main script and zoom, not on
    // top, no overrides. The variables keep their values (they belong to the skin).
    void reset();
    const PrefsSettings& pending() const { return m_now; }

    // --- skin -----------------------------------------------------------------------------
    const std::vector<std::string>& skins() const { return m_skins; } // under the pending root
    // A new root keeps the active skin if it has one of that name, else takes its first skin.
    void set_root(const std::string& root);
    void set_active(const std::string& skin);
    void set_main(const std::string& file); // "" = automatic
    // A folder someone picked as "the skin": the skin itself (it has a main script or a
    // foo_ui_panels.ini) -> its parent as root, it as active; else a folder of skins -> it as
    // root, its first skin active. Applied with set_root/set_active.
    void choose_skin_folder(const std::string& folder);
    std::string skin_dir() const;
    std::vector<std::string> main_choices() const;     // the skin folder's *.txt
    std::string main_script(std::string* why = nullptr) const; // the path it resolves to, "" = none
    std::string skin_warning() const;                  // "" when the choice is unambiguous
    // An image of the skin for the picker: its own (`preview` in foo_ui_panels.ini, else
    // preview.* / screenshot.* in the folder), else the one the engine saved when it showed it.
    std::string preview_image() const;
    static std::string skin_preview_file(const std::string& skinDir);

    // --- main script text (the Script tab) -------------------------------------------------
    const std::string& script() const { return m_script; }
    const std::string& script_path() const { return m_scriptPath; }
    void set_script(const std::string& text) { m_script = text; }

    // --- window ---------------------------------------------------------------------------
    static const std::vector<int>& zoom_choices(); // index 0 of a picker = automatic
    int zoom_index() const;                        // nearest choice at or below the setting
    void set_zoom_index(int i);
    void set_on_top(bool on) { m_now.onTop = on; }

    // --- overrides ------------------------------------------------------------------------
    std::string font_face() const { return get(kFontFaceKey); }
    std::string font_size() const { return get(kFontSizeKey); }
    void set_font(const std::string& face, const std::string& size);
    bool accent(gfx::Color& out) const; // false = none (the skin's own)
    void set_accent(gfx::Color c);
    void clear_accent();
    void clear_overrides();

    // --- variables ------------------------------------------------------------------------
    std::vector<std::pair<std::string, std::string>> variables() const; // the skin's (not "_…"), sorted
    void set_variable(const std::string& key, const std::string& value);
    // Adds the pvars the main script (as edited) and its panel scripts reference but that have no
    // value yet, so they can be set before the skin first does.
    void rescan_variables();
    static std::vector<std::string> scan_pvar_names(const std::string& script);

private:
    std::string get(const char* k) const { auto it = m_now.pvars.find(k); return it == m_now.pvars.end() ? std::string() : it->second; }
    void refresh_skins();
    void load_script();

    PrefsBackend& m_b;
    PrefsSettings m_now, m_applied;
    std::vector<std::string> m_skins;
    std::string m_script, m_appliedScript, m_scriptPath;
};

} // namespace pui
