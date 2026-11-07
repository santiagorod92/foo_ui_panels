#pragma once
#include "../ui/view.h"
#include <string>

namespace pui {

class SkinEngine;

class AlbumArt : public ui::View {
public:
    AlbumArt(SkinEngine* engine, std::string name) : m_engine(engine), m_name(std::move(name)) {}
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }
    void on_timer(int) override;
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    gfx::ImagePtr current_image() const;
    SkinEngine* m_engine = nullptr;
    std::string m_name;
    gfx::ImagePtr m_cur, m_prev;
    unsigned long long m_fadeStart = 0;
};

}
