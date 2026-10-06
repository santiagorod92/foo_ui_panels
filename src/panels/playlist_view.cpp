#include "playlist_view.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include "../core/playlist_ops.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace pui {

static const int HEADER_H = 46;
static const int ROW_H = 19;

// Compiled titleformat scripts (lazy, shared).
struct PLScripts {
    service_ptr_t<titleformat_object> key, artist, album, year, genre, row, len, rating, cover, title;
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
        c->compile_safe(title,  "[%title%]");
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

PlaylistView::Events::Events(PlaylistView* v)
    : playlist_callback_single_impl_base(flag_on_items_added | flag_on_items_reordered | flag_on_items_removed |
                                         flag_on_items_modified | flag_on_items_replaced | flag_on_playlist_switch |
                                         flag_on_items_selection_change | flag_on_item_focus_change |
                                         flag_on_item_ensure_visible),
      m_v(v) {}

// Per playlist, by name (indices shift as playlists are added and removed).
std::string PlaylistView::scroll_state_key() const {
    auto pm = playlist_manager::get();
    pfc::string8 name;
    const t_size i = pm->get_active_playlist();
    if (i != pfc_infinite) pm->playlist_get_name(i, name);
    return std::string("playlist.scroll.") + name.c_str();
}

void PlaylistView::on_attached() {
    if (!m_events) m_events = std::make_unique<Events>(this);
}

const std::vector<PlaylistView::Group>& PlaylistView::groups() {
    if (!m_dirty) return m_groups;
    g_pl.ensure();
    m_groups.clear();
    auto pm = playlist_manager::get();
    t_size n = pm->activeplaylist_get_item_count();
    pfc::string8 last; bool first = true;
    for (t_size i = 0; i < n; ++i) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, i);
        pfc::string8 k = fmt(h, g_pl.key);
        if (first || strcmp(k, last) != 0) { m_groups.push_back(Group{}); last = k; first = false; }
        m_groups.back().items.push_back(i);
    }
    m_dirty = false;
    return m_groups;
}

static const gfx::FontSpec kHdrFont{ "Segoe UI", 15, false, true };
static const gfx::FontSpec kSubFont{ "Segoe UI", 12, false };
static const gfx::FontSpec kRowFont{ "Segoe UI", 12, false };

void PlaylistView::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    // Background: the skin wallpaper (so it matches the main window) under a translucent dark
    // overlay so the track list stays readable. Falls back to a navy gradient. The row highlight
    // colour is tinted from the wallpaper so it matches instead of being a stark blue.
    // Colours: the skin config's `color.playlist.<role>` / `color.<role>`, else these defaults.
    auto col = [this](const char* role, gfx::Color def) { return m_engine ? m_engine->color("playlist", role, def) : def; };
    bool drewbg = false;
    gfx::Color hl(36, 86, 180);
    const bool hlFixed = m_engine && m_engine->configured_color("playlist", "highlight", hl); // configured: no tinting
    if (m_engine && !hlFixed) m_engine->theme_color(hl);
    if (m_engine) {
        const std::string wppath = m_engine->background_path(); // "" = the skin has none / it's off
        if (!wppath.empty()) {
            // Transparent: show exactly what the skin canvas drew beneath us (its wallpaper at
            // the skin's own alpha), so the list sits on the same background as the rest.
            drewbg = m_engine->draw_canvas_snapshot(cv, *host());
            if (!drewbg) {
                drewbg = draw_image(cv, wppath, 0, 0, W, H);
                if (drewbg) fill_alpha(cv, 0, 0, W, H, col("overlay", gfx::Color(24, 24, 26)), 215);
            }
            if (drewbg) {
                gfx::Color avg;
                if (!hlFixed && image_avg_color(wppath, avg)) { // tint highlight to the wallpaper
                    auto up = [](int v){ int r = v * 3; return r > 255 ? 255 : (r < 40 ? 40 : r); };
                    hl = gfx::Color(up(avg.r), up(avg.g), up(avg.b));
                }
            }
        }
    }
    if (!drewbg && m_engine) drewbg = m_engine->draw_canvas_snapshot(cv, *host()); // wallpaper off
    if (!drewbg) fill_gradient_v(cv, 0, 0, W, H, col("background", gfx::Color(30, 30, 34)));

    const unsigned kLine = gfx::kSingleLine;

    const auto& groups = this->groups();
    auto pm = playlist_manager::get();
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);

    if (m_restoreScroll && m_engine) { // this playlist's last position (clamped below)
        m_restoreScroll = false;
        m_scroll = m_savedScroll = atoi(m_engine->view_state(scroll_state_key()).c_str());
    }
    int y = -m_scroll;
    for (const auto& g : groups) {
        if (g.items.empty()) continue;
        // ---- album header ----
        if (y + HEADER_H > 0 && y < H) {
            metadb_handle_ptr h0; pm->activeplaylist_get_item_handle(h0, g.items[0]);
            // cover thumb: folder.* on disk, else the album-art pipeline (navidrome:// etc.)
            {
                pfc::string8 cov = fmt(h0, g_pl.cover);
                draw_cover_art(cv, cov.get_ptr(), h0, 5, y + 4, 38, 38);
            }
            cv.set_font(kHdrFont);
            pfc::string8 art = fmt(h0, g_pl.artist);
            cv.draw_text(art.get_ptr(), gfx::Rect::ltrb(50, y + 2, W - 70, y + 18), kLine | gfx::kEndEllipsis,
                         col("text", gfx::Color(235, 240, 255)));
            // track count, right
            char cnt[32]; snprintf(cnt, sizeof cnt, "%u TRACKS", (unsigned)g.items.size());
            cv.set_font(kSubFont);
            cv.draw_text(cnt, gfx::Rect::ltrb(W - 120, y + 3, W - 6, y + 18), gfx::kAlignRight | kLine,
                         col("text_dim", gfx::Color(150, 170, 210)));
            // album
            pfc::string8 alb = fmt(h0, g_pl.album);
            cv.draw_text(alb.get_ptr(), gfx::Rect::ltrb(50, y + 18, W - 70, y + 32), kLine | gfx::kEndEllipsis,
                         col("text_secondary", gfx::Color(200, 210, 235)));
            // year + genre
            pfc::string8 yr = fmt(h0, g_pl.year);
            cv.draw_text(yr.get_ptr(), gfx::Rect::ltrb(50, y + 31, W - 70, y + 45), kLine, col("text_dim", gfx::Color(140, 160, 200)));
            pfc::string8 gen = fmt(h0, g_pl.genre);
            cv.draw_text(gen.get_ptr(), gfx::Rect::ltrb(W - 160, y + 31, W - 6, y + 45),
                         gfx::kAlignRight | kLine | gfx::kEndEllipsis, col("text_dim", gfx::Color(140, 160, 200)));
            // separator line
            cv.line(4, y + HEADER_H - 1, W - 4, y + HEADER_H - 1, col("separator", gfx::Color(40, 55, 90)));
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
                gfx::Color tc = (playing || selected) ? col("row_text_selected", gfx::Color(255, 255, 255))
                                                       : col("row_text", gfx::Color(205, 215, 235));
                pfc::string8 line = fmt(h, g_pl.row);
                cv.draw_text(line.get_ptr(), gfx::Rect::ltrb(10, y + 1, W - 150, y + ROW_H), kLine | gfx::kEndEllipsis, tc);
                // duration (right)
                pfc::string8 ln = fmt(h, g_pl.len);
                cv.draw_text(ln.get_ptr(), gfx::Rect::ltrb(W - 46, y + 1, W - 8, y + ROW_H), gfx::kAlignRight | kLine, tc);
                // rating stars, left of duration: the skin's `asset.rating_stars` ({n} = 0..5 stars)
                int r = ((int)idx == m_hover_row)   // live preview while hovering the stars
                            ? m_hover_stars
                            : atoi(fmt(h, g_pl.rating).get_ptr());
                if (r < 0) r = 0; if (r > 5) r = 5;
                const std::string stars = m_engine ? m_engine->asset("rating_stars", r) : std::string();
                if (!stars.empty())
                    draw_image(cv, stars, W - 46 - 60, y + (ROW_H - 11) / 2, 55, 11);
            }
            y += ROW_H;
        }
    }
    m_content_h = y + m_scroll;
    const int before = m_scroll;
    clamp_scroll();
    if (m_scroll != before) invalidate(); // the restored position was past the end
    if (m_engine && m_scroll != m_savedScroll) {
        m_savedScroll = m_scroll;
        m_engine->set_view_state(scroll_state_key(), std::to_string(m_scroll));
    }

    // Drag-to-reorder insertion point: a bar between rows, in the highlight colour.
    if (m_dragging && m_drop_idx >= 0) {
        const t_size n = pm->activeplaylist_get_item_count();
        int ly = (t_size)m_drop_idx < n ? item_top(m_drop_idx) : (n ? item_top((int)n - 1) + ROW_H : 0);
        ly -= m_scroll;
        cv.fill_rect(gfx::Rect{ 2, ly - 1, W - 4, 2 }, gfx::Color(std::min(255, hl.r + 80), std::min(255, hl.g + 80), std::min(255, hl.b + 80)));
    }
}

enum { kTimerFind = 2, kTimerAutoScroll = 3, kFindResetMs = 1000, kDragThreshold = 4 };

int PlaylistView::item_at(int cy) {
    const auto& groups = this->groups();
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

int PlaylistView::item_top(int want) {
    int y = 0;
    for (const auto& g : groups()) {
        if (g.items.empty()) continue;
        y += HEADER_H;
        if (want >= (int)g.items.front() && want <= (int)g.items.back())
            return y + (want - (int)g.items.front()) * ROW_H;
        y += (int)g.items.size() * ROW_H;
    }
    return -1;
}

// Before the row whose upper half is under y (a group header counts as its first row's upper
// half), after the last row below everything.
int PlaylistView::drop_index_at(int cy) {
    int y = -m_scroll;
    t_size last = 0; bool any = false;
    for (const auto& g : groups()) {
        if (g.items.empty()) continue;
        if (cy < y + HEADER_H) return (int)g.items.front();
        y += HEADER_H;
        for (t_size idx : g.items) {
            if (cy < y + ROW_H / 2) return (int)idx;
            y += ROW_H;
            last = idx; any = true;
        }
    }
    return any ? (int)last + 1 : 0;
}

void PlaylistView::clamp_scroll() {
    int maxs = m_content_h - (host() ? host()->bounds().h : 0); if (maxs < 0) maxs = 0;
    if (m_scroll > maxs) m_scroll = maxs;
    if (m_scroll < 0) m_scroll = 0;
}

void PlaylistView::ensure_visible(int idx) {
    if (!host()) return;
    const int top = item_top(idx);
    if (top < 0) return;
    const int H = host()->bounds().h;
    // The group's first row brings its header along, so the album stays identifiable.
    bool first = false;
    for (const auto& g : groups()) if (!g.items.empty() && (int)g.items.front() == idx) { first = true; break; }
    const int want = first ? top - HEADER_H : top;
    if (want < m_scroll) m_scroll = want;
    else if (top + ROW_H > m_scroll + H) m_scroll = top + ROW_H - H;
    invalidate();
}

bool PlaylistView::on_stars(int x) const {
    if (!has_stars() || !host()) return false;
    const int W = host()->bounds().w;
    const int starX = W - 106, starW = 55; // must match the paint() star rect
    return x >= starX && x < starX + starW;
}

void PlaylistView::on_click(int x, int y, bool dbl, bool shift, bool ctrl) {
    int idx = item_at(y);
    if (idx < 0) return;
    auto pm = playlist_manager::get();
    // Click on the rating stars → set this track's rating (1..5) like the skin.
    if (!dbl && !shift && !ctrl && on_stars(x)) {
        const int starX = host()->bounds().w - 106, starW = 55;
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
    clamp_scroll();
    invalidate();
}

bool PlaylistView::has_stars() const {
    return m_engine && !m_engine->asset("rating_stars", 0).empty();
}

void PlaylistView::on_mouse_move(int x, int y, unsigned, bool left_down) {
    if (m_press_idx >= 0 && left_down) {
        if (!m_dragging && std::abs(y - m_press_y) > kDragThreshold) {
            m_dragging = true;
            host()->set_timer(kTimerAutoScroll, 50); // keeps scrolling while held past an edge
        }
        if (m_dragging) {
            m_press_y = y; // the auto-scroll timer re-evaluates the drop point from here
            const int d = drop_index_at(y);
            if (d != m_drop_idx) { m_drop_idx = d; invalidate(); }
            return;
        }
    }
    const int W = host()->bounds().w;
    const int starX = W - 106, starW = 55;
    int hr = -1, hs = 0, idx = item_at(y);
    if (idx >= 0 && on_stars(x)) {
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
        if (e.double_click) { on_click(e.x, e.y, true, false, false); return; }
        const bool shift = (e.mods & ui::kShift) != 0, ctrl = (e.mods & ui::kCtrl) != 0;
        const int idx = item_at(e.y);
        auto pm = playlist_manager::get();
        // A plain press on an already selected row keeps the selection (it may be the start of
        // dragging all of it); the click's single-select happens on release instead.
        m_press_deferred = idx >= 0 && !shift && !ctrl && !on_stars(e.x) && pm->activeplaylist_is_item_selected(idx);
        if (!m_press_deferred) on_click(e.x, e.y, false, shift, ctrl);
        if (idx >= 0 && !on_stars(e.x) && pm->activeplaylist_is_item_selected(idx)) {
            m_press_idx = idx; m_press_y = e.y; m_dragging = false; m_drop_idx = -1;
            host()->capture_mouse(true);
        }
    } else if (e.button == ui::MouseButton::Right) {
        on_rclick(e.x, e.y);
    }
}

void PlaylistView::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button != ui::MouseButton::Left || m_press_idx < 0) return;
    host()->capture_mouse(false);
    host()->kill_timer(kTimerAutoScroll);
    if (m_dragging) finish_drag(e.y);
    else if (m_press_deferred) on_click(e.x, e.y, false, false, false);
    m_press_idx = -1; m_press_deferred = false; m_dragging = false; m_drop_idx = -1;
    invalidate();
}

// Move the selection to the insertion point (drop_order): one undoable reorder, like the stock
// playlist views.
void PlaylistView::finish_drag(int y) {
    auto pm = playlist_manager::get();
    const t_size n = pm->activeplaylist_get_item_count();
    pfc::bit_array_bittable mask(n);
    pm->activeplaylist_get_selection_mask(mask);
    std::vector<bool> sel(n);
    for (t_size i = 0; i < n; ++i) sel[i] = mask.get(i);
    const std::vector<size_t> order = drop_order(sel, (size_t)drop_index_at(y));
    if (order.empty()) return;
    pm->activeplaylist_undo_backup();
    pm->activeplaylist_reorder_items(order.data(), n);
    m_anchor = -1;
}

void PlaylistView::on_timer(int id) {
    if (id == kTimerFind) { host()->kill_timer(kTimerFind); m_find.clear(); return; }
    if (id == kTimerAutoScroll && m_dragging) {
        const int H = host()->bounds().h, old = m_scroll;
        if (m_press_y < ROW_H) m_scroll -= ROW_H;
        else if (m_press_y > H - ROW_H) m_scroll += ROW_H;
        clamp_scroll();
        if (m_scroll != old) { m_drop_idx = drop_index_at(m_press_y); invalidate(); }
    }
}

void PlaylistView::on_drop_files(const std::vector<std::string>& paths, int, int y) {
    SkinEngine::add_files(paths, (t_size)drop_index_at(y));
}

void PlaylistView::move_focus(int idx, unsigned mods) {
    auto pm = playlist_manager::get();
    const int n = (int)pm->activeplaylist_get_item_count();
    if (n <= 0) return;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (mods & ui::kShift) {
        if (m_anchor < 0) { m_anchor = (int)pm->activeplaylist_get_focus_item(); if (m_anchor < 0 || m_anchor >= n) m_anchor = idx; }
        const int a = std::min(m_anchor, idx), b = std::max(m_anchor, idx);
        pm->activeplaylist_set_selection(bit_array_true(), bit_array_range(a, b - a + 1, true));
    } else if (!(mods & ui::kCtrl)) {
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        m_anchor = idx;
    }
    pm->activeplaylist_set_focus_item(idx);
    ensure_visible(idx);
}

// Letters/digits typed with no modifier jump to the next track whose title — or album artist,
// the grouped view's headline — starts with what was typed in the last second.
bool PlaylistView::find_as_you_type(int key) {
    if (!((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key == ' ' && !m_find.empty())))
        return false;
    m_find += (char)key;
    host()->set_timer(kTimerFind, kFindResetMs);
    auto pm = playlist_manager::get();
    const t_size n = pm->activeplaylist_get_item_count();
    if (!n) return true;
    g_pl.ensure();
    auto starts = [this](const pfc::string8& s) { // ASCII case-insensitive prefix (keys are A-Z/0-9/space)
        if (s.length() < m_find.size()) return false;
        for (size_t i = 0; i < m_find.size(); ++i)
            if (toupper((unsigned char)s[i]) != (unsigned char)m_find[i]) return false;
        return true;
    };
    // A fresh search starts after the focused track (so typing the same letter again moves on);
    // a longer prefix re-tests the focused one first.
    t_size focus = pm->activeplaylist_get_focus_item();
    if (focus >= n) focus = 0;
    const t_size first = m_find.size() == 1 ? focus + 1 : focus;
    for (t_size k = 0; k < n; ++k) {
        const t_size i = (first + k) % n;
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, i);
        if (starts(fmt(h, g_pl.title)) || starts(fmt(h, g_pl.artist))) { move_focus((int)i, 0); break; }
    }
    return true;
}

// Ctrl+A / Cmd+A selects the whole playlist, Delete drops the selection from it, arrows /
// PgUp / PgDn / Home / End move the focus (Shift extends the selection, Ctrl moves only the
// focus), Enter plays the focused track, letters find as you type — what the stock playlist
// views bind; the panel has keyboard focus from the click that reached it. macOS reports
// Command as kCtrl (mods_of in mac_view.mm), and its own Delete key arrives as kKeyBackspace,
// so both spellings count.
bool PlaylistView::on_key_down(int key, unsigned mods) {
    auto pm = playlist_manager::get();
    if (key == 'A' && (mods & ui::kCtrl) && !(mods & ui::kAlt)) {
        pm->activeplaylist_set_selection(bit_array_true(), bit_array_true());
        invalidate();
        return true;
    }
    if ((key == ui::kKeyDelete || key == ui::kKeyBackspace) && !(mods & (ui::kCtrl | ui::kAlt))) {
        pm->activeplaylist_undo_backup(); // same as the stock views: the removal stays undoable
        pm->activeplaylist_remove_selection();
        m_anchor = -1; // the shift-range anchor indexed items that are gone now
        invalidate();
        return true;
    }
    if (mods & ui::kAlt) return false;
    const int n = (int)pm->activeplaylist_get_item_count();
    int focus = (int)pm->activeplaylist_get_focus_item();
    if (focus < 0 || focus >= n) focus = -1;
    const int page = std::max(1, (host() ? host()->bounds().h : 0) / ROW_H - 1);
    switch (key) {
    case ui::kKeyUp:       move_focus(focus < 0 ? 0 : focus - 1, mods); return true;
    case ui::kKeyDown:     move_focus(focus + 1, mods); return true;
    case ui::kKeyPageUp:   move_focus(focus < 0 ? 0 : focus - page, mods); return true;
    case ui::kKeyPageDown: move_focus(focus + page, mods); return true;
    case ui::kKeyHome:     move_focus(0, mods); return true;
    case ui::kKeyEnd:      move_focus(n - 1, mods); return true;
    case ui::kKeyEnter:
        if (focus >= 0) pm->activeplaylist_execute_default_action(focus);
        return true;
    }
    if (!(mods & ui::kCtrl)) return find_as_you_type(key);
    return false;
}

} // namespace pui
