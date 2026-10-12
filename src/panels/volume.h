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
    void on_wheel(int x, int y, float notches) override;
    bool on_key_down(int key, unsigned mods) override;

private:
    void set_from_x(int x);
    void changed();
    SkinEngine* m_engine = nullptr;
    float m_wheel = 0;
};

}
