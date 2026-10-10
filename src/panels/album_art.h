#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class AlbumArt : public ui::View {
public:
    explicit AlbumArt(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }
    void on_timer(int) override;
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    gfx::ImagePtr current_image() const;
    SkinEngine* m_engine = nullptr;
    gfx::ImagePtr m_cur, m_prev;
    unsigned long long m_fadeStart = 0;
};

}
