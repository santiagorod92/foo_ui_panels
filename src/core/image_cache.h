#pragma once
#include "../fb2k.h"
#include "../gfx/canvas.h"
#include <functional>
#include <string>

namespace pui {

gfx::ImagePtr load_image(const std::string& path);

bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h,
                int alpha = 255, int rotateflip = 0);

bool draw_cover_art(gfx::Canvas& cv, const std::string& path, const metadb_handle_ptr& track,
                    int x, int y, int w, int h, int alpha = 255, int rotateflip = 0);

bool draw_image_region(gfx::Canvas& cv, const gfx::Image& img, int x, int y, int w, int h);

void set_art_ready_callback(std::function<void()> cb);

gfx::ImagePtr cover_image(const std::string& path, const metadb_handle_ptr& track);
gfx::ImagePtr data_image(const std::string& key, const void* data, size_t size);

void draw_image_strips(gfx::Canvas& cv, const gfx::Image& img, float x0, float x1, float yMid,
                       float h0, float h1, int alpha, bool reflection);

bool draw_image_data(gfx::Canvas& cv, const std::string& key, const void* data, size_t size,
                     int x, int y, int w, int h);

bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                     float sx, float sy, float sw, float sh, int alpha = 255);

bool image_natural_size(const std::string& path, int& w, int& h);

void fill_gradient_v(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color base);

void fill_alpha(gfx::Canvas& cv, int x, int y, int w, int h, gfx::Color c, int alpha);

bool image_avg_color(const std::string& path, gfx::Color& out);

std::string resolve_wildcard(const std::string& path);

bool file_exists(const std::string& path);

void images_shutdown();

}
