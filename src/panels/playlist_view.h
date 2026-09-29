// Native album-grouped playlist (replaces the skin's foo_uie_elplaylist, which is a Columns-UI
// panel we can't host in a DUI replacement). Draws the active playlist grouped by album with a
// header (cover thumb + album artist / album / year / genre / track count) and per-track rows
// (track no, title, rating stars, duration), like the fooAvA playlist.
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class PlaylistView : public ui::View {
public:
    explicit PlaylistView(SkinEngine* engine) : m_engine(engine) {}

    void on_attached() override { host()->set_timer(1, 1000); } // refresh now-playing / elapsed
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override { invalidate(); }
    void on_resize(int, int) override { invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_leave() override;
    void on_mouse_down(const ui::MouseEvent& e) override;

private:
    void on_click(int x, int y, bool dbl, bool shift, bool ctrl);
    void on_rclick(int x, int y); // right-click -> foobar context menu for the selection
    int  item_at(int y);   // active-playlist item index at view-y, or -1

    SkinEngine* m_engine = nullptr;
    int m_scroll = 0;       // vertical scroll offset (px)
    int m_content_h = 0;    // total laid-out height (for clamping)
    int m_anchor = -1;      // last clicked item (shift-range anchor)
    int m_hover_row = -1;   // item whose stars the cursor is over (preview)
    int m_hover_stars = 0;  // previewed star count (1..5) for m_hover_row
};

} // namespace pui
