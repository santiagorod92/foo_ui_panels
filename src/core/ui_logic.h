#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pui {

constexpr int kZoomMin = 50, kZoomMax = 400;
int clamp_zoom(int pct);
int zoom_step(int pct, int dir);
double zoom_factor_for(int setting_pct, int system_dpi);
int to_device(int logical, double f);
int to_logical(int device, double f);

std::vector<std::string> split_actions(const std::string& s);
void parse_anchor(const std::string& s, std::string& halign, std::string& valign);
void name_hash128(const std::string& name, uint8_t out[16]);

struct MiniPlan {
    bool act = false;
    bool enter = false;
    int w = 0, h = 0;
};
MiniPlan mini_mode_plan(int curW, int curH, int miniW, int miniH, int savedW, int savedH);

bool filter_matches(const std::string& filter, const std::string& text);

enum class AlbumSort { Artist, Album, Year, Added };
AlbumSort album_sort_from(const std::string& s);
const char* album_sort_name(AlbumSort s);

struct AlbumKeys { std::string artist, album, year, added; };
std::vector<size_t> album_view(const std::vector<AlbumKeys>& all, AlbumSort sort, const std::string& filter);

std::string format_time(double seconds);
double seek_position_at(int x, int width, double length);
double seek_by(double pos, double length, double delta);
std::string seek_tooltip(double at, double length);
std::string volume_label(float db);
float volume_step(float db, int steps);
int wheel_steps(float& acc, float notches);
std::string folder_picker_start(const std::string& current, const std::string& avoid);

enum ResizeEdge : int { kEdgeLeft = 1, kEdgeTop = 2, kEdgeRight = 4, kEdgeBottom = 8 };
struct EdgeRect { int l = 0, t = 0, r = 0, b = 0; };
EdgeRect resize_rect(EdgeRect start, int edges, int dx, int dy, int min_w, int min_h);

}
