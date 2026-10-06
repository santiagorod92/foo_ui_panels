#include "lyrics_panel.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include "../core/fs_util.h"
#include "../core/lyrics_parse.h"
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
// Keys whose lyrics the user just replaced (a new search, a tap-sync), with the file that holds
// them: it beats tags/sidecars for the rest of the session.
std::map<std::string, std::string> g_prefer;

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
    int karaoke = 1;                       // 0 off, 1 word-timed lyrics only, 2 also estimated
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
    s.karaoke = std::max(0, std::min(2, num("lyr.karaoke", s.karaoke)));
    s.plain = parse_col(e->get_pvar("lyr.col.plain"), s.plain);
    s.synced = parse_col(e->get_pvar("lyr.col.synced"), s.synced);
    s.current = parse_col(e->get_pvar("lyr.col.current"), s.current);
    return s;
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

// Per-track timing corrections (ms), keyed like the cache ("artist - title", lower-cased) and kept
// in <cache dir>/offsets.txt as "<ms>\t<key>" lines: lyrics drift track by track, not globally.
std::map<std::string, int>& track_offsets() {
    static std::map<std::string, int> m;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string text, dir = cache_dir();
        if (!dir.empty() && read_small_file(dir + kPathSep + "offsets.txt", text)) {
            size_t pos = 0;
            while (pos < text.size()) {
                size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
                std::string line = text.substr(pos, nl - pos); pos = nl + 1;
                size_t tab = line.find('\t');
                if (tab != std::string::npos && tab + 1 < line.size()) m[line.substr(tab + 1)] = atoi(line.c_str());
            }
        }
    }
    return m;
}

void set_track_offset(const std::string& key, int ms) {
    auto& m = track_offsets();
    if (ms) m[key] = ms; else m.erase(key);
    std::string dir = cache_dir(), text;
    if (dir.empty()) return;
    for (auto& [k, v] : m) text += std::to_string(v) + "\t" + k + "\n";
    write_file(dir + kPathSep + "offsets.txt", text);
}

int track_offset(const std::string& key) {
    auto& m = track_offsets(); auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
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

// The track's path without its extension, for sidecar .lrc/.txt files ("" if not a local file).
std::string sidecar_base(const metadb_handle_ptr& np) {
    pfc::string8 path = np->get_path();
    if (strncmp(path, "file://", 7) != 0) return {};
    std::string nat = to_native(path);
    size_t dot = nat.find_last_of('.'), sl = nat.find_last_of("\\/");
    if (nat.empty() || dot == std::string::npos || (sl != std::string::npos && dot < sl)) return {};
    return nat.substr(0, dot);
}

// Tags, then sidecar files, then the local cache — synced lyrics from any of them beat plain ones.
bool load_local(const metadb_handle_ptr& np, const TrackIds& ids, std::vector<LyricLine>& lines, bool& synced) {
    static const char* kFields[] = { "syncedlyrics", "synced lyrics", "lyrics", "unsyncedlyrics",
                                     "unsynced lyrics", "unsynced_lyrics" };
    std::string text;
    auto pref = g_prefer.find(ids.key);
    if (pref != g_prefer.end() && read_small_file(pref->second, text) && parse_lyrics(text, lines, synced))
        return true;
    std::vector<LyricLine> plain;
    // True once synced lyrics are in `lines`; plain ones are kept as the fallback.
    auto take = [&](const std::string& t) {
        std::vector<LyricLine> l; bool s = false;
        if (!parse_lyrics(t, l, s)) return false;
        if (s) { lines = std::move(l); synced = true; return true; }
        if (plain.empty()) plain = std::move(l);
        return false;
    };
    file_info_impl info;
    if (np->get_info(info)) {
        for (t_size i = 0; i < info.meta_get_count(); ++i) {
            std::string name = lower(info.meta_enum_name(i));
            bool match = false; for (auto f : kFields) if (name == f) match = true;
            if (match && info.meta_enum_value_count(i) > 0 && take(info.meta_enum_value(i, 0))) return true;
        }
    }
    const std::string base = sidecar_base(np);
    if (!base.empty())
        for (const char* ext : { ".lrc", ".txt" })
            if (read_small_file(base + ext, text) && take(text)) return true;
    if (!ids.cache.empty() && read_small_file(ids.cache, text) && take(text)) return true;
    if (plain.empty()) return false;
    lines = std::move(plain); synced = false;
    return true;
}

// ---- lrclib.net ----------------------------------------------------------------------------
// lrclib marks tracks without vocals instead of giving them lyrics: cached as this one line, so
// the panel says so instead of "No lyrics found" and doesn't search again.
const char* const kInstrumental = "\xE2\x99\xAA Instrumental \xE2\x99\xAA";

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
        // An explicit search replaces whatever was showing (tags/sidecar included) this session.
        g_prefer[key] = cacheFile;
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
    if (trackChanged) { m_scroll = 0; m_holdUntil = 0; m_sync.cancel(); }
    m_lyrics = Lyrics();
    m_idsKey.clear();
    if (!np.is_valid()) return;
    TrackIds ids = ids_for(np);
    m_idsKey = ids.key;
    if (!load_local(np, ids, m_lyrics.lines, m_lyrics.synced) && read_settings(m_engine).online
        && !ids.title.empty() && !g_failed.count(ids.key))
        start_fetch();
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
            std::string q = "artist_name=" + url_encode(ids.artist) + "&track_name=" + url_encode(ids.title);
            std::string exact = q;
            if (!ids.album.empty()) exact += "&album_name=" + url_encode(ids.album);
            if (ids.duration > 0) exact += "&duration=" + std::to_string(ids.duration);
            auto pick = [&]() {
                if (json_string(body, "syncedLyrics", text) || json_string(body, "plainLyrics", text)) return true;
                if (json_true(body, "instrumental")) { text = kInstrumental; return true; }
                return false;
            };
            if (http_get(base + "get?" + exact, body, ab)) ok = pick();
            if (!ok && http_get(base + "search?" + q, body, ab)) ok = pick();
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

    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, m_engine ? m_engine->color("lyrics", "background", gfx::Color(22, 22, 26)) : gfx::Color(22, 22, 26));

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
    const bool syncing = m_sync.active();
    if (m_lyrics.lines.empty() && !syncing) {
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
        // In sync mode the lines being stamped are listed instead, in the same layout.
        const size_t n = syncing ? m_sync.lines().size() : m_lyrics.lines.size();
        auto text_of = [&](size_t i) -> const std::string& {
            return syncing ? m_sync.lines()[i] : m_lyrics.lines[i].text;
        };
        // Current line: last timestamp at/before the playback position (syncing: the line the
        // next tap stamps, or the last one once all are done).
        int cur = -1;
        double pos = 0;
        if (syncing) {
            cur = m_sync.done() ? (int)n - 1 : (int)m_sync.next();
        } else if (m_lyrics.synced && np.is_valid()) {
            const int ms = s.offsetMs + track_offset(m_idsKey);
            pos = playback_control::get()->playback_get_position() + ms / 1000.0;
            for (size_t i = 0; i < n; ++i) if (m_lyrics.lines[i].t <= pos) cur = (int)i; else break;
        }
        struct Placed { int y, h; };
        std::vector<Placed> pl(n);
        const int gap = std::max(4, s.size / 3);
        int total = 0;
        for (size_t i = 0; i < n; ++i) {
            // The current line is bold, which may wrap onto one more row.
            cv.set_font((int)i == cur ? fbold : fnorm);
            int h = cv.text_height(text_of(i), tw, dtf);
            pl[i] = { total, h };
            total += h + gap;
        }
        const bool follow = syncing || m_lyrics.synced;
        const int topPad = H / 3;
        double target = m_scroll;
        if (follow) {
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
        for (size_t i = 0; i < n; ++i) {
            int y = y0 + pl[i].y;
            if (y + pl[i].h < 0 || y > H) continue;
            const std::string& text = text_of(i);
            if (text.empty()) continue;
            const bool isCur = (int)i == cur && !(syncing && m_sync.done());
            cv.set_font(isCur ? fbold : fnorm);
            gfx::Rect r{ padX, y, tw, pl[i].h };
            gfx::Color col;
            if (syncing) col = isCur ? s.current : (m_sync.time(i) >= 0 ? s.synced : s.plain);
            else col = !m_lyrics.synced ? s.plain : (isCur ? s.current : s.synced);
            if (!syncing && isCur && paint_karaoke_line(cv, i, r, pos, s.karaoke, s.current, s.plain)) continue;
            cv.draw_text(text, gfx::Rect{ r.x + 1, r.y + 1, r.w, r.h }, dtf, black);
            cv.draw_text(text, r, dtf, col);
        }
        if (syncing) {
            // Instructions + progress strip along the top.
            size_t count = 0;
            for (auto& l : m_sync.lines()) if (!l.empty()) ++count;
            char hdr[200];
            if (m_sync.done())
                snprintf(hdr, sizeof hdr, "All %zu lines stamped - right-click to save", count);
            else
                snprintf(hdr, sizeof hdr, "Sync %zu/%zu - Space or click: stamp the highlighted line as it starts - Backspace: undo - Esc: cancel",
                         m_sync.stamped(), count);
            gfx::FontSpec fh{ "Segoe UI", (float)std::max(8, s.size * 2 / 3), true };
            cv.set_font(fh);
            const unsigned hf = gfx::kAlignCenter | gfx::kWordWrap;
            const int hh = cv.text_height(hdr, W - 16, hf) + 10;
            fill_alpha(cv, 0, 0, W, hh, gfx::Color(0, 0, 0), 170);
            cv.draw_text(hdr, gfx::Rect{ 8, 5, W - 16, hh - 10 }, hf, s.current);
        }
    }
    m_settling = !settled;
}

// The current line with its sung part in `sung`, the rest in `unsung`, the word being sung
// blending from one to the other. False (nothing drawn) when the line has no karaoke timing.
bool LyricsPanel::paint_karaoke_line(gfx::Canvas& cv, size_t i, const gfx::Rect& r, double pos, int mode,
                                     gfx::Color sung, gfx::Color unsung) {
    if (mode <= 0 || i >= m_lyrics.lines.size()) return false;
    const LyricLine& line = m_lyrics.lines[i];
    const double next = i + 1 < m_lyrics.lines.size() ? m_lyrics.lines[i + 1].t : -1;
    const KaraokeTiming k = karaoke_timing(line, next, mode >= 2);
    if (k.words.empty()) return false;
    int rows = 0;
    const auto pieces = layout_karaoke(line.text, k.words, r.w,
                                       [&](std::string_view t) { return cv.text_width(t); }, &rows);
    const int lh = std::max(1, cv.text_height("Ag", r.w, gfx::kSingleLine));
    const int top = r.y + std::max(0, (r.h - rows * lh) / 2);
    const unsigned f = gfx::kSingleLine | gfx::kNoClip;
    auto mix = [](int a, int b, double t) { return (int)(a + (b - a) * t + 0.5); };
    for (const auto& p : pieces) {
        const std::string_view t = std::string_view(line.text).substr(p.start, p.len);
        const double prog = p.seg < 0 ? 1.0 : karaoke_progress(k, (size_t)p.seg, pos);
        const gfx::Color c(mix(unsung.r, sung.r, prog), mix(unsung.g, sung.g, prog), mix(unsung.b, sung.b, prog));
        const gfx::Rect pr{ r.x + p.x, top + p.row * lh, cv.text_width(t) + 4, lh };
        cv.draw_text(t, gfx::Rect{ pr.x + 1, pr.y + 1, pr.w, pr.h }, f, gfx::Color(0, 0, 0));
        cv.draw_text(t, pr, f, c);
    }
    return true;
}

// ---- tap-to-sync ------------------------------------------------------------------------------

void LyricsPanel::begin_sync() {
    std::vector<std::string> lines;
    for (auto& l : m_lyrics.lines) lines.push_back(l.text);
    m_sync.begin(lines);
    if (!m_sync.active()) return;
    m_scroll = 0; m_holdUntil = 0;
    // From the top: the first tap belongs to the first line.
    auto pc = playback_control::get();
    if (pc->is_playing() && pc->playback_can_seek()) pc->playback_seek(0);
    host()->focus();
    invalidate();
}

void LyricsPanel::sync_stamp() {
    auto pc = playback_control::get();
    if (!pc->is_playing() || !m_sync.stamp(pc->playback_get_position() + read_settings(m_engine).offsetMs / 1000.0))
        return;
    m_holdUntil = 0;
    invalidate();
}

void LyricsPanel::sync_undo() {
    const int line = m_sync.undo();
    if (line < 0) return;
    // Back to just before the previous stamped line, so the undone one can be tapped again.
    double prev = 0;
    for (int i = line; i-- > 0;) if (m_sync.time(i) >= 0) { prev = m_sync.time(i) - 1.0; break; }
    auto pc = playback_control::get();
    if (pc->is_playing() && pc->playback_can_seek())
        pc->playback_seek(std::max(0.0, prev - read_settings(m_engine).offsetMs / 1000.0));
    m_holdUntil = 0;
    invalidate();
}

void LyricsPanel::save_sync(bool sidecar) {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (!np.is_valid() || !m_sync.stamped()) return;
    const TrackIds ids = ids_for(np);
    const std::string path = sidecar ? (sidecar_base(np).empty() ? std::string() : sidecar_base(np) + ".lrc") : ids.cache;
    if (path.empty() || !write_file(path, m_sync.to_lrc(ids.artist, ids.title, ids.album))) {
        ui::message_box(host(), "Lyrics", "Couldn't save the lyrics to\n" + (path.empty() ? std::string("(no location)") : path));
        return;
    }
    // Fresh timings: the old per-track correction no longer applies.
    set_track_offset(ids.key, 0);
    g_prefer[ids.key] = path;
    g_failed.erase(ids.key);
    ++g_version;
    m_sync.cancel();
    for (auto* p : g_panels) p->invalidate();
}

void LyricsPanel::show_sync_menu(int x, int y) {
    enum { kStamp = 1, kUndo, kSaveSidecar, kSaveCache, kCancel };
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    const bool canSidecar = np.is_valid() && !sidecar_base(np).empty();
    const bool any = m_sync.stamped() > 0;
    auto item = [](std::string label, int id, bool enabled = true) {
        ui::MenuItem m; m.label = std::move(label); m.id = id; m.enabled = enabled; return m;
    };
    ui::Menu menu;
    menu.push_back(item("Stamp the highlighted line (Space)", kStamp, !m_sync.done()));
    menu.push_back(item("Undo the last stamp (Backspace)", kUndo, any));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Save as .lrc next to the track", kSaveSidecar, any && canSidecar));
    menu.push_back(item("Save to the lyrics cache", kSaveCache, any));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Cancel syncing (Esc)", kCancel));
    switch (ui::popup_menu(host(), x, y, menu)) {
    case kStamp: sync_stamp(); break;
    case kUndo: sync_undo(); break;
    case kSaveSidecar: save_sync(true); break;
    case kSaveCache: save_sync(false); break;
    case kCancel: m_sync.cancel(); invalidate(); break;
    }
}

// ---- settings menu ----------------------------------------------------------------------------

void LyricsPanel::show_settings_menu(SkinEngine* engine, ui::ViewHost* owner, int x, int y) {
    if (!engine) return;
    const Settings s = read_settings(engine);
    enum { kSize = 100, kDim = 200, kOffset = 300, kTrackOffset = 400, kKaraoke = 500, kColPlain = 1, kColSynced = 2,
           kColCur = 3, kOnline = 4, kFetchNow = 5, kReset = 6, kTrackOffsetReset = 7, kSync = 8 };
    static const int kTrackSteps[] = { -500, -100, 100, 500 };
    // The now-playing track's key (per-track offset) and whether it has lyrics showing.
    std::string key; bool haveLyrics = false;
    for (auto* p : g_panels) if (!p->m_idsKey.empty()) { key = p->m_idsKey; haveLyrics = !p->m_lyrics.lines.empty(); break; }
    const int trackMs = key.empty() ? 0 : track_offset(key);
    static const int kSizes[] = { 11, 13, 15, 17, 20, 24, 28 };
    static const int kDims[] = { 0, 25, 45, 65, 80, 90 };
    static const int kOffsets[] = { -2000, -1000, -500, 0, 500, 1000, 2000 };

    auto item = [](std::string label, int id, bool checked = false) {
        ui::MenuItem m; m.label = std::move(label); m.id = id; m.checked = checked; return m;
    };
    auto sub = [](std::string label, ui::Menu children) {
        ui::MenuItem m; m.label = std::move(label); m.children = std::move(children); return m;
    };
    ui::Menu sizes, dims, offs, cols, toffs, kara;
    char l[32];
    for (int i = 0; i < 7; ++i) { snprintf(l, sizeof l, "%d pt", kSizes[i]); sizes.push_back(item(l, kSize + i, s.size == kSizes[i])); }
    for (int i = 0; i < 6; ++i) { snprintf(l, sizeof l, "%d%%", kDims[i]); dims.push_back(item(l, kDim + i, s.dim == kDims[i])); }
    for (int i = 0; i < 7; ++i) { snprintf(l, sizeof l, "%+.1f s", kOffsets[i] / 1000.0); offs.push_back(item(l, kOffset + i, s.offsetMs == kOffsets[i])); }
    for (int i = 0; i < 4; ++i) { snprintf(l, sizeof l, "%+.1f s", kTrackSteps[i] / 1000.0); toffs.push_back(item(l, kTrackOffset + i)); }
    toffs.push_back(ui::MenuItem::sep());
    snprintf(l, sizeof l, "Reset (now %+.1f s)", trackMs / 1000.0);
    toffs.push_back(item(l, kTrackOffsetReset));
    kara.push_back(item("Off", kKaraoke + 0, s.karaoke == 0));
    kara.push_back(item("Word-timed lyrics only", kKaraoke + 1, s.karaoke == 1));
    kara.push_back(item("All synced lyrics (estimated timing)", kKaraoke + 2, s.karaoke == 2));
    cols.push_back(item("Lyrics without timestamps...", kColPlain));
    cols.push_back(item("Timestamped lines...", kColSynced));
    cols.push_back(item("Current line...", kColCur));
    ui::Menu menu;
    menu.push_back(sub("Font size", sizes));
    menu.push_back(sub("Colours", cols));
    menu.push_back(sub("Cover darkening", dims));
    menu.push_back(sub("Timing offset (all tracks)", offs));
    if (!key.empty()) menu.push_back(sub("Timing offset (this track)", toffs));
    menu.push_back(sub("Karaoke highlight", kara));
    menu.push_back(ui::MenuItem::sep());
    {
        ui::MenuItem m = item("Sync these lyrics by tapping (restarts the track)...", kSync);
        m.enabled = haveLyrics;
        menu.push_back(m);
    }
    menu.push_back(item(haveLyrics ? "Search online again (replace these lyrics)" : "Search lyrics online now (lrclib.net)", kFetchNow));
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
    else if (cmd >= kKaraoke && cmd < kKaraoke + 3) engine->set_pvar("lyr.karaoke", std::to_string(cmd - kKaraoke));
    else if (cmd >= kTrackOffset && cmd < kTrackOffset + 4) set_track_offset(key, trackMs + kTrackSteps[cmd - kTrackOffset]);
    else if (cmd == kTrackOffsetReset) set_track_offset(key, 0);
    else if (cmd == kColPlain) pick("lyr.col.plain", s.plain);
    else if (cmd == kColSynced) pick("lyr.col.synced", s.synced);
    else if (cmd == kColCur) pick("lyr.col.current", s.current);
    else if (cmd == kOnline) engine->set_pvar("lyr.online", s.online ? "0" : "1");
    else if (cmd == kSync) { for (auto* p : g_panels) if (!p->m_lyrics.lines.empty() && p->host() && p->host()->visible()) { p->begin_sync(); break; } }
    else if (cmd == kFetchNow) { for (auto* p : g_panels) if (p->host() && p->host()->visible()) { p->start_fetch(); break; } }
    else if (cmd == kReset)
        for (const char* k : { "lyr.size", "lyr.dim", "lyr.offset", "lyr.online", "lyr.col.plain", "lyr.col.synced", "lyr.col.current", "lyr.karaoke" })
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
    if (m_sync.active()) {
        host()->focus();
        if (e.button == ui::MouseButton::Left) sync_stamp();
        else if (e.button == ui::MouseButton::Right) show_sync_menu(e.x, e.y);
        return;
    }
    if (e.button == ui::MouseButton::Left) {
        if (!m_link.empty() && m_link.contains(e.x, e.y)) start_fetch();
    } else if (e.button == ui::MouseButton::Right) {
        show_settings_menu(m_engine, host(), e.x, e.y);
    }
}

bool LyricsPanel::on_key_down(int key, unsigned mods) {
    if (!m_sync.active() || (mods & (ui::kCtrl | ui::kAlt))) return false;
    switch (key) {
    case ' ': case ui::kKeyEnter: case ui::kKeyDown: sync_stamp(); return true;
    case ui::kKeyBackspace: case ui::kKeyUp: sync_undo(); return true;
    case ui::kKeyEscape: m_sync.cancel(); invalidate(); return true;
    }
    return false;
}

} // namespace pui
