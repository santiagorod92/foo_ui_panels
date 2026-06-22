// GDI+ image loading + drawing for the skin draw engine.
// Images are cached by path (the skin script repaints every frame, so we must
// not decode on each paint).
#pragma once
#include "win_sdk.h"
#include <string>

namespace pui {

// Draw `path` into the rect (x,y,w,h) on dc. w/h == 0 => natural size.
// alpha 0..255. Returns false if the image could not be loaded.
bool draw_image(HDC dc, const std::string& path, int x, int y, int w, int h, int alpha = 255);

// Free the image cache + shut down GDI+ (call at component shutdown).
void images_shutdown();

} // namespace pui
