#include "album_art.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <algorithm>

namespace pui {

void AlbumArt::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (W <= 0 || H <= 0) return;
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());
    if (m_engine) m_engine->draw_canvas_snapshot(cv, *host());

    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    gfx::ImagePtr img;
    if (np.is_valid()) {
        // Same folder-image pattern the other cover views use, then the album-art pipeline
        // (embedded art, navidrome:// …); a miss repaints via the engine's art-ready callback.
        pfc::string8 path;
        service_ptr_t<titleformat_object> tf;
        titleformat_compiler::get()->compile_safe(tf, "$replace(%path%,%filename_ext%,*folder*.*)");
        np->format_title(nullptr, path, tf, nullptr);
        img = cover_image(path.get_ptr(), np);
    }
    if (!img && m_engine) {
        const std::string nocover = m_engine->asset("nocover");
        if (!nocover.empty()) img = load_image(nocover);
    }
    if (!img || img->width() <= 0 || img->height() <= 0) return;

    // Fit inside the panel, centred, aspect kept.
    const double s = std::min((double)W / img->width(), (double)H / img->height());
    const float dw = (float)(img->width() * s), dh = (float)(img->height() * s);
    cv.draw_image(*img, gfx::RectF{ (W - dw) / 2, (H - dh) / 2, dw, dh },
                  gfx::RectF{ 0, 0, (float)img->width(), (float)img->height() }, 255, false, gfx::Interp::High);
}

} // namespace pui
