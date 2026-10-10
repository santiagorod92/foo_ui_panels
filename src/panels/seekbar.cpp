#include "seekbar.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <string>

namespace pui {

void Seekbar::paint(gfx::Canvas& cv) {
    if (m_engine && m_engine->draw_canvas_snapshot(cv, *host())) return;
    const int W = cv.width(), H = cv.height();

    auto pc = playback_control::get();
    double len = pc->playback_get_length(), pos = pc->playback_get_position();
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

void Seekbar::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left) return;
    const int W = host()->bounds().w;
    auto pc = playback_control::get();
    double len = pc->playback_get_length();
    if (len > 0 && W > 0 && pc->playback_can_seek())
        pc->playback_seek(len * e.x / W);
    if (m_engine && m_engine->main_window()) m_engine->main_window()->invalidate();
}

}
