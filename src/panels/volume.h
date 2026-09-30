// Native volume bar: click/drag to set the volume over the skin's own volume art. The skin
// script draws the bar itself; this view shows the canvas beneath it and handles the mouse.
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class Volume : public ui::View {
public:
    explicit Volume(SkinEngine* engine) : m_engine(engine) {}
    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override { invalidate(); }
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    void set_from_x(int x);
    SkinEngine* m_engine = nullptr;
};

} // namespace pui
