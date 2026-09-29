// Native spectrum analyser (the EQ bars under the cover when playing). Replaces the skin's
// "Channel spectrum panel" (a Columns UI panel type we can't host in a DUI replacement — Columns
// UI panels are its own ui_extension system, never exposed as generic ui_element services, so
// there is no way to host the real one here). Log/linear frequency bands over an FFT from
// foobar2000's visualisation stream, bar height on an 80 dB window. Frames are drawn on a
// dedicated render thread: on the UI thread the bars froze for 50-100ms every time the canvas /
// track display / playlist repainted, which read as a slow, stuttering analyser.
#pragma once
#include "win_sdk.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace pui {

class SkinEngine;

class Spectrum {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    void set_mirror(bool mirror) { m_mirror = mirror; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void refresh_cfg();   // UI thread: snapshot skin state the render thread needs
    void render_loop();   // render thread
    void render_frame();  // render thread
    void show_context_menu(int x, int y); // right-click: Bars/Standard mode, Linear/Log scale

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream_v2> m_vis; // v2 for set_channel_mode(mono), as the reference does
    bool m_mirror = false; // true = lower strip, the inverted reflection; the upper strip rises
    RECT m_bgPos = {}; // last window rect; the backing cover only needs a forced repaint when we move/resize
    HDC m_mem = nullptr; HBITMAP m_bmp = nullptr; HGDIOBJ m_oldBmp = nullptr; // cached backbuffer (render thread only)
    int m_bw = 0, m_bh = 0;
    // Skin state read on the UI thread (the engine's pvar map is rewritten by every canvas
    // render, so the render thread must never touch it) and handed over under m_cfgMx.
    struct Cfg { COLORREF accent = RGB(0, 140, 220); bool bars = true, log = true, mirror = false;
                 HWND backing = nullptr; POINT org = {}; };
    Cfg m_cfg; std::mutex m_cfgMx;
    std::thread m_thread; std::atomic<bool> m_stop{false};
    bool m_highResTimer = false; // timeBeginPeriod(1) active; pair with timeEndPeriod on destroy
};

} // namespace pui
