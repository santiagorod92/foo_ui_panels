// Native volume slider. Replaces fooAvA's "Volume Panel". Self-drawn, no assets.
#pragma once
#include "win_sdk.h"

namespace pui {

class SkinEngine; // for theme accent colour

class Volume {
public:
    HWND create(HWND parent, SkinEngine* engine = nullptr);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void set_from_x(int x);
    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
};

} // namespace pui
