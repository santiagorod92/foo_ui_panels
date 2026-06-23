#include "volume.h"
#include "skin_engine.h"
#include "image.h"
#include <string>
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

HWND Volume::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
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
    int fw = (int)(rc.right * frac);

    std::string base = m_engine ? m_engine->base_dir() : std::string();
    int cb = m_engine ? m_engine->colour_index() : 2;
    COLORREF accent = RGB(0, 140, 220); if (m_engine) m_engine->theme_color(accent);
    // Transparent groove (skin bg shows through).
    HWND parent = GetParent(m_wnd);
    POINT org = { 0, 0 }; MapWindowPoints(m_wnd, parent, &org, 1);
    HDC pdc = GetDC(parent);
    BitBlt(mem, 0, 0, rc.right, rc.bottom, pdc, org.x, org.y, SRCCOPY);
    ReleaseDC(parent, pdc);
    // Thin themed level bar (bar/v{cb}.png ~3px) + round knob (bar/vol{cb}.png 10x12), centred.
    const int bh = 4, by = (rc.bottom - bh) / 2;
    bool drew = false;
    if (fw > 0 && !base.empty())
        drew = draw_image(mem, base + "/images/fooAVA/bar/v" + std::to_string(cb) + ".png", 0, by, fw, bh);
    if (fw > 0 && !drew) fill_gradient_v(mem, 0, by, fw, bh, accent);
    if (!base.empty())
        draw_image(mem, base + "/images/fooAVA/bar/vol" + std::to_string(cb) + ".png",
                   fw - 5, (rc.bottom - 12) / 2, 0, 0); // knob (natural 10x12)

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
