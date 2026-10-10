#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class Seekbar : public ui::View {
public:
    explicit Seekbar(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_mouse_down(const ui::MouseEvent& e) override;

private:
    SkinEngine* m_engine = nullptr;
};

}
