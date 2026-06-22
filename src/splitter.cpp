#include "splitter.h"
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_splitter";

void Splitter::register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND Splitter::create(HWND parent, Orient orient, float ratio) {
    register_class();
    m_orient = orient;
    m_ratio  = ratio;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_wnd;
}

void Splitter::set_panes(HWND a, HWND b) {
    m_a = a; m_b = b;
    layout();
}

RECT Splitter::bar_rect() const {
    RECT rc; GetClientRect(m_wnd, &rc);
    const int t = bar_thickness();
    RECT bar = rc;
    if (m_orient == Orient::Vertical) {
        int x = (int)((rc.right - t) * m_ratio);
        bar.left = x; bar.right = x + t;
    } else {
        int y = (int)((rc.bottom - t) * m_ratio);
        bar.top = y; bar.bottom = y + t;
    }
    return bar;
}

void Splitter::layout() {
    if (!m_wnd) return;
    RECT rc; GetClientRect(m_wnd, &rc);
    RECT bar = bar_rect();
    if (m_a) {
        if (m_orient == Orient::Vertical)
            MoveWindow(m_a, 0, 0, bar.left, rc.bottom, TRUE);
        else
            MoveWindow(m_a, 0, 0, rc.right, bar.top, TRUE);
    }
    if (m_b) {
        if (m_orient == Orient::Vertical)
            MoveWindow(m_b, bar.right, 0, rc.right - bar.right, rc.bottom, TRUE);
        else
            MoveWindow(m_b, 0, bar.bottom, rc.right, rc.bottom - bar.bottom, TRUE);
    }
}

LRESULT CALLBACK Splitter::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    Splitter* self = reinterpret_cast<Splitter*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Splitter*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);

    switch (msg) {
    case WM_SIZE:
        self->layout();
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(wnd, &ps);
        FillRect(dc, &ps.rcPaint, (HBRUSH)(COLOR_BTNFACE + 1));
        RECT bar = self->bar_rect();
        DrawEdge(dc, &bar, EDGE_RAISED,
                 self->m_orient == Orient::Vertical ? (BF_LEFT | BF_RIGHT) : (BF_TOP | BF_BOTTOM));
        EndPaint(wnd, &ps);
        return 0;
    }

    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(wnd, &pt);
        RECT bar = self->bar_rect();
        if (PtInRect(&bar, pt)) {
            SetCursor(LoadCursor(nullptr, self->m_orient == Orient::Vertical ? IDC_SIZEWE : IDC_SIZENS));
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT bar = self->bar_rect();
        if (PtInRect(&bar, pt)) { self->m_dragging = true; SetCapture(wnd); }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (self->m_dragging) {
            RECT rc; GetClientRect(wnd, &rc);
            float r;
            if (self->m_orient == Orient::Vertical)
                r = rc.right ? (float)GET_X_LPARAM(lp) / rc.right : 0.5f;
            else
                r = rc.bottom ? (float)GET_Y_LPARAM(lp) / rc.bottom : 0.5f;
            if (r < 0.05f) r = 0.05f; if (r > 0.95f) r = 0.95f;
            self->m_ratio = r;
            self->layout();
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (self->m_dragging) { self->m_dragging = false; ReleaseCapture(); }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
