#include "image_cache.h"
#include "builtin_images.h"
#include "fs_util.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <set>
#include <unordered_map>
#include <vector>

namespace pui {

namespace {
using Clock = std::chrono::steady_clock;

struct Entry {
    gfx::ImagePtr img;
    size_t bytes = 0;
    uint64_t used = 0;
    Clock::time_point retry;
};
std::unordered_map<std::string, Entry> g_cache;
size_t g_bytes = 0;
uint64_t g_clock = 0;
constexpr size_t kBudget = 256u << 20;
constexpr auto kMissTtl = std::chrono::seconds(30);
constexpr auto kArtMissTtl = std::chrono::minutes(5);
constexpr auto kNowPlayingRetry = std::chrono::seconds(1);
constexpr int kNowPlayingRetries = 5;
std::map<std::string, int> g_retryCount;

size_t image_bytes(const gfx::ImagePtr& img) {
    return img ? (size_t)img->width() * (size_t)img->height() * 4 : 0;
}

bool cache_get(const std::string& key, gfx::ImagePtr& out) {
    auto it = g_cache.find(key);
    if (it == g_cache.end()) return false;
    Entry& e = it->second;
    if (!e.img && Clock::now() >= e.retry) { g_cache.erase(it); return false; }
    e.used = ++g_clock;
    out = e.img;
    return true;
}

void cache_drop(const std::string& key) {
    auto it = g_cache.find(key);
    if (it == g_cache.end()) return;
    g_bytes -= it->second.bytes;
    g_cache.erase(it);
}

void cache_evict() {
    if (g_bytes <= kBudget) return;
    std::vector<std::pair<uint64_t, const std::string*>> order;
    order.reserve(g_cache.size());
    for (auto& [k, e] : g_cache) if (e.img) order.emplace_back(e.used, &k);
    std::sort(order.begin(), order.end());
    std::vector<std::string> victims;
    size_t bytes = g_bytes;
    for (size_t i = 0; i + 1 < order.size() && bytes > kBudget / 4 * 3; ++i) {
        bytes -= g_cache[*order[i].second].bytes;
        victims.push_back(*order[i].second);
    }
    for (auto& k : victims) cache_drop(k);
}

gfx::ImagePtr cache_put(const std::string& key, gfx::ImagePtr img, Clock::duration missTtl) {
    cache_drop(key);
    Entry e;
    e.img = img;
    e.bytes = image_bytes(img);
    e.used = ++g_clock;
    if (!img) e.retry = Clock::now() + missTtl;
    g_bytes += e.bytes;
    g_cache[key] = std::move(e);
    cache_evict();
    return img;
}

std::function<void()> g_onArtReady;

std::string g_npPath;
album_art_data_ptr g_npData;
bool g_npRegistered = false;

std::string art_key(const metadb_handle_ptr& track) { return "\x01" "art:" + std::string(track->get_path()); }

struct NowPlayingArtNotify : now_playing_album_art_notify {
    void on_album_art(album_art_data::ptr data) override {
        metadb_handle_ptr np;
        if (!data.is_valid() || !playback_control::get()->get_now_playing(np)) return;
        g_npPath = np->get_path();
        g_npData = data;
        std::string key = art_key(np);
        g_retryCount.erase(key);
        if (cache_put(key, gfx::decode_image_memory(data->data(), data->size()), kArtMissTtl) && g_onArtReady)
            g_onArtReady();
    }
};
NowPlayingArtNotify g_npNotify;

std::set<std::string> g_artPending;
int g_artInflight = 0;
constexpr int kMaxArtInflight = 4;
bool g_shutdown = false;

void ensure_started() {
    if (g_npRegistered || g_shutdown) return;
    g_npRegistered = true;
    now_playing_album_art_notify_manager::get()->add(&g_npNotify);
}

void request_art(const std::string& key, const metadb_handle_ptr& track, bool nowPlaying) {
    if (g_shutdown || g_artPending.count(key) || g_artInflight >= kMaxArtInflight) return;
    g_artPending.insert(key);
    ++g_artInflight;
    fb2k::splitTask([key, track, nowPlaying]() {
        album_art_data_ptr data;
        try {
            abort_callback& abort = fb2k::mainAborter();
            pfc::list_t<GUID> ids; ids.add_item(album_art_ids::cover_front);
            auto extractor = album_art_manager_v2::get()->open(pfc::list_single_ref_t(track), ids, abort);
            data = extractor->query(album_art_ids::cover_front, abort);
        } catch (...) {}
        fb2k::inMainThread([key, nowPlaying, data]() {
            if (g_shutdown) return;
            --g_artInflight;
            g_artPending.erase(key);
            gfx::ImagePtr cur;
            if (cache_get(key, cur) && cur) return;
            gfx::ImagePtr img = data.is_valid() ? gfx::decode_image_memory(data->data(), data->size()) : nullptr;
            Clock::duration ttl = kArtMissTtl;
            if (img) g_retryCount.erase(key);
            else if (nowPlaying && ++g_retryCount[key] < kNowPlayingRetries) ttl = kNowPlayingRetry;
            cache_put(key, img, ttl);
            if (g_onArtReady) g_onArtReady();
        });
    });
}

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

}

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

namespace {

gfx::ImagePtr load(const std::string& rawpath) {
    ensure_started();
    gfx::ImagePtr img;
    if (cache_get(rawpath, img)) return img;
    if (rawpath.compare(0, std::char_traits<char>::length(kBuiltinImagePrefix), kBuiltinImagePrefix) == 0) {
        const BuiltinImage* b = find_builtin_image(rawpath.substr(std::char_traits<char>::length(kBuiltinImagePrefix)));
        return cache_put(rawpath, b ? gfx::decode_image_memory(b->data, b->size) : nullptr, kMissTtl);
    }
    std::string path = resolve_wildcard(rawpath);
    img = path.empty() ? nullptr : gfx::decode_image_file(path);
    return cache_put(rawpath, img, kMissTtl);
}

gfx::ImagePtr load_from_memory(const std::string& key, const void* data, size_t size) {
    gfx::ImagePtr img;
    if (cache_get(key, img)) return img;
    img = (data && size) ? gfx::decode_image_memory(data, size) : nullptr;
    return cache_put(key, img, kMissTtl);
}

struct ScaledKey {
    const gfx::Image* src; int w, h;
    bool operator<(const ScaledKey& o) const { return std::tie(src, w, h) < std::tie(o.src, o.w, o.h); }
};
struct Scaled { std::weak_ptr<gfx::Image> src; gfx::ImagePtr img; uint64_t used = 0; };
std::map<ScaledKey, Scaled> g_scaled;
size_t g_scaledBytes = 0;
constexpr size_t kScaledBudget = 96u << 20;
constexpr int kScaledMinArea = 128 * 128;

gfx::ImagePtr scaled_copy(const gfx::ImagePtr& img, int pw, int ph) {
    const ScaledKey key{ img.get(), pw, ph };
    auto it = g_scaled.find(key);
    if (it != g_scaled.end()) {
        if (it->second.src.lock() == img) { it->second.used = ++g_clock; return it->second.img; }
        g_scaledBytes -= image_bytes(it->second.img);
        g_scaled.erase(it);
    }
    gfx::ImagePtr out = gfx::resample_image(*img, pw, ph);
    if (!out) return nullptr;
    g_scaledBytes += image_bytes(out);
    g_scaled[key] = Scaled{ img, out, ++g_clock };
    while (g_scaledBytes > kScaledBudget && g_scaled.size() > 1) {
        auto victim = g_scaled.begin();
        for (auto i = g_scaled.begin(); i != g_scaled.end(); ++i) if (i->second.used < victim->second.used) victim = i;
        g_scaledBytes -= image_bytes(victim->second.img);
        g_scaled.erase(victim);
    }
    return out;
}

void blit(gfx::Canvas& cv, const gfx::ImagePtr& img, int x, int y, int w, int h, int alpha, int rotateflip) {
    if (w <= 0) w = img->width();
    if (h <= 0) h = img->height();
    const gfx::RectF dst{ (float)x, (float)y, (float)w, (float)h };
    const bool flip = rotateflip == 6;
    const double s = cv.device_scale();
    const int pw = (int)std::lround(w * s), ph = (int)std::lround(h * s);
    if ((pw != img->width() || ph != img->height()) && pw * ph >= kScaledMinArea && pw <= 8192 && ph <= 8192) {
        if (gfx::ImagePtr sc = scaled_copy(img, pw, ph)) {
            cv.draw_image(*sc, dst, gfx::RectF{}, alpha, flip, gfx::Interp::Bilinear);
            return;
        }
    }
    cv.draw_image(*img, dst, gfx::RectF{}, alpha, flip);
}

gfx::ImagePtr resolve_cover(const std::string& path, const metadb_handle_ptr& track) {
    if (gfx::ImagePtr b = load(path)) return b;
    if (!track.is_valid()) return nullptr;
    std::string key = art_key(track);
    gfx::ImagePtr img;
    if (cache_get(key, img)) return img;

    metadb_handle_ptr np;
    bool is_now_playing = playback_control::get()->get_now_playing(np) && track == np;
    if (is_now_playing && g_npPath == track->get_path() && g_npData.is_valid())
        return cache_put(key, gfx::decode_image_memory(g_npData->data(), g_npData->size()), kArtMissTtl);
    request_art(key, track, is_now_playing);
    return nullptr;
}
}

void set_art_ready_callback(std::function<void()> cb) { g_onArtReady = std::move(cb); }

gfx::ImagePtr load_image(const std::string& path) { return load(path); }

bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int rotateflip) {
    gfx::ImagePtr img = load(path);
    if (!img) return false;
    blit(cv, img, x, y, w, h, alpha, rotateflip);
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
    blit(cv, img, x, y, w, h, 255, 0);
    return true;
}

bool draw_cover_art(gfx::Canvas& cv, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha, int rotateflip) {
    gfx::ImagePtr img = resolve_cover(path, track);
    if (!img) return false;
    blit(cv, img, x, y, w, h, alpha, rotateflip);
    return true;
}

gfx::ImagePtr cover_image(const std::string& path, const metadb_handle_ptr& track) {
    return resolve_cover(path, track);
}

gfx::ImagePtr data_image(const std::string& key, const void* data, size_t size) {
    return load_from_memory("\x02" + key, data, size);
}

void draw_image_strips(gfx::Canvas& cv, const gfx::Image& img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection) {
    if (x1 - x0 < 1.0f) return;
    const float iw = (float)img.width(), ih = (float)img.height();
    const float width = x1 - x0;
    if (!reflection && std::abs(h0 - h1) < 0.5f) {
        cv.draw_image(img, gfx::RectF{ x0, yMid - h0 * 0.5f, width, h0 }, gfx::RectF{ 0, 0, iw, ih },
                      alpha, false, gfx::Interp::Bilinear);
        return;
    }
    const int n = std::max(1, (int)(width / 3.0f));
    const float dx = width / n;
    const float refl = 0.45f;
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
    g_shutdown = true;
    g_onArtReady = nullptr;
    if (g_npRegistered) { now_playing_album_art_notify_manager::get()->remove(&g_npNotify); g_npRegistered = false; }
    g_npData.release();
    g_cache.clear(); g_bytes = 0;
    g_scaled.clear(); g_scaledBytes = 0;
    gfx::platform_images_shutdown();
}

bool draw_image_region(gfx::Canvas& cv, const gfx::Image& img, int x, int y, int w, int h) {
    int sx = std::max(0, x), sy = std::max(0, y);
    int ex = std::min(img.width(), x + w), ey = std::min(img.height(), y + h);
    if (ex <= sx || ey <= sy) return false;
    cv.draw_image(img, gfx::RectF{ (float)(sx - x), (float)(sy - y), (float)(ex - sx), (float)(ey - sy) },
                  gfx::RectF{ (float)sx, (float)sy, (float)(ex - sx), (float)(ey - sy) });
    return true;
}

}
