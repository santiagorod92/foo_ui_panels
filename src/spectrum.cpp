#include "spectrum.h"
#include "skin_engine.h"
#include "image.h"
#include <cmath>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_spectrum";

void Spectrum::register_class() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND Spectrum::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    try { visualisation_manager::get()->create_stream(m_vis, visualisation_manager::KStreamFlagNewFFT); }
    catch (...) {}
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 40, nullptr); // ~25fps
    return m_wnd;
}

void Spectrum::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    // Dark bg (sits over the cover-area carbon, which is near-black).
    HBRUSH bg = CreateSolidBrush(RGB(8, 8, 10)); FillRect(mem, &rc, bg); DeleteObject(bg);

    COLORREF accent = RGB(0, 140, 220); if (m_engine) m_engine->theme_color(accent);

    const unsigned fft = 512;
    audio_chunk_impl chunk;
    double t = 0; bool ok = false;
    if (m_vis.is_valid() && m_vis->get_absolute_time(t))
        ok = m_vis->get_spectrum_absolute(chunk, t, fft);
    if (ok && chunk.get_sample_count() > 0) {
        const audio_sample* d = chunk.get_data();
        const unsigned n = chunk.get_sample_count();      // = fft/2
        // Thin bars mirrored around a horizontal centre line (like the skin's EQ): bright bar up,
        // dimmer reflection down. Bar slot ~5px.
        const int slot = 5;
        const int bars = rc.right / slot > 4 ? rc.right / slot : 4;
        const int cy = rc.bottom / 2;
        const int up = (int)(rc.bottom * 0.46), dn = (int)(rc.bottom * 0.30); // up/reflection extents
        COLORREF dim = RGB(GetRValue(accent) * 60 / 100, GetGValue(accent) * 60 / 100, GetBValue(accent) * 60 / 100);
        for (int i = 0; i < bars; ++i) {
            double f0 = std::pow((double)i / bars, 2.0);
            double f1 = std::pow((double)(i + 1) / bars, 2.0);
            unsigned a = (unsigned)(f0 * n * 0.7), b = (unsigned)(f1 * n * 0.7);
            if (b <= a) b = a + 1; if (b > n) b = n;
            double mag = 0; for (unsigned k = a; k < b; ++k) if (d[k] > mag) mag = d[k];
            double v = std::sqrt(mag) * 1.4; if (v > 1) v = 1;       // perceptual scale
            int hu = (int)(v * up), hd = (int)(v * dn);
            int x = i * slot, w = slot - 1; if (w < 1) w = 1;
            if (hu > 0) fill_gradient_v(mem, x, cy - hu, w, hu, accent);          // bar up (bright)
            if (hd > 0) { HBRUSH rb = CreateSolidBrush(dim);                       // reflection down (dim)
                RECT r = { x, cy + 1, x + w, cy + 1 + hd }; FillRect(mem, &r, rb); DeleteObject(rb); }
        }
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

LRESULT CALLBACK Spectrum::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    Spectrum* self = reinterpret_cast<Spectrum*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Spectrum*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_DESTROY: KillTimer(wnd, 1); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
