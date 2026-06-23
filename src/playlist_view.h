// Native album-grouped playlist (replaces the skin's foo_uie_elplaylist, which is a Columns-UI
// panel we can't host in a DUI replacement). Draws the active playlist grouped by album with a
// header (cover thumb + album artist / album / year / genre / track count) and per-track rows
// (track no, title, rating stars, duration), like the fooAvA playlist.
#pragma once
#include "win_sdk.h"

namespace pui {

class SkinEngine;

class PlaylistView {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void on_click(int x, int y, bool dbl, bool shift, bool ctrl);
    void on_rclick(int x, int y); // right-click -> foobar context menu for the selection
    int  item_at(int y);   // active-playlist item index at client-y, or -1

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    int m_scroll = 0;       // vertical scroll offset (px)
    int m_content_h = 0;    // total laid-out height (for clamping)
    int m_anchor = -1;      // last clicked item (shift-range anchor)
    int m_hover_row = -1;   // item whose stars the cursor is over (preview)
    int m_hover_stars = 0;  // previewed star count (1..5) for m_hover_row
    bool m_tracking = false; // TrackMouseEvent armed (for WM_MOUSELEAVE)
};

} // namespace pui
