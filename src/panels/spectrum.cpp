#include "spectrum.h"
#include "../core/skin_engine.h"
#include <cmath>
#include <algorithm>

namespace pui {

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

Spectrum::Spectrum(SkinEngine* engine) : m_engine(engine) {
    try { visualisation_manager::get()->create_stream(m_vis, visualisation_manager::KStreamFlagNewFFT); }
    catch (...) {}
    // Mono, like the reference renderer: a stereo chunk would interleave L/R and shift every
    // frequency by one bin, and it halves the work.
    if (m_vis.is_valid()) {
        try { m_vis->set_channel_mode(visualisation_stream_v2::channel_mode_mono); } catch (...) {}
    }
}

void Spectrum::on_attached() {
    refresh_cfg();
    host()->set_timer(1, 100); // keeps m_cfg current (theme colour, mode, backdrop)
}

void Spectrum::refresh_cfg() {
    Cfg c;
    c.mirror = m_mirror;
    if (m_engine) m_engine->theme_color(c.accent);
    // The lower strip is the inverted reflection: render it in a darkened shade of the theme
    // colour so it reads as a shadow under the analyser.
    if (c.mirror) c.accent = c.accent.scaled(45);
    c.bars = !m_engine || m_engine->pvar_str("spectrum.mode") != "standard";  // default Bars
    c.log  = !m_engine || m_engine->pvar_str("spectrum.scale") != "linear";   // default Log
    // Transparent background: the last frame of the panel we sit on (the cover), else the
    // master canvas.
    if (m_engine && host()) {
        gfx::Rect b = host()->bounds();
        int ox = 0, oy = 0;
        c.backdrop = m_engine->backdrop_for(b, ox, oy);
        c.bx = b.x - ox; c.by = b.y - oy;
    }
    std::lock_guard<std::mutex> lk(m_cfgMx);
    m_cfg = c;
}

void Spectrum::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (W <= 0 || H <= 0) return;
    Cfg cfg; { std::lock_guard<std::mutex> lk(m_cfgMx); cfg = m_cfg; }

    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());
    if (cfg.backdrop) {
        const gfx::Image& img = *cfg.backdrop;
        int sx = std::max(0, cfg.bx), sy = std::max(0, cfg.by);
        int ex = std::min(img.width(), cfg.bx + W), ey = std::min(img.height(), cfg.by + H);
        if (ex > sx && ey > sy)
            cv.draw_image(img, gfx::RectF{ (float)(sx - cfg.bx), (float)(sy - cfg.by), (float)(ex - sx), (float)(ey - sy) },
                          gfx::RectF{ (float)sx, (float)sy, (float)(ex - sx), (float)(ey - sy) });
    }

    const gfx::Color accent = cfg.accent;
    const bool barsMode = cfg.bars, logScale = cfg.log, mirror = cfg.mirror;

    audio_chunk_impl chunk;
    double t = 0; bool ok = false;
    if (m_vis.is_valid() && m_vis->get_absolute_time(t))
        ok = m_vis->get_spectrum_absolute(chunk, t, kFftSize);
    // Anything not mono means an input component interfered with the visualisation API; the
    // reference renderer bails out rather than misreading interleaved samples.
    if (!ok || chunk.get_sample_count() == 0 || chunk.get_channels() != 1) return;
    const audio_sample* d = chunk.get_data();
    const unsigned n = chunk.get_sample_count();
    const unsigned sr = chunk.get_sample_rate();
    const float maxF = std::min(kMaxFreq, sr / 2.f - 1.f);

    if (barsMode) {
        // The skin lays the analyser out as two stacked strips: the UPPER strip is the
        // analyser itself and its bars rise upward from the strip's bottom edge; the LOWER
        // strip is the inverted reflection and its bars descend from its top edge. Each strip
        // therefore draws one half only — no per-strip centre mirror — so the whole reads as
        // a single analyser plus its reflection below.
        const int bars = W / kBarW;
        for (int i = 0; i < bars; ++i) {
            auto [s, e] = band_bins(column_band(i, bars, logScale, kMinFreq, maxF), n, sr);
            double v = 0; for (unsigned k = s; k <= e; ++k) v = std::max(v, (double)d[k]);
            int yp = magnitude_to_height(v, H);
            int left = 1 + i * kBarW, right = left + kBarW - kBarGap;
            for (int k = 1; k <= yp; k += 2) {
                gfx::Rect rt = mirror
                    ? gfx::Rect::ltrb(left, k - 1, right, k)               // reflection: descend from top
                    : gfx::Rect::ltrb(left, H - k, right, H - k + 1);      // rise from bottom
                cv.fill_rect(rt, accent);
            }
        }
    } else {
        // Standard smooth mode, same up-only / down-only split.
        for (int x = 0; x < W; ++x) {
            auto [s, e] = band_bins(column_band(x, W, logScale, kMinFreq, maxF), n, sr);
            double v = 0; for (unsigned k = s; k <= e; ++k) v = std::max(v, (double)d[k]);
            int yp = magnitude_to_height(v, H);
            cv.fill_rect(mirror ? gfx::Rect{ x, 0, 1, yp } : gfx::Rect{ x, H - yp, 1, yp }, accent);
        }
    }
}

void Spectrum::show_context_menu(int x, int y) {
    if (!m_engine) return;
    bool barsMode = m_engine->pvar_str("spectrum.mode") != "standard";
    bool logScale = m_engine->pvar_str("spectrum.scale") != "linear";

    ui::Menu menu;
    auto item = [](const char* label, int id, bool checked) {
        ui::MenuItem m; m.label = label; m.id = id; m.checked = checked; return m;
    };
    menu.push_back(item("Bars", IDM_SPECTRUM_BARS, barsMode));
    menu.push_back(item("Standard", IDM_SPECTRUM_STANDARD, !barsMode));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Logarithmic Scale", IDM_SPECTRUM_LOG, logScale));
    menu.push_back(item("Linear Scale", IDM_SPECTRUM_LINEAR, !logScale));

    switch (ui::popup_menu(host(), x, y, menu)) {
    case IDM_SPECTRUM_BARS:     m_engine->set_pvar("spectrum.mode", "bars"); break;
    case IDM_SPECTRUM_STANDARD: m_engine->set_pvar("spectrum.mode", "standard"); break;
    case IDM_SPECTRUM_LOG:      m_engine->set_pvar("spectrum.scale", "log"); break;
    case IDM_SPECTRUM_LINEAR:   m_engine->set_pvar("spectrum.scale", "linear"); break;
    default: return;
    }
    refresh_cfg();
}

void Spectrum::on_mouse_up(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Right) show_context_menu(e.x, e.y);
}

} // namespace pui
