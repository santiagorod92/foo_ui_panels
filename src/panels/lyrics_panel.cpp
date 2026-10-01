#include "lyrics_panel.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include "../core/fs_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace pui {

namespace {

const int kTimer = 1;
const int kTickMs = 50;

std::vector<LyricsPanel*> g_panels;      // live instances, for repaint-all after a setting/fetch change
unsigned g_version = 0;                  // bumped whenever a fetch stores a new cache file
std::set<std::string> g_fetching, g_failed;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

// ---- settings (persisted as pvars) ------------------------------------------------------
struct Settings {
    int size = 15;
    gfx::Color plain{ 240, 240, 240 };   // lyrics without timestamps
    gfx::Color synced{ 165, 170, 180 };  // timestamped lines that aren't playing right now
    gfx::Color current{ 255, 214, 90 };  // timestamped line at the current playback time
    int dim = 65;                          // % black over the cover
    int offsetMs = 0;                      // shift lyric timing
    bool online = false;                   // auto-search lrclib.net when nothing is found locally
};

gfx::Color parse_col(const std::string& s, gfx::Color def) {
    int r, g, b;
    if (sscanf(s.c_str(), "%d-%d-%d", &r, &g, &b) == 3) return gfx::Color(r & 255, g & 255, b & 255);
    return def;
}
std::string fmt_col(gfx::Color c) {
    char buf[32]; snprintf(buf, sizeof buf, "%d-%d-%d", c.r, c.g, c.b);
    return buf;
}

Settings read_settings(SkinEngine* e) {
    Settings s;
    if (!e) return s;
    auto num = [&](const char* k, int def) {
        std::string v = e->get_pvar(k); return v.empty() ? def : atoi(v.c_str());
    };
    s.size = std::max(8, std::min(48, num("lyr.size", s.size)));
    s.dim = std::max(0, std::min(95, num("lyr.dim", s.dim)));
    s.offsetMs = num("lyr.offset", 0);
    s.online = e->get_pvar("lyr.online") == "1";
    s.plain = parse_col(e->get_pvar("lyr.col.plain"), s.plain);
    s.synced = parse_col(e->get_pvar("lyr.col.synced"), s.synced);
    s.current = parse_col(e->get_pvar("lyr.col.current"), s.current);
    return s;
}

// ---- lyric text parsing ------------------------------------------------------------------
bool parse_time(const std::string& tag, double& out) {
    int m = 0, sec = 0; size_t i = 0;
    if (tag.empty() || !isdigit((unsigned char)tag[0])) return false;
    while (i < tag.size() && isdigit((unsigned char)tag[i])) m = m * 10 + (tag[i++] - '0');
    if (i >= tag.size() || tag[i] != ':') return false;
    ++i;
    if (i >= tag.size() || !isdigit((unsigned char)tag[i])) return false;
    while (i < tag.size() && isdigit((unsigned char)tag[i])) sec = sec * 10 + (tag[i++] - '0');
    double frac = 0;
    if (i < tag.size() && (tag[i] == '.' || tag[i] == ':')) {
        ++i; double scale = 0.1;
        while (i < tag.size() && isdigit((unsigned char)tag[i])) { frac += (tag[i++] - '0') * scale; scale /= 10; }
    }
    if (i != tag.size()) return false;
    out = m * 60.0 + sec + frac;
    return true;
}

// LRC (or plain) text -> lines. Lines with no [mm:ss.xx] tag are kept only for plain lyrics.
bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    lines.clear(); synced = false;
    std::vector<std::pair<double, std::string>> timed, plain;
    double offset = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r')) line.pop_back();

        std::vector<double> ts; size_t p = 0; bool meta = false;
        while (p < line.size() && line[p] == '[') {
            size_t e = line.find(']', p);
            if (e == std::string::npos) break;
            std::string tag = line.substr(p + 1, e - p - 1);
            double t;
            if (parse_time(tag, t)) { ts.push_back(t); p = e + 1; }
            else {
                if (ts.empty()) {
                    meta = true;
                    if (lower(tag).compare(0, 7, "offset:") == 0) offset = atof(tag.c_str() + 7) / 1000.0;
                }
                break;
            }
        }
        if (meta && ts.empty()) continue;
        std::string body = line.substr(p);
        // strip inline word timestamps <mm:ss.xx>
        std::string clean;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '<') { size_t e = body.find('>', i); double t; if (e != std::string::npos && parse_time(body.substr(i + 1, e - i - 1), t)) { i = e; continue; } }
            clean += body[i];
        }
        body = trim(clean);
        if (!ts.empty()) for (double t : ts) timed.push_back({ t - offset, body });
        else plain.push_back({ -1, body });
    }
    if (!timed.empty()) {
        std::stable_sort(timed.begin(), timed.end(), [](auto& a, auto& b) { return a.first < b.first; });
        lines = timed; synced = true;
    } else {
        while (!plain.empty() && plain.back().second.empty()) plain.pop_back();
        while (!plain.empty() && plain.front().second.empty()) plain.erase(plain.begin());
        lines = plain;
    }
    return !lines.empty();
}

// ---- file helpers -------------------------------------------------------------------------
bool read_small_file(const std::string& native, std::string& out) {
    FILE* f = fs_open(native, "rb");
    if (!f) return false;
    char buf[8192]; size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < (4u << 20)) out.append(buf, n);
    fclose(f);
    return !out.empty();
}

std::string to_native(const char* fsurl) {
    pfc::string8 n;
    try { filesystem::g_get_native_path(fsurl, n); } catch (...) { return {}; }
    return n.c_str();
}

std::string cache_dir() {
    std::string d = to_native(core_api::get_profile_path());
    if (d.empty()) return {};
    d += kPathSep; d += "foo_ui_panels_lyrics";
    std::error_code ec;
    std::filesystem::create_directory(fs_path(d), ec);
    return d;
}

std::string sanitize(std::string s) {
    for (auto& c : s) if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32) c = '_';
    if (s.size() > 120) s.resize(120);
    return s;
}

// ---- track info ----------------------------------------------------------------------------
pfc::string8 tf(const metadb_handle_ptr& h, const char* script) {
    service_ptr_t<titleformat_object> obj;
    titleformat_compiler::get()->compile_safe(obj, script);
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, obj, nullptr);
    return o;
}

struct TrackIds {
    std::string key, artist, title, album, cache;
    int duration = 0;
};
TrackIds ids_for(const metadb_handle_ptr& np) {
    TrackIds t;
    t.artist = tf(np, "[%artist%]").c_str();
    if (t.artist.empty()) t.artist = tf(np, "[%album artist%]").c_str();
    t.title = tf(np, "[%title%]").c_str();
    t.album = tf(np, "[%album%]").c_str();
    t.duration = (int)(np->get_length() + 0.5);
    t.key = lower(t.artist + " - " + t.title);
    std::string dir = cache_dir();
    if (!dir.empty() && !t.title.empty()) t.cache = dir + kPathSep + sanitize(t.artist + " - " + t.title) + ".lrc";
    return t;
}

// Tags, then sidecar files, then the local cache.
bool load_local(const metadb_handle_ptr& np, const TrackIds& ids,
                std::vector<std::pair<double, std::string>>& lines, bool& synced) {
    static const char* kFields[] = { "syncedlyrics", "synced lyrics", "lyrics", "unsyncedlyrics",
                                     "unsynced lyrics", "unsynced_lyrics" };
    file_info_impl info;
    bool have = false;
    std::vector<std::pair<double, std::string>> l; bool s = false;
    if (np->get_info(info)) {
        for (t_size i = 0; i < info.meta_get_count(); ++i) {
            std::string name = lower(info.meta_enum_name(i));
            bool match = false; for (auto f : kFields) if (name == f) match = true;
            if (!match || info.meta_enum_value_count(i) == 0) continue;
            if (parse_lyrics(info.meta_enum_value(i, 0), l, s) && (!have || s)) {
                lines = l; synced = s; have = true;
                if (s) return true;
            }
        }
        if (have) return true;
    }
    pfc::string8 path = np->get_path();
    if (strncmp(path, "file://", 7) == 0) {
        std::string nat = to_native(path);
        size_t dot = nat.find_last_of('.'), sl = nat.find_last_of("\\/");
        if (!nat.empty() && dot != std::string::npos && (sl == std::string::npos || dot > sl)) {
            std::string base = nat.substr(0, dot), text;
            for (const char* ext : { ".lrc", ".txt" })
                if (read_small_file(base + ext, text) && parse_lyrics(text, lines, synced)) return true;
        }
    }
    std::string text;
    if (!ids.cache.empty() && read_small_file(ids.cache, text) && parse_lyrics(text, lines, synced)) return true;
    return false;
}

// ---- lrclib.net ----------------------------------------------------------------------------
std::string urlenc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

void utf8_append(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
}

// First non-null string value of `"key":` in the JSON text (also works on the search array).
bool json_string(const std::string& j, const char* key, std::string& out) {
    std::string k = std::string("\"") + key + "\"";
    size_t from = 0;
    for (;;) {
        size_t p = j.find(k, from);
        if (p == std::string::npos) return false;
        p += k.size(); from = p;
        while (p < j.size() && (j[p] == ' ' || j[p] == ':' || j[p] == '\n' || j[p] == '\t')) ++p;
        if (p >= j.size() || j[p] != '"') continue; // null
        ++p; out.clear();
        while (p < j.size() && j[p] != '"') {
            if (j[p] == '\\' && p + 1 < j.size()) {
                ++p;
                switch (j[p]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                case 'u': {
                    // \uXXXX, a surrogate pair as two of them. Malformed input (truncated, a lone
                    // or mismatched surrogate) becomes U+FFFD instead of reading past the body.
                    auto hex4 = [&](size_t at, unsigned& v) {
                        if (at + 4 > j.size()) return false;
                        v = 0;
                        for (size_t k = at; k < at + 4; ++k) {
                            char c = j[k]; v <<= 4;
                            if (c >= '0' && c <= '9') v |= c - '0';
                            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                            else return false;
                        }
                        return true;
                    };
                    unsigned cp = 0, lo = 0;
                    if (!hex4(p + 1, cp)) { out += "\xEF\xBF\xBD"; break; }
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        if (j.compare(p + 1, 2, "\\u") == 0 && hex4(p + 3, lo) && lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6;
                        } else cp = 0xFFFD;
                    } else if (cp >= 0xDC00 && cp < 0xE000) cp = 0xFFFD;
                    utf8_append(out, cp); break; }
                default: out += j[p];
                }
            } else out += j[p];
            ++p;
        }
        if (!trim(out).empty()) return true;
    }
}

bool http_get(const std::string& url, std::string& body, abort_callback& ab) {
    try {
        auto req = http_client::get()->create_request("GET");
        req->add_header("User-Agent", "foo_ui_panels lyrics (foobar2000 component)");
        file::ptr f = req->run(url.c_str(), ab);
        char buf[4096];
        body.clear();
        for (;;) {
            t_size n = f->read(buf, sizeof buf, ab);
            if (!n) break;
            body.append(buf, n);
            if (body.size() > (2u << 20)) break;
        }
        return !body.empty();
    } catch (...) { return false; }
}

void finish_fetch(const std::string& key, bool ok, const std::string& text, const std::string& cacheFile) {
    g_fetching.erase(key);
    if (ok && !cacheFile.empty()) {
        write_file(cacheFile, text);
        ++g_version;
    } else if (!ok) g_failed.insert(key);
    for (auto* p : g_panels) p->invalidate();
}

} // namespace

// ---- LyricsPanel -----------------------------------------------------------------------------

void LyricsPanel::on_attached() {
    g_panels.push_back(this);
    host()->set_timer(kTimer, kTickMs);
}

LyricsPanel::~LyricsPanel() {
    g_panels.erase(std::remove(g_panels.begin(), g_panels.end(), this), g_panels.end());
}

void LyricsPanel::refresh_track() {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    std::string key = np.is_valid() ? std::string(np->get_path()) + "#" + std::to_string(np->get_subsong_index()) : std::string();
    if (key == m_key && m_version == g_version) return;
    const bool trackChanged = key != m_key;
    m_key = key; m_version = g_version;
    if (trackChanged) { m_scroll = 0; m_holdUntil = 0; }
    m_lyrics = Lyrics();
    if (!np.is_valid()) return;
    TrackIds ids = ids_for(np);
    std::vector<std::pair<double, std::string>> lines; bool synced = false;
    if (load_local(np, ids, lines, synced)) {
        m_lyrics.synced = synced;
        for (auto& l : lines) m_lyrics.lines.push_back({ l.first, l.second });
    } else if (read_settings(m_engine).online && !ids.title.empty() && !g_failed.count(ids.key)) {
        start_fetch();
    }
}

void LyricsPanel::start_fetch() {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (!np.is_valid()) return;
    TrackIds ids = ids_for(np);
    if (ids.title.empty() || g_fetching.count(ids.key)) return;
    g_fetching.insert(ids.key); g_failed.erase(ids.key);
    invalidate();
    fb2k::splitTask([ids]() {
        std::string body, text;
        bool ok = false;
        try {
            // Signalled when foobar2000 quits (which waits for splitTask work): a slow lrclib.net
            // must not hold up closing the player.
            abort_callback& ab = fb2k::mainAborter();
            const std::string base = "https://lrclib.net/api/";
            std::string q = "artist_name=" + urlenc(ids.artist) + "&track_name=" + urlenc(ids.title);
            std::string exact = q;
            if (!ids.album.empty()) exact += "&album_name=" + urlenc(ids.album);
            if (ids.duration > 0) exact += "&duration=" + std::to_string(ids.duration);
            if (http_get(base + "get?" + exact, body, ab))
                ok = json_string(body, "syncedLyrics", text) || json_string(body, "plainLyrics", text);
            if (!ok && http_get(base + "search?" + q, body, ab))
                ok = json_string(body, "syncedLyrics", text) || json_string(body, "plainLyrics", text);
        } catch (...) {}
        fb2k::inMainThread([ids, ok, text]() { finish_fetch(ids.key, ok, text, ids.cache); });
    });
}

void LyricsPanel::on_timer(int) {
    playback_control* pc = playback_control::get().get_ptr();
    const bool moving = pc->is_playing() && !pc->is_paused();
    if ((m_lyrics.synced && moving) || m_settling || ++m_idle >= 20) { m_idle = 0; invalidate(); }
}

void LyricsPanel::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (W <= 0 || H <= 0) return;
    refresh_track();
    const Settings s = read_settings(m_engine);

    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color(22, 22, 26));

    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (np.is_valid()) {
        // Cover as background, scaled to cover the panel without distortion (centre-cropped).
        std::string path = tf(np, "$replace(%path%,%filename_ext%,*folder*.*)").c_str();
        const int side = std::max(W, H);
        draw_cover_art(cv, path, np, (W - side) / 2, (H - side) / 2, side, side);
    }
    if (s.dim > 0) fill_alpha(cv, 0, 0, W, H, gfx::Color(0, 0, 0), s.dim * 255 / 100);

    gfx::FontSpec fnorm{ "Segoe UI", (float)s.size, true }, fbold = fnorm;
    fbold.bold = true;
    cv.set_font(fnorm);
    m_link = gfx::Rect();
    const unsigned dtf = gfx::kAlignCenter | gfx::kWordWrap;
    const int padX = std::max(14, W / 12), tw = std::max(20, W - padX * 2);
    const gfx::Color black(0, 0, 0);

    bool settled = true;
    if (m_lyrics.lines.empty()) {
        std::string msg, link;
        const TrackIds ids = np.is_valid() ? ids_for(np) : TrackIds();
        if (!np.is_valid()) msg = "Nothing playing";
        else if (g_fetching.count(ids.key)) msg = "Searching lrclib.net...";
        else if (g_failed.count(ids.key)) { msg = "No lyrics found online"; link = "Search again"; }
        else { msg = "No lyrics found"; link = "Search online (lrclib.net)"; }
        const int th = cv.text_height(msg, tw, dtf);
        int y = H / 2 - th - 4;
        gfx::Rect rm{ padX, y, tw, th };
        cv.draw_text(msg, gfx::Rect{ rm.x + 1, rm.y + 1, rm.w, rm.h }, dtf, black);
        cv.draw_text(msg, rm, dtf, s.plain);
        if (!link.empty()) {
            gfx::FontSpec fl = fnorm; fl.underline = true;
            cv.set_font(fl);
            int lw = std::min(tw, cv.text_width(link)), lh = cv.text_height(link, tw, dtf);
            gfx::Rect rl{ (W - lw) / 2, y + th + 12, lw, lh };
            cv.draw_text(link, rl, dtf, s.current);
            m_link = gfx::Rect{ rl.x - 8, rl.y - 6, rl.w + 16, rl.h + 12 };
            cv.set_font(fnorm);
        }
    } else {
        struct Placed { int y, h; };
        std::vector<Placed> pl(m_lyrics.lines.size());
        const int gap = std::max(4, s.size / 3);
        int total = 0;
        for (size_t i = 0; i < m_lyrics.lines.size(); ++i) {
            int h = cv.text_height(m_lyrics.lines[i].text, tw, dtf);
            pl[i] = { total, h };
            total += h + gap;
        }
        // Current line: last timestamp at/before the playback position.
        int cur = -1;
        if (m_lyrics.synced && np.is_valid()) {
            double pos = playback_control::get()->playback_get_position() + s.offsetMs / 1000.0;
            for (size_t i = 0; i < m_lyrics.lines.size(); ++i) if (m_lyrics.lines[i].t <= pos) cur = (int)i; else break;
        }
        const int topPad = H / 3;
        double target = m_scroll;
        if (m_lyrics.synced) {
            if (cur >= 0) target = pl[cur].y + pl[cur].h / 2.0 - H / 2.0 + topPad;
            else target = 0;
            if (tick_ms() >= m_holdUntil) {
                double d = target - m_scroll;
                if (std::abs(d) < 1.0) m_scroll = target; else { m_scroll += d * 0.2; settled = false; }
            }
        } else {
            m_scroll = std::max(0.0, std::min(m_scroll, (double)std::max(0, total + topPad - H)));
        }
        const int y0 = topPad - (int)m_scroll;
        for (size_t i = 0; i < m_lyrics.lines.size(); ++i) {
            int y = y0 + pl[i].y;
            if (y + pl[i].h < 0 || y > H) continue;
            const std::string& text = m_lyrics.lines[i].text;
            if (text.empty()) continue;
            const bool isCur = (int)i == cur;
            cv.set_font(isCur ? fbold : fnorm);
            gfx::Rect r{ padX, y, tw, pl[i].h };
            cv.draw_text(text, gfx::Rect{ r.x + 1, r.y + 1, r.w, r.h }, dtf, black);
            cv.draw_text(text, r, dtf, !m_lyrics.synced ? s.plain : (isCur ? s.current : s.synced));
        }
    }
    m_settling = !settled;
}

// ---- settings menu ----------------------------------------------------------------------------

void LyricsPanel::show_settings_menu(SkinEngine* engine, ui::ViewHost* owner, int x, int y) {
    if (!engine) return;
    const Settings s = read_settings(engine);
    enum { kSize = 100, kDim = 200, kOffset = 300, kColPlain = 1, kColSynced = 2, kColCur = 3,
           kOnline = 4, kFetchNow = 5, kReset = 6 };
    static const int kSizes[] = { 11, 13, 15, 17, 20, 24, 28 };
    static const int kDims[] = { 0, 25, 45, 65, 80, 90 };
    static const int kOffsets[] = { -2000, -1000, -500, 0, 500, 1000, 2000 };

    auto item = [](std::string label, int id, bool checked = false) {
        ui::MenuItem m; m.label = std::move(label); m.id = id; m.checked = checked; return m;
    };
    auto sub = [](std::string label, ui::Menu children) {
        ui::MenuItem m; m.label = std::move(label); m.children = std::move(children); return m;
    };
    ui::Menu sizes, dims, offs, cols;
    char l[32];
    for (int i = 0; i < 7; ++i) { snprintf(l, sizeof l, "%d pt", kSizes[i]); sizes.push_back(item(l, kSize + i, s.size == kSizes[i])); }
    for (int i = 0; i < 6; ++i) { snprintf(l, sizeof l, "%d%%", kDims[i]); dims.push_back(item(l, kDim + i, s.dim == kDims[i])); }
    for (int i = 0; i < 7; ++i) { snprintf(l, sizeof l, "%+.1f s", kOffsets[i] / 1000.0); offs.push_back(item(l, kOffset + i, s.offsetMs == kOffsets[i])); }
    cols.push_back(item("Lyrics without timestamps...", kColPlain));
    cols.push_back(item("Timestamped lines...", kColSynced));
    cols.push_back(item("Current line...", kColCur));
    ui::Menu menu;
    menu.push_back(sub("Font size", sizes));
    menu.push_back(sub("Colours", cols));
    menu.push_back(sub("Cover darkening", dims));
    menu.push_back(sub("Timing offset", offs));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Search lyrics online now (lrclib.net)", kFetchNow));
    menu.push_back(item("Search online automatically", kOnline, s.online));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Reset lyrics settings", kReset));

    const int cmd = ui::popup_menu(owner, x, y, menu);
    if (!cmd) return;

    auto pick = [&](const char* key, gfx::Color cur) {
        if (ui::choose_color(owner, cur)) engine->set_pvar(key, fmt_col(cur));
    };
    if (cmd >= kSize && cmd < kSize + 7) engine->set_pvar("lyr.size", std::to_string(kSizes[cmd - kSize]));
    else if (cmd >= kDim && cmd < kDim + 6) engine->set_pvar("lyr.dim", std::to_string(kDims[cmd - kDim]));
    else if (cmd >= kOffset && cmd < kOffset + 7) engine->set_pvar("lyr.offset", std::to_string(kOffsets[cmd - kOffset]));
    else if (cmd == kColPlain) pick("lyr.col.plain", s.plain);
    else if (cmd == kColSynced) pick("lyr.col.synced", s.synced);
    else if (cmd == kColCur) pick("lyr.col.current", s.current);
    else if (cmd == kOnline) engine->set_pvar("lyr.online", s.online ? "0" : "1");
    else if (cmd == kFetchNow) { for (auto* p : g_panels) if (p->host() && p->host()->visible()) { p->start_fetch(); break; } }
    else if (cmd == kReset)
        for (const char* k : { "lyr.size", "lyr.dim", "lyr.offset", "lyr.online", "lyr.col.plain", "lyr.col.synced", "lyr.col.current" })
            engine->set_pvar(k, "");
    for (auto* p : g_panels) p->invalidate();
}

void LyricsPanel::on_wheel(int, int, float notches) {
    m_scroll -= notches * 3.0 * std::max(8, read_settings(m_engine).size) * 1.6;
    if (m_lyrics.synced) m_holdUntil = tick_ms() + 4000;
    invalidate();
}

void LyricsPanel::on_mouse_move(int x, int y, unsigned, bool) {
    const bool over = !m_link.empty() && m_link.contains(x, y);
    if (over != m_overLink) {
        m_overLink = over;
        host()->set_cursor(over ? ui::Cursor::Hand : ui::Cursor::Arrow);
    }
}

void LyricsPanel::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Left) {
        if (!m_link.empty() && m_link.contains(e.x, e.y)) start_fetch();
    } else if (e.button == ui::MouseButton::Right) {
        show_settings_menu(m_engine, host(), e.x, e.y);
    }
}

} // namespace pui
