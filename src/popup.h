// Floating popup window that runs a PanelsUI script (e.g. fooAvA's settings panel,
// opened by the gear button's 'POPUP:FOOAvA_settings.ava' action). Self-draws via the
// shared SkinEngine draw funcs and routes its clicks back through run_button_action.
#pragma once
#include "win_sdk.h"
#include "button.h"
#include <vector>

namespace pui {

class SkinEngine;

class Popup {
public:
    // Compile `script` and show a top-level window (w×h client) centred on `owner`.
    // If already open, just re-show/raise it.
    void show(HWND owner, SkinEngine* engine, const char* script,
              int w, int h, const wchar_t* title);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void on_click(int x, int y);

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    service_ptr_t<titleformat_object> m_script;
    std::vector<Button> m_buttons;
};

} // namespace pui
