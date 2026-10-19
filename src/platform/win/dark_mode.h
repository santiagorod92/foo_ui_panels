#pragma once
#include "win_sdk.h"
#include <foobar2000/SDK/coreDarkMode.h>

namespace pui::win::dark {

bool enabled();

struct Palette {
    COLORREF bg, text, grayText, field, fieldText, border, tabSel;
    HBRUSH bgBrush, fieldBrush;
};
const Palette& palette(bool dark);

void theme_control(HWND ctl, bool dark);
void theme_children(HWND parent, bool dark);

void subclass_tab(HWND tab, const bool* dark);

LRESULT ctl_color(UINT msg, HDC dc, bool grayText = false);

void set_app_mode(bool dark);
void paint_menubar_line(HWND wnd);

class WindowTheme : private ui_config_callback_impl {
public:
    explicit WindowTheme(HWND wnd);

private:
    void ui_colors_changed() override;
    fb2k::CCoreDarkModeHooks m_hooks;
};

}
