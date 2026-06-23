#include "popup.h"
#include "skin_engine.h"

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_popup";

void Popup::register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);
}

void Popup::show(HWND owner, SkinEngine* engine, const char* script,
                 int w, int h, const wchar_t* title) {
    register_class();
    m_engine = engine;
    titleformat_compiler::get()->compile_safe(m_script, script);

    if (m_wnd) { // already open: refresh + raise
        InvalidateRect(m_wnd, nullptr, TRUE);
        SetForegroundWindow(m_wnd);
        return;
    }

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    RECT rc = { 0, 0, w, h };
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    // centre on the owner window
    RECT orc = {}; if (owner) GetWindowRect(owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - ww) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - wh) / 2;

    m_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, title, style, x, y, ww, wh,
                            owner, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 1000, nullptr); // reflect external pvar/theme changes
}

void Popup::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    // The settings script paints its own opaque background; start from black.
    HBRUSH bg = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(mem, &rc, bg);

    metadb_handle_ptr track;
    playback_control::get()->get_now_playing(track);
    if (m_engine) m_engine->draw_script(mem, rc.right, rc.bottom, m_script, track, &m_buttons);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

void Popup::on_click(int x, int y) {
    if (!m_engine) return;
    for (const auto& b : m_buttons) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
            m_engine->run_button_action(b.action); // repaints main UI on a pvar change
            InvalidateRect(m_wnd, nullptr, TRUE);   // and reflect the toggle here
            return;
        }
    }
}

LRESULT CALLBACK Popup::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    Popup* self = reinterpret_cast<Popup*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Popup*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: if (self) self->on_click((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_CLOSE: DestroyWindow(wnd); return 0;
    case WM_DESTROY: KillTimer(wnd, 1); if (self) self->m_wnd = nullptr; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
