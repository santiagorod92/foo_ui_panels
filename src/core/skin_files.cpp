#include <algorithm>
#include "skin_engine.h"
#include "fs_util.h"
#include "image_cache.h"
#include "log.h"
#include "diagnostics.h"
#include "skin_paths.h"
#include "skin_templates.h"
#include "../panels/track_display.h"
#include "../panels/popup.h"
#include <foobar2000/SDK/cfg_var.h>

namespace pui {

bool SkinEngine::load(const char* script) {
    service_ptr_t<titleformat_object> obj;
    if (!titleformat_compiler::get()->compile(obj, script)) {
        log::console(log::Level::Error, "skin", "skin script failed to compile");
        return false;
    }
    m_script = obj;
    return true;
}

std::string SkinEngine::first_run_setup(const std::string& dir) {
    if (!skins_root().empty() || !active_skin().empty() || dir != component_dir()) return dir;
    SkinConfig cfg;
    cfg.load(dir);
    if (!resolve_main_script(dir, cfg).empty()) return dir;
    const std::string parent = layout_skins_dir();
    std::string err;
    const std::string made = parent.empty() ? std::string() : install_skin_template(skin_templates().front(), parent, &err);
    if (made.empty()) { log::console(log::Level::Error, "skin", "couldn't set up the default layout: " + err); return dir; }
    log::console(log::Level::Note, "skin", "first run, default layout written to " + made);
    set_skins_root(parent);
    set_active_skin(fs_utf8(fs_path(made).filename()));
    m_wizard = true;
    return made;
}

bool SkinEngine::load_skin(const std::string& dirIn) {
    start_logging();
    const std::string dir = first_run_setup(dirIn);
    log::note("skin", "loading skin " + dir);
    m_st.base = dir;
    if (m_main) m_main->set_tray("");
    m_st.cfg.load(dir);
    if (!m_st.cfg.empty()) log::console(log::Level::Info, "skin", std::string("skin config ") + SkinConfig::kFileName + " loaded");
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    seed_pvars();
    std::string why;
    m_mainPath = resolve_main_script(dir, m_st.cfg, &why);
    if (!why.empty()) log::console(log::Level::Warn, "skin", why);
    bool ok = load_main_script();
    m_previewAt = m_wizard ? 0 : tick_ms() + 3000;
    m_previewBy = m_previewAt + 60000;
    m_fileTimes = scan_skin_files();
    m_nextScan = tick_ms() + 1000;
    if (!ok) log::error("skin", "skin loaded with errors: " + dir);
    return ok;
}

bool SkinEngine::load_main_script() {
    if (m_wizard) return load(layout_wizard_script().c_str());
    std::string skin = m_mainPath.empty() ? std::string() : read_file(m_mainPath);
    if (m_mainPath.empty() || skin.empty())
        log::console(log::Level::Warn, "skin", "no main script in " + m_st.base + ", using the built-in test skin");
    else {
        log::console(log::Level::Info, "skin", "main script " + m_mainPath + " (" + std::to_string(skin.size()) + " bytes)");
        diagnose_script(script_label(m_mainPath), skin);
    }
    bool ok = load(skin.empty() ? builtin_test_skin() : skin.c_str());
    if (!ok) {
        log::console(log::Level::Error, "skin", "main script failed to compile: " + m_mainPath);
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
    if (m_mainPath.empty())
        for (const std::string& c : main_script_candidates(m_st.base)) {
            auto t = std::filesystem::last_write_time(fs_path(m_st.base + "/" + c), ec);
            if (!ec) out[m_st.base + "/" + c] = t;
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
    if (m_wizard || m_st.base.empty() || tick_ms() < m_nextScan) return;
    m_nextScan = tick_ms() + 1000;
    FileTimes now = scan_skin_files();
    if (now == m_fileTimes) return;
    std::set<std::string> changed;
    for (auto& [path, t] : now) {
        auto it = m_fileTimes.find(path);
        if (it == m_fileTimes.end() || it->second != t) changed.insert(path);
    }
    for (auto& [path, t] : m_fileTimes) if (!now.count(path)) changed.insert(path);
    m_fileTimes = std::move(now);
    if (changed.empty()) return;

    m_reported.clear(); m_st.imagesChecked.clear();
    bool any = false;
    if (changed.count(m_st.base + "/" + SkinConfig::kFileName) || m_mainPath.empty()) {
        log::console(log::Level::Info, "skin", std::string("reloaded ") + SkinConfig::kFileName);
        m_st.cfg.load(m_st.base);
        seed_pvars();
        std::string why;
        m_mainPath = resolve_main_script(m_st.base, m_st.cfg, &why);
        if (!why.empty()) log::console(log::Level::Warn, "skin", why);
        load_main_script();
        m_fileTimes = scan_skin_files();
        changed.clear();
        for (auto& [path, t] : m_fileTimes) changed.insert(path);
        any = true;
    } else if (changed.count(m_mainPath)) {
        if (load_main_script()) {
            log::console(log::Level::Info, "skin", "reloaded " + script_label(m_mainPath));
            any = true;
        }
    }
    auto panel_file = [&](const std::string& name) { return panels_dir() + "/" + name + ".txt"; };
    for (auto& [name, slot] : m_panels) {
        if (slot.kind != Kind::TrackDisplay || !changed.count(panel_file(name))) continue;
        std::string sc = read_panel_script(name);
        static_cast<TrackDisplay*>(slot.view.get())->set_script(sc.empty() ? nullptr : sc.c_str());
        log::console(log::Level::Info, "skin", "reloaded " + script_label(panel_file(name)));
        any = true;
    }
    if (m_popup && !m_popupFile.empty() && changed.count(panel_file(m_popupFile))) {
        std::string sc = read_panel_script(m_popupFile);
        if (!sc.empty()) {
            m_popup->set_script(sc.c_str());
            if (m_popupHost) m_popupHost->invalidate();
            log::console(log::Level::Info, "skin", "reloaded " + script_label(panel_file(m_popupFile)));
            any = true;
        }
    }
    if (!any) return;
    m_evalcache.clear(); m_subcache.clear(); m_st.tfvars.clear();
    repaint_all();
}

namespace {
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
    destroy_panels();
    set_main_window(w);
    m_reported.clear(); m_st.imagesChecked.clear(); m_problems.clear();
    m_evalcache.clear(); m_subcache.clear(); m_st.tfvars.clear();
    m_st.buttons.clear(); m_st.placements.clear(); m_childShown.clear();
    load_skin(resolve_skin_dir());
    repaint_all();
}

void SkinEngine::show_layout_wizard() {
    m_wizard = true;
    m_previewAt = 0;
    reload_skin();
}

void SkinEngine::close_layout_wizard() {
    if (!m_wizard) return;
    m_wizard = false;
    reload_skin();
}

void SkinEngine::reload_all() {
    const std::vector<SkinEngine*> engines = live();
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
        report_once("panel script not found: " + path);
        m_problems[script_label(path)] = { { "The panel's script is missing: " + script_label(path), true } };
        return {};
    }
    diagnose_script(script_label(path), s);
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
    auto match = [&](const std::string& pat, const std::string& key) {
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

}
