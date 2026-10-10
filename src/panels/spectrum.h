#pragma once
#include "../ui/view.h"
#include <mutex>

namespace pui {

class SkinEngine;

class Spectrum : public ui::View {
public:
    explicit Spectrum(SkinEngine* engine);
    void set_mirror(bool mirror) { m_mirror = mirror; }

    void on_attached() override;
    void on_timer(int) override { refresh_cfg(); }
    void paint(gfx::Canvas& cv) override;
    void on_mouse_up(const ui::MouseEvent& e) override;

private:
    void refresh_cfg();
    void show_context_menu(int x, int y);

    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream_v2> m_vis;
    bool m_mirror = false;
    struct Cfg {
        gfx::Color accent{ 0, 140, 220 }; bool bars = true, log = true, mirror = false;
        gfx::ImagePtr backdrop; int bx = 0, by = 0;
    };
    Cfg m_cfg; std::mutex m_cfgMx;
};

}
