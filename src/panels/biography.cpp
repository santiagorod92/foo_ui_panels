#include "biography.h"
#include "../core/skin_engine.h"
#include "../core/http_get.h"
#include "../core/list_logic.h"
#include "../core/log.h"
#include "../core/navidrome_link.h"
#include "../core/ui_logic.h"
#include <algorithm>

namespace pui {

static const gfx::FontSpec kTitleFont{ "Segoe UI", 16, false, true };
static const gfx::FontSpec kTextFont{ "Segoe UI", 12, false };
static const int kPad = 10;

gfx::Color Biography::col(const char* role, gfx::Color def) const {
    return m_engine ? m_engine->color("biography", role, def) : def;
}

void Biography::on_destroy() {
    m_alive->store(false);
    m_abort->abort();
}

void Biography::refresh_artist() {
    metadb_handle_ptr np;
    std::string artist;
    if (playback_control::get()->get_now_playing(np) && np.is_valid()) {
        if (m_artistScript.is_empty()) titleformat_compiler::get()->compile_safe(m_artistScript, "[%artist%]");
        pfc::string8 out;
        np->format_title(nullptr, out, m_artistScript, nullptr);
        artist = out.c_str();
    }
    if (artist == m_artist) return;
    m_artist = artist;
    m_scroll = 0;
    if (!artist.empty() && !m_cache.count(artist)) fetch(artist);
}

void Biography::fetch(const std::string& artist) {
    auto api = navidrome_library_as<navidrome::navidrome_library_api_v3>();
    if (api.is_empty()) return;
    m_cache[artist];
    auto alive = m_alive; auto abort = m_abort; Biography* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::artist_info_sink {
            bool found = false;
            std::string bio, image, lastfm;
            void on_artist_info(const char*, const char* b, const char* img, const char* url) override {
                found = true;
                bio = html_to_text(b ? b : "");
                image = img ? img : "";
                lastfm = url ? url : "";
            }
        } sink;
        pfc::string8 err;
        bool ok = false;
        try { ok = api->artist_info(artist.c_str(), sink, *abort, err); } catch (...) { err = "exception"; }
        if (abort->is_aborting()) return;
        if (!ok) log::warn("navidrome", "artist info for '" + artist + "' failed: " + err.c_str());
        std::string bytes;
        if (ok && !sink.image.empty()) http_get(sink.image, bytes, *abort, "biography", 8u << 20);
        if (abort->is_aborting()) return;
        auto info = std::make_shared<Sink>(sink);
        auto data = std::make_shared<std::string>(std::move(bytes));
        fb2k::inMainThread([=]() {
            if (!alive->load()) return;
            Info& i = self->m_cache[artist];
            i.loading = false;
            i.found = info->found;
            i.bio = info->bio; i.image_url = info->image; i.lastfm_url = info->lastfm;
            if (!data->empty()) i.image = gfx::decode_image_memory(data->data(), data->size());
            self->invalidate();
        });
    });
}

void Biography::paint(gfx::Canvas& cv) {
    refresh_artist();
    const int W = cv.width(), H = cv.height();
    const gfx::Color bg = col("background", gfx::Color(22, 22, 26)), text = col("text", gfx::Color(220, 222, 230)),
                     dim = col("text_secondary", gfx::Color(140, 146, 160)), link = col("link", gfx::Color(120, 170, 255));
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, bg);
    m_link = gfx::Rect();
    auto message = [&](const std::string& s) {
        cv.set_font(kTextFont);
        cv.draw_text(s, gfx::Rect{ kPad, kPad, W - 2 * kPad, H - 2 * kPad }, gfx::kAlignCenter | gfx::kVCenter | gfx::kWordWrap, dim);
    };
    if (m_artist.empty()) { message(tr("Nothing is playing.")); return; }
    auto lib = navidrome_library();
    if (lib.is_empty()) { message(tr("Artist information comes from foo_navidrome, which isn't installed.")); return; }
    if (!lib->is_configured()) { message(tr("Navidrome is not configured (Preferences > Tools > Navidrome).")); return; }
    auto it = m_cache.find(m_artist);
    if (it == m_cache.end()) { message(tr("This version of foo_navidrome has no artist information.")); return; }
    const Info& info = it->second;
    if (info.loading) { message(tr("Loading artist information...")); return; }

    int y = kPad - m_scroll;
    const int tw = std::max(10, W - 2 * kPad);
    if (info.image && info.image->width() > 0 && info.image->height() > 0) {
        const float iw = (float)info.image->width(), ih = (float)info.image->height();
        const float maxH = std::max(40.0f, H * 0.45f), s = std::min(tw / iw, maxH / ih);
        const float dw = iw * s, dh = ih * s;
        cv.draw_image(*info.image, gfx::RectF{ kPad + (tw - dw) / 2, (float)y, dw, dh }, gfx::RectF{ 0, 0, iw, ih });
        y += (int)dh + kPad;
    }
    cv.set_font(kTitleFont);
    const int th = cv.text_height(m_artist, tw, gfx::kWordWrap);
    cv.draw_text(m_artist, gfx::Rect{ kPad, y, tw, th }, gfx::kWordWrap, text);
    y += th + kPad / 2;
    cv.set_font(kTextFont);
    const std::string bio = info.found && !info.bio.empty() ? info.bio : std::string(tr("No biography for this artist."));
    const int bh = cv.text_height(bio, tw, gfx::kWordWrap);
    cv.draw_text(bio, gfx::Rect{ kPad, y, tw, bh }, gfx::kWordWrap, info.found ? text : dim);
    y += bh + kPad;
    if (!info.lastfm_url.empty()) {
        const std::string label = tr("Open on Last.fm");
        const int lw = cv.text_width(label), lh = cv.text_height(label, tw, gfx::kSingleLine);
        m_link = gfx::Rect{ kPad, y, std::min(lw, tw), lh };
        cv.draw_text(label, m_link, gfx::kSingleLine, link);
        if (m_overLink) cv.line(m_link.x, m_link.y + m_link.h - 1, m_link.x + m_link.w, m_link.y + m_link.h - 1, link);
        y += lh + kPad;
    }
    m_content = y + m_scroll;
    const ScrollThumb t = scroll_thumb(m_scroll, m_content, H);
    if (t.visible) cv.fill_rect_alpha(gfx::Rect{ W - 5, t.y, 4, t.h }, dim, 200);
}

void Biography::on_wheel(int, int, float notches) {
    m_scroll = pui::clamp_scroll(m_scroll - (int)(notches * 48), m_content, host() ? host()->bounds().h : 0);
    invalidate();
}

void Biography::on_mouse_move(int x, int y, unsigned, bool) {
    const bool over = !m_link.empty() && m_link.contains(x, y);
    if (over == m_overLink) return;
    m_overLink = over;
    host()->set_cursor(over ? ui::Cursor::Hand : ui::Cursor::Arrow);
    invalidate();
}

void Biography::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left || m_link.empty() || !m_link.contains(e.x, e.y)) return;
    auto it = m_cache.find(m_artist);
    if (it != m_cache.end() && !it->second.lastfm_url.empty()) ui::open_url(it->second.lastfm_url);
}

}
