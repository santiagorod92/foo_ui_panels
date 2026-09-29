#include "album_list.h"
#include "skin_engine.h"
#include "image.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_album_list";
static const char* kPlaylistName = "Album Browser";

// foobar strings are UTF-8 — draw via DrawTextW (DrawTextA mojibakes non-ASCII).
static int dtW(HDC dc, const char* s, LPRECT r, UINT f) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return DrawTextW(dc, w.c_str(), -1, r, f);
}

static int dtWUpper(HDC dc, const char* s, LPRECT r, UINT f) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    CharUpperBuffW(&w[0], (DWORD)w.size());
    return DrawTextW(dc, w.c_str(), -1, r, f);
}

// Compiled titleformat scripts (lazy, shared) — same grouping key shape as playlist_view.cpp.
struct ALScripts {
    service_ptr_t<titleformat_object> key, artist, album, cover;
    bool ok = false;
    void ensure() {
        if (ok) return;
        auto c = titleformat_compiler::get();
        c->compile_safe(key,    "[%album artist%]|[%album%]");
        c->compile_safe(artist, "[%album artist%]");
        c->compile_safe(album,  "[%album%]");
        c->compile_safe(cover,  "$replace(%path%,%filename_ext%,*folder*.*)");
        ok = true;
    }
};
static ALScripts g_al;

static pfc::string8 fmt(const metadb_handle_ptr& h, const service_ptr_t<titleformat_object>& s) {
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, s, nullptr); return o;
}

static navidrome::navidrome_library_api::ptr navidrome_api() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

static std::string lower_str(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

void AlbumList::register_class() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
}

HWND AlbumList::create(HWND parent, SkinEngine* engine, bool coverflow) {
    register_class();
    m_engine = engine;
    m_coverflow = coverflow;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_wnd;
}

void AlbumList::ensure_built() { if (!m_built) rebuild(); }

// Scans the whole Media Library once and groups by album artist + album. Library size is
// typically thousands of tracks, not millions — a full rescan is cheap enough to do on demand
// (create + manual refresh) without needing incremental library-callback tracking.
void AlbumList::rebuild() {
    m_built = true;
    g_al.ensure();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);

    std::vector<std::pair<pfc::string8, t_size>> keyed; // (group key, index into `all`)
    keyed.reserve(all.get_count());
    for (t_size i = 0; i < all.get_count(); ++i)
        keyed.push_back({ fmt(all[i], g_al.key), i });
    std::sort(keyed.begin(), keyed.end(),
              [](auto& a, auto& b) { return strcmp(a.first, b.first) < 0; });

    m_groups.clear();
    for (auto& kv : keyed) {
        metadb_handle_ptr h = all[kv.second];
        if (m_groups.empty() || strcmp(m_groups.back().key, kv.first) != 0) {
            Group g; g.key = kv.first; g.artist = fmt(h, g_al.artist); g.album = fmt(h, g_al.album);
            g.cover_track = h;
            m_groups.push_back(std::move(g));
        }
        m_groups.back().items.push_back(h);
    }
    m_selected = -1; m_hover = -1; m_scroll = 0;
    start_remote_load();
}

// Albums foo_navidrome publishes (none of them are in the Media Library until played/queued).
// The blocking list call runs on a worker; results are merged in batches so the grid fills as
// the server answers.
void AlbumList::start_remote_load() {
    ++m_gen;
    m_remote_err.clear(); m_remote_loading = false;
    m_cover_failed.clear();
    auto api = navidrome_api();
    if (api.is_empty()) return;
    if (!api->is_configured()) { m_remote_err = "Navidrome is not configured (Preferences > Tools > Navidrome)."; return; }
    m_remote_loading = true;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; AlbumList* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_album_sink {
            std::vector<Group> batch;
            std::function<void(std::vector<Group>&&)> post;
            void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char*, const char* coverArtId, int, int) override {
                Group g; g.remote = true; g.remote_id = albumId; g.remote_cover = coverArtId;
                g.artist = artistName; g.album = albumName;
                g.key = (lower_str(artistName) + "|" + lower_str(albumName)).c_str();
                batch.push_back(std::move(g));
                if (batch.size() >= 120) flush();
            }
            void flush() { if (!batch.empty()) { post(std::move(batch)); batch.clear(); } }
        } sink;
        sink.post = [=](std::vector<Group>&& b) {
            auto shared = std::make_shared<std::vector<Group>>(std::move(b));
            fb2k::inMainThread([=]() {
                if (!alive->load() || self->m_gen != gen) return;
                self->merge_remote(std::move(*shared));
            });
        };
        pfc::string8 err;
        bool ok = false;
        try { ok = api->list_albums(sink, *abort, err); } catch (...) { err = "Navidrome library request failed."; }
        sink.flush();
        std::string errs = ok ? std::string() : std::string(err.c_str());
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->m_remote_loading = false; self->m_remote_err = errs;
            InvalidateRect(self->m_wnd, nullptr, FALSE);
        });
    });
}

void AlbumList::merge_remote(std::vector<Group>&& add) {
    std::string selKey = (m_selected >= 0 && m_selected < (int)m_groups.size())
        ? std::string(m_groups[m_selected].key.c_str()) : std::string();
    std::vector<std::string> before; // keys by index before the merge (cover-flow keeps its centre)
    for (auto& g : m_groups) before.push_back(g.key.c_str());
    std::set<std::string> have;
    for (auto& g : m_groups) have.insert(lower_str(g.key.c_str()));
    for (auto& g : add) {
        if (have.count(lower_str(g.key.c_str()))) continue; // already in the local library
        have.insert(lower_str(g.key.c_str()));
        m_groups.push_back(std::move(g));
    }
    std::stable_sort(m_groups.begin(), m_groups.end(), [](const Group& a, const Group& b) {
        return lower_str(a.key.c_str()) < lower_str(b.key.c_str());
    });
    std::string cfKey;
    if (m_coverflow && m_cf_target >= 0 && m_cf_target < (int)before.size()) cfKey = before[m_cf_target];
    m_selected = -1; m_hover = -1;
    if (!selKey.empty())
        for (size_t i = 0; i < m_groups.size(); ++i) if (selKey == m_groups[i].key.c_str()) { m_selected = (int)i; break; }
    if (m_coverflow) {
        int idx = -1;
        if (m_cf_user && !cfKey.empty())
            for (size_t i = 0; i < m_groups.size(); ++i) if (cfKey == m_groups[i].key.c_str()) { idx = (int)i; break; }
        if (idx < 0) idx = group_index_for_now_playing();
        m_cf_target = idx; m_cf_pos = (float)idx; m_cf_init = true;
    }
    InvalidateRect(m_wnd, nullptr, FALSE);
}

void AlbumList::request_cover(const std::string& coverId, int size) {
    const std::string ck = coverId + "@" + std::to_string(size);
    if (coverId.empty() || m_covers.count(ck) || m_cover_pending.count(ck) ||
        m_cover_failed.count(ck) || m_cover_inflight >= 4) return;
    auto api = navidrome_api();
    if (api.is_empty()) return;
    m_cover_pending.insert(ck); ++m_cover_inflight;
    auto alive = m_alive; auto abort = m_abort; AlbumList* self = this;
    fb2k::splitTask([=]() {
        album_art_data_ptr data;
        try { data = api->fetch_cover(coverId.c_str(), size, *abort); } catch (...) {}
        fb2k::inMainThread([=]() {
            if (!alive->load()) return;
            --self->m_cover_inflight; self->m_cover_pending.erase(ck);
            if (data.is_valid()) self->m_covers[ck] = data; else self->m_cover_failed.insert(ck);
            InvalidateRect(self->m_wnd, nullptr, FALSE);
        });
    });
}

void AlbumList::layout_metrics(int clientW, int& cols, int& cellW, int& cellH, int& gutter, int& margin) const {
    // fooAvA's COLLECTION grid: tight dark tiles, four across in the standard-width panel.
    gutter = 5; margin = 6;
    cols = std::max(1, std::min(8, (clientW - margin * 2 + gutter) / (100 + gutter)));
    cellW = (clientW - margin * 2 - (cols - 1) * gutter) / cols;
    cellH = (cellW - 10) * 490 / 559 + 10 + 32; // CD case (559x490) + label lines
}

int AlbumList::item_at(int x, int y) const {
    if (m_coverflow) {
        POINT p = { x, y };
        for (auto it = m_cf_hits.rbegin(); it != m_cf_hits.rend(); ++it) if (PtInRect(&it->second, p)) return it->first;
        return -1;
    }
    RECT rc; GetClientRect(m_wnd, &rc);
    int cols, cellW, cellH, gutter, margin;
    layout_metrics(rc.right, cols, cellW, cellH, gutter, margin);
    int gy = y + m_scroll - margin;
    if (x < margin || gy < 0) return -1;
    int col = (x - margin) / (cellW + gutter);
    int row = gy / (cellH + gutter);
    if (col >= cols) return -1;
    if ((x - margin) % (cellW + gutter) > cellW) return -1; // in the gutter
    if (gy % (cellH + gutter) > cellH) return -1;
    int idx = row * cols + col;
    return (idx >= 0 && idx < (int)m_groups.size()) ? idx : -1;
}

void AlbumList::paint() {
    ensure_built();
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    bool drewbg = m_engine && m_engine->draw_canvas_background(mem, m_wnd, rc.right, rc.bottom);
    if (!drewbg) fill_gradient_v(mem, 0, 0, rc.right, rc.bottom, RGB(28, 28, 32));
    else if (m_coverflow) fill_alpha(mem, 0, 0, rc.right, rc.bottom, RGB(20, 20, 22), 90); // reflections need a darker floor
    SetBkMode(mem, TRANSPARENT);

    COLORREF accent = RGB(0, 140, 220); if (m_engine) m_engine->theme_color(accent);
    static HFONT fTitle = CreateFontA(-11, 0,0,0, FW_BOLD,   0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");
    static HFONT fArtist = CreateFontA(-10, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");

    int cols, cellW, cellH, gutter, margin;
    layout_metrics(rc.right, cols, cellW, cellH, gutter, margin);
    const std::string nocover = m_engine ? m_engine->base_dir() + "/images/fooAVA/nocoverb.png" : std::string();
    const std::string casepng = m_engine ? m_engine->base_dir() + "/images/fooAVA/cdcaseck3.png" : std::string();

    if (m_groups.empty()) {
        SetTextColor(mem, RGB(200, 205, 220)); SelectObject(mem, fArtist);
        RECT r = rc;
        pfc::string8 msg;
        if (m_remote_loading) msg = "Loading Navidrome library...";
        else if (!m_remote_err.empty()) msg = m_remote_err.c_str();
        else msg = library_manager::get()->is_library_enabled()
            ? "No albums found in the Media Library." : "Media Library is not configured (Preferences > Media Library).";
        dtW(mem, msg.get_ptr(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    const bool cf = m_coverflow && !m_groups.empty();
    if (cf) paint_coverflow(mem, rc);
    for (size_t i = 0; !cf && i < m_groups.size(); ++i) {
        int col = (int)(i % cols), row = (int)(i / cols);
        int x = margin + col * (cellW + gutter), y = margin + row * (cellH + gutter) - m_scroll;
        if (y + cellH < 0 || y > rc.bottom) continue;

        const auto& g = m_groups[i];
        // No tile backdrop: the skin wallpaper shows through around each case.
        if ((int)i == m_selected || (int)i == m_hover) {
            COLORREF hlc = (int)i == m_selected ? accent : RGB(255,255,255);
            int a = (int)i == m_selected ? 70 : 32;
            fill_alpha(mem, x, y, cellW, cellH, hlc, a);
        }
        // Each album sits in a jewel case (cdcaseck3.png, spine on the left): the cover goes in the
        // case's window, the case art on top.
        const int inset = 5, caseW = cellW - inset * 2, caseH = caseW * 490 / 559;
        const int ax = x + inset, ay = y + inset;
        const int cx = ax + caseW * 57 / 559, cy = ay + caseH * 8 / 490;
        const int cw = caseW * 494 / 559, ch = caseH * 474 / 490;
        bool drew = false;
        if (g.remote) {
            const std::string ck = g.remote_cover + "@160";
            auto it = m_covers.find(ck);
            if (it != m_covers.end()) {
                drew = draw_image_data(mem, ck, it->second->data(), it->second->size(), cx, cy, cw, ch);
            } else {
                request_cover(g.remote_cover, 160);
                if (m_cover_failed.count(ck)) drew = draw_image(mem, nocover, cx, cy, cw, ch);
                else fill_alpha(mem, cx, cy, cw, ch, RGB(40, 42, 52), 200);
                drew = true;
            }
        } else {
            pfc::string8 cov = fmt(g.cover_track, g_al.cover);
            drew = draw_cover_art(mem, cov.get_ptr(), g.cover_track, cx, cy, cw, ch);
        }
        if (!drew) draw_image(mem, nocover, cx, cy, cw, ch);
        draw_image(mem, casepng, ax, ay, caseW, caseH);
        const int art = caseH; // label rows start below the case

        SelectObject(mem, fTitle); SetTextColor(mem, RGB(235, 240, 255));
        RECT rt = { x + 3, ay + art + 3, x + cellW - 3, ay + art + 17 };
        dtWUpper(mem, g.album.get_ptr(), &rt, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(mem, fArtist); SetTextColor(mem, RGB(150, 160, 185));
        RECT ra = { x + 3, ay + art + 17, x + cellW - 3, ay + art + 30 };
        dtWUpper(mem, g.artist.get_ptr(), &ra, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    int rows = ((int)m_groups.size() + cols - 1) / cols;
    m_content_h = margin * 2 + rows * (cellH + gutter);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

int AlbumList::group_index_for_now_playing() const {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (np.is_valid() && !m_groups.empty()) {
        g_al.ensure();
        std::string ar = lower_str(fmt(np, g_al.artist).c_str()), al = lower_str(fmt(np, g_al.album).c_str());
        for (size_t i = 0; i < m_groups.size(); ++i)
            if (lower_str(m_groups[i].album.c_str()) == al && lower_str(m_groups[i].artist.c_str()) == ar) return (int)i;
    }
    return 0;
}

void AlbumList::set_target(int idx) {
    if (m_groups.empty()) return;
    idx = std::max(0, std::min((int)m_groups.size() - 1, idx));
    m_cf_user = true;
    m_cf_target = idx; m_selected = idx;
    SetTimer(m_wnd, 1, 16, nullptr);
    InvalidateRect(m_wnd, nullptr, FALSE);
}

// Cover-flow carousel: the centre cover face-on, neighbours turned away as perspective
// trapezoids (taller outer edge), each with a faint reflection. Glides between albums.
void AlbumList::paint_coverflow(HDC mem, const RECT& rc) {
    const int W = rc.right, H = rc.bottom, n = (int)m_groups.size();
    if (!m_cf_init) { m_cf_init = true; m_cf_target = group_index_for_now_playing(); m_cf_pos = (float)m_cf_target; }
    m_cf_target = std::max(0, std::min(n - 1, m_cf_target));
    const float pos = m_cf_pos;
    const int c = (int)std::lround(pos), range = 5;
    const float S = std::min(H * 0.48f, W * 0.32f);
    const float cx = W * 0.5f, yMid = H * 0.38f;
    const float sideOff = S * 0.68f, spacing = S * 0.20f;
    m_cf_hits.clear();

    auto draw_one = [&](int idx) {
        if (idx < 0 || idx >= n) return;
        const Group& g = m_groups[idx];
        const float d = idx - pos, ad = std::fabs(d), t = std::min(ad, 1.0f), sign = d < 0 ? -1.0f : 1.0f;
        const float xc = cx + sign * (sideOff * t + spacing * std::max(ad - 1.0f, 0.0f));
        const float w = S * (1.0f - 0.62f * t);
        const float hOuter = S * (1.0f - 0.08f * t), hInner = S * (1.0f - 0.30f * t);
        const float x0 = xc - w / 2, x1 = xc + w / 2;
        const float h0 = d < 0 ? hOuter : hInner, h1 = d < 0 ? hInner : hOuter;

        ImageHandle im = nullptr;
        if (g.remote) {
            const std::string ck = g.remote_cover + "@300";
            auto it = m_covers.find(ck);
            if (it != m_covers.end()) im = data_image(ck, it->second->data(), it->second->size());
            else request_cover(g.remote_cover, 300);
        } else {
            pfc::string8 cov = fmt(g.cover_track, g_al.cover);
            im = cover_image(cov.get_ptr(), g.cover_track);
        }
        if (im) {
            draw_image_strips(mem, im, x0, x1, yMid, h0, h1, 255, false);
            draw_image_strips(mem, im, x0, x1, yMid, h0, h1, 70, true);
        } else {
            const float hm = std::max(h0, h1);
            POINT q[4] = { {(LONG)x0, (LONG)(yMid - h0 / 2)}, {(LONG)x1, (LONG)(yMid - h1 / 2)},
                           {(LONG)x1, (LONG)(yMid + h1 / 2)}, {(LONG)x0, (LONG)(yMid + h0 / 2)} };
            HBRUSH b = CreateSolidBrush(RGB(58, 62, 76)); HGDIOBJ ob = SelectObject(mem, b);
            HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
            Polygon(mem, q, 4);
            SelectObject(mem, ob); SelectObject(mem, op); DeleteObject(b);
            (void)hm;
        }
        RECT hit = { (LONG)x0, (LONG)(yMid - std::max(h0, h1) / 2), (LONG)x1, (LONG)(yMid + std::max(h0, h1) / 2) };
        m_cf_hits.push_back({ idx, hit });
    };
    for (int k = range; k >= 1; --k) draw_one(c - k);
    for (int k = range; k >= 1; --k) draw_one(c + k);
    draw_one(c);

    // Caption for the album nearest the centre.
    const Group& cg = m_groups[c];
    static HFONT fTitle = CreateFontA(-17, 0,0,0, FW_BOLD,   0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");
    static HFONT fSub   = CreateFontA(-13, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");
    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, fTitle); SetTextColor(mem, RGB(245, 247, 255));
    RECT rt = { 12, H - 50, W - 12, H - 28 };
    dtW(mem, cg.album.get_ptr(), &rt, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(mem, fSub); SetTextColor(mem, RGB(175, 185, 210));
    char sub[64]; snprintf(sub, sizeof sub, "  -  %d / %d", c + 1, n);
    pfc::string8 line = cg.artist; line << sub;
    RECT ra = { 12, H - 28, W - 12, H - 8 };
    dtW(mem, line.get_ptr(), &ra, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

// Replaces the dedicated "Album Browser" playlist with this album and starts playing it —
// mirrors the "browse the library, double-click to play" UX every album browser offers.
void AlbumList::play_group(int idx) {
    if (idx < 0 || idx >= (int)m_groups.size()) return;
    if (m_groups[idx].remote) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_album(m_groups[idx].remote_id.c_str(), true, true);
        return;
    }
    auto pm = playlist_manager::get();
    t_size n = pm->get_playlist_count(), target = pfc_infinite;
    for (t_size i = 0; i < n; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        if (nm == kPlaylistName) { target = i; break; }
    }
    if (target == pfc_infinite) target = pm->create_playlist(kPlaylistName, strlen(kPlaylistName), pfc_infinite);
    if (target == pfc_infinite) return;
    pm->playlist_remove_items(target, bit_array_true());
    metadb_handle_list list; for (auto& h : m_groups[idx].items) list.add_item(h);
    pm->playlist_insert_items(target, 0, list, bit_array_true());
    pm->set_active_playlist(target);
    pm->playlist_execute_default_action(target, 0);
}

void AlbumList::on_click(int x, int y, bool dbl) {
    SetFocus(m_wnd);
    int idx = item_at(x, y);
    if (idx < 0) return;
    m_selected = idx;
    InvalidateRect(m_wnd, nullptr, FALSE);
    if (dbl) play_group(idx);
    else if (m_coverflow && idx != m_cf_target) set_target(idx);
}

void AlbumList::on_rclick(int x, int y) {
    SetFocus(m_wnd);
    int idx = item_at(x, y);
    if (idx < 0) return;
    m_selected = idx;
    InvalidateRect(m_wnd, nullptr, FALSE);
    POINT pt = { x, y }; ClientToScreen(m_wnd, &pt);
    if (m_groups[idx].remote) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Play album");
        AppendMenuW(menu, MF_STRING, 2, L"Add to current playlist");
        UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_wnd, nullptr);
        DestroyMenu(menu);
        auto api = navidrome_api();
        if (cmd && api.is_valid()) api->play_album(m_groups[idx].remote_id.c_str(), cmd == 1, cmd == 1);
        return;
    }
    metadb_handle_list sel; for (auto& h : m_groups[idx].items) sel.add_item(h);
    contextmenu_manager::win32_run_menu_context(m_wnd, sel, &pt, contextmenu_manager::flag_show_shortcuts);
}

LRESULT CALLBACK AlbumList::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    AlbumList* self = reinterpret_cast<AlbumList*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<AlbumList*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_SIZE: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_DESTROY:
        if (self) { self->m_alive->store(false); self->m_abort->abort(); }
        return 0;
    case WM_TIMER:
        if (self && self->m_coverflow) {
            float diff = (float)self->m_cf_target - self->m_cf_pos;
            if (std::fabs(diff) < 0.004f) { self->m_cf_pos = (float)self->m_cf_target; KillTimer(wnd, 1); }
            else self->m_cf_pos += diff * 0.22f;
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (self && self->m_coverflow) {
            self->set_target(self->m_cf_target - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            return 0;
        }
        if (self) {
            int d = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            self->m_scroll -= d * 60;
            if (self->m_scroll < 0) self->m_scroll = 0;
            RECT rc; GetClientRect(wnd, &rc);
            int maxs = self->m_content_h - rc.bottom; if (maxs < 0) maxs = 0;
            if (self->m_scroll > maxs) self->m_scroll = maxs;
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self) {
            if (!self->m_tracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
                TrackMouseEvent(&tme); self->m_tracking = true;
            }
            int h = self->item_at(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (h != self->m_hover) { self->m_hover = h; InvalidateRect(wnd, nullptr, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self) { self->m_tracking = false;
            if (self->m_hover != -1) { self->m_hover = -1; InvalidateRect(wnd, nullptr, FALSE); } }
        return 0;
    case WM_LBUTTONDOWN:   if (self) self->on_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false); return 0;
    case WM_LBUTTONDBLCLK: if (self) self->on_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true); return 0;
    case WM_RBUTTONDOWN:   if (self) self->on_rclick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_KEYDOWN:
        if (self && self->m_coverflow) {
            int t = self->m_cf_target;
            switch (wp) {
            case VK_LEFT: self->set_target(t - 1); return 0;
            case VK_RIGHT: self->set_target(t + 1); return 0;
            case VK_PRIOR: self->set_target(t - 5); return 0;
            case VK_NEXT: self->set_target(t + 5); return 0;
            case VK_HOME: self->set_target(0); return 0;
            case VK_END: self->set_target((int)self->m_groups.size() - 1); return 0;
            case VK_RETURN: self->play_group(t); return 0;
            }
        }
        if (self && wp == VK_RETURN && self->m_selected >= 0) { self->play_group(self->m_selected); return 0; }
        if (self && wp == VK_F5) { self->rebuild(); InvalidateRect(wnd, nullptr, FALSE); return 0; }
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
