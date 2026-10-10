#pragma once
#include "win_sdk.h"

namespace pui::win {

double zoom();
void refresh_zoom(HWND main);
UINT window_dpi(HWND w);

}
