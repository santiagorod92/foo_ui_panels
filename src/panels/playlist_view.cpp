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

const GroupedRows& PlaylistView::layout() {
    if (!m_dirty) return m_layout;
    g_pl.ensure();
    auto pm = playlist_manager::get();
    const t_size n = pm->activeplaylist_get_item_count();
    std::vector<std::string> keys(n);
    for (t_size i = 0; i < n; ++i) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, i);
        keys[i] = fmt(h, g_pl.key).c_str();
    }
    m_layout.set_groups(GroupedRows::runs(keys));
    m_dirty = false;
    return m_layout;
}

static const gfx::FontSpec kHdrFont{ "Segoe UI", 15, false, true };
static const gfx::FontSpec kSubFont{ "Segoe UI", 12, false };
static const gfx::FontSpec kRowFont{ "Segoe UI", 12, false };

void PlaylistView::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    auto col = [this](const char* role, gfx::Color def) { return m_engine ? m_engine->color("playlist", role, def) : def; };
    bool drewbg = false;
    gfx::Color hl(36, 86, 180);
    const bool hlFixed = m_engine && m_engine->configured_color("playlist", "highlight", hl);
    if (m_engine && !hlFixed) m_engine->theme_color(hl);
    if (m_engine) {
        const std::string wppath = m_engine->background_path();
        if (!wppath.empty()) {
            drewbg = m_engine->draw_canvas_snapshot(cv, *host());
            if (!drewbg) {
                drewbg = draw_image(cv, wppath, 0, 0, W, H);
                if (drewbg) fill_alpha(cv, 0, 0, W, H, col("overlay", gfx::Color(24, 24, 26)), 215);
            }
            if (drewbg) {
                gfx::Color avg;
                if (!hlFixed && image_avg_color(wppath, avg)) {
                    auto up = [](int v){ int r = v * 3; return r > 255 ? 255 : (r < 40 ? 40 : r); };
                    hl = gfx::Color(up(avg.r), up(avg.g), up(avg.b));
                }
            }
        }
    }
    if (!drewbg && m_engine) drewbg = m_engine->draw_canvas_snapshot(cv, *host());
    if (!drewbg) fill_gradient_v(cv, 0, 0, W, H, col("background", gfx::Color(30, 30, 34)));

    const unsigned kLine = gfx::kSingleLine;

    const GroupedRows& L = layout();
    const int HEADER_H = L.header_h(), ROW_H = L.row_h();
    auto pm = playlist_manager::get();
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);

    if (m_restoreScroll && m_engine) {
        m_restoreScroll = false;
        m_scroll = m_savedScroll = atoi(m_engine->view_state(scroll_state_key()).c_str());
    }
    clamp_scroll();
    if (m_engine && m_scroll != m_savedScroll) {
        m_savedScroll = m_scroll;
        m_engine->set_view_state(scroll_state_key(), std::to_string(m_scroll));
    }
    for (size_t g = L.group_count() ? L.group_at(m_scroll) : 0; g < L.group_count(); ++g) {
        int y = L.group_top(g) - m_scroll;
        if (y >= H) break;
        const t_size first = L.group_start(g), count = L.group_size(g);
        if (y + HEADER_H > 0) {
            metadb_handle_ptr h0; pm->activeplaylist_get_item_handle(h0, first);
            {
                pfc::string8 cov = fmt(h0, g_pl.cover);
                draw_cover_art(cv, cov.get_ptr(), h0, 5, y + 4, 38, 38);
            }
            cv.set_font(kHdrFont);
            pfc::string8 art = fmt(h0, g_pl.artist);
            cv.draw_text(art.get_ptr(), gfx::Rect::ltrb(50, y + 2, W - 70, y + 18), kLine | gfx::kEndEllipsis,
                         col("text", gfx::Color(235, 240, 255)));
            char cnt[32]; snprintf(cnt, sizeof cnt, "%u TRACKS", (unsigned)count);
            cv.set_font(kSubFont);
            cv.draw_text(cnt, gfx::Rect::ltrb(W - 120, y + 3, W - 6, y + 18), gfx::kAlignRight | kLine,
                         col("text_dim", gfx::Color(150, 170, 210)));
            pfc::string8 alb = fmt(h0, g_pl.album);
            cv.draw_text(alb.get_ptr(), gfx::Rect::ltrb(50, y + 18, W - 70, y + 32), kLine | gfx::kEndEllipsis,
                         col("text_secondary", gfx::Color(200, 210, 235)));
            pfc::string8 yr = fmt(h0, g_pl.year);
            cv.draw_text(yr.get_ptr(), gfx::Rect::ltrb(50, y + 31, W - 70, y + 45), kLine, col("text_dim", gfx::Color(140, 160, 200)));
            pfc::string8 gen = fmt(h0, g_pl.genre);
            cv.draw_text(gen.get_ptr(), gfx::Rect::ltrb(W - 160, y + 31, W - 6, y + 45),
                         gfx::kAlignRight | kLine | gfx::kEndEllipsis, col("text_dim", gfx::Color(140, 160, 200)));
            cv.line(4, y + HEADER_H - 1, W - 4, y + HEADER_H - 1, col("separator", gfx::Color(40, 55, 90)));
        }
        y += HEADER_H;
        for (t_size idx = first; idx < first + count && y < H; ++idx) {
            if (y + ROW_H > 0) {
                metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
                bool playing = np.is_valid() && h == np;
                bool selected = pm->activeplaylist_is_item_selected(idx);
                if (playing) {
                    fill_gradient_v(cv, 2, y, W - 4, ROW_H, hl);
                } else if (selected) {
                    fill_alpha(cv, 2, y, W - 4, ROW_H, hl, 90);
                    auto lt = [](int v){ int r = v + 60; return r > 255 ? 255 : r; };
                    cv.line(6, y + ROW_H - 1, W - 8, y + ROW_H - 1, gfx::Color(lt(hl.r), lt(hl.g), lt(hl.b)));
                }
                cv.set_font(kRowFont);
                gfx::Color tc = (playing || selected) ? col("row_text_selected", gfx::Color(255, 255, 255))
                                                       : col("row_text", gfx::Color(205, 215, 235));
                pfc::string8 line = fmt(h, g_pl.row);
                cv.draw_text(line.get_ptr(), gfx::Rect::ltrb(10, y + 1, W - 150, y + ROW_H), kLine | gfx::kEndEllipsis, tc);
                pfc::string8 ln = fmt(h, g_pl.len);
                cv.draw_text(ln.get_ptr(), gfx::Rect::ltrb(W - 46, y + 1, W - 8, y + ROW_H), gfx::kAlignRight | kLine, tc);
                int r = ((int)idx == m_hover_row)
                            ? m_hover_stars
                            : atoi(fmt(h, g_pl.rating).get_ptr());
                if (r < 0) r = 0; if (r > 5) r = 5;
                const std::string stars = m_engine ? m_engine->asset("rating_stars", r) : std::string();
                const StarStrip ss = star_strip(W);
                if (!stars.empty())
                    draw_image(cv, stars, ss.x, y + (ROW_H - 11) / 2, ss.w, 11);
            }
            y += ROW_H;
        }
    }

    if (m_dragging && m_drop_idx >= 0) {
        const t_size n = pm->activeplaylist_get_item_count();
        int ly = (t_size)m_drop_idx < n ? L.item_top(m_drop_idx) : (n ? L.item_top((int)n - 1) + ROW_H : 0);
        ly -= m_scroll;
        cv.fill_rect(gfx::Rect{ 2, ly - 1, W - 4, 2 }, gfx::Color(std::min(255, hl.r + 80), std::min(255, hl.g + 80), std::min(255, hl.b + 80)));
    }
}

enum { kTimerFind = 2, kTimerAutoScroll = 3, kFindResetMs = 1000, kDragThreshold = 4 };

int PlaylistView::item_at(int y) { return layout().item_at(y + m_scroll); }

int PlaylistView::drop_index_at(int y) { return layout().drop_index_at(y + m_scroll); }

void PlaylistView::clamp_scroll() {
    m_scroll = pui::clamp_scroll(m_scroll, layout().content_height(), host() ? host()->bounds().h : 0);
}

void PlaylistView::ensure_visible(int idx) {
    if (!host()) return;
    m_scroll = layout().reveal(m_scroll, idx, host()->bounds().h);
    invalidate();
}

int PlaylistView::star_under(int x) const {
    if (!has_stars() || !host()) return 0;
    return star_at(star_strip(host()->bounds().w), x);
}

void PlaylistView::on_click(int x, int y, bool dbl, bool shift, bool ctrl) {
    int idx = item_at(y);
    if (idx < 0) return;
    auto pm = playlist_manager::get();
    const int star = star_under(x);
    if (!dbl && !shift && !ctrl && star) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, idx);
        if (m_engine) m_engine->set_rating(h, star);
        invalidate();
        return;
    }
    if (shift && m_anchor >= 0) {
        int a = m_anchor < idx ? m_anchor : idx, b = m_anchor < idx ? idx : m_anchor;
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection(bit_array_range(a, b - a + 1, true), bit_array_true());
    } else if (ctrl) {
        pm->activeplaylist_set_selection_single(idx, !pm->activeplaylist_is_item_selected(idx));
        m_anchor = idx;
    } else {
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        m_anchor = idx;
    }
    pm->activeplaylist_set_focus_item(idx);
    if (dbl) pm->activeplaylist_execute_default_action(idx);
    invalidate();
}

void PlaylistView::on_rclick(int x, int y) {
    auto pm = playlist_manager::get();
    int idx = item_at(y);
    if (idx >= 0 && !pm->activeplaylist_is_item_selected(idx)) {
        pm->activeplaylist_clear_selection();
        pm->activeplaylist_set_selection_single(idx, true);
        pm->activeplaylist_set_focus_item(idx);
        m_anchor = idx;
        invalidate();
    }
    metadb_handle_list sel; pm->activeplaylist_get_selected_items(sel);
    if (sel.get_count() == 0) return;
    ui::track_context_menu(*host(), x, y, sel);
    invalidate();
}

void PlaylistView::on_wheel(int, int, float notches) {
    m_scroll -= (int)(notches * m_layout.row_h() * 3);
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
            host()->set_timer(kTimerAutoScroll, 50);
        }
        if (m_dragging) {
            m_press_y = y;
            const int d = drop_index_at(y);
            if (d != m_drop_idx) { m_drop_idx = d; invalidate(); }
            return;
        }
    }
    int hr = -1, hs = 0;
    const int idx = item_at(y), star = star_under(x);
    if (idx >= 0 && star) { hr = idx; hs = star; }
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
        const bool onStars = star_under(e.x) != 0;
        m_press_deferred = idx >= 0 && !shift && !ctrl && !onStars && pm->activeplaylist_is_item_selected(idx);
        if (!m_press_deferred) on_click(e.x, e.y, false, shift, ctrl);
        if (idx >= 0 && !onStars && pm->activeplaylist_is_item_selected(idx)) {
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
        const int H = host()->bounds().h, old = m_scroll, ROW_H = m_layout.row_h();
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

bool PlaylistView::find_as_you_type(int key) {
    if (!typeahead_key(key, !m_find.empty())) return false;
    m_find += (char)key;
    host()->set_timer(kTimerFind, kFindResetMs);
    auto pm = playlist_manager::get();
    const t_size n = pm->activeplaylist_get_item_count();
    if (!n) return true;
    g_pl.ensure();
    const int hit = typeahead_find(n, pm->activeplaylist_get_focus_item(), m_find.size(), [&](size_t i) {
        metadb_handle_ptr h; pm->activeplaylist_get_item_handle(h, i);
        return starts_with_upper(fmt(h, g_pl.title).c_str(), m_find) ||
               starts_with_upper(fmt(h, g_pl.artist).c_str(), m_find);
    });
    if (hit >= 0) move_focus(hit, 0);
    return true;
}

bool PlaylistView::on_key_down(int key, unsigned mods) {
    auto pm = playlist_manager::get();
    if (key == 'A' && (mods & ui::kCtrl) && !(mods & ui::kAlt)) {
        pm->activeplaylist_set_selection(bit_array_true(), bit_array_true());
        invalidate();
        return true;
    }
    if ((key == ui::kKeyDelete || key == ui::kKeyBackspace) && !(mods & (ui::kCtrl | ui::kAlt))) {
        pm->activeplaylist_undo_backup();
        pm->activeplaylist_remove_selection();
        m_anchor = -1;
        invalidate();
        return true;
    }
    if (mods & ui::kAlt) return false;
    const int n = (int)pm->activeplaylist_get_item_count();
    int focus = (int)pm->activeplaylist_get_focus_item();
    if (focus < 0 || focus >= n) focus = -1;
    const int page = std::max(1, (host() ? host()->bounds().h : 0) / m_layout.row_h() - 1);
    const int to = list_nav(key, focus, n, page);
    if (to >= 0) { move_focus(to, mods); return true; }
    if (key == ui::kKeyEnter) {
        if (focus >= 0) pm->activeplaylist_execute_default_action(focus);
        return true;
    }
    if (!(mods & ui::kCtrl)) return find_as_you_type(key);
    return false;
}

}
