// Phase 2 — host a foobar2000 Default-UI element (ui_element) inside one of our panes.
// fooAvA's $panel(name, "uie_type", ...) maps onto this: instantiate a registered
// UI element by name and place its window in the layout.
#pragma once
#include "win_sdk.h"

namespace pui {

class PanelHost : public ui_element_instance_callback_receiver {
public:
    // Instantiate the first ui_element whose name contains `name` (case-insensitive),
    // as a child of `parent`. Returns the hosted window, or null if none matched.
    HWND create(HWND parent, const char* name);
    HWND wnd() const { return m_inst.is_valid() ? m_inst->get_wnd() : nullptr; }
    void destroy() { m_inst.release(); }

    // Host callback overrides (defaults from the helper are fine for now).
    bool is_edit_mode_enabled() override { return false; }

    // Logs all available element names to the fb2k console (discovery aid).
    static void log_available();

private:
    ui_element_instance_ptr m_inst;
};

} // namespace pui
