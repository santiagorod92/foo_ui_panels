#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class Volume : public ui::View {
public:
    explicit Volume(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    void set_from_x(int x);
    SkinEngine* m_engine = nullptr;
};

}
