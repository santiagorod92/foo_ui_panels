// Native spectrum analyser (the EQ bars under the cover when playing). Replaces the skin's
// "Channel spectrum panel" (a Columns UI panel type we can't host in a DUI replacement — Columns
// UI panels are its own ui_extension system, never exposed as generic ui_element services, so
// there is no way to host the real one here). Log/linear frequency bands over an FFT from
// foobar2000's visualisation stream, bar height on an 80 dB window. Painted by the host's
// render thread (ViewOptions::render_fps): on the UI thread the bars froze for 50-100ms every
// time the canvas / track display / playlist repainted, which read as a slow, stuttering analyser.
#pragma once
#include "../ui/view.h"
#include <mutex>

namespace pui {

class SkinEngine;

class Spectrum : public ui::View {
public:
    explicit Spectrum(SkinEngine* engine);
    void set_mirror(bool mirror) { m_mirror = mirror; }

    void on_attached() override;
    void on_timer(int) override { refresh_cfg(); }
    void paint(gfx::Canvas& cv) override; // render thread
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    void refresh_cfg();   // UI thread: snapshot skin state the render thread needs
    void show_context_menu(int x, int y); // right-click: Bars/Standard mode, Linear/Log scale

    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream_v2> m_vis; // v2 for set_channel_mode(mono), as the reference does
    bool m_mirror = false; // true = lower strip, the inverted reflection; the upper strip rises
    // Skin state read on the UI thread (the engine's pvar map is rewritten by every canvas
    // render, so the render thread must never touch it) and handed over under m_cfgMx.
    struct Cfg {
        gfx::Color accent{ 0, 140, 220 }; bool bars = true, log = true, mirror = false;
        gfx::ImagePtr backdrop; int bx = 0, by = 0; // what lies beneath us, and our offset into it
    };
    Cfg m_cfg; std::mutex m_cfgMx;
};

} // namespace pui
