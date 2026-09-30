// Image loading + drawing helpers for the skin draw engine and the native panels, on top of
// gfx::Canvas. Images are decoded once and cached by path (the skin script repaints every frame,
// so we must not decode on each paint). Platform-free: decoding is gfx::decode_image_*.
#pragma once
#include "../fb2k.h"
#include "../gfx/canvas.h"
#include <string>

namespace pui {

// Decoded + cached image at `path` (UTF-8; '*'/'?' wildcards in the file name resolve to the
// first match, e.g. "C:/Album/*folder*.*"). nullptr if it can't load.
gfx::ImagePtr load_image(const std::string& path);

// Draw `path` into the rect (x,y,w,h). w/h == 0 => natural size.
// alpha 0..255. rotateflip: legacy GDI+ RotateFlipType (6 = vertical mirror, used for reflections).
// Returns false if the image could not be loaded.
bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h,
                int alpha = 255, int rotateflip = 0);

// Like draw_image, but if `path` can't be loaded from disk (e.g. the track's %path% isn't a
// filesystem path — a streaming source like foo_navidrome) and `track` is valid, falls back to
// album_art_manager_v2 (queries whatever album_art_extractor is registered for that track, local
// or remote — the same API the default DUI/CUI album art views use). Result cached per track path.
bool draw_cover_art(gfx::Canvas& cv, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha = 255, int rotateflip = 0);

// Same resolution as draw_cover_art (disk path, then the album-art pipeline) without drawing.
gfx::ImagePtr cover_image(const std::string& path, const metadb_handle_ptr& track);
// Decode+cache in-memory jpeg/png bytes under `key` (stable and unique per image).
gfx::ImagePtr data_image(const std::string& key, const void* data, size_t size);

// Draw `img` between x0..x1 as vertical strips whose height varies linearly h0 -> h1 about
// yMid — a cheap perspective trapezoid for cover-flow side covers. `reflection` instead draws
// the mirrored lower part of the image just below that edge. alpha 0..255.
void draw_image_strips(gfx::Canvas& cv, const gfx::Image& img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection);

// Draw an in-memory encoded image (album art fetched with no on-disk path). `key` as data_image.
bool draw_image_data(gfx::Canvas& cv, const std::string& key, const void* data, size_t size,
                     int x, int y, int w, int h);

// Draw the (sx,sy,sw,sh) part of an image file (in source pixels) into (dx,dy,dw,dh) — used by
// $imageabs2's crop arguments (progress / volume fill: only the first N px of the bar image).
bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                     float sx, float sy, float sw, float sh, int alpha = 255);

// Natural pixel size of an image file (false if it can't load).
bool image_natural_size(const std::string& path, int& w, int& h);

// Fill a rect with a smooth vertical gradient derived from `base` (lighter top → darker
// bottom) — the glossy themed look used by the seek/volume bars.
void fill_gradient_v(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color base);

// Fill a rect with a solid colour at constant `alpha` (0..255).
void fill_alpha(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color c, int alpha);

// Dominant (averaged) colour of an image — used to tint the playlist highlight to the
// wallpaper. Returns false if the image can't load.
bool image_avg_color(const std::string& path, gfx::Color& out);

// True if `path` names an existing regular file (UTF-8).
bool file_exists(const std::string& path);

// Free the image cache (call at component shutdown).
void images_shutdown();

} // namespace pui
