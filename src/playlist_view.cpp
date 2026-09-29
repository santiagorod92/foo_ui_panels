#include "playlist_view.h"
#include "skin_engine.h"
#include "image.h"
#include <vector>
#include <string>
#include <windowsx.h>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_playlist";
static const int HEADER_H = 46;
static const int ROW_H = 19;

// foobar strings are UTF-8 — draw them via DrawTextW (DrawTextA would mojibake "’" -> "â€™").
// Drop-in for dtW(dc,s,-1,r,f): the length arg is ignored (always NUL-terminated).
static int dtW(HDC dc, const char* s, int, LPRECT r, UINT f) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return DrawTextW(dc, w.c_str(), -1, r, f);
}

// Compiled titleformat scripts (lazy, shared).
struct PLScripts {
    service_ptr_t<titleformat_object> key, artist, album, year, genre, row, len, rating, cover;
    bool ok = false;
    void ensure() {
        if (ok) return;
        auto c = titleformat_compiler::get();
        c->compile_safe(key,    "[%album artist%]|[%album%]");
        c->compile_safe(artist, "[%album artist%]");
        c->compile_safe(album,  "[%album%]");
        c->compile_safe(year,   "[%date%]");
        c->compile_safe(genre,  "[%genre%]");
        c->compile_safe(row,    "$num(%tracknumber%,2). [%title%]");
        c->compile_safe(len,    "[%length%]");
        // navidrome:// tracks keep their rating in NAVIDROME_RATING (foo_navidrome), not RATING
        c->compile_safe(rating, "$if2(%navidrome_rating%,[%rating%])");
        c->compile_safe(cover,  "$replace(%path%,%filename_ext%,*folder*.*)");
        ok = true;
    }
};
static PLScripts g_pl;

static pfc::string8 fmt(const metadb_handle_ptr& h, const service_ptr_t<titleformat_object>& s) {
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, s, nullptr); return o;
}

// Groups of consecutive playlist items sharing an album key.
struct Group { std::vector<t_size> items; };
static std::vector<Group> build_groups() {
    g_pl.ensure();
    std::vector<Group> groups;
    auto pm = playlist_manager::get();
    t_size n = pm->activeplaylist_get_item_count();
    pfc::string8 last; bool first = true;
    for (t_size i = 0; i < n; ++i) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, i);
        pfc::string8 k = fmt(h, g_pl.key);
        if (first || strcmp(k, last) != 0) { groups.push_back(Group{}); last = k; first = false; }
        groups.back().items.push_back(i);
    }
    return groups;
}

void PlaylistView::register_class() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.style         = CS_DBLCLKS; // receive WM_LBUTTONDBLCLK
    RegisterClassExW(&wc);
}

HWND PlaylistView::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 1000, nullptr); // refresh now-playing / elapsed
    return m_wnd;
}

void PlaylistView::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    std::string sbase0 = m_engine ? m_engine->base_dir() : std::string();
    // Background: the skin wallpaper (so it matches the main window) under a translucent dark
    // overlay so the track list stays readable. Falls back to a navy gradient. The row highlight
    // colour (m_hl) is tinted from the wallpaper so it matches instead of being a stark blue.
    bool drewbg = false;
    COLORREF hl = RGB(36, 86, 180); if (m_engine) m_engine->theme_color(hl);
    if (m_engine && !sbase0.empty() && m_engine->pvar_int("backgroundd", 0)) {
        std::string wp = m_engine->pvar_str("background"); // e.g. "walls\1.jpg"
        if (!wp.empty()) {
            for (auto& ch : wp) if (ch == '\\') ch = '/';
            std::string wppath = sbase0 + "/images/fooAVA/" + wp;
            // Transparent: show exactly what the skin canvas drew beneath us (its wallpaper at
            // the skin's own alpha), so the list sits on the same background as the rest.
            drewbg = m_engine->draw_canvas_snapshot(mem, m_wnd);
            if (!drewbg) {
                drewbg = draw_image(mem, wppath, 0, 0, rc.right, rc.bottom);
                if (drewbg) fill_alpha(mem, 0, 0, rc.right, rc.bottom, RGB(24, 24, 26), 215);
            }
            if (drewbg) {
                COLORREF avg;
                if (image_avg_color(wppath, avg)) { // tint highlight to the wallpaper
                    auto up = [](int v){ int r = v * 3; return r > 255 ? 255 : (r < 40 ? 40 : r); };
                    hl = RGB(up(GetRValue(avg)), up(GetGValue(avg)), up(GetBValue(avg)));
                }
            }
        }
    }
    if (!drewbg && m_engine) drewbg = m_engine->draw_canvas_snapshot(mem, m_wnd); // wallpaper off
    if (!drewbg) fill_gradient_v(mem, 0, 0, rc.right, rc.bottom, RGB(30, 30, 34));
    SetBkMode(mem, TRANSPARENT);

    static HFONT fHdr = CreateFontA(-15, 0,0,0, FW_BOLD,   0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");
    static HFONT fSub = CreateFontA(-12, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");
    static HFONT fRow = CreateFontA(-12, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0, "Segoe UI");

    std::string sbase = m_engine ? m_engine->base_dir() : std::string();

    auto groups = build_groups();
    auto pm = playlist_manager::get();
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);

    int y = -m_scroll;
    for (const auto& g : groups) {
        if (g.items.empty()) continue;
        // ---- album header ----
        if (y + HEADER_H > 0 && y < rc.bottom) {
            metadb_handle_ptr h0; pm->activeplaylist_get_item_handle(h0, g.items[0]);
            // cover thumb
            if (!sbase.empty()) {
                pfc::string8 cov = fmt(h0, g_pl.cover);
                // folder.* on disk, else the album-art pipeline (navidrome:// etc. have no folder)
                draw_cover_art(mem, cov.get_ptr(), h0, 5, y + 4, 38, 38);
            }
            SetTextColor(mem, RGB(235, 240, 255)); SelectObject(mem, fHdr);
            pfc::string8 art = fmt(h0, g_pl.artist);
            RECT r1 = { 50, y + 2, rc.right - 70, y + 18 };
            dtW(mem, art.get_ptr(), -1, &r1, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            // track count, right
            char cnt[32]; wsprintfA(cnt, "%u TRACKS", (unsigned)g.items.size());
            RECT rc1 = { rc.right - 120, y + 3, rc.right - 6, y + 18 };
            SetTextColor(mem, RGB(150, 170, 210)); SelectObject(mem, fSub);
            dtW(mem, cnt, -1, &rc1, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            // album
            SelectObject(mem, fSub); SetTextColor(mem, RGB(200, 210, 235));
            pfc::string8 alb = fmt(h0, g_pl.album);
            RECT r2 = { 50, y + 18, rc.right - 70, y + 32 };
            dtW(mem, alb.get_ptr(), -1, &r2, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            // year + genre
            SetTextColor(mem, RGB(140, 160, 200));
            pfc::string8 yr = fmt(h0, g_pl.year);
            RECT r3 = { 50, y + 31, rc.right - 70, y + 45 };
            dtW(mem, yr.get_ptr(), -1, &r3, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            pfc::string8 gen = fmt(h0, g_pl.genre);
            RECT rg = { rc.right - 160, y + 31, rc.right - 6, y + 45 };
            dtW(mem, gen.get_ptr(), -1, &rg, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            // separator line
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 55, 90));
            HGDIOBJ op = SelectObject(mem, pen);
            MoveToEx(mem, 4, y + HEADER_H - 1, nullptr); LineTo(mem, rc.right - 4, y + HEADER_H - 1);
            SelectObject(mem, op); DeleteObject(pen);
        }
        y += HEADER_H;
        // ---- track rows ----
        for (t_size idx : g.items) {
            if (y + ROW_H > 0 && y < rc.bottom) {
                metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
                bool playing = np.is_valid() && h == np;
                bool selected = pm->activeplaylist_is_item_selected(idx);
                if (playing) { // now-playing: gradient bar tinted to the wallpaper
                    fill_gradient_v(mem, 2, y, rc.right - 4, ROW_H, hl);
                } else if (selected) { // click selection: translucent band + bright underline
                    fill_alpha(mem, 2, y, rc.right - 4, ROW_H, hl, 90);
                    auto lt = [](int v){ int r = v + 60; return r > 255 ? 255 : r; };
                    HPEN pen = CreatePen(PS_SOLID, 1, RGB(lt(GetRValue(hl)), lt(GetGValue(hl)), lt(GetBValue(hl))));
                    HGDIOBJ op = SelectObject(mem, pen);
                    MoveToEx(mem, 6, y + ROW_H - 1, nullptr); LineTo(mem, rc.right - 8, y + ROW_H - 1);
                    SelectObject(mem, op); DeleteObject(pen);
                }
                SelectObject(mem, fRow);
                SetTextColor(mem, (playing || selected) ? RGB(255, 255, 255) : RGB(205, 215, 235));
                pfc::string8 line = fmt(h, g_pl.row);
                RECT rr = { 10, y + 1, rc.right - 150, y + ROW_H };
                dtW(mem, line.get_ptr(), -1, &rr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                // duration (right)
                pfc::string8 ln = fmt(h, g_pl.len);
                RECT rd = { rc.right - 46, y + 1, rc.right - 8, y + ROW_H };
                dtW(mem, ln.get_ptr(), -1, &rd, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                // rating stars (skin images), left of duration
                if (!sbase.empty()) {
                    int r = ((int)idx == m_hover_row)   // live preview while hovering the stars
                                ? m_hover_stars
                                : atoi(fmt(h, g_pl.rating).get_ptr());
                    if (r < 0) r = 0; if (r > 5) r = 5;
                    char p[8]; wsprintfA(p, "%ds1.png", r);
                    draw_image(mem, sbase + "/images/fooAVA/rating_stars24/" + p,
                               rc.right - 46 - 60, y + (ROW_H - 11) / 2, 55, 11);
                }
            }
            y += ROW_H;
        }
    }
    m_content_h = y + m_scroll;

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

int PlaylistView::item_at(int cy) {
    auto groups = build_groups();
    int y = -m_scroll;
    for (const auto& g : groups) {
        if (g.items.empty()) continue;
        y += HEADER_H;
        for (t_size idx : g.items) {
            if (cy >= y && cy < y + ROW_H) return (int)idx;
            y += ROW_H;
        }
    }
    return -1;
}

void PlaylistView::on_click(int x, int y, bool dbl, bool shift, bool ctrl) {
    int idx = item_at(y);
    if (idx < 0) return;
    auto pm = playlist_manager::get();
    // Click on the rating stars → set this track's rating (1..5) like the skin.
    RECT rc; GetClientRect(m_wnd, &rc);
    const int starX = rc.right - 106, starW = 55; // must match the paint() star rect
    if (!dbl && !shift && !ctrl && x >= starX && x < starX + starW) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
        int star = (x - starX) * 5 / starW + 1; if (star < 1) star = 1; if (star > 5) star = 5;
        if (m_engine) m_engine->set_rating(h, star);
        InvalidateRect(m_wnd, nullptr, FALSE);
        return;
    }
    if (shift && m_anchor >= 0) {                 // range select [anchor..idx]
        int a = m_anchor < idx ? m_anchor : idx, b = m_anchor < idx ? idx : m_anchor;
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection(bit_array_range(a, b - a + 1, true), bit_array_true());
    } else if (ctrl) {                            // toggle idx
        pm->activeplaylist_set_selection_single(idx, !pm->activeplaylist_is_item_selected(idx));
        m_anchor = idx;
    } else {                                      // single select
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        m_anchor = idx;
    }
    pm->activeplaylist_set_focus_item(idx);
    if (dbl) pm->activeplaylist_execute_default_action(idx); // play
    InvalidateRect(m_wnd, nullptr, FALSE);
}

void PlaylistView::on_rclick(int x, int y) {
    auto pm = playlist_manager::get();
    int idx = item_at(y);
    if (idx >= 0 && !pm->activeplaylist_is_item_selected(idx)) { // right-click outside selection → select it
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        pm->activeplaylist_set_focus_item(idx);
        m_anchor = idx;
        InvalidateRect(m_wnd, nullptr, FALSE);
    }
    metadb_handle_list sel; pm->activeplaylist_get_selected_items(sel);
    if (sel.get_count() == 0) return;
    POINT pt = { x, y }; ClientToScreen(m_wnd, &pt);
    // The classic foobar context menu (Properties / tagging / etc.) for the selected tracks.
    contextmenu_manager::win32_run_menu_context(m_wnd, sel, &pt,
                                                contextmenu_manager::flag_show_shortcuts);
    InvalidateRect(m_wnd, nullptr, FALSE);
}

LRESULT CALLBACK PlaylistView::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    PlaylistView* self = reinterpret_cast<PlaylistView*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<PlaylistView*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_SIZE: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL:
        if (self) {
            int d = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            self->m_scroll -= d * ROW_H * 3;
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
            int x = GET_X_LPARAM(lp), yy = GET_Y_LPARAM(lp);
            RECT rc; GetClientRect(wnd, &rc);
            const int starX = rc.right - 106, starW = 55;
            int hr = -1, hs = 0, idx = self->item_at(yy);
            if (idx >= 0 && x >= starX && x < starX + starW) {
                hr = idx; hs = (x - starX) * 5 / starW + 1; if (hs < 1) hs = 1; if (hs > 5) hs = 5;
            }
            if (hr != self->m_hover_row || hs != self->m_hover_stars) {
                self->m_hover_row = hr; self->m_hover_stars = hs;
                InvalidateRect(wnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self) { self->m_tracking = false;
            if (self->m_hover_row != -1) { self->m_hover_row = -1; InvalidateRect(wnd, nullptr, FALSE); } }
        return 0;
    case WM_LBUTTONDOWN:   if (self) self->on_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false,
                                                     (wp & MK_SHIFT) != 0, (wp & MK_CONTROL) != 0); return 0;
    case WM_LBUTTONDBLCLK: if (self) self->on_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true, false, false); return 0;
    case WM_RBUTTONDOWN:   if (self) self->on_rclick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_DESTROY: KillTimer(wnd, 1); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
