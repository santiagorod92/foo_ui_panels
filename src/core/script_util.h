#pragma once
#include "../gfx/canvas.h"
#include <string>
#include <vector>

namespace pui {

long eval_expr(const std::string& in);

gfx::Color parse_rgb(const char* s);
bool parse_config_color(const std::string& v, gfx::Color& out);
bool find_color(const std::string& spec, const char* key, gfx::Color& out);
bool parse_argb(const std::string& s, gfx::Color& out, int& alpha);
unsigned long parse_hex_colorref(const std::string& s);
std::string hex_colorref(unsigned long c);

std::string trailing_literal(const std::string& body);
std::string clean_action(std::string a);
std::string unquote(std::string s);
void parse_img_opts(const std::string& o, int& alpha, int& flip);
unsigned text_opts(const std::string& o);
void parse_glow_tail(const std::string& t, int& expand, int& alpha);
enum class PanelKind { TrackDisplay, Seekbar, Volume, Playlist, Spectrum, PeakMeter, AlbumArt, AlbumList,
                       Lyrics, QuickSearch, LibraryTree, Embedded };
enum class PanelZ { Child, Top, Bottom };
PanelKind panel_kind(const std::string& type, PanelZ* z = nullptr);
const char* panel_kind_name(PanelKind k);
const char* map_type(const std::string& t);

std::string menu_label(const char* s);
int best_menu_match(const std::vector<std::string>& paths, const std::string& action);

}
