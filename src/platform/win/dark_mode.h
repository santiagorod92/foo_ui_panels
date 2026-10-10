#pragma once
#include "win_sdk.h"

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

}
