// Native seekbar: progress bar + click-to-seek. Replaces fooAvA's "Seek Panel"
// (a uie panel with no DUI equivalent). Draws itself (no assets needed).
#pragma once
#include "win_sdk.h"

namespace pui {

class SkinEngine; // for theme accent colour

class Seekbar {
public:
    HWND create(HWND parent, SkinEngine* engine = nullptr);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
};

} // namespace pui
