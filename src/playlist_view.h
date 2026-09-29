// Native album-grouped playlist (replaces the skin's foo_uie_elplaylist, which is a Columns-UI
// panel we can't host in a DUI replacement). Draws the active playlist grouped by album with a
// header (cover thumb + album artist / album / year / genre / track count) and per-track rows
// (track no, title, rating stars, duration), like the fooAvA playlist.
#pragma once
#include "win_sdk.h"

struct IDropTarget;
struct IDataObject;

namespace pui {

class SkinEngine;

class PlaylistView {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

    // Called by the window's IDropTarget (see playlist_view.cpp). client_pt is client coords.
    void drag_over(POINT client_pt);
    void drag_leave();
    void handle_drop(IDataObject* data, POINT client_pt, bool self_source);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void on_click(int x, int y, bool dbl, bool shift, bool ctrl);
    void on_rclick(int x, int y); // right-click -> foobar context menu for the selection
    void on_keydown(WPARAM vk, bool ctrl); // Delete/Enter/Ctrl+A common playlist actions
    int  item_at(int y);   // active-playlist item index at client-y, or -1
    t_size drop_index_at(int cy); // absolute playlist insertion index for a drop at client-y
    int  boundary_y_at(int cy);   // pixel y of the row boundary nearest client-y (drop indicator)

    void on_lbuttondown(int x, int y, bool shift, bool ctrl);
    void on_lbuttonup();
    void start_drag(); // DoDragDrop() for the current selection once the drag threshold is crossed

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    int m_scroll = 0;       // vertical scroll offset (px)
    int m_content_h = 0;    // total laid-out height (for clamping)
    int m_anchor = -1;      // last clicked item (shift-range anchor)
    int m_hover_row = -1;   // item whose stars the cursor is over (preview)
    int m_hover_stars = 0;  // previewed star count (1..5) for m_hover_row
    bool m_tracking = false; // TrackMouseEvent armed (for WM_MOUSELEAVE)

    IDropTarget* m_drop_target = nullptr;

    // Drag source (dragging the current selection out, or reordering within this list).
    int m_drag_candidate = -1;    // item pressed with a selection-collapse deferred (may become a drag)
    POINT m_drag_pt = { 0, 0};    // press point, for the drag-start distance threshold
    bool m_drag_pending_click = false;

    // Drag target visual feedback: y of the insertion line while something is dragged over us, or -1.
    int m_drop_y = -1;
};

} // namespace pui
