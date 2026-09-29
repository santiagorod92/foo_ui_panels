#include "playlist_view.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <vector>
#include <string>

namespace pui {

static const int HEADER_H = 46;
static const int ROW_H = 19;

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

static const gfx::FontSpec kHdrFont{ "Segoe UI", 15, false, true };
static const gfx::FontSpec kSubFont{ "Segoe UI", 12, false };
static const gfx::FontSpec kRowFont{ "Segoe UI", 12, false };

void PlaylistView::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    std::string sbase0 = m_engine ? m_engine->base_dir() : std::string();
    // Background: the skin wallpaper (so it matches the main window) under a translucent dark
    // overlay so the track list stays readable. Falls back to a navy gradient. The row highlight
    // colour is tinted from the wallpaper so it matches instead of being a stark blue.
    bool drewbg = false;
    gfx::Color hl(36, 86, 180); if (m_engine) m_engine->theme_color(hl);
    if (m_engine && !sbase0.empty() && m_engine->pvar_int("backgroundd", 0)) {
        std::string wp = m_engine->pvar_str("background"); // e.g. "walls\1.jpg"
        if (!wp.empty()) {
            for (auto& ch : wp) if (ch == '\\') ch = '/';
            std::string wppath = sbase0 + "/images/fooAVA/" + wp;
            // Transparent: show exactly what the skin canvas drew beneath us (its wallpaper at
            // the skin's own alpha), so the list sits on the same background as the rest.
            drewbg = m_engine->draw_canvas_snapshot(cv, *host());
            if (!drewbg) {
                drewbg = draw_image(cv, wppath, 0, 0, W, H);
                if (drewbg) fill_alpha(cv, 0, 0, W, H, gfx::Color(24, 24, 26), 215);
            }
            if (drewbg) {
                gfx::Color avg;
                if (image_avg_color(wppath, avg)) { // tint highlight to the wallpaper
                    auto up = [](int v){ int r = v * 3; return r > 255 ? 255 : (r < 40 ? 40 : r); };
                    hl = gfx::Color(up(avg.r), up(avg.g), up(avg.b));
                }
            }
        }
    }
    if (!drewbg && m_engine) drewbg = m_engine->draw_canvas_snapshot(cv, *host()); // wallpaper off
    if (!drewbg) fill_gradient_v(cv, 0, 0, W, H, gfx::Color(30, 30, 34));

    std::string sbase = m_engine ? m_engine->base_dir() : std::string();
    const unsigned kLine = gfx::kSingleLine;

    auto groups = build_groups();
    auto pm = playlist_manager::get();
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);

    int y = -m_scroll;
    for (const auto& g : groups) {
        if (g.items.empty()) continue;
        // ---- album header ----
        if (y + HEADER_H > 0 && y < H) {
            metadb_handle_ptr h0; pm->activeplaylist_get_item_handle(h0, g.items[0]);
            // cover thumb
            if (!sbase.empty()) {
                pfc::string8 cov = fmt(h0, g_pl.cover);
                // folder.* on disk, else the album-art pipeline (navidrome:// etc. have no folder)
                draw_cover_art(cv, cov.get_ptr(), h0, 5, y + 4, 38, 38);
            }
            cv.set_font(kHdrFont);
            pfc::string8 art = fmt(h0, g_pl.artist);
            cv.draw_text(art.get_ptr(), gfx::Rect::ltrb(50, y + 2, W - 70, y + 18), kLine | gfx::kEndEllipsis,
                         gfx::Color(235, 240, 255));
            // track count, right
            char cnt[32]; snprintf(cnt, sizeof cnt, "%u TRACKS", (unsigned)g.items.size());
            cv.set_font(kSubFont);
            cv.draw_text(cnt, gfx::Rect::ltrb(W - 120, y + 3, W - 6, y + 18), gfx::kAlignRight | kLine,
                         gfx::Color(150, 170, 210));
            // album
            pfc::string8 alb = fmt(h0, g_pl.album);
            cv.draw_text(alb.get_ptr(), gfx::Rect::ltrb(50, y + 18, W - 70, y + 32), kLine | gfx::kEndEllipsis,
                         gfx::Color(200, 210, 235));
            // year + genre
            pfc::string8 yr = fmt(h0, g_pl.year);
            cv.draw_text(yr.get_ptr(), gfx::Rect::ltrb(50, y + 31, W - 70, y + 45), kLine, gfx::Color(140, 160, 200));
            pfc::string8 gen = fmt(h0, g_pl.genre);
            cv.draw_text(gen.get_ptr(), gfx::Rect::ltrb(W - 160, y + 31, W - 6, y + 45),
                         gfx::kAlignRight | kLine | gfx::kEndEllipsis, gfx::Color(140, 160, 200));
            // separator line
            cv.line(4, y + HEADER_H - 1, W - 4, y + HEADER_H - 1, gfx::Color(40, 55, 90));
        }
        y += HEADER_H;
        // ---- track rows ----
        for (t_size idx : g.items) {
            if (y + ROW_H > 0 && y < H) {
                metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
                bool playing = np.is_valid() && h == np;
                bool selected = pm->activeplaylist_is_item_selected(idx);
                if (playing) { // now-playing: gradient bar tinted to the wallpaper
                    fill_gradient_v(cv, 2, y, W - 4, ROW_H, hl);
                } else if (selected) { // click selection: translucent band + bright underline
                    fill_alpha(cv, 2, y, W - 4, ROW_H, hl, 90);
                    auto lt = [](int v){ int r = v + 60; return r > 255 ? 255 : r; };
                    cv.line(6, y + ROW_H - 1, W - 8, y + ROW_H - 1, gfx::Color(lt(hl.r), lt(hl.g), lt(hl.b)));
                }
                cv.set_font(kRowFont);
                gfx::Color tc = (playing || selected) ? gfx::Color(255, 255, 255) : gfx::Color(205, 215, 235);
                pfc::string8 line = fmt(h, g_pl.row);
                cv.draw_text(line.get_ptr(), gfx::Rect::ltrb(10, y + 1, W - 150, y + ROW_H), kLine | gfx::kEndEllipsis, tc);
                // duration (right)
                pfc::string8 ln = fmt(h, g_pl.len);
                cv.draw_text(ln.get_ptr(), gfx::Rect::ltrb(W - 46, y + 1, W - 8, y + ROW_H), gfx::kAlignRight | kLine, tc);
                // rating stars (skin images), left of duration
                if (!sbase.empty()) {
                    int r = ((int)idx == m_hover_row)   // live preview while hovering the stars
                                ? m_hover_stars
                                : atoi(fmt(h, g_pl.rating).get_ptr());
                    if (r < 0) r = 0; if (r > 5) r = 5;
                    char p[16]; snprintf(p, sizeof p, "%ds1.png", r);
                    draw_image(cv, sbase + "/images/fooAVA/rating_stars24/" + p,
                               W - 46 - 60, y + (ROW_H - 11) / 2, 55, 11);
                }
            }
            y += ROW_H;
        }
    }
    m_content_h = y + m_scroll;
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
    const int W = host()->bounds().w;
    const int starX = W - 106, starW = 55; // must match the paint() star rect
    if (!dbl && !shift && !ctrl && x >= starX && x < starX + starW) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
        int star = (x - starX) * 5 / starW + 1; if (star < 1) star = 1; if (star > 5) star = 5;
        if (m_engine) m_engine->set_rating(h, star);
        invalidate();
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
    invalidate();
}

void PlaylistView::on_rclick(int x, int y) {
    auto pm = playlist_manager::get();
    int idx = item_at(y);
    if (idx >= 0 && !pm->activeplaylist_is_item_selected(idx)) { // right-click outside selection → select it
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        pm->activeplaylist_set_focus_item(idx);
        m_anchor = idx;
        invalidate();
    }
    metadb_handle_list sel; pm->activeplaylist_get_selected_items(sel);
    if (sel.get_count() == 0) return;
    // The classic foobar context menu (Properties / tagging / etc.) for the selected tracks.
    ui::track_context_menu(*host(), x, y, sel);
    invalidate();
}

void PlaylistView::on_wheel(int, int, float notches) {
    m_scroll -= (int)(notches * ROW_H * 3);
    if (m_scroll < 0) m_scroll = 0;
    int maxs = m_content_h - host()->bounds().h; if (maxs < 0) maxs = 0;
    if (m_scroll > maxs) m_scroll = maxs;
    invalidate();
}

void PlaylistView::on_mouse_move(int x, int y, unsigned, bool) {
    const int W = host()->bounds().w;
    const int starX = W - 106, starW = 55;
    int hr = -1, hs = 0, idx = item_at(y);
    if (idx >= 0 && x >= starX && x < starX + starW) {
        hr = idx; hs = (x - starX) * 5 / starW + 1; if (hs < 1) hs = 1; if (hs > 5) hs = 5;
    }
    if (hr != m_hover_row || hs != m_hover_stars) {
        m_hover_row = hr; m_hover_stars = hs;
        invalidate();
    }
}

void PlaylistView::on_mouse_leave() {
    if (m_hover_row != -1) { m_hover_row = -1; invalidate(); }
}

void PlaylistView::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Left) {
        if (e.double_click) on_click(e.x, e.y, true, false, false);
        else on_click(e.x, e.y, false, (e.mods & ui::kShift) != 0, (e.mods & ui::kCtrl) != 0);
    } else if (e.button == ui::MouseButton::Right) {
        on_rclick(e.x, e.y);
    }
}

} // namespace pui
