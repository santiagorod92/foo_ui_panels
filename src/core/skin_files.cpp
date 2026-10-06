#include <algorithm>
#include "skin_engine.h"
#include "fs_util.h"
#include "image_cache.h"
#include "skin_paths.h"
#include "../panels/track_display.h"
#include "../panels/popup.h"
#include <foobar2000/SDK/cfg_var.h>

// SkinEngine: loading a skin, hot reload of its files, panel scripts, and the persistent pvars.

namespace pui {

bool SkinEngine::load(const char* script) {
    service_ptr_t<titleformat_object> obj;
    if (!titleformat_compiler::get()->compile(obj, script)) {
        console::print("Panels UI: skin script failed to compile");
        return false;
    }
    m_script = obj;
    return true;
}

bool SkinEngine::load_skin(const std::string& dir) {
    m_st.base = dir;
    if (m_main) m_main->set_tray(""); // only while the (new) skin keeps asking for it
    m_st.cfg.load(dir);
    if (!m_st.cfg.empty()) console::printf("Panels UI: skin config %s loaded", SkinConfig::kFileName);
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    seed_pvars();
    std::string why;
    m_mainPath = resolve_main_script(dir, m_st.cfg, &why);
    if (!why.empty()) console::printf("Panels UI: %s", why.c_str());
    bool ok = load_main_script();
    m_previewAt = tick_ms() + 3000; // see save_preview()
    m_previewBy = m_previewAt + 60000;
    m_fileTimes = scan_skin_files();
    m_nextScan = tick_ms() + 1000;
    return ok;
}

bool SkinEngine::load_main_script() {
    std::string skin = m_mainPath.empty() ? std::string() : read_file(m_mainPath);
    if (m_mainPath.empty() || skin.empty())
        console::printf("Panels UI: no main script in %s, using the built-in test skin", m_st.base.c_str());
    else {
        console::printf("Panels UI: main script %s (%u bytes)", m_mainPath.c_str(), (unsigned)skin.size());
        diagnose_script(script_label(m_mainPath), skin);
    }
    bool ok = load(skin.empty() ? builtin_test_skin() : skin.c_str());
    if (!ok) {
        console::print("Panels UI: main script failed to compile");
        add_script_problem(script_label(m_mainPath), "Doesn't compile (unbalanced parentheses or quotes?) — the previous version keeps running");
    }
    return ok;
}

std::string SkinEngine::script_label(const std::string& path) const {
    if (path.compare(0, m_st.base.size() + 1, m_st.base + "/") == 0) return path.substr(m_st.base.size() + 1);
    return path;
}

SkinEngine::FileTimes SkinEngine::scan_skin_files() const {
    FileTimes out;
    if (m_st.base.empty()) return out;
    std::error_code ec;
    for (const std::string& f : { m_mainPath, m_st.base + "/" + SkinConfig::kFileName }) {
        if (f.empty()) continue;
        auto t = std::filesystem::last_write_time(fs_path(f), ec);
        if (!ec) out[f] = t;
    }
    const std::string pdir = panels_dir();
    std::filesystem::directory_iterator it(fs_path(pdir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->path().extension() != ".txt") continue;
        auto ft = it->last_write_time(e2);
        if (!e2) out[pdir + "/" + fs_utf8(it->path().filename())] = ft;
    }
    return out;
}

// The skin picker's thumbnail (Preferences): the whole window a few seconds after the skin loaded,
// once its panels have painted and the album art has arrived — preferably while something plays
// (a skin shows itself best with a track), waiting up to a minute for that. Not in mini mode — the
// compact layout would make a poor picture — and not while minimised (the capture fails; tried
// again on the next load).
void SkinEngine::save_preview() {
    if (!m_previewAt || tick_ms() < m_previewAt || !m_main) return;
    if (!playback_control::get()->is_playing() && tick_ms() < m_previewBy) { m_previewAt = tick_ms() + 2000; return; }
    m_previewAt = 0;
    if (in_mini_mode() || m_st.base.empty()) return;
    const std::string path = skin_preview_cache_path(m_st.base);
    gfx::ImagePtr img = path.empty() ? nullptr : m_main->capture();
    if (!img) return;
    std::error_code ec;
    std::filesystem::create_directories(fs_path(path).parent_path(), ec);
    gfx::encode_png_file(*img, path, 480);
}

void SkinEngine::check_skin_changes() {
    save_preview();
    if (m_st.base.empty() || tick_ms() < m_nextScan) return;
    m_nextScan = tick_ms() + 1000;
    FileTimes now = scan_skin_files();
    if (now == m_fileTimes) return;
    std::set<std::string> changed;
    for (auto& [path, t] : now) {
        auto it = m_fileTimes.find(path);
        if (it == m_fileTimes.end() || it->second != t) changed.insert(path);
    }
    for (auto& [path, t] : m_fileTimes) if (!now.count(path)) changed.insert(path); // deleted
    m_fileTimes = std::move(now);
    if (changed.empty()) return;

    m_reported.clear(); m_st.imagesChecked.clear(); // a reload re-reports what's still wrong
    bool any = false;
    if (changed.count(m_st.base + "/" + SkinConfig::kFileName)) {
        // The config can name another main script, art folder, panel folder...: start over.
        console::printf("Panels UI: reloaded %s", SkinConfig::kFileName);
        m_st.cfg.load(m_st.base);
        seed_pvars();
        std::string why;
        m_mainPath = resolve_main_script(m_st.base, m_st.cfg, &why);
        if (!why.empty()) console::printf("Panels UI: %s", why.c_str());
        load_main_script();
        m_fileTimes = scan_skin_files();
        changed.clear();
        for (auto& [path, t] : m_fileTimes) changed.insert(path); // re-read every panel script
        any = true;
    } else if (changed.count(m_mainPath)) {
        if (load_main_script()) {
            console::printf("Panels UI: reloaded %s", script_label(m_mainPath).c_str());
            any = true;
        }
    }
    auto panel_file = [&](const std::string& name) { return panels_dir() + "/" + name + ".txt"; };
    for (auto& [name, slot] : m_panels) {
        if (slot.kind != Kind::TrackDisplay || !changed.count(panel_file(name))) continue;
        std::string sc = read_panel_script(name);
        static_cast<TrackDisplay*>(slot.view.get())->set_script(sc.empty() ? nullptr : sc.c_str());
        console::printf("Panels UI: reloaded %s", script_label(panel_file(name)).c_str());
        any = true;
    }
    if (m_popup && !m_popupFile.empty() && changed.count(panel_file(m_popupFile))) {
        std::string sc = read_panel_script(m_popupFile);
        if (!sc.empty()) {
            m_popup->set_script(sc.c_str());
            if (m_popupHost) m_popupHost->invalidate();
            console::printf("Panels UI: reloaded %s", script_label(panel_file(m_popupFile)).c_str());
            any = true;
        }
    }
    if (!any) return;
    // Snippets cached by text and $puts values belong to the old scripts.
    m_evalcache.clear(); m_subcache.clear(); m_st.tfvars.clear();
    repaint_all();
}

// Persistent pvar store (serialized "key=value" lines).
namespace {
// {1B5C9A40-7E2D-4C8A-9F31-6A0B2D4E8C70}
const GUID g_pvars_guid =
    { 0x1b5c9a40, 0x7e2d, 0x4c8a, { 0x9f, 0x31, 0x6a, 0x0b, 0x2d, 0x4e, 0x8c, 0x70 } };
cfg_var_modern::cfg_string g_pvars_cfg(g_pvars_guid, "");
}

PvarMap load_all_pvars() {
    pfc::string8 data = g_pvars_cfg.get();
    return parse_pvars(std::string(data.get_ptr(), data.length()));
}

void save_all_pvars(const PvarMap& pvars) {
    g_pvars_cfg.set(serialize_pvars(pvars).c_str());
}

void SkinEngine::load_pvars() {
    m_st.pvars = load_all_pvars();
}

// `pvar.once.<name> = <value>`: set once, then left to the user — e.g. a skin turning on a view
// whose original plugin is dead but which a native panel now provides. The reserved
// `_once.<name>` pvar remembers it was done.
void SkinEngine::seed_pvars() {
    bool changed = false;
    for (auto& [name, value] : m_st.cfg.with_prefix("pvar.once.")) {
        std::string& mark = m_st.pvars["_once." + name];
        if (mark == "1") continue;
        m_st.pvars[name] = value; mark = "1"; changed = true;
    }
    if (changed) save_pvars();
}

void SkinEngine::save_pvars() {
    save_all_pvars(m_st.pvars);
}

void SkinEngine::reload_skin() {
    save_pvars();
    ui::MainWindow* w = m_main;
    destroy_panels(); // drops the play callback too; set_main_window brings it back
    set_main_window(w);
    m_reported.clear(); m_st.imagesChecked.clear(); m_problems.clear();
    m_evalcache.clear(); m_subcache.clear(); m_st.tfvars.clear();
    m_st.buttons.clear(); m_st.placements.clear(); m_childShown.clear();
    load_skin(resolve_skin_dir());
    repaint_all();
}

void SkinEngine::reload_all() {
    const std::vector<SkinEngine*> engines = live(); // a reload never adds/removes, but be safe
    for (SkinEngine* e : engines) e->reload_skin();
}

std::string SkinEngine::read_panel_script_raw(const std::string& name) {
    if (m_st.base.empty()) return {};
    return read_file(panels_dir() + "/" + name + ".txt");
}

std::string SkinEngine::read_panel_script(const std::string& name) {
    if (m_st.base.empty()) return {};
    std::string path = panels_dir() + "/" + name + ".txt";
    std::string s = read_file(path);
    if (s.empty() && !file_exists(path)) {
        // Said once: a skin folder without its panel scripts otherwise just shows placeholder
        // panels with no clue why, while the main script loads fine.
        report_once("Panels UI: panel script not found: " + path);
        m_problems[script_label(path)] = { { "The panel's script is missing: " + script_label(path), true } };
        return {};
    }
    diagnose_script(script_label(path), s);
    // `cover.pvar`: set that pvar to the track folder's cover (`cover.pattern`, wildcards
    // resolved by the image loader) before the script runs — for skins whose own cover lookup
    // relied on a plugin that's gone.
    const std::string coverPvar = m_st.cfg.str("cover.pvar");
    if (coverPvar.empty()) return s;
    return "$setpvar(" + coverPvar + ",$replace(%path%,%filename_ext%," +
           m_st.cfg.str("cover.pattern", "*folder*.*") + "))" + s;
}

void SkinEngine::open_main_script_editor() {
    if (!m_main || m_mainPath.empty()) return;
    const std::string label = script_label(m_mainPath);
    ui::open_text_editor(*m_main, "code:main", "Edit code - " + label, read_file(m_mainPath),
                         [this](const std::string& text) {
                             if (!write_file(m_mainPath, text)) {
                                 ui::message_box(nullptr, "Edit code", "Could not save " + m_mainPath);
                                 return false;
                             }
                             // Applied here, not again by the hot reload.
                             std::error_code ec;
                             auto t = std::filesystem::last_write_time(fs_path(m_mainPath), ec);
                             if (!ec) m_fileTimes[m_mainPath] = t;
                             m_reported.clear();
                             load_main_script();
                             m_evalcache.clear(); m_subcache.clear(); m_st.tfvars.clear();
                             repaint_all();
                             return true;
                         });
}

bool SkinEngine::save_panel_script(const std::string& name, const std::string& text) {
    if (m_st.base.empty()) return false;
    const std::string path = panels_dir() + "/" + name + ".txt";
    if (!write_file(path, text)) return false;
    // The editor applies it itself: don't have the hot reload pick it up a second time.
    std::error_code ec;
    auto t = std::filesystem::last_write_time(fs_path(path), ec);
    if (!ec) m_fileTimes[path] = t;
    m_reported.clear();
    return true;
}

int SkinEngine::pvar_int(const std::string& key, int def) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    auto it = m_st.pvars.find(key);
    return (it != m_st.pvars.end() && !it->second.empty()) ? atoi(it->second.c_str()) : def;
}

std::string SkinEngine::pvar_str(const std::string& key) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    auto it = m_st.pvars.find(key);
    return it != m_st.pvars.end() ? it->second : std::string();
}

void SkinEngine::set_pvar(const std::string& key, const std::string& value) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    m_st.pvars[key] = value;
    save_pvars();
}

// The skin's own "first run" onboarding. Legacy Panels UI cleared a skin's first-run flags when
// the user opened its settings popup; nothing in a skin's scripts does (fooAvA blinks its settings
// button at 1Hz while first.boot is 0, forever). Opening any popup counts as "configured": the
// pvars `onboarding.pvars` lists go to 1 — space-separated names, `prefix*` patterns, and
// `!name` exclusions (fooAvA: `first.* !first.cf`, as first.cf=1 means "show the page asking
// to install foo_chronflow", which would replace the native cover flow).
void SkinEngine::complete_onboarding() {
    auto lower = [](std::string t) { for (auto& c : t) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return t; };
    std::vector<std::string> want, skip;
    {
        std::string list = lower(m_st.cfg.str("onboarding.pvars"));
        for (char& c : list) if (c == '\t') c = ' ';
        size_t i = 0;
        while (i < list.size()) {
            size_t j = list.find(' ', i);
            if (j == std::string::npos) j = list.size();
            std::string tok = list.substr(i, j - i);
            if (!tok.empty() && tok[0] == '!') skip.push_back(tok.substr(1));
            else if (!tok.empty()) want.push_back(tok);
            i = j + 1;
        }
    }
    if (want.empty()) return;
    auto match = [&](const std::string& pat, const std::string& key) { // pvar names ignore case
        const std::string k = lower(key);
        if (!pat.empty() && pat.back() == '*') return k.compare(0, pat.size() - 1, pat, 0, pat.size() - 1) == 0;
        return k == pat;
    };
    auto any = [&](const std::vector<std::string>& pats, const std::string& k) {
        for (auto& p : pats) if (match(p, k)) return true;
        return false;
    };
    bool changed = false;
    for (auto& [k, v] : m_st.pvars) {
        if (any(want, k) && !any(skip, k) && v != "1") { v = "1"; changed = true; }
    }
    if (!changed) return;
    save_pvars();
    repaint_all();
}

} // namespace pui
