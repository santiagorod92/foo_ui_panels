// Native seekbar: click-to-seek over the skin's own progress bar. Replaces fooAvA's "Seek Panel"
// (a uie panel with no DUI equivalent). The skin script draws the bar itself; this view shows
// the canvas beneath it and handles the mouse (own drawing is only a fallback).
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class Seekbar : public ui::View {
public:
    explicit Seekbar(SkinEngine* engine) : m_engine(engine) {}
    // No timer of its own: it only shows the canvas snapshot, and every canvas paint repaints
    // it (SkinEngine::refresh_bars).
    void paint(gfx::Canvas& cv) override;
    void on_mouse_down(const ui::MouseEvent& e) override;

private:
    SkinEngine* m_engine = nullptr;
};

} // namespace pui
