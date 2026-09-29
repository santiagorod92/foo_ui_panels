#include "spectrum.h"
#include "skin_engine.h"
#include "image.h"
#include <cmath>
#include <algorithm>
#include <windowsx.h>

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_spectrum";

namespace {

// Frequency range [lo, hi) in Hz shown by output column `col` of `cols`: the analyser's
// [minFreq, maxFreq] span split evenly either in log space (equal musical width per column, so
// bass gets as many columns as treble) or linearly.
struct Band { float lo, hi; };
Band column_band(unsigned col, unsigned cols, bool logScale, float minFreq, float maxFreq) {
    const float t0 = (float)col / (float)cols, t1 = (float)(col + 1) / (float)cols;
    if (logScale) {
        const float span = std::log(maxFreq / minFreq);
        return { minFreq * std::exp(span * t0), minFreq * std::exp(span * t1) };
    }
    return { minFreq + (maxFreq - minFreq) * t0, minFreq + (maxFreq - minFreq) * t1 };
}

// FFT bins [first, last] whose centre frequencies fall inside `band`. The spectrum chunk holds
// `binCount` magnitudes spanning 0..Nyquist, so bin k sits at k * (Nyquist / binCount) Hz.
// A band narrower than one bin (low end of a log scale) still maps to the nearest single bin.
std::pair<unsigned, unsigned> band_bins(Band band, unsigned binCount, unsigned sampleRate) {
    const float binHz = (sampleRate * 0.5f) / (float)binCount;
    const int maxBin = (int)binCount - 1;
    int first = std::clamp((int)std::floor(band.lo / binHz + 0.5f), 0, maxBin);
    int last  = std::clamp((int)std::ceil(band.hi / binHz - 0.5f) - 1, 0, maxBin);
    if (last < first) last = first;
    return { (unsigned)first, (unsigned)last };
}

// Bar height in pixels for a linear magnitude: shown on an 80 dB window (-80 dB = empty,
// 0 dB = full height).
int magnitude_to_height(double magnitude, int height) {
    constexpr double kFloorDb = -80.0;
    const double db = magnitude > 0 ? 20.0 * std::log10(magnitude) : kFloorDb;
    const double fill = std::clamp((db - kFloorDb) / -kFloorDb, 0.0, 1.0);
    return (int)std::lround(fill * height);
}

// Columns UI defaults to 4096, but that's a ~93ms window at 44.1kHz: every transient gets smeared
// across several frames and the bars rise/fall sluggishly. 2048 (~46ms) reacts twice as fast
// while keeping enough low-frequency resolution for the log-scaled bass bars.
constexpr unsigned kFftSize = 2048;
constexpr float kMinFreq = 50.f, kMaxFreq = 22050.f;
constexpr int kBarW = 14, kBarGap = 1; // extra-fat LED bars requested by the user

enum {
    IDM_SPECTRUM_BARS = 1, IDM_SPECTRUM_STANDARD, IDM_SPECTRUM_LOG, IDM_SPECTRUM_LINEAR,
};

} // namespace

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
    // Mono, like the reference renderer: a stereo chunk would interleave L/R and shift every
    // frequency by one bin, and it halves the work.
    if (m_vis.is_valid()) {
        try { m_vis->set_channel_mode(visualisation_stream_v2::channel_mode_mono); } catch (...) {}
    }
    m_wnd = CreateWindowExW(WS_EX_TRANSPARENT, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) {
        // timeBeginPeriod sharpens Sleep() in the render loop (default Windows/Wine granularity
        // is ~15.6ms, which makes frames land unevenly and the bars stutter).
        MMRESULT mr = timeBeginPeriod(1);
        if (mr != TIMERR_NOCANDO) m_highResTimer = true;
        refresh_cfg();
        SetTimer(m_wnd, 1, 100, nullptr); // keeps m_cfg current (theme colour, mode, backing window)
        m_thread = std::thread([this] { render_loop(); });
    }
    return m_wnd;
}

void Spectrum::refresh_cfg() {
    Cfg c;
    c.mirror = m_mirror;
    if (m_engine) m_engine->theme_color(c.accent);
    // The lower strip is the inverted reflection: render it in a darkened shade of the theme
    // colour so it reads as a shadow under the analyser.
    if (c.mirror)
        c.accent = RGB((GetRValue(c.accent) * 45) / 100, (GetGValue(c.accent) * 45) / 100, (GetBValue(c.accent) * 45) / 100);
    c.bars = !m_engine || m_engine->pvar_str("spectrum.mode") != "standard";  // default Bars
    c.log  = !m_engine || m_engine->pvar_str("spectrum.scale") != "linear";   // default Log

    // Transparent background: an opaque child occludes the cover beneath it, so Windows never
    // repaints the covered strip and copying it yields white. With WS_EX_TRANSPARENT our window
    // doesn't occlude — the cover paints the strip whenever it next repaints — so we mostly just
    // copy its already-painted surface. Force one cover repaint only when our window moved or
    // resized (fresh strip that may never have been painted); never per-frame: forcing the whole
    // cover to redraw at 25fps is what made the analyser flicker.
    HWND backing = nullptr;
    RECT sr; GetWindowRect(m_wnd, &sr);
    for (HWND ch = GetWindow(GetParent(m_wnd), GW_CHILD); ch; ch = GetWindow(ch, GW_HWNDNEXT)) {
        if (!IsWindowVisible(ch) || ch == m_wnd) continue;
        RECT cr; GetWindowRect(ch, &cr);
        if (cr.right <= sr.left || cr.left >= sr.right || cr.bottom <= sr.top || cr.top >= sr.bottom) continue;
        backing = ch;
    }
    if (!backing) backing = GetParent(m_wnd);
    if (sr.left != m_bgPos.left || sr.top != m_bgPos.top ||
        sr.right != m_bgPos.right || sr.bottom != m_bgPos.bottom) {
        m_bgPos = sr;
        RedrawWindow(backing, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    c.backing = backing;
    MapWindowPoints(m_wnd, backing, &c.org, 1);

    std::lock_guard<std::mutex> lk(m_cfgMx);
    m_cfg = c;
}

void Spectrum::render_loop() {
    // ~60fps on a steady clock. Nothing in here sends messages to the UI thread (GetDC/BitBlt
    // don't), so WM_DESTROY can join us without risking a deadlock.
    constexpr DWORD kFrameMs = 16;
    DWORD next = timeGetTime();
    while (!m_stop.load()) {
        render_frame();
        next += kFrameMs;
        DWORD now = timeGetTime();
        if ((int)(next - now) > 0) Sleep(next - now);
        else next = now; // fell behind (e.g. system stall): don't try to catch up in a burst
    }
}

void Spectrum::render_frame() {
    if (!IsWindowVisible(m_wnd)) return;
    RECT rc; GetClientRect(m_wnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    Cfg cfg; { std::lock_guard<std::mutex> lk(m_cfgMx); cfg = m_cfg; }
    if (!cfg.backing) return;
    HDC dc = GetDC(m_wnd);
    if (!dc) return;
    // Cached backbuffer: recreate only when the window resizes (per-frame CreateCompatibleDC/
    // SelectObject is a classic GDI bottleneck that makes the animation stutter).
    if (!m_mem || m_bw != rc.right || m_bh != rc.bottom) {
        if (m_oldBmp) SelectObject(m_mem, m_oldBmp);
        if (m_bmp) DeleteObject(m_bmp);
        if (m_mem) DeleteDC(m_mem);
        m_mem = CreateCompatibleDC(dc);
        m_bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        m_oldBmp = SelectObject(m_mem, m_bmp);
        m_bw = rc.right; m_bh = rc.bottom;
    }
    HDC mem = m_mem;

    if (HDC bdc = GetDC(cfg.backing)) {
        BitBlt(mem, 0, 0, rc.right, rc.bottom, bdc, cfg.org.x, cfg.org.y, SRCCOPY);
        ReleaseDC(cfg.backing, bdc);
    }

    const COLORREF accent = cfg.accent;
    const bool barsMode = cfg.bars, logScale = cfg.log, mirror = cfg.mirror;

    audio_chunk_impl chunk;
    double t = 0; bool ok = false;
    if (m_vis.is_valid() && m_vis->get_absolute_time(t))
        ok = m_vis->get_spectrum_absolute(chunk, t, kFftSize);
    // Anything not mono means an input component interfered with the visualisation API; the
    // reference renderer bails out rather than misreading interleaved samples.
    if (ok && chunk.get_sample_count() > 0 && chunk.get_channels() == 1) {
        const audio_sample* d = chunk.get_data();
        const unsigned n = chunk.get_sample_count();
        const unsigned sr = chunk.get_sample_rate();
        const float maxF = std::min(kMaxFreq, sr / 2.f - 1.f);
        HBRUSH br = CreateSolidBrush(accent);

        if (barsMode) {
            // The skin lays the analyser out as two stacked strips: the UPPER strip is the
            // analyser itself and its bars rise upward from the strip's bottom edge; the LOWER
            // strip is the inverted reflection and its bars descend from its top edge. Each strip
            // therefore draws one half only — no per-strip centre mirror — so the whole reads as
            // a single analyser plus its reflection below.
            const int bars = rc.right / kBarW;
            for (int i = 0; i < bars; ++i) {
                auto [s, e] = band_bins(column_band(i, bars, logScale, kMinFreq, maxF), n, sr);
                double v = 0; for (unsigned k = s; k <= e; ++k) v = std::max(v, (double)d[k]);
                int yp = magnitude_to_height(v, rc.bottom);
                int left = 1 + i * kBarW, right = left + kBarW - kBarGap;
                for (int k = 1; k <= yp; k += 2) {
                    RECT rt = mirror
                              ? RECT{ left, k - 1, right, k }      // reflection: descend from top
                              : RECT{ left, rc.bottom - k, right, rc.bottom - k + 1 }; // rise from bottom
                    FillRect(mem, &rt, br);
                }
            }
        } else {
            // Standard smooth mode, same up-only / down-only split.
            for (int x = 0; x < rc.right; ++x) {
                auto [s, e] = band_bins(column_band(x, rc.right, logScale, kMinFreq, maxF), n, sr);
                double v = 0; for (unsigned k = s; k <= e; ++k) v = std::max(v, (double)d[k]);
                int yp = magnitude_to_height(v, rc.bottom);
                RECT rt = mirror ? RECT{ x, 0, x + 1, yp } : RECT{ x, rc.bottom - yp, x + 1, rc.bottom };
                FillRect(mem, &rt, br);
            }
        }
        DeleteObject(br);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    ReleaseDC(m_wnd, dc);
}

void Spectrum::show_context_menu(int x, int y) {
    if (!m_engine) return;
    bool barsMode = m_engine->pvar_str("spectrum.mode") != "standard";
    bool logScale = m_engine->pvar_str("spectrum.scale") != "linear";

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (barsMode ? MF_CHECKED : 0), IDM_SPECTRUM_BARS, L"Bars");
    AppendMenuW(menu, MF_STRING | (!barsMode ? MF_CHECKED : 0), IDM_SPECTRUM_STANDARD, L"Standard");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (logScale ? MF_CHECKED : 0), IDM_SPECTRUM_LOG, L"Logarithmic Scale");
    AppendMenuW(menu, MF_STRING | (!logScale ? MF_CHECKED : 0), IDM_SPECTRUM_LINEAR, L"Linear Scale");

    POINT pt = { x, y }; ClientToScreen(m_wnd, &pt);
    SetForegroundWindow(m_wnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_wnd, nullptr);
    PostMessage(m_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_SPECTRUM_BARS:     m_engine->set_pvar("spectrum.mode", "bars"); break;
    case IDM_SPECTRUM_STANDARD: m_engine->set_pvar("spectrum.mode", "standard"); break;
    case IDM_SPECTRUM_LOG:      m_engine->set_pvar("spectrum.scale", "log"); break;
    case IDM_SPECTRUM_LINEAR:   m_engine->set_pvar("spectrum.scale", "linear"); break;
    default: return;
    }
    refresh_cfg();
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
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); EndPaint(wnd, &ps); return 0; } // render thread draws
    case WM_TIMER:
        if (self) self->refresh_cfg();
        return 0;
    case WM_RBUTTONUP:
        if (self) self->show_context_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_DESTROY:
        KillTimer(wnd, 1);
        if (self) {
            self->m_stop = true;
            if (self->m_thread.joinable()) self->m_thread.join();
            if (self->m_oldBmp) SelectObject(self->m_mem, self->m_oldBmp);
            if (self->m_bmp) DeleteObject(self->m_bmp);
            if (self->m_mem) DeleteDC(self->m_mem);
            if (self->m_highResTimer) timeEndPeriod(1);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
