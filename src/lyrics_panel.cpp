#include "lyrics_panel.h"
#include "skin_engine.h"
#include "image.h"
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace pui {

namespace {

const wchar_t* kClass = L"foo_ui_panels_lyrics";
const UINT_PTR kTimer = 1;
const UINT kTickMs = 50;

std::vector<LyricsPanel*> g_panels;      // live instances, for repaint-all after a setting/fetch change
unsigned g_version = 0;                  // bumped whenever a fetch stores a new cache file
std::set<std::string> g_fetching, g_failed;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

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
    COLORREF plain = RGB(240, 240, 240);   // lyrics without timestamps
    COLORREF synced = RGB(165, 170, 180);  // timestamped lines that aren't playing right now
    COLORREF current = RGB(255, 214, 90);  // timestamped line at the current playback time
    int dim = 65;                          // % black over the cover
    int offsetMs = 0;                      // shift lyric timing
    bool online = false;                   // auto-search lrclib.net when nothing is found locally
};

COLORREF parse_col(const std::string& s, COLORREF def) {
    int r, g, b;
    if (sscanf(s.c_str(), "%d-%d-%d", &r, &g, &b) == 3) return RGB(r & 255, g & 255, b & 255);
    return def;
}
std::string fmt_col(COLORREF c) {
    char buf[32]; snprintf(buf, sizeof buf, "%d-%d-%d", GetRValue(c), GetGValue(c), GetBValue(c));
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
bool read_file(const std::string& native, std::string& out) {
    FILE* f = _wfopen(widen(native).c_str(), L"rb");
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
    d += "\\foo_ui_panels_lyrics";
    CreateDirectoryW(widen(d).c_str(), nullptr);
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
    if (!dir.empty() && !t.title.empty()) t.cache = dir + "\\" + sanitize(t.artist + " - " + t.title) + ".lrc";
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
                if (read_file(base + ext, text) && parse_lyrics(text, lines, synced)) return true;
        }
    }
    std::string text;
    if (!ids.cache.empty() && read_file(ids.cache, text) && parse_lyrics(text, lines, synced)) return true;
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
                    unsigned cp = (unsigned)strtoul(j.substr(p + 1, 4).c_str(), nullptr, 16); p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && j.compare(p + 1, 2, "\\u") == 0) {
                        unsigned lo = (unsigned)strtoul(j.substr(p + 3, 4).c_str(), nullptr, 16); p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
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
        if (FILE* f = _wfopen(widen(cacheFile).c_str(), L"wb")) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
        ++g_version;
    } else if (!ok) g_failed.insert(key);
    for (auto* p : g_panels) if (p->wnd()) InvalidateRect(p->wnd(), nullptr, FALSE);
}

} // namespace

// ---- LyricsPanel -----------------------------------------------------------------------------

void LyricsPanel::register_class() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND LyricsPanel::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) { g_panels.push_back(this); SetTimer(m_wnd, kTimer, kTickMs, nullptr); }
    return m_wnd;
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
            abort_callback_impl ab;
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

void LyricsPanel::on_timer() {
    playback_control* pc = playback_control::get().get_ptr();
    const bool moving = pc->is_playing() && !pc->is_paused();
    if ((m_lyrics.synced && moving) || m_settling || ++m_idle >= 20) { m_idle = 0; invalidate(); }
}

void LyricsPanel::paint(HDC dc) {
    RECT rc; GetClientRect(m_wnd, &rc);
    const int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    refresh_track();
    const Settings s = read_settings(m_engine);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ ob = SelectObject(mem, bmp);
    { HBRUSH b = CreateSolidBrush(RGB(22, 22, 26)); FillRect(mem, &rc, b); DeleteObject(b); }

    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (np.is_valid()) {
        // Cover as background, scaled to cover the panel without distortion (centre-cropped).
        std::string path = tf(np, "$replace(%path%,%filename_ext%,*folder*.*)").c_str();
        const int side = std::max(W, H);
        draw_cover_art(mem, path, np, (W - side) / 2, (H - side) / 2, side, side);
    }
    if (s.dim > 0) fill_alpha(mem, 0, 0, W, H, RGB(0, 0, 0), s.dim * 255 / 100);

    const int dpi = GetDeviceCaps(mem, LOGPIXELSY);
    auto mkfont = [&](int weight) {
        return CreateFontW(-MulDiv(s.size, dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    HFONT fnorm = mkfont(FW_NORMAL), fbold = mkfont(FW_BOLD);
    HGDIOBJ oldf = SelectObject(mem, fnorm);
    SetBkMode(mem, TRANSPARENT);
    m_link = RECT();
    const UINT dtf = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
    const int padX = std::max(14, W / 12), tw = std::max(20, W - padX * 2);

    bool settled = true;
    if (m_lyrics.lines.empty()) {
        std::string msg, link;
        const TrackIds ids = np.is_valid() ? ids_for(np) : TrackIds();
        if (!np.is_valid()) msg = "Nothing playing";
        else if (g_fetching.count(ids.key)) msg = "Searching lrclib.net...";
        else if (g_failed.count(ids.key)) { msg = "No lyrics found online"; link = "Search again"; }
        else { msg = "No lyrics found"; link = "Search online (lrclib.net)"; }
        std::wstring wm = widen(msg);
        RECT r = { padX, 0, padX + tw, 0 };
        DrawTextW(mem, wm.c_str(), -1, &r, dtf | DT_CALCRECT);
        int th = r.bottom;
        int y = H / 2 - th - 4;
        RECT rm = { padX, y, padX + tw, y + th };
        SetTextColor(mem, RGB(0, 0, 0)); RECT sh = rm; OffsetRect(&sh, 1, 1); DrawTextW(mem, wm.c_str(), -1, &sh, dtf);
        SetTextColor(mem, s.plain); DrawTextW(mem, wm.c_str(), -1, &rm, dtf);
        if (!link.empty()) {
            LOGFONTW lf; GetObjectW(fnorm, sizeof lf, &lf); lf.lfUnderline = TRUE;
            HFONT fl = CreateFontIndirectW(&lf); SelectObject(mem, fl);
            std::wstring wl = widen(link);
            RECT rl = { padX, y + th + 12, padX + tw, y + th + 12 + 40 };
            DrawTextW(mem, wl.c_str(), -1, &rl, dtf | DT_CALCRECT);
            int w = rl.right - rl.left; rl.left = (W - w) / 2; rl.right = rl.left + w;
            SetTextColor(mem, s.current); DrawTextW(mem, wl.c_str(), -1, &rl, dtf);
            m_link = rl; InflateRect(&m_link, 8, 6);
            SelectObject(mem, fnorm); DeleteObject(fl);
        }
    } else {
        struct Placed { int y, h; };
        std::vector<Placed> pl(m_lyrics.lines.size());
        const int gap = std::max(4, s.size / 3);
        int total = 0;
        for (size_t i = 0; i < m_lyrics.lines.size(); ++i) {
            std::wstring w = widen(m_lyrics.lines[i].text);
            RECT r = { 0, 0, tw, 0 };
            DrawTextW(mem, w.empty() ? L" " : w.c_str(), -1, &r, dtf | DT_CALCRECT);
            pl[i] = { total, r.bottom };
            total += r.bottom + gap;
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
            if (GetTickCount64() >= m_holdUntil) {
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
            std::wstring w = widen(m_lyrics.lines[i].text);
            if (w.empty()) continue;
            const bool isCur = (int)i == cur;
            SelectObject(mem, isCur ? fbold : fnorm);
            RECT r = { padX, y, padX + tw, y + pl[i].h };
            RECT sh = r; OffsetRect(&sh, 1, 1);
            SetTextColor(mem, RGB(0, 0, 0)); DrawTextW(mem, w.c_str(), -1, &sh, dtf);
            SetTextColor(mem, !m_lyrics.synced ? s.plain : (isCur ? s.current : s.synced));
            DrawTextW(mem, w.c_str(), -1, &r, dtf);
        }
    }
    m_settling = !settled;

    SelectObject(mem, oldf); DeleteObject(fnorm); DeleteObject(fbold);
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
}

// ---- settings menu ----------------------------------------------------------------------------

void LyricsPanel::show_settings_menu(SkinEngine* engine, HWND owner) {
    if (!engine) return;
    const Settings s = read_settings(engine);
    enum { kSize = 100, kDim = 200, kOffset = 300, kColPlain = 1, kColSynced = 2, kColCur = 3,
           kOnline = 4, kFetchNow = 5, kReset = 6 };
    static const int kSizes[] = { 11, 13, 15, 17, 20, 24, 28 };
    static const int kDims[] = { 0, 25, 45, 65, 80, 90 };
    static const int kOffsets[] = { -2000, -1000, -500, 0, 500, 1000, 2000 };

    HMENU menu = CreatePopupMenu(), sizes = CreatePopupMenu(), dims = CreatePopupMenu(),
          offs = CreatePopupMenu(), cols = CreatePopupMenu();
    for (int i = 0; i < 7; ++i) {
        wchar_t l[24]; swprintf(l, 24, L"%d pt", kSizes[i]);
        AppendMenuW(sizes, MF_STRING | (s.size == kSizes[i] ? MF_CHECKED : 0), kSize + i, l);
    }
    for (int i = 0; i < 6; ++i) {
        wchar_t l[24]; swprintf(l, 24, L"%d%%", kDims[i]);
        AppendMenuW(dims, MF_STRING | (s.dim == kDims[i] ? MF_CHECKED : 0), kDim + i, l);
    }
    for (int i = 0; i < 7; ++i) {
        wchar_t l[32]; swprintf(l, 32, L"%+.1f s", kOffsets[i] / 1000.0);
        AppendMenuW(offs, MF_STRING | (s.offsetMs == kOffsets[i] ? MF_CHECKED : 0), kOffset + i, l);
    }
    AppendMenuW(cols, MF_STRING, kColPlain, L"Lyrics without timestamps...");
    AppendMenuW(cols, MF_STRING, kColSynced, L"Timestamped lines...");
    AppendMenuW(cols, MF_STRING, kColCur, L"Current line...");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sizes, L"Font size");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)cols, L"Colours");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)dims, L"Cover darkening");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)offs, L"Timing offset");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kFetchNow, L"Search lyrics online now (lrclib.net)");
    AppendMenuW(menu, MF_STRING | (s.online ? MF_CHECKED : 0), kOnline, L"Search online automatically");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kReset, L"Reset lyrics settings");

    POINT pt; GetCursorPos(&pt);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (!cmd) return;

    auto pick = [&](const char* key, COLORREF cur) {
        static COLORREF custom[16] = {};
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner = owner; cc.rgbResult = cur; cc.lpCustColors = custom;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (ChooseColorW(&cc)) engine->set_pvar(key, fmt_col(cc.rgbResult));
    };
    if (cmd >= kSize && cmd < kSize + 7) engine->set_pvar("lyr.size", std::to_string(kSizes[cmd - kSize]));
    else if (cmd >= kDim && cmd < kDim + 6) engine->set_pvar("lyr.dim", std::to_string(kDims[cmd - kDim]));
    else if (cmd >= kOffset && cmd < kOffset + 7) engine->set_pvar("lyr.offset", std::to_string(kOffsets[cmd - kOffset]));
    else if (cmd == kColPlain) pick("lyr.col.plain", s.plain);
    else if (cmd == kColSynced) pick("lyr.col.synced", s.synced);
    else if (cmd == kColCur) pick("lyr.col.current", s.current);
    else if (cmd == kOnline) engine->set_pvar("lyr.online", s.online ? "0" : "1");
    else if (cmd == kFetchNow) { for (auto* p : g_panels) if (IsWindowVisible(p->wnd())) { p->start_fetch(); break; } }
    else if (cmd == kReset)
        for (const char* k : { "lyr.size", "lyr.dim", "lyr.offset", "lyr.online", "lyr.col.plain", "lyr.col.synced", "lyr.col.current" })
            engine->set_pvar(k, "");
    for (auto* p : g_panels) p->invalidate();
}

LRESULT CALLBACK LyricsPanel::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    LyricsPanel* self = reinterpret_cast<LyricsPanel*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<LyricsPanel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(wnd, &ps);
        if (self) self->paint(dc);
        EndPaint(wnd, &ps); return 0;
    }
    case WM_TIMER: if (self) self->on_timer(); return 0;
    case WM_SIZE: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL:
        if (self) {
            int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            self->m_scroll -= notches * 3.0 * std::max(8, read_settings(self->m_engine).size) * 1.6;
            if (self->m_lyrics.synced) self->m_holdUntil = GetTickCount64() + 4000;
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            self->m_overLink = !IsRectEmpty(&self->m_link) && PtInRect(&self->m_link, p);
        }
        return 0;
    case WM_SETCURSOR:
        if (self && self->m_overLink) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN:
        if (self && !IsRectEmpty(&self->m_link)) {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&self->m_link, p)) self->start_fetch();
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (self) show_settings_menu(self->m_engine, wnd);
        return 0;
    case WM_NCDESTROY:
        if (self) g_panels.erase(std::remove(g_panels.begin(), g_panels.end(), self), g_panels.end());
        KillTimer(wnd, kTimer);
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
