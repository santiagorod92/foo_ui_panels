#include "seekbar.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include "../core/ui_logic.h"
#include <string>

namespace pui {

void Seekbar::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    auto pc = playback_control::get();
    const double len = pc->playback_get_length();
    if (!m_engine || !m_engine->draw_canvas_snapshot(cv, *host())) {
        double pos = m_dragging ? seek_position_at(m_hoverX, W, len) : pc->playback_get_position();
        double frac = (len > 0) ? pos / len : 0.0;
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        int fw = (int)(W * frac);

        gfx::Color accent(0, 140, 220); if (m_engine) m_engine->theme_color(accent);
        cv.fill_rect(gfx::Rect{ 0, 0, W, H }, m_engine ? m_engine->color("seekbar", "background", gfx::Color(12, 12, 14)) : gfx::Color(12, 12, 14));
        const int bh = 4, by = (H - bh) / 2;
        bool drew = false;
        const std::string fill = m_engine ? m_engine->asset("bar_fill") : std::string();
        if (fw > 0 && !fill.empty()) drew = draw_image(cv, fill, 0, by, fw, bh);
        if (fw > 0 && !drew) fill_gradient_v(cv, 0, by, fw, bh, accent);
    }
    if (m_hoverX >= 0 && m_hoverX < W && len > 0) {
        cv.fill_rect_alpha(gfx::Rect{ m_hoverX - 1, 0, 3, H }, gfx::Color(0, 0, 0), 90);
        cv.fill_rect_alpha(gfx::Rect{ m_hoverX, 0, 1, H }, gfx::Color(255, 255, 255), m_dragging ? 230 : 160);
    }
}

void Seekbar::repaint() {
    invalidate();
    if (m_engine && m_engine->main_window()) m_engine->main_window()->invalidate();
}

void Seekbar::seek_to(double t) {
    auto pc = playback_control::get();
    if (pc->playback_get_length() > 0 && pc->playback_can_seek()) pc->playback_seek(t);
    repaint();
}

void Seekbar::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left) return;
    if (playback_control::get()->playback_get_length() <= 0) return;
    m_dragging = true;
    m_hoverX = e.x;
    host()->capture_mouse(true);
    invalidate();
}

void Seekbar::on_mouse_move(int x, int, unsigned, bool left_down) {
    if (m_dragging && !left_down) m_dragging = false;
    m_hoverX = x;
    const double len = playback_control::get()->playback_get_length();
    host()->set_tooltip(seek_tooltip(seek_position_at(x, host()->bounds().w, len), len));
    invalidate();
}

void Seekbar::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left || !m_dragging) return;
    m_dragging = false;
    host()->capture_mouse(false);
    seek_to(seek_position_at(e.x, host()->bounds().w, playback_control::get()->playback_get_length()));
}

void Seekbar::on_mouse_leave() {
    if (m_dragging) return;
    m_hoverX = -1;
    host()->set_tooltip({});
    invalidate();
}

void Seekbar::on_wheel(int, int, float notches) {
    const int steps = wheel_steps(m_wheel, notches);
    if (!steps) return;
    auto pc = playback_control::get();
    seek_to(seek_by(pc->playback_get_position(), pc->playback_get_length(), steps * 5.0));
}

bool Seekbar::on_key_down(int key, unsigned mods) {
    auto pc = playback_control::get();
    const double pos = pc->playback_get_position(), len = pc->playback_get_length();
    const double step = (mods & ui::kShift) ? 30.0 : 5.0;
    switch (key) {
    case ui::kKeyLeft:  seek_to(seek_by(pos, len, -step)); return true;
    case ui::kKeyRight: seek_to(seek_by(pos, len, step)); return true;
    case ui::kKeyHome:  seek_to(0); return true;
    default: return false;
    }
}

}
