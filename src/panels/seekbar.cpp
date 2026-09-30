#include "seekbar.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <string>

namespace pui {

void Seekbar::on_attached() { host()->set_timer(1, 500); }

void Seekbar::paint(gfx::Canvas& cv) {
    // The skin script draws this bar itself (rail art, fill, knob — from the playback/volume
    // fields); show exactly what the canvas rendered beneath us and only handle the mouse here.
    // The own drawing below is just a fallback until the first canvas snapshot exists.
    if (m_engine && m_engine->draw_canvas_snapshot(cv, *host())) return;
    const int W = cv.width(), H = cv.height();

    auto pc = playback_control::get();
    double len = pc->playback_get_length(), pos = pc->playback_get_position();
    double frac = (len > 0) ? pos / len : 0.0;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int fw = (int)(W * frac);

    std::string base = m_engine ? m_engine->base_dir() : std::string();
    int cb = m_engine ? m_engine->colour_index() : 2;
    gfx::Color accent(0, 140, 220); if (m_engine) m_engine->theme_color(accent);
    // Solid dark groove matching the skin's near-black bottom bar.
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color(12, 12, 14));
    // Played portion: thin themed bar graphic (bar/v{colour.b}.png is a ~3px coloured gloss),
    // vertically centred; fall back to a thin gradient if the image is missing.
    const int bh = 4, by = (H - bh) / 2;
    bool drew = false;
    if (fw > 0 && !base.empty())
        drew = draw_image(cv, base + "/images/fooAVA/bar/v" + std::to_string(cb) + ".png", 0, by, fw, bh);
    if (fw > 0 && !drew) fill_gradient_v(cv, 0, by, fw, bh, accent);
}

void Seekbar::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left) return;
    const int W = host()->bounds().w;
    auto pc = playback_control::get();
    double len = pc->playback_get_length();
    if (len > 0 && W > 0 && pc->playback_can_seek())
        pc->playback_seek(len * e.x / W);
    // The canvas redraws the bar, then refresh_bars() repaints us from the new snapshot.
    if (m_engine && m_engine->main_window()) m_engine->main_window()->invalidate();
}

} // namespace pui
