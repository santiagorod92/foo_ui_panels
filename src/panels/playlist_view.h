// Native album-grouped playlist (replaces the skin's foo_uie_elplaylist, which is a Columns-UI
// panel we can't host in a DUI replacement). Draws the active playlist grouped by album with a
// header (cover thumb + album artist / album / year / genre / track count) and per-track rows
// (track no, title, rating stars, duration), like the fooAvA playlist.
#pragma once
#include "../ui/view.h"
#include "../fb2k.h"
#include <memory>
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class PlaylistView : public ui::View {
public:
    explicit PlaylistView(SkinEngine* engine) : m_engine(engine) {}

    // Repaints come from playlist callbacks (and the engine's play_callback for now-playing).
    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { clamp_scroll(); invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_leave() override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_up(const ui::MouseEvent& e) override;
    bool on_key_down(int key, unsigned mods) override;
    void on_timer(int id) override;
    void on_drop_files(const std::vector<std::string>& paths, int x, int y) override;

private:
    void on_click(int x, int y, bool dbl, bool shift, bool ctrl);
    void on_rclick(int x, int y); // right-click -> foobar context menu for the selection
    int  item_at(int y);    // active-playlist item index at view-y, or -1
    int  item_top(int idx); // content-space y of item idx's row (scroll not applied), or -1
    int  drop_index_at(int y); // insertion point (0..count) for a drop at view-y
    bool has_stars() const; // the skin has rating-star art (asset.rating_stars): stars are clickable
    bool on_stars(int x) const;
    void clamp_scroll();
    void ensure_visible(int idx);
    // Keyboard: focus `idx`, selecting like a click would (shift extends from the anchor, ctrl
    // only moves the focus).
    void move_focus(int idx, unsigned mods);
    bool find_as_you_type(int key);
    void finish_drag(int y);

    // Album groups of the active playlist: consecutive items sharing an album key. Rebuilt only
    // when the playlist changes (m_dirty), not per paint / mouse move — formatting every item
    // each time was O(playlist) titleformat runs per event.
    struct Group { std::vector<t_size> items; };
    const std::vector<Group>& groups();
    struct Events : playlist_callback_single_impl_base {
        explicit Events(PlaylistView* v);
        PlaylistView* m_v;
        void changed() { m_v->m_dirty = true; m_v->invalidate(); }
        void on_items_added(t_size, metadb_handle_list_cref, const bit_array&) override { changed(); }
        void on_items_reordered(const t_size*, t_size) override { changed(); }
        void on_items_removed(const bit_array&, t_size, t_size) override { m_v->m_anchor = -1; changed(); }
        void on_items_modified(const bit_array&) override { changed(); }
        void on_items_replaced(const bit_array&, const pfc::list_base_const_t<playlist_callback::t_on_items_replaced_entry>&) override { changed(); }
        void on_playlist_switch() override { m_v->m_restoreScroll = true; m_v->m_anchor = -1; changed(); }
        void on_items_selection_change(const bit_array&, const bit_array&) override { m_v->invalidate(); }
        void on_item_focus_change(t_size, t_size) override { m_v->invalidate(); }
        // "Show now playing" & co.
        void on_item_ensure_visible(t_size idx) override { m_v->ensure_visible((int)idx); }
    };

    SkinEngine* m_engine = nullptr;
    std::unique_ptr<Events> m_events;
    std::vector<Group> m_groups;
    bool m_dirty = true;
    int m_scroll = 0;       // vertical scroll offset (px)
    // Each playlist's scroll position is kept (SkinEngine::view_state) and restored when it is
    // shown again, at the first paint that has laid it out.
    bool m_restoreScroll = true;
    int m_savedScroll = -1;
    std::string scroll_state_key() const;
    int m_content_h = 0;    // total laid-out height (for clamping)
    int m_anchor = -1;      // last clicked item (shift-range anchor)
    int m_hover_row = -1;   // item whose stars the cursor is over (preview)
    int m_hover_stars = 0;  // previewed star count (1..5) for m_hover_row
    std::string m_find;     // find-as-you-type buffer (cleared after a pause)

    // Drag-to-reorder: a press on a selected row arms it, moving past a few px starts it.
    int m_press_idx = -1, m_press_y = 0;
    bool m_press_deferred = false; // plain click on a selected row: single-select on release, not press
    bool m_dragging = false;
    int m_drop_idx = -1;           // insertion point while dragging
};

} // namespace pui
