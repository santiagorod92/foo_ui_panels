#pragma once
#include "win_sdk.h"

namespace pui {

class PanelHost : public ui_element_instance_callback_receiver {
public:
    HWND create(HWND parent, const char* name);
    HWND wnd() const { return m_inst.is_valid() ? m_inst->get_wnd() : nullptr; }
    void destroy() { m_inst.release(); }

    bool is_edit_mode_enabled() override { return false; }

    static void log_available();

private:
    ui_element_instance_ptr m_inst;
};

}
