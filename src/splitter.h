// Phase 2 — resizable splitter container (raw Win32, no ATL/WTL).
// A Splitter is a child window holding two panes (A/B) divided by a draggable
// bar. Panes are arbitrary child HWNDs, so splitters nest to form the layout tree.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

namespace pui {

enum class Orient { Vertical, Horizontal }; // Vertical bar => A left / B right; Horizontal => A top / B bottom

class Splitter {
public:
    // Create the splitter window as a child of `parent`. ratio = fraction of size given to pane A.
    HWND create(HWND parent, Orient orient, float ratio);
    HWND hwnd() const { return m_wnd; }

    // Assign pane child windows (each may itself be a Splitter's hwnd). Triggers layout.
    void set_panes(HWND a, HWND b);

    float ratio() const { return m_ratio; }

    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void layout();
    RECT bar_rect() const;
    int  bar_thickness() const { return 6; }

    HWND   m_wnd  = nullptr;
    HWND   m_a    = nullptr;
    HWND   m_b    = nullptr;
    Orient m_orient = Orient::Vertical;
    float  m_ratio  = 0.5f;
    bool   m_dragging = false;
};

} // namespace pui
