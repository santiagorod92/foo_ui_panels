// GDI+ image loading + drawing for the skin draw engine.
// Images are cached by path (the skin script repaints every frame, so we must
// not decode on each paint).
#pragma once
#include "win_sdk.h"
#include <string>

namespace pui {

// Draw `path` into the rect (x,y,w,h) on dc. w/h == 0 => natural size.
// alpha 0..255. rotateflip: GDI+ RotateFlipType (6 = vertical mirror, used for reflections).
// Returns false if the image could not be loaded.
bool draw_image(HDC dc, const std::string& path, int x, int y, int w, int h,
                int alpha = 255, int rotateflip = 0);

// Like draw_image, but if `path` can't be loaded from disk (e.g. the track's %path% isn't a
// filesystem path — a streaming source like foo_navidrome) and `track` is valid, falls back to
// album_art_manager_v2 (queries whatever album_art_extractor is registered for that track, local
// or remote — the same API the default DUI/CUI album art views use). Result cached per track path.
bool draw_cover_art(HDC dc, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha = 255, int rotateflip = 0);

// Opaque handle to a decoded, cached image (owned by the cache; valid until images_shutdown()).
typedef void* ImageHandle;
// Same resolution as draw_cover_art (disk path, then the album-art pipeline) without drawing.
ImageHandle cover_image(const std::string& path, const metadb_handle_ptr& track);
// Decode+cache in-memory jpeg/png bytes under `key` (see draw_image_data).
ImageHandle data_image(const std::string& key, const void* data, size_t size);
// Draw `img` between x0..x1 as vertical strips whose height varies linearly h0 -> h1 about
// yMid — a cheap perspective trapezoid for cover-flow side covers. `reflection` instead draws
// the mirrored lower part of the image just below that edge. alpha 0..255.
void draw_image_strips(HDC dc, ImageHandle img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection);

// Draw an in-memory encoded image (jpeg/png bytes, e.g. album art fetched from a remote source
// with no on-disk path). `key` must be stable and unique per image (decoded copy is cached by it).
bool draw_image_data(HDC dc, const std::string& key, const void* data, size_t size,
                     int x, int y, int w, int h);

// Draw the (sx,sy,sw,sh) part of an image file (in source pixels) into (dx,dy,dw,dh) — used by
// $imageabs2's crop arguments (progress / volume fill: only the first N px of the bar image).
bool draw_image_part(HDC dc, const std::string& path, int dx, int dy, int dw, int dh,
                     float sx, float sy, float sw, float sh, int alpha = 255);

// Natural pixel size of an image file (false if it can't load).
bool image_natural_size(const std::string& path, int& w, int& h);

// Fill a rect with a smooth vertical gradient derived from `base` (lighter top → darker
// bottom) — the glossy themed look used by the seek/volume bars. Links msimg32.
void fill_gradient_v(HDC dc, int x, int y, int w, int h, COLORREF base);

// Fill a rect with a solid colour at constant `alpha` (0..255) via AlphaBlend (msimg32) —
// e.g. a translucent dark overlay over a wallpaper, or a selection band.
void fill_alpha(HDC dc, int x, int y, int w, int h, COLORREF c, int alpha);

// Dominant (1x1-downscaled) colour of an image — used to tint the playlist highlight to the
// wallpaper. Returns false if the image can't load.
bool image_avg_color(const std::string& path, COLORREF& out);

// Free the image cache + shut down GDI+ (call at component shutdown).
void images_shutdown();

} // namespace pui
