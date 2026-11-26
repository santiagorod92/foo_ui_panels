#include "popup.h"
#include "../core/skin_engine.h"
#include "../core/log.h"

namespace pui {

void PopupView::set_script(const char* script) {
    titleformat_compiler::get()->compile_safe(m_script, defer_button_images(script ? script : "").c_str());
}

void PopupView::paint(gfx::Canvas& cv) {
    cv.fill_rect(gfx::Rect{ 0, 0, cv.width(), cv.height() }, gfx::Color());
    metadb_handle_ptr track;
    playback_control::get()->get_now_playing(track);
    if (m_engine) m_engine->draw_script(cv, cv.width(), cv.height(), m_script, track, &m_buttons, -1, -1, &m_placements);
    if (!m_placements.empty() && !m_warnedPanels) {
        m_warnedPanels = true;
        log::warn("skin", "a popup places panels ('" + m_placements.front().name + "', " + m_placements.front().type +
                              "); panels inside popups aren't supported yet, left out");
    }
}

void PopupView::on_mouse_down(const ui::MouseEvent& e) {
    if (!m_engine || e.button != ui::MouseButton::Left) return;
    for (const auto& b : m_buttons) {
        if (button_hit(b, e.x, e.y)) {
            std::string act = b.action;
            m_engine->run_button_action(act);
            invalidate();
            return;
        }
    }
}

}
