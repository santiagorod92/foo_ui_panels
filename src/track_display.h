// Native "Track Display" panel: a child window that paints now-playing info by
// running a titleformat script through the shared draw engine each repaint.
// Replaces the legacy fooAvA "Track Display" uie panel (which had no DUI equivalent).
#pragma once
#include "win_sdk.h"

namespace pui {

class SkinEngine; // shares pvars / base dir / draw funcs

class TrackDisplay {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    void set_script(const char* spec); // compiles; falls back to a default now-playing script

    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    service_ptr_t<titleformat_object> m_script;
};

} // namespace pui
