// Native seekbar: progress bar + click-to-seek. Replaces fooAvA's "Seek Panel"
// (a uie panel with no DUI equivalent). Draws itself (no assets needed).
#pragma once
#include "win_sdk.h"

namespace pui {

class Seekbar {
public:
    HWND create(HWND parent);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    HWND m_wnd = nullptr;
};

} // namespace pui
