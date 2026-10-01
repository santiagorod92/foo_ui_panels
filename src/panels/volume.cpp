#include "volume.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <string>

namespace pui {

void Volume::set_from_x(int x) {
    const int W = host()->bounds().w;
    if (W <= 0) return;
    float f = (float)x / W; if (f < 0) f = 0; if (f > 1) f = 1;
    playback_control::get()->set_volume(f * 100.0f - 100.0f); // dB: -100..0
    // The canvas redraws the bar, then refresh_bars() repaints us from the new snapshot.
    if (m_engine && m_engine->main_window()) m_engine->main_window()->invalidate();
}

void Volume::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left) return;
    host()->capture_mouse(true);
    set_from_x(e.x);
}

void Volume::on_mouse_move(int x, int, unsigned, bool left_down) { if (left_down) set_from_x(x); }

void Volume::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Left) host()->capture_mouse(false);
}

void Volume::paint(gfx::Canvas& cv) {
    // The skin script draws this bar itself (rail art, fill, knob — from the playback/volume
    // fields); show exactly what the canvas rendered beneath us and only handle the mouse here.
    // The own drawing below is just a fallback until the first canvas snapshot exists.
    if (m_engine && m_engine->draw_canvas_snapshot(cv, *host())) return;
    const int W = cv.width(), H = cv.height();

    float vol = playback_control::get()->get_volume();      // -100..0
    float frac = (vol + 100.0f) / 100.0f; if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int fw = (int)(W * frac);

    gfx::Color accent(0, 140, 220); if (m_engine) m_engine->theme_color(accent);
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color(12, 12, 14)); // dark groove
    // Level bar: the skin's `asset.bar_fill` (else a theme-colour gradient), plus its
    // `asset.volume_knob` centred on the level, at its natural size, if it has one.
    const int bh = 4, by = (H - bh) / 2;
    bool drew = false;
    const std::string fill = m_engine ? m_engine->asset("bar_fill") : std::string();
    if (fw > 0 && !fill.empty()) drew = draw_image(cv, fill, 0, by, fw, bh);
    if (fw > 0 && !drew) fill_gradient_v(cv, 0, by, fw, bh, accent);
    const std::string knob = m_engine ? m_engine->asset("volume_knob") : std::string();
    int kw = 0, kh = 0;
    if (!knob.empty() && image_natural_size(knob, kw, kh))
        draw_image(cv, knob, fw - kw / 2, (H - kh) / 2, 0, 0);
}

} // namespace pui
