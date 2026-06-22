#include "volume.h"
#include <windowsx.h>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_volume";

void Volume::register_class() {
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

HWND Volume::create(HWND parent) {
    register_class();
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 500, nullptr);
    return m_wnd;
}

void Volume::set_from_x(int x) {
    RECT rc; GetClientRect(m_wnd, &rc);
    if (rc.right <= 0) return;
    float f = (float)x / rc.right; if (f < 0) f = 0; if (f > 1) f = 1;
    playback_control::get()->set_volume(f * 100.0f - 100.0f); // dB: -100..0
    InvalidateRect(m_wnd, nullptr, FALSE);
}

void Volume::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    float vol = playback_control::get()->get_volume();      // -100..0
    float frac = (vol + 100.0f) / 100.0f; if (frac < 0) frac = 0; if (frac > 1) frac = 1;

    HBRUSH track = CreateSolidBrush(RGB(40, 40, 50));
    FillRect(mem, &rc, track); DeleteObject(track);
    RECT fill = rc; fill.right = (LONG)(rc.right * frac);
    HBRUSH lvl = CreateSolidBrush(RGB(80, 200, 120));
    FillRect(mem, &fill, lvl); DeleteObject(lvl);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

LRESULT CALLBACK Volume::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    Volume* self = reinterpret_cast<Volume*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: if (self) { SetCapture(wnd); self->set_from_x(GET_X_LPARAM(lp)); } return 0;
    case WM_MOUSEMOVE: if (self && (wp & MK_LBUTTON)) self->set_from_x(GET_X_LPARAM(lp)); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); return 0;
    case WM_DESTROY: KillTimer(wnd, 1); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
