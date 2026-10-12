#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class Seekbar : public ui::View {
public:
    explicit Seekbar(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_up(const ui::MouseEvent& e) override;
    void on_mouse_leave() override;
    void on_wheel(int x, int y, float notches) override;
    bool on_key_down(int key, unsigned mods) override;

private:
    void seek_to(double t);
    void repaint();
    SkinEngine* m_engine = nullptr;
    int m_hoverX = -1;
    bool m_dragging = false;
    float m_wheel = 0;
};

}
