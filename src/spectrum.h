// Native spectrum analyser (the EQ bars under the cover when playing). Replaces the skin's
// "Channel spectrum panel" (a vis component we can't host in a DUI replacement). Reads audio
// via visualisation_manager and draws themed bars.
#pragma once
#include "win_sdk.h"

namespace pui {

class SkinEngine;

class Spectrum {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream> m_vis;
};

} // namespace pui
