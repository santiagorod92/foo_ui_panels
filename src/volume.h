// Native volume slider. Replaces fooAvA's "Volume Panel". Self-drawn, no assets.
#pragma once
#include "win_sdk.h"

namespace pui {

class Volume {
public:
    HWND create(HWND parent);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void set_from_x(int x);
    HWND m_wnd = nullptr;
};

} // namespace pui
