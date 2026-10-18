#include "ui_logic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace pui {

static const int kZoomPresets[] = { 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };

int clamp_zoom(int pct) { return std::max(kZoomMin, std::min(kZoomMax, pct)); }

int zoom_step(int pct, int dir) {
    if (dir > 0) {
        for (int p : kZoomPresets) if (p > pct) return p;
        return std::max(pct, kZoomPresets[std::size(kZoomPresets) - 1]);
    }
    if (dir < 0) {
        for (int i = (int)std::size(kZoomPresets) - 1; i >= 0; --i) if (kZoomPresets[i] < pct) return kZoomPresets[i];
        return std::min(pct, kZoomPresets[0]);
    }
    return pct;
}

double zoom_factor_for(int setting_pct, int system_dpi) {
    if (setting_pct <= 0) return (system_dpi > 0 ? system_dpi : 96) / 96.0;
    return clamp_zoom(setting_pct) / 100.0;
}

int to_device(int logical, double f) { return (int)std::lround(logical * f); }
int to_logical(int device, double f) { return f > 0 ? (int)std::floor(device / f) : device; }

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_actions(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t semi = s.find(';', start);
        std::string part = trim(s.substr(start, semi == std::string::npos ? std::string::npos : semi - start));
        if (!part.empty()) out.push_back(part);
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return out;
}

void parse_anchor(const std::string& s, std::string& halign, std::string& valign) {
    halign.clear(); valign.clear();
    std::vector<std::string> tok;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == ':' || c == ',') { if (!cur.empty()) tok.push_back(cur); cur.clear(); }
        else cur += (char)toupper((unsigned char)c);
    }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() > 0) halign = tok[0];
    if (tok.size() > 1) valign = tok[1];
}

void name_hash128(const std::string& name, uint8_t out[16]) {
    uint64_t a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    for (unsigned char c : name) {
        a = (a ^ c) * 0x100000001b3ull;
        b = (b ^ (unsigned char)(c + 0x5b)) * 0x100000001b3ull;
    }
    for (int i = 0; i < 8; ++i) { out[i] = (uint8_t)(a >> (i * 8)); out[8 + i] = (uint8_t)(b >> (i * 8)); }
}

MiniPlan mini_mode_plan(int curW, int curH, int miniW, int miniH, int savedW, int savedH) {
    MiniPlan p;
    if (miniW <= 0 || miniH <= 0) return p;
    if (curW == miniW && curH == miniH) {
        if (savedW <= 0 || savedH <= 0 || (savedW == miniW && savedH == miniH)) return p;
        p.act = true; p.enter = false; p.w = savedW; p.h = savedH;
        return p;
    }
    p.act = true; p.enter = true; p.w = miniW; p.h = miniH;
    return p;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool filter_matches(const std::string& filter, const std::string& text) {
    const std::string hay = lower(text);
    std::string word;
    auto ok = [&](const std::string& w) { return w.empty() || hay.find(w) != std::string::npos; };
    for (char c : lower(filter)) {
        if (c == ' ') { if (!ok(word)) return false; word.clear(); }
        else word += c;
    }
    return ok(word);
}

AlbumSort album_sort_from(const std::string& s) {
    const std::string l = lower(s);
    if (l == "album") return AlbumSort::Album;
    if (l == "year") return AlbumSort::Year;
    if (l == "decade") return AlbumSort::Decade;
    if (l == "added") return AlbumSort::Added;
    return AlbumSort::Artist;
}

const char* album_sort_name(AlbumSort s) {
    switch (s) {
    case AlbumSort::Album: return "album";
    case AlbumSort::Year: return "year";
    case AlbumSort::Decade: return "decade";
    case AlbumSort::Added: return "added";
    default: return "artist";
    }
}

std::vector<size_t> album_view(const std::vector<AlbumKeys>& all, AlbumSort sort, const std::string& filter) {
    std::vector<size_t> v;
    for (size_t i = 0; i < all.size(); ++i)
        if (filter.empty() || filter_matches(filter, all[i].artist + " " + all[i].album)) v.push_back(i);
    auto newest = [](const std::string& a, const std::string& b) {
        if (a.empty() != b.empty()) return b.empty();
        return a > b;
    };
    switch (sort) {
    case AlbumSort::Album:
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return lower(all[a].album) < lower(all[b].album); });
        break;
    case AlbumSort::Year:
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return newest(all[a].year, all[b].year); });
        break;
    case AlbumSort::Decade: {
        auto decade = [](const std::string& y) { return y.size() >= 4 ? y.substr(0, 3) : std::string(); };
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return newest(decade(all[a].year), decade(all[b].year)); });
        break;
    }
    case AlbumSort::Added:
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) { return newest(all[a].added, all[b].added); });
        break;
    default: break;
    }
    return v;
}

std::string format_time(double seconds) {
    long long t = seconds > 0 ? (long long)seconds : 0;
    const long long h = t / 3600, m = (t / 60) % 60, sec = t % 60;
    char buf[32];
    if (h > 0) snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, sec);
    else snprintf(buf, sizeof buf, "%lld:%02lld", m, sec);
    return buf;
}

double seek_position_at(int x, int width, double length) {
    if (width <= 0 || length <= 0) return 0;
    return length * std::clamp((double)x / width, 0.0, 1.0);
}

double seek_by(double pos, double length, double delta) {
    if (length <= 0) return 0;
    return std::clamp(pos + delta, 0.0, length);
}

std::string seek_tooltip(double at, double length) {
    if (length <= 0) return {};
    return format_time(at) + " / " + format_time(length);
}

std::string volume_label(float db) {
    if (db <= -100.0f) return "Muted";
    if (db >= -0.05f) return "0 dB";
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f dB", db);
    return buf;
}

float volume_step(float db, int steps) {
    return std::clamp(db + steps * 2.5f, -100.0f, 0.0f);
}

int wheel_steps(float& acc, float notches) {
    if ((acc > 0 && notches < 0) || (acc < 0 && notches > 0)) acc = 0;
    acc += notches;
    const int steps = (int)acc;
    acc -= (float)steps;
    return steps;
}

std::string folder_picker_start(const std::string& current, const std::string& avoid) {
    if (current.empty()) return {};
    if (!avoid.empty() && current.compare(0, avoid.size(), avoid) == 0 &&
        (current.size() == avoid.size() || current[avoid.size()] == '/' || current[avoid.size()] == '\\'))
        return {};
    return current;
}

EdgeRect resize_rect(EdgeRect start, int edges, int dx, int dy, int min_w, int min_h) {
    EdgeRect r = start;
    if (edges & kEdgeLeft)   r.l = std::min(start.l + dx, start.r - min_w);
    if (edges & kEdgeRight)  r.r = std::max(start.r + dx, start.l + min_w);
    if (edges & kEdgeTop)    r.t = std::min(start.t + dy, start.b - min_h);
    if (edges & kEdgeBottom) r.b = std::max(start.b + dy, start.t + min_h);
    return r;
}

std::vector<size_t> cache_victims(const std::vector<CacheItem>& items, size_t total, size_t budget) {
    std::vector<size_t> out;
    if (total <= budget) return out;
    std::vector<size_t> order;
    for (size_t i = 0; i < items.size(); ++i) if (!items[i].in_use) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].used < items[b].used; });
    for (size_t i : order) {
        if (total <= budget / 4 * 3) break;
        total -= std::min(total, items[i].bytes);
        out.push_back(i);
    }
    return out;
}

}
