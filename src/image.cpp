#include "image.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <map>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

namespace pui {

namespace {
ULONG_PTR g_token = 0;
bool g_started = false;
std::map<std::string, std::shared_ptr<Gdiplus::Bitmap>> g_cache;
std::map<std::string, int> g_retryCount; // draw_cover_art: bounded retries for the now-playing-art wait

// Art the centralized now-playing loader delivered, tied to the track it belongs to. Its
// current() lags a track change (still returns the PREVIOUS track's art until the async load
// finishes), so it must never be read blind — see resolve_cover.
std::string g_npPath;
album_art_data_ptr g_npData;
bool g_npRegistered = false;

Gdiplus::Bitmap* load_from_memory(const std::string& key, const void* data, size_t size);

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
    if (g_started) return;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_token, &in, nullptr) == Gdiplus::Ok)
        g_started = true;
    if (!g_npRegistered) {
        g_npRegistered = true;
        now_playing_album_art_notify_manager::get()->add(&g_npNotify);
    }
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Resolve a wildcard path (e.g. C:\Album\*folder*.jpg -> C:\Album\Folder.jpg).
std::string resolve_wildcard(const std::string& p) {
    if (p.find('*') == std::string::npos && p.find('?') == std::string::npos) return p;
    std::string q = p; for (auto& c : q) if (c == '/') c = '\\';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(q.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return p;
    FindClose(h);
    auto slash = q.find_last_of('\\');
    std::string dir = (slash == std::string::npos) ? std::string() : q.substr(0, slash + 1);
    return dir + fd.cFileName;
}

Gdiplus::Bitmap* load(const std::string& rawpath) {
    std::string path = resolve_wildcard(rawpath);
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second.get();
    auto bmp = std::make_shared<Gdiplus::Bitmap>(widen(path).c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) { g_cache[path] = nullptr; return nullptr; }
    g_cache[path] = bmp;
    return bmp.get();
}

// Decode+cache an in-memory image (album art bytes fetched via album_art_manager_v2, which has
// no on-disk path to key a normal load() call). `key` must be a stable, collision-free cache key
// (the caller uses "\x01art:" + track path so it never collides with a real file path).
Gdiplus::Bitmap* load_from_memory(const std::string& key, const void* data, size_t size) {
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.get();
    std::shared_ptr<Gdiplus::Bitmap> bmp;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hg) {
        void* p = GlobalLock(hg);
        if (p) { memcpy(p, data, size); GlobalUnlock(hg); }
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(hg, TRUE, &stream) == S_OK) {
            auto b = std::make_shared<Gdiplus::Bitmap>(stream);
            if (b->GetLastStatus() == Gdiplus::Ok) bmp = b;
            stream->Release(); // owns hg now (TRUE above), releases it too
        } else {
            GlobalFree(hg);
        }
    }
    g_cache[key] = bmp; // cache the miss too, so a track with no art isn't re-queried every paint
    return bmp.get();
}

void blit(HDC dc, Gdiplus::Bitmap* bmp, int x, int y, int w, int h, int alpha, int rotateflip) {
    const int iw = (int)bmp->GetWidth(), ih = (int)bmp->GetHeight();
    if (w <= 0) w = iw;
    if (h <= 0) h = ih;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    Gdiplus::ImageAttributes ia; Gdiplus::ImageAttributes* pia = nullptr;
    if (alpha < 255) {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f;
        cm.m[3][3] = alpha / 255.0f;
        ia.SetColorMatrix(&cm); pia = &ia;
    }

    // RotateFlipType 6 == Rotate180FlipX == vertical mirror (used for reflections).
    // Map via a destination parallelogram so the cached bitmap isn't mutated.
    Gdiplus::Point dest[3];
    if (rotateflip == 6) {
        dest[0] = Gdiplus::Point(x, y + h);   // image top-left -> bottom
        dest[1] = Gdiplus::Point(x + w, y + h);
        dest[2] = Gdiplus::Point(x, y);       // image bottom-left -> top
    } else {
        dest[0] = Gdiplus::Point(x, y);
        dest[1] = Gdiplus::Point(x + w, y);
        dest[2] = Gdiplus::Point(x, y + h);
    }
    g.DrawImage(bmp, dest, 3, 0, 0, iw, ih, Gdiplus::UnitPixel, pia);
}

// Front cover for `track`: `path` on disk if it loads, else the album-art pipeline.
Gdiplus::Bitmap* resolve_cover(const std::string& path, const metadb_handle_ptr& track) {
    ensure_started();
    if (Gdiplus::Bitmap* b = load(path)) return b;
    if (!track.is_valid()) return nullptr;
    std::string key = "\x01" "art:" + std::string(track->get_path());
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.get();

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
    Gdiplus::Bitmap* bmp = data.is_valid() ? load_from_memory(key, data->data(), data->size()) : nullptr;
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

bool draw_image(HDC dc, const std::string& path, int x, int y, int w, int h, int alpha, int rotateflip) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp) return false;
    blit(dc, bmp, x, y, w, h, alpha, rotateflip);
    return true;
}

bool draw_image_part(HDC dc, const std::string& path, int dx, int dy, int dw, int dh,
                     float sx, float sy, float sw, float sh, int alpha) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return false;
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::ImageAttributes ia;
    if (alpha < 255) {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f; cm.m[3][3] = alpha / 255.0f;
        ia.SetColorMatrix(&cm);
    }
    g.DrawImage(bmp, Gdiplus::Rect(dx, dy, dw, dh), sx, sy, sw, sh, Gdiplus::UnitPixel, &ia);
    return true;
}

bool image_natural_size(const std::string& path, int& w, int& h) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp) return false;
    w = (int)bmp->GetWidth(); h = (int)bmp->GetHeight();
    return true;
}

bool draw_image_data(HDC dc, const std::string& key, const void* data, size_t size,
                     int x, int y, int w, int h) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load_from_memory("\x02" + key, data, size);
    if (!bmp) return false;
    blit(dc, bmp, x, y, w, h, 255, 0);
    return true;
}

bool draw_cover_art(HDC dc, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha, int rotateflip) {
    Gdiplus::Bitmap* bmp = resolve_cover(path, track);
    if (!bmp) return false;
    blit(dc, bmp, x, y, w, h, alpha, rotateflip);
    return true;
}

ImageHandle cover_image(const std::string& path, const metadb_handle_ptr& track) {
    return resolve_cover(path, track);
}

ImageHandle data_image(const std::string& key, const void* data, size_t size) {
    ensure_started();
    return load_from_memory("\x02" + key, data, size);
}

void draw_image_strips(HDC dc, ImageHandle img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection) {
    auto* bmp = static_cast<Gdiplus::Bitmap*>(img);
    if (!bmp || x1 - x0 < 1.0f) return;
    const float iw = (float)bmp->GetWidth(), ih = (float)bmp->GetHeight();
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::ImageAttributes ia;
    if (alpha < 255) {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f; cm.m[3][3] = alpha / 255.0f;
        ia.SetColorMatrix(&cm);
    }
    ia.SetWrapMode(Gdiplus::WrapModeTileFlipXY); // no edge bleed between adjacent strips
    const float width = x1 - x0;
    if (!reflection && std::abs(h0 - h1) < 0.5f) { // face-on: one plain draw, no strips
        Gdiplus::RectF dest(x0, yMid - h0 * 0.5f, width, h0);
        g.DrawImage(bmp, dest, 0, 0, iw, ih, Gdiplus::UnitPixel, &ia);
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
            Gdiplus::RectF dest(dxL, yMid - h * 0.5f, dx + 0.6f, h);
            g.DrawImage(bmp, dest, sx, 0, sw, ih, Gdiplus::UnitPixel, &ia);
        } else {
            float yb = yMid + h * 0.5f + 2.0f, hr = h * refl;
            Gdiplus::PointF dest[3] = { {dxL, yb + hr}, {dxL + dx + 0.6f, yb + hr}, {dxL, yb} };
            g.DrawImage(bmp, dest, 3, sx, ih * (1.0f - refl), sw, ih * refl, Gdiplus::UnitPixel, &ia);
        }
    }
}

void fill_gradient_v(HDC dc, int x, int y, int w, int h, COLORREF base) {
    if (w <= 0 || h <= 0) return;
    auto sc = [](int v, double f) { int r = (int)(v * f); return r < 0 ? 0 : (r > 255 ? 255 : r); };
    COLORREF top = RGB(sc(GetRValue(base),1.7), sc(GetGValue(base),1.7), sc(GetBValue(base),1.7));
    COLORREF bot = RGB(sc(GetRValue(base),0.55), sc(GetGValue(base),0.55), sc(GetBValue(base),0.55));
    TRIVERTEX v[2] = {
        { x,     y,     (COLOR16)(GetRValue(top)<<8), (COLOR16)(GetGValue(top)<<8), (COLOR16)(GetBValue(top)<<8), 0 },
        { x + w, y + h, (COLOR16)(GetRValue(bot)<<8), (COLOR16)(GetGValue(bot)<<8), (COLOR16)(GetBValue(bot)<<8), 0 },
    };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void fill_alpha(HDC dc, int x, int y, int w, int h, COLORREF c, int alpha) {
    if (w <= 0 || h <= 0) return;
    HDC md = CreateCompatibleDC(dc);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) { unsigned char* px = (unsigned char*)bits;
        px[0] = GetBValue(c); px[1] = GetGValue(c); px[2] = GetRValue(c); px[3] = 255; }
    HGDIOBJ ob = SelectObject(md, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
    AlphaBlend(dc, x, y, w, h, md, 0, 0, 1, 1, bf);
    SelectObject(md, ob); DeleteObject(bmp); DeleteDC(md);
}

bool image_avg_color(const std::string& path, COLORREF& out) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp) return false;
    Gdiplus::Bitmap tiny(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&tiny);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bmp, Gdiplus::Rect(0, 0, 1, 1),
                0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(), Gdiplus::UnitPixel);
    Gdiplus::Color c;
    if (tiny.GetPixel(0, 0, &c) != Gdiplus::Ok) return false;
    out = RGB(c.GetR(), c.GetG(), c.GetB());
    return true;
}

void images_shutdown() {
    if (g_npRegistered) { now_playing_album_art_notify_manager::get()->remove(&g_npNotify); g_npRegistered = false; }
    g_npData.release();
    g_cache.clear();
    if (g_started) { Gdiplus::GdiplusShutdown(g_token); g_started = false; }
}

} // namespace pui
