#include "peak_meter.h"
#include "../core/skin_engine.h"
#include "../core/image_cache.h"
#include <algorithm>
#include <cmath>

namespace pui {

namespace {
constexpr double kWindow = 0.05;   // seconds of audio per reading
constexpr int kSegW = 4, kSegGap = 1, kBarGap = 2;
constexpr unsigned kMaxChannels = 8;
} // namespace

PeakMeter::PeakMeter(SkinEngine* engine) : m_engine(engine) {
    try { visualisation_manager::get()->create_stream(m_vis, 0); } catch (...) {}
}

void PeakMeter::on_attached() {
    refresh_cfg();
    host()->set_timer(1, 100); // theme colour + backdrop
}

void PeakMeter::refresh_cfg() {
    Cfg c;
    if (m_engine) m_engine->theme_color(c.accent);
    if (m_engine && host()) {
        gfx::Rect b = host()->bounds();
        int ox = 0, oy = 0;
        c.backdrop = m_engine->backdrop_for(b, ox, oy);
        c.bx = b.x - ox; c.by = b.y - oy;
    }
    std::lock_guard<std::mutex> lk(m_cfgMx);
    m_cfg = c;
}

void PeakMeter::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    if (W <= 0 || H <= 0) return;
    Cfg cfg; { std::lock_guard<std::mutex> lk(m_cfgMx); cfg = m_cfg; }

    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());
    if (cfg.backdrop) draw_image_region(cv, *cfg.backdrop, cfg.bx, cfg.by, W, H);

    // Per-channel peak of the last kWindow seconds; nothing playing reads as silence, so the
    // bars fall away instead of freezing.
    std::vector<double> db;
    audio_chunk_impl chunk;
    double t = 0;
    if (m_vis.is_valid() && m_vis->get_absolute_time(t) && m_vis->get_chunk_absolute(chunk, t - kWindow, kWindow)) {
        const unsigned nch = std::min(chunk.get_channels(), kMaxChannels);
        const size_t n = chunk.get_sample_count();
        const audio_sample* d = chunk.get_data();
        if (nch && d) {
            std::vector<double> peak(nch, 0.0);
            for (size_t i = 0; i < n; ++i)
                for (unsigned c = 0; c < nch; ++c)
                    peak[c] = std::max(peak[c], (double)std::fabs(d[i * chunk.get_channels() + c]));
            for (double p : peak) db.push_back(amp_to_db(p));
        }
    }
    if (db.empty()) db.assign(m_ch.empty() ? 2 : m_ch.size(), kMeterFloorDb);
    if (m_ch.size() != db.size()) m_ch.assign(db.size(), MeterBallistics{});

    const auto now = std::chrono::steady_clock::now();
    const double dt = m_last.time_since_epoch().count() ? std::chrono::duration<double>(now - m_last).count() : 0.0;
    m_last = now;

    const int nch = (int)m_ch.size();
    const int barH = std::max(1, (H - kBarGap * (nch - 1)) / nch);
    const int segs = std::max(1, W / kSegW);
    const gfx::Color dim = cfg.accent.scaled(25), hot = gfx::Color(255, 255, 255);
    for (int c = 0; c < nch; ++c) {
        m_ch[c].update(db[c], dt);
        const int y = c * (barH + kBarGap);
        const int lit = (int)std::lround(meter_fill(m_ch[c].level) * segs);
        const int peakSeg = (int)std::lround(meter_fill(m_ch[c].peak) * segs) - 1;
        for (int s = 0; s < segs; ++s) {
            const gfx::Rect r{ s * kSegW, y, kSegW - kSegGap, barH };
            if (s == peakSeg && peakSeg >= 0) cv.fill_rect(r, hot);
            else cv.fill_rect(r, s < lit ? cfg.accent : dim);
        }
    }
}

} // namespace pui
