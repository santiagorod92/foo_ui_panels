// Native peak meter for the legacy "Peakmeter" panel type (fooAvA shows it instead of the
// analyser when its `sanalyser` setting is 1). Stereo — or as many channels as the stream has, up
// to 8 — horizontal LED bars on a -60..0 dB scale in the theme accent, with a held-peak marker.
// Painted on the host's render thread like the spectrum (ViewOptions::render_fps), from
// foobar2000's visualisation stream; no Default UI "Peak Meter" element needed (none on macOS).
#pragma once
#include "../ui/view.h"
#include "../core/meter_math.h"
#include <chrono>
#include <mutex>
#include <vector>

namespace pui {

class SkinEngine;

class PeakMeter : public ui::View {
public:
    explicit PeakMeter(SkinEngine* engine);

    void on_attached() override;
    void on_timer(int) override { refresh_cfg(); }
    void paint(gfx::Canvas& cv) override; // render thread

private:
    void refresh_cfg(); // UI thread: skin state the render thread needs (as Spectrum does)

    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream> m_vis;
    struct Cfg {
        gfx::Color accent{ 0, 140, 220 };
        gfx::ImagePtr backdrop; int bx = 0, by = 0;
    };
    Cfg m_cfg; std::mutex m_cfgMx;
    // Render thread only.
    std::vector<MeterBallistics> m_ch;
    std::chrono::steady_clock::time_point m_last{};
};

} // namespace pui
