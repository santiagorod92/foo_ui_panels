#include "track_display.h"
#include "../core/skin_engine.h"
#include "lyrics_panel.h"
#include "cover_menu.h"

namespace pui {

static const char* kDefaultScript =
    "$font(Segoe UI,15,b)$drawstring([%title%],6,4,%el_width%,22,255-255-255,)"
    "$font(Segoe UI,11,)$drawstring([%artist%][ \xe2\x80\x94 %album%],6,28,%el_width%,18,200-200-210,)"
    "$font(Segoe UI,11,)$drawstring([%playback_time% / %length%],6,48,%el_width%,18,160-200-255,)";

TrackDisplay::TrackDisplay(SkinEngine* engine, std::string name)
    : m_engine(engine), m_name(std::move(name)) {
    set_script(nullptr);
}

void TrackDisplay::on_attached() {
    host()->set_timer(1, 1000);
}

void TrackDisplay::on_timer(int) {
    if (SkinEngine::playback_ticking()) invalidate();
}

void TrackDisplay::set_script(const char* spec) {
    const char* text = spec && *spec ? spec : kDefaultScript;
    if (!titleformat_compiler::get()->compile(m_script, text)) {
        if (m_engine && spec && *spec)
            m_engine->add_script_problem(m_engine->panel_script_label(m_name),
                                         "Doesn't compile (unbalanced parentheses or quotes?)");
        titleformat_compiler::get()->compile_safe(m_script, text);
    }
}

void TrackDisplay::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (!m_engine || !m_engine->draw_canvas_snapshot(cv, *host()))
        cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());

    metadb_handle_ptr track;
    playback_control::get()->get_now_playing(track);
    if (m_engine) m_engine->draw_script(cv, W, H, m_script, track, &m_buttons,
                                        m_hoverX, m_hoverY, &m_childPlacements);
    if (m_engine) m_engine->store_panel_frame(m_name, cv.snapshot(gfx::Rect{ 0, 0, W, H }), host()->bounds());
    m_problemMarker = m_engine ? m_engine->draw_problem_marker(cv, W, H, m_engine->panel_script_label(m_name)) : gfx::Rect{};
    host_children();
}

void TrackDisplay::host_children() {
    if (!m_engine) return;
    const gfx::Rect org = host()->bounds();
    std::set<std::string> shown;
    for (const auto& raw : m_childPlacements) {
        Placement p = m_engine->remap_panel(raw);
        if (p.name == m_name) continue;
        m_engine->host_child_panel(p, org.x, org.y);
        shown.insert(p.name);
    }
    for (const auto& n : m_shownChildren)
        if (!shown.count(n)) m_engine->hide_child_panel(n);
    m_shownChildren.swap(shown);
}

bool TrackDisplay::update_hover(int x, int y) {
    auto hit = [&](int hx, int hy) {
        for (size_t i = 0; i < m_buttons.size(); ++i)
            if (button_hit(m_buttons[i], hx, hy)) return (int)i;
        return -1;
    };
    int before = hit(m_hoverX, m_hoverY);
    m_hoverX = x; m_hoverY = y;
    return hit(x, y) != before;
}

bool TrackDisplay::apply_code(const std::string& utf8) {
    if (!m_engine || m_name.empty()) return false;
    if (!m_engine->save_panel_script(m_name, utf8)) return false;
    std::string sc = m_engine->read_panel_script(m_name);
    set_script(sc.empty() ? nullptr : sc.c_str());
    invalidate();
    return true;
}

void TrackDisplay::open_code_editor() {
    if (!m_engine || m_name.empty() || m_engine->base_dir().empty() || !m_engine->main_window()) return;
    ui::open_text_editor(*m_engine->main_window(), "code:" + m_name, "Edit code - " + m_name,
                         m_engine->read_panel_script_raw(m_name),
                         [this](const std::string& text) {
                             if (apply_code(text)) return true;
                             ui::message_box(host(), "Edit code", "Could not save the panel script.");
                             return false;
                         });
}

void TrackDisplay::on_rclick(int x, int y) {
    for (const auto& child : m_shownChildren)
        if (m_engine && m_engine->is_lyrics_panel(child)) {
            LyricsPanel::show_settings_menu(m_engine, host(), x, y);
            return;
        }
    ui::Menu m;
    enum { kEdit = 1, kCover = 100 };
    if (add_cover_menu_items(m, kCover)) m.push_back(ui::MenuItem::sep());
    ui::MenuItem edit; edit.label = "Edit code..."; edit.id = kEdit; m.push_back(edit);
    const int id = ui::popup_menu(host(), x, y, m);
    if (id == kEdit) open_code_editor();
    else run_cover_menu_item(m_engine, id, kCover);
}

void TrackDisplay::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Left) on_click(e.x, e.y);
}

void TrackDisplay::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Right) on_rclick(e.x, e.y);
}

void TrackDisplay::on_mouse_move(int x, int y, unsigned, bool) {
    if (update_hover(x, y)) invalidate();
    host()->set_tooltip(m_problemMarker.contains(x, y) && m_engine
                            ? m_engine->problem_tooltip(m_engine->panel_script_label(m_name))
                            : tooltip_at(m_buttons, x, y));
}

void TrackDisplay::on_mouse_leave() {
    if (update_hover(-1, -1)) invalidate();
    host()->set_tooltip({});
}

void TrackDisplay::on_visibility(bool shown) {
    if (!shown && m_engine) {
        for (const auto& n : m_shownChildren) m_engine->hide_child_panel(n);
        m_shownChildren.clear();
    }
}

void TrackDisplay::on_destroy() {
    ui::close_text_editor("code:" + m_name);
}

void TrackDisplay::on_click(int x, int y) {
    if (!m_engine) return;
    if (m_problemMarker.contains(x, y)) { open_code_editor(); return; }
    for (const auto& b : m_buttons) {
        if (button_hit(b, x, y)) {
            m_engine->run_button_action(b.action);
            invalidate();
            return;
        }
    }
}

}
