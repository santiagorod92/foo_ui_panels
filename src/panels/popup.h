// Floating popup that runs a PanelsUI script (e.g. fooAvA's settings panel, opened by the gear
// button's 'POPUP:FOOAvA_settings.ava' action). Self-draws via the shared SkinEngine draw funcs
// and routes its clicks back through run_button_action. Hosted in a ui::create_popup_window.
#pragma once
#include "../ui/view.h"
#include "../core/button.h"
#include <vector>

namespace pui {

class SkinEngine;

class PopupView : public ui::View {
public:
    explicit PopupView(SkinEngine* engine) : m_engine(engine) {}
    void set_script(const char* script);

    void on_attached() override { host()->set_timer(1, 1000); } // reflect external pvar/theme changes
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override { invalidate(); }
    void on_mouse_down(const ui::MouseEvent& e) override;

private:
    SkinEngine* m_engine = nullptr;
    service_ptr_t<titleformat_object> m_script;
    std::vector<Button> m_buttons;
};

} // namespace pui
