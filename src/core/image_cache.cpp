#include "image_cache.h"
#include "fs_util.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

namespace pui {

namespace {
std::map<std::string, gfx::ImagePtr> g_cache;
std::map<std::string, int> g_retryCount; // draw_cover_art: bounded retries for the now-playing-art wait

// Art the centralized now-playing loader delivered, tied to the track it belongs to. Its
// current() lags a track change (still returns the PREVIOUS track's art until the async load
// finishes), so it must never be read blind — see resolve_cover.
std::string g_npPath;
album_art_data_ptr g_npData;
bool g_npRegistered = false;

gfx::ImagePtr load_from_memory(const std::string& key, const void* data, size_t size);

struct NowPlayingArtNotify : now_playing_album_art_notify {
    void on_album_art(album_art_data::ptr data) override {
        metadb_handle_ptr np;
        if (!data.is_valid() || !playback_control::get()->get_now_playing(np)) return;
        g_npPath = np->get_path();
        g_npData = data;
        // Replace whatever this track cached before (a stale or "no art yet" entry).
        std::string key = "\x01" "art:" + g_npPath;
        g_cache.erase(key);
        g_retryCount.erase(key);
        load_from_memory(key, data->data(), data->size());
    }
};
NowPlayingArtNotify g_npNotify;

void ensure_started() {
    if (g_npRegistered) return;
    g_npRegistered = true;
    now_playing_album_art_notify_manager::get()->add(&g_npNotify);
}

// Case-insensitive '*'/'?' match (ASCII folding, like the Windows file APIs).
bool wild_match(const char* pat, const char* s) {
    auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
    const char *star = nullptr, *resume = nullptr;
    while (*s) {
        if (*pat == '*') { star = pat++; resume = s; }
        else if (*pat == '?' || low(*pat) == low(*s)) { ++pat; ++s; }
        else if (star) { pat = star + 1; s = ++resume; }
        else return false;
    }
    while (*pat == '*') ++pat;
    return !*pat;
}

// Resolve a wildcard in the file-name part (e.g. C:/Album/*folder*.jpg -> C:/Album/Folder.jpg).
std::string resolve_wildcard(const std::string& p) {
    if (p.find('*') == std::string::npos && p.find('?') == std::string::npos) return p;
    std::string q = p; for (auto& c : q) if (c == '\\') c = '/';
    size_t slash = q.find_last_of('/');
    std::string dir = slash == std::string::npos ? std::string(".") : q.substr(0, slash);
    std::string pat = slash == std::string::npos ? q : q.substr(slash + 1);
    std::error_code ec;
    std::filesystem::directory_iterator it(fs_path(dir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::string name = fs_utf8(it->path().filename());
        if (wild_match(pat.c_str(), name.c_str()) && it->is_regular_file(ec))
            return (slash == std::string::npos ? std::string() : q.substr(0, slash + 1)) + name;
    }
    return p;
}

gfx::ImagePtr load(const std::string& rawpath) {
    ensure_started();
    std::string path = resolve_wildcard(rawpath);
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second;
    gfx::ImagePtr img = path.empty() ? nullptr : gfx::decode_image_file(path);
    g_cache[path] = img; // the miss is cached too
    return img;
}

// Decode+cache an in-memory image (album art bytes fetched via album_art_manager_v2, which has
// no on-disk path to key a normal load() call). `key` must be a stable, collision-free cache key
// (the caller uses "\x01art:" + track path so it never collides with a real file path).
gfx::ImagePtr load_from_memory(const std::string& key, const void* data, size_t size) {
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    gfx::ImagePtr img = (data && size) ? gfx::decode_image_memory(data, size) : nullptr;
    g_cache[key] = img; // cache the miss too, so a track with no art isn't re-queried every paint
    return img;
}

void blit(gfx::Canvas& cv, const gfx::Image& img, int x, int y, int w, int h, int alpha, int rotateflip) {
    if (w <= 0) w = img.width();
    if (h <= 0) h = img.height();
    // RotateFlipType 6 == Rotate180FlipX == vertical mirror (used for reflections).
    cv.draw_image(img, gfx::RectF{ (float)x, (float)y, (float)w, (float)h }, gfx::RectF{},
                  alpha, rotateflip == 6);
}

// Front cover for `track`: `path` on disk if it loads, else the album-art pipeline.
gfx::ImagePtr resolve_cover(const std::string& path, const metadb_handle_ptr& track) {
    ensure_started();
    if (gfx::ImagePtr b = load(path)) return b;
    if (!track.is_valid()) return nullptr;
    std::string key = "\x01" "art:" + std::string(track->get_path());
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;

    album_art_data_ptr data;
    // The now-playing art loader is fed live by the active decoder (see SDK album_art.h:
    // "since various components require the album art of the now-playing track, a
    // centralized loader has been provided"), so it can supply art for streaming sources
    // (e.g. foo_navidrome) whose extractor can only answer while actually decoding, not on
    // a cold out-of-band per-file query. Only valid when `track` really is the playing item,
    // and only once the loader's notification (NowPlayingArtNotify) has tied its art to it.
    metadb_handle_ptr np;
    bool is_now_playing = playback_control::get()->get_now_playing(np) && track == np;
    if (is_now_playing && g_npPath == track->get_path()) data = g_npData;
    if (!data.is_valid()) {
        try {
            abort_callback_impl ab;
            pfc::list_t<GUID> ids; ids.add_item(album_art_ids::cover_front);
            auto extractor = album_art_manager_v2::get()->open(pfc::list_single_ref_t(track), ids, ab);
            data = extractor->query(album_art_ids::cover_front, ab);
        } catch (...) {}
    }
    gfx::ImagePtr bmp;
    if (data.is_valid()) {
        bmp = gfx::decode_image_memory(data->data(), data->size());
        if (bmp) g_cache[key] = bmp;
    }
    // A miss for the now-playing track might just mean the now_playing loader hasn't finished
    // its async load yet — retry a few repaints (~5s, TrackDisplay's 1s timer) before giving
    // up, so we don't hammer a (possibly remote) extractor every repaint for the rest of
    // playback. A non-playing track (e.g. a playlist thumbnail) has no live source to wait on,
    // so its miss is cached as permanent immediately.
    if (!bmp) {
        if (!is_now_playing || ++g_retryCount[key] >= 5) g_cache[key] = nullptr;
    } else {
        g_retryCount.erase(key);
    }
    return bmp;
}
} // namespace

gfx::ImagePtr load_image(const std::string& path) { return load(path); }

bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int rotateflip) {
    gfx::ImagePtr img = load(path);
    if (!img) return false;
    blit(cv, *img, x, y, w, h, alpha, rotateflip);
    return true;
}

bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                     float sx, float sy, float sw, float sh, int alpha) {
    gfx::ImagePtr img = load(path);
    if (!img || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return false;
    cv.draw_image(*img, gfx::RectF{ (float)dx, (float)dy, (float)dw, (float)dh },
                  gfx::RectF{ sx, sy, sw, sh }, alpha);
    return true;
}

bool image_natural_size(const std::string& path, int& w, int& h) {
    gfx::ImagePtr img = load(path);
    if (!img) return false;
    w = img->width(); h = img->height();
    return true;
}

bool draw_image_data(gfx::Canvas& cv, const std::string& key, const void* data, size_t size,
                     int x, int y, int w, int h) {
    gfx::ImagePtr img = load_from_memory("\x02" + key, data, size);
    if (!img) return false;
    blit(cv, *img, x, y, w, h, 255, 0);
    return true;
}

bool draw_cover_art(gfx::Canvas& cv, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha, int rotateflip) {
    gfx::ImagePtr img = resolve_cover(path, track);
    if (!img) return false;
    blit(cv, *img, x, y, w, h, alpha, rotateflip);
    return true;
}

gfx::ImagePtr cover_image(const std::string& path, const metadb_handle_ptr& track) {
    return resolve_cover(path, track);
}

gfx::ImagePtr data_image(const std::string& key, const void* data, size_t size) {
    ensure_started();
    return load_from_memory("\x02" + key, data, size);
}

void draw_image_strips(gfx::Canvas& cv, const gfx::Image& img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection) {
    if (x1 - x0 < 1.0f) return;
    const float iw = (float)img.width(), ih = (float)img.height();
    const float width = x1 - x0;
    if (!reflection && std::abs(h0 - h1) < 0.5f) { // face-on: one plain draw, no strips
        cv.draw_image(img, gfx::RectF{ x0, yMid - h0 * 0.5f, width, h0 }, gfx::RectF{ 0, 0, iw, ih },
                      alpha, false, gfx::Interp::Bilinear);
        return;
    }
    const int n = std::max(1, (int)(width / 3.0f));
    const float dx = width / n;
    const float refl = 0.45f; // portion of the cover mirrored below it
    for (int i = 0; i < n; ++i) {
        float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        float h = h0 + (h1 - h0) * ((t0 + t1) * 0.5f);
        float sx = iw * t0, sw = iw * (t1 - t0);
        float dxL = x0 + dx * i;
        if (!reflection) {
            cv.draw_image(img, gfx::RectF{ dxL, yMid - h * 0.5f, dx + 0.6f, h }, gfx::RectF{ sx, 0, sw, ih },
                          alpha, false, gfx::Interp::Bilinear);
        } else {
            float yb = yMid + h * 0.5f + 2.0f, hr = h * refl;
            cv.draw_image(img, gfx::RectF{ dxL, yb, dx + 0.6f, hr },
                          gfx::RectF{ sx, ih * (1.0f - refl), sw, ih * refl },
                          alpha, true, gfx::Interp::Bilinear);
        }
    }
}

void fill_gradient_v(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color base) {
    if (w <= 0 || h <= 0) return;
    auto sc = [](int v, double f) { int r = (int)(v * f); return r < 0 ? 0 : (r > 255 ? 255 : r); };
    gfx::Color top(sc(base.r, 1.7), sc(base.g, 1.7), sc(base.b, 1.7));
    gfx::Color bot(sc(base.r, 0.55), sc(base.g, 0.55), sc(base.b, 0.55));
    cv.gradient_v(gfx::Rect{ x, y, w, h }, top, bot);
}

void fill_alpha(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color c, int alpha) {
    if (w <= 0 || h <= 0) return;
    cv.fill_rect_alpha(gfx::Rect{ x, y, w, h }, c, alpha);
}

bool image_avg_color(const std::string& path, gfx::Color& out) {
    gfx::ImagePtr img = load(path);
    if (!img) return false;
    out = img->average_color();
    return true;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(fs_path(path), ec);
}

void images_shutdown() {
    if (g_npRegistered) { now_playing_album_art_notify_manager::get()->remove(&g_npNotify); g_npRegistered = false; }
    g_npData.release();
    g_cache.clear();
    gfx::platform_images_shutdown();
}

} // namespace pui
