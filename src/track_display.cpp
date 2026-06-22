#include "track_display.h"
#include "skin_engine.h"

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_trackdisplay";

// Default content until per-panel scripts are wired from the skin.
static const char* kDefaultScript =
    "$font(Segoe UI,15,b)$drawstring([%title%],6,4,%el_width%,22,255-255-255,)"
    "$font(Segoe UI,11,)$drawstring([%artist%][ \xe2\x80\x94 %album%],6,28,%el_width%,18,200-200-210,)"
    "$font(Segoe UI,11,)$drawstring([%playback_time% / %length%],6,48,%el_width%,18,160-200-255,)";

void TrackDisplay::register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND TrackDisplay::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    if (m_script.is_empty()) set_script(nullptr);
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 1000, nullptr); // refresh elapsed time / track changes
    return m_wnd;
}

void TrackDisplay::set_script(const char* spec) {
    titleformat_compiler::get()->compile_safe(m_script, spec && *spec ? spec : kDefaultScript);
}

void TrackDisplay::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    // Transparent background: copy what the parent painted behind us.
    HWND parent = GetParent(m_wnd);
    POINT org = { 0, 0 }; MapWindowPoints(m_wnd, parent, &org, 1);
    HDC pdc = GetDC(parent);
    BitBlt(mem, 0, 0, rc.right, rc.bottom, pdc, org.x, org.y, SRCCOPY);
    ReleaseDC(parent, pdc);

    metadb_handle_ptr track;
    playback_control::get()->get_now_playing(track);
    if (m_engine) m_engine->draw_script(mem, rc.right, rc.bottom, m_script, track);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

LRESULT CALLBACK TrackDisplay::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    TrackDisplay* self = reinterpret_cast<TrackDisplay*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<TrackDisplay*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_SIZE:  InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_DESTROY: KillTimer(wnd, 1); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
