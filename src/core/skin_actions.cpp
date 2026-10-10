#include <algorithm>
#include "skin_engine.h"
#include "script_util.h"
#include "navidrome_rating_api.h"
#include "ui_settings.h"
#include "../panels/popup.h"
#include "prefs_model.h"
#include "prefs_store.h"
#include "skin_paths.h"
#include "skin_templates.h"
#include "log.h"
#include <cstdio>
#include <cstring>

namespace pui {

class meta_set_filter : public file_info_filter {
public:
    meta_set_filter(const char* field, const char* value) : m_field(field), m_value(value) {}
    bool apply_filter(metadb_handle_ptr, t_filestats, file_info& info) override {
        if (m_value.is_empty() || m_value == "0") info.meta_remove_field(m_field);
        else info.meta_set(m_field, m_value);
        return true;
    }
private:
    pfc::string8 m_field, m_value;
};

void SkinEngine::add_files(const std::vector<std::string>& paths, t_size at) {
    if (paths.empty()) return;
    pfc::list_t<const char*> urls;
    for (auto& p : paths) urls.add_item(p.c_str());
    auto notify = process_locations_notify::create([at](metadb_handle_list_cref items) {
        auto pm = playlist_manager::get();
        t_size pl = pm->get_active_playlist();
        if (pl == pfc_infinite) { pl = pm->create_playlist_autoname(); pm->set_active_playlist(pl); }
        const t_size n = pm->playlist_get_item_count(pl);
        const t_size base = (at == pfc_infinite || at > n) ? n : at;
        pm->playlist_undo_backup(pl);
        pm->playlist_clear_selection(pl);
        pm->playlist_insert_items(pl, base, items, bit_array_true());
    });
    playlist_incoming_item_filter_v2::get()->process_locations_async(
        urls, playlist_incoming_item_filter_v2::op_flag_delay_ui, nullptr, nullptr,
        core_api::get_main_window(), notify);
}

void SkinEngine::show_tray_menu() {
    if (!m_main) return;
    enum { kPlayPause = 1, kStop, kPrev, kNext, kShow, kHide, kExit };
    auto pc = playback_control::get();
    auto item = [](const char* label, int id) { ui::MenuItem m; m.label = label; m.id = id; return m; };
    ui::Menu menu;
    menu.push_back(item(pc->is_playing() && !pc->is_paused() ? "Pause" : "Play", kPlayPause));
    menu.push_back(item("Stop", kStop));
    menu.push_back(item("Previous", kPrev));
    menu.push_back(item("Next", kNext));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Show foobar2000", kShow));
    menu.push_back(item("Hide foobar2000", kHide));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Exit", kExit));
    switch (ui::popup_menu_at_cursor(*m_main, menu)) {
    case kPlayPause: pc->play_or_pause(); break;
    case kStop:      pc->stop(); break;
    case kPrev:      pc->previous(); break;
    case kNext:      pc->next(); break;
    case kShow:      standard_commands::main_activate(); break;
    case kHide:      standard_commands::main_hide(); break;
    case kExit:      standard_commands::main_exit(); break;
    default: break;
    }
}

struct MenuCommands { std::vector<std::string> paths; std::vector<GUID> guids; };
static const MenuCommands& menu_commands() {
    static MenuCommands mc;
    if (!mc.paths.empty()) return mc;
    struct Group { GUID parent; std::string name; };
    struct Less { bool operator()(const GUID& a, const GUID& b) const { return memcmp(&a, &b, sizeof a) < 0; } };
    std::map<GUID, Group, Less> groups;
    for (auto g : mainmenu_group::enumerate()) {
        Group gr{ g->get_parent(), {} };
        mainmenu_group_popup::ptr pop;
        if (g->service_query_t(pop)) { pfc::string8 n; pop->get_display_string(n); gr.name = menu_label(n); }
        groups[g->get_guid()] = gr;
    }
    auto group_path = [&](GUID id) {
        std::string path;
        for (int depth = 0; id != pfc::guid_null && depth < 16; ++depth) {
            auto it = groups.find(id);
            if (it == groups.end()) break;
            if (!it->second.name.empty()) path = it->second.name + (path.empty() ? "" : "/") + path;
            id = it->second.parent;
        }
        return path;
    };
    for (auto p : mainmenu_commands::enumerate()) {
        const std::string prefix = group_path(p->get_parent());
        const t_uint32 n = p->get_command_count();
        for (t_uint32 i = 0; i < n; ++i) {
            pfc::string8 nm; p->get_name(i, nm);
            mc.paths.push_back(prefix.empty() ? menu_label(nm) : prefix + "/" + menu_label(nm));
            mc.guids.push_back(p->get_command(i));
        }
    }
    return mc;
}

static bool run_action(const std::string& action) {
    const MenuCommands& mc = menu_commands();
    const int i = best_menu_match(mc.paths, action);
    if (i >= 0 && mainmenu_commands::g_execute(mc.guids[(size_t)i])) return true;
    log::console(log::Level::Warn, "action", "no command for action '" + action + "'");
    return false;
}

bool SkinEngine::run_button_action(const std::string& action) {
    const std::string a = m_st.cfg.str("action.remap." + action, action);
    auto pc = playback_control::get();
    if (a == "Previous")          { pc->previous(); return true; }
    if (a == "Next")              { pc->next(); return true; }
    if (a == "Stop")              { pc->stop(); return true; }
    if (a == "Play" || a == "Pause" || a == "play" || a == "pause") { pc->play_or_pause(); return true; }
    if (a == "Playback/Random")   { pc->start(playback_control::track_command_rand, false); return true; }

    if (a.compare(0, 9, "PVAR:SET:") == 0) {
        std::string rest = a.substr(9);
        size_t c = rest.find(':');
        if (c != std::string::npos) {
            m_st.pvars[unquote(rest.substr(0, c))] = unquote(rest.substr(c + 1));
            save_pvars();
            repaint_all();
        }
        return true;
    }
    if (a.compare(0, 12, "PVAR:TOGGLE:") == 0) {
        const std::string key = unquote(a.substr(12));
        if (!key.empty()) {
            const std::string cur = pvar_str(key);
            m_st.pvars[key] = (cur.empty() || cur == "0") ? "1" : "0";
            save_pvars();
            repaint_all();
        }
        return true;
    }
    if (a.compare(0, 11, "WINDOWSIZE:") == 0) {
        std::vector<std::string> tok; std::string rest = a.substr(11), cur;
        for (char ch : rest) { if (ch == ':') { tok.push_back(cur); cur.clear(); } else cur += ch; }
        tok.push_back(cur);
        if (tok.size() >= 2) {
            int w = atoi(unquote(tok[0]).c_str());
            int h = atoi(unquote(tok[1]).c_str());
            std::string halign = tok.size() >= 3 ? tok[2] : std::string();
            std::string valign = tok.size() >= 4 ? tok[3] : std::string();
            if (m_main && w > 0 && h > 0) m_main->resize_client(w, h, halign, valign);
        }
        return true;
    }
    if (a.compare(0, 6, "POPUP:") == 0) {
        const std::string file = unquote(a.substr(6));
        std::string sc = read_panel_script(file);
        if (sc.empty()) {
            ui::message_box(nullptr, "Panels UI",
                            "This skin button opens a window whose script is missing:\n" +
                            panels_dir() + "/" + file + ".txt");
            return true;
        }
        m_popupFile = file;
        if (!sc.empty() && m_main) {
            if (!m_popup) m_popup = std::make_unique<PopupView>(this);
            m_popup->set_script(sc.c_str());
            if (m_popupHost) {
                m_popupHost->invalidate();
                m_popupHost->focus();
            } else {
                std::vector<int> sz = m_st.cfg.nums("popup.size." + file);
                if (sz.size() != 2) sz = m_st.cfg.nums("popup.size");
                if (sz.size() != 2 || sz[0] <= 0 || sz[1] <= 0) sz = { 400, 500 };
                std::string title = file;
                if (title.size() > 4 && pfc::stricmp_ascii(title.c_str() + title.size() - 4, ".ava") == 0)
                    title.resize(title.size() - 4);
                for (auto& c : title) if (c == '_') c = ' ';
                m_popupHost = ui::create_popup_window(*m_main, m_popup.get(), sz[0], sz[1], title,
                                                      [this] { m_popupHost.reset(); });
            }
            complete_onboarding();
        }
        return true;
    }
    if (a.compare(0, 6, "ONTOP:") == 0) {
        const std::string v = a.substr(6);
        set_always_on_top(v == "ON" ? true : v == "OFF" ? false : !always_on_top());
        apply_view_settings();
        return true;
    }
    if (a.compare(0, 5, "ZOOM:") == 0) {
        const std::string v = a.substr(5);
        step_zoom(v == "IN" ? +1 : v == "OUT" ? -1 : 0);
        return true;
    }
    if (a == "MINIMODE:TOGGLE") { toggle_mini_mode(); return true; }
    if (a == "PREFERENCES") { show_preferences_page(); return true; }
    if (a.compare(0, 4, "URL:") == 0) { ui::open_url(unquote(a.substr(4))); return true; }
    if (a == "SKIN:CHOOSE_FOLDER") {
        if (!m_main) return true;
        PrefsModel prefs(prefs_backend());
        prefs.load();
        const std::string dir = ui::choose_folder(*m_main, "Choose a skin folder, or a folder with one skin per subfolder",
                                                  prefs.pending().root);
        if (dir.empty()) return true;
        prefs.choose_skin_folder(dir);
        apply_skin_choice(prefs);
        const std::string problem = prefs.chosen_folder_problem(dir);
        if (!problem.empty()) {
            ui::message_box(nullptr, "Panels UI", problem);
            if (prefs.main_choices().size() > 1) show_preferences_page();
        }
        return true;
    }
    if (a == "WIZARD:OPEN") { show_layout_wizard(); return true; }
    if (a == "WIZARD:CLOSE") { close_layout_wizard(); return true; }
    if (a.compare(0, 14, "SKIN:TEMPLATE:") == 0) {
        const SkinTemplate* t = find_skin_template(a.substr(14));
        const std::string parent = layout_skins_dir();
        if (!t || parent.empty()) return true;
        std::string err;
        const std::string dir = install_skin_template(*t, parent, &err);
        if (dir.empty()) { ui::message_box(nullptr, "Panels UI", err); return true; }
        PrefsModel prefs(prefs_backend());
        prefs.load();
        prefs.choose_skin_folder(dir);
        apply_skin_choice(prefs);
        return true;
    }
    if (a == "MENU") {
        if (m_main) m_main->show_main_menu();
        return true;
    }
    if (a == "Previous playlist" || a == "Next playlist") {
        auto pm = playlist_manager::get();
        const t_size n = pm->get_playlist_count();
        if (n) {
            t_size cur = pm->get_active_playlist();
            if (cur == pfc_infinite) cur = 0;
            else cur = (a == "Next playlist") ? (cur + 1) % n : (cur + n - 1) % n;
            pm->set_active_playlist(cur);
        }
        repaint_all();
        return true;
    }
    if (a == "PLAYLISTS-MENU") {
        auto pm = playlist_manager::get();
        const t_size n = pm->get_playlist_count(), active = pm->get_active_playlist();
        ui::Menu m;
        for (t_size i = 0; i < n; ++i) {
            pfc::string8 nm; pm->playlist_get_name(i, nm);
            ui::MenuItem it; it.label = nm.get_ptr(); it.id = (int)(i + 1); it.checked = (i == active);
            m.push_back(it);
        }
        int cmd = m_main ? ui::popup_menu_at_cursor(*m_main, m) : 0;
        if (cmd > 0 && (t_size)cmd <= n) pm->set_active_playlist((t_size)cmd - 1);
        repaint_all();
        return true;
    }
    if (a == "MENUBAR:toggle") {
        int cur = m_st.pvars.count("menubar") ? atoi(m_st.pvars["menubar"].c_str()) : 1;
        int nv = cur ? 0 : 1; m_st.pvars["menubar"] = std::to_string(nv); save_pvars();
        if (m_main) m_main->set_menubar_visible(nv != 0);
        repaint_all();
        return true;
    }
    {
        auto pm = playlist_manager::get();
        const t_size n = pm->playback_order_get_count();
        for (t_size i = 0; i < n; ++i)
            if (stricmp_utf8(pm->playback_order_get_name(i), a.c_str()) == 0) {
                pm->playback_order_set_active(i); repaint_all(); return true;
            }
    }
    if (a.compare(0, 8, "TAG:SET:") == 0) {
        std::string rest = a.substr(8); size_t c = rest.find(':');
        if (c != std::string::npos) {
            std::string field = rest.substr(0, c), value = rest.substr(c + 1);
            metadb_handle_ptr track; playback_control::get()->get_now_playing(track);
            if (track.is_valid()) {
                if (field == "rating") {
                    set_rating(track, atoi(value.c_str()));
                } else {
                    metadb_handle_list list; list.add_item(track);
                    service_ptr_t<file_info_filter> f =
                        new service_impl_t<meta_set_filter>(field.c_str(), value.c_str());
                    metadb_io_v2::get()->update_info_async(
                        list, f, core_api::get_main_window(), 0, nullptr);
                }
            }
        }
        repaint_all();
        return true;
    }
    return run_action(a);
}

bool SkinEngine::handle_click(int x, int y) {
    if (m_problemMarker.contains(x, y)) { open_main_script_editor(); return true; }
    std::vector<std::string> actions;
    for (const auto& b : m_st.buttons) if (button_hit(b, x, y)) actions.push_back(b.action);
    for (const auto& a : actions) run_button_action(a);
    return !actions.empty();
}

bool SkinEngine::update_hover(int x, int y) {
    int before = -1, after = -1;
    for (size_t i = 0; i < m_st.buttons.size(); ++i) {
        if (button_hit(m_st.buttons[i], m_hoverX, m_hoverY)) before = (int)i;
        if (button_hit(m_st.buttons[i], x, y)) after = (int)i;
    }
    m_hoverX = x; m_hoverY = y;
    if (m_main) m_main->set_tooltip(m_problemMarker.contains(x, y) ? problem_tooltip(main_script_label())
                                                                 : tooltip_at(m_st.buttons, x, y));
    return before != after;
}

void SkinEngine::set_rating(const metadb_handle_ptr& track, int stars) {
    if (track.is_empty()) return;
    if (stars < 0) stars = 0; if (stars > 5) stars = 5;

    service_enum_t<navidrome::navidrome_rating_api> e;
    service_ptr_t<navidrome::navidrome_rating_api> api;
    while (e.next(api)) {
        if (api->is_navidrome_track(track)) {
            log::info("rating", "navidrome track rated " + std::to_string(stars));
            api->set_rating_async(track, stars);
            return;
        }
    }

    log::info("rating", std::string("RATING tag set to ") + std::to_string(stars) + " on " + track->get_path());
    char v[8] = ""; if (stars > 0) sprintf(v, "%d", stars);
    metadb_handle_list list; list.add_item(track);
    service_ptr_t<file_info_filter> f = new service_impl_t<meta_set_filter>("RATING", v);
    metadb_io_v2::get()->update_info_async(list, f, core_api::get_main_window(), 0, nullptr);
}

void SkinEngine::apply_skin_choice(PrefsModel& prefs) {
    const bool wasWizard = m_wizard;
    const std::string before = m_st.base;
    m_wizard = false;
    prefs.apply();
    if (wasWizard && m_st.base == before) reload_skin();
}

}
