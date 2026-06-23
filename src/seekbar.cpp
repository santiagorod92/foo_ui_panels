#include "seekbar.h"
#include "skin_engine.h"
#include <windowsx.h>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_seekbar";

void Seekbar::register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_HAND);
    RegisterClassExW(&wc);
}

HWND Seekbar::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 500, nullptr);
    return m_wnd;
}

void Seekbar::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    auto pc = playback_control::get();
    double len = pc->playback_get_length(), pos = pc->playback_get_position();
    double frac = (len > 0) ? pos / len : 0.0;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;

    HBRUSH track = CreateSolidBrush(RGB(40, 40, 50));
    FillRect(mem, &rc, track); DeleteObject(track);
    RECT fill = rc; fill.right = (LONG)(rc.right * frac);
    COLORREF accent = RGB(0, 140, 220);
    if (m_engine) m_engine->theme_color(accent); // follow the skin theme
    HBRUSH prog = CreateSolidBrush(accent);
    FillRect(mem, &fill, prog); DeleteObject(prog);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

LRESULT CALLBACK Seekbar::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    Seekbar* self = reinterpret_cast<Seekbar*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        RECT rc; GetClientRect(wnd, &rc);
        auto pc = playback_control::get();
        double len = pc->playback_get_length();
        if (len > 0 && rc.right > 0 && pc->playback_can_seek())
            pc->playback_seek(len * GET_X_LPARAM(lp) / rc.right);
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY: KillTimer(wnd, 1); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
