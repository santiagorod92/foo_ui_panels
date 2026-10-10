#include "album_art.h"
#include "cover_menu.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include "../core/fs_util.h"
#include <algorithm>

namespace pui {

static const int kFadeMs = 350;
static const int kFadeTimer = 1;

gfx::ImagePtr AlbumArt::current_image() const {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    gfx::ImagePtr img;
    if (np.is_valid()) {
        static service_ptr_t<titleformat_object> tf;
        if (tf.is_empty()) titleformat_compiler::get()->compile_safe(tf, "$replace(%path%,%filename_ext%,*folder*.*)");
        pfc::string8 path;
        np->format_title(nullptr, path, tf, nullptr);
        img = cover_image(path.get_ptr(), np);
    }
    if (!img && m_engine) {
        const std::string nocover = m_engine->asset("nocover");
        if (!nocover.empty()) img = load_image(nocover);
    }
    if (img && (img->width() <= 0 || img->height() <= 0)) img = nullptr;
    return img;
}

void AlbumArt::on_timer(int id) {
    if (id != kFadeTimer) return;
    if (!m_fadeStart || tick_ms() - m_fadeStart >= (unsigned long long)kFadeMs) {
        m_fadeStart = 0; m_prev.reset();
        host()->kill_timer(kFadeTimer);
    }
    invalidate();
}

static void draw_fitted(gfx::Canvas& cv, const gfx::Image& img, int W, int H, int alpha) {
    const double s = std::min((double)W / img.width(), (double)H / img.height());
    const float dw = (float)(img.width() * s), dh = (float)(img.height() * s);
    cv.draw_image(img, gfx::RectF{ (W - dw) / 2, (H - dh) / 2, dw, dh },
                  gfx::RectF{ 0, 0, (float)img.width(), (float)img.height() }, alpha, false, gfx::Interp::High);
}

void AlbumArt::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (W <= 0 || H <= 0) return;
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());
    if (m_engine) m_engine->draw_canvas_snapshot(cv, *host());

    gfx::ImagePtr img = current_image();
    if (img != m_cur) {
        m_prev = m_cur; m_cur = img;
        m_fadeStart = m_prev || m_cur ? tick_ms() : 0;
        if (m_fadeStart) host()->set_timer(kFadeTimer, 16);
    }
    int alpha = 255;
    if (m_fadeStart) {
        const unsigned long long t = tick_ms() - m_fadeStart;
        alpha = t >= (unsigned long long)kFadeMs ? 255 : (int)(255 * t / kFadeMs);
    }
    if (m_prev && alpha < 255) draw_fitted(cv, *m_prev, W, H, 255 - alpha * (m_cur ? 0 : 1));
    if (m_cur) draw_fitted(cv, *m_cur, W, H, std::max(1, alpha));
}

void AlbumArt::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Right) return;
    ui::Menu m;
    if (!add_cover_menu_items(m, 1)) return;
    run_cover_menu_item(m_engine, ui::popup_menu(host(), e.x, e.y, m), 1);
}

}
