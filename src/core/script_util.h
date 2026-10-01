// Pure helpers of the skin engine: parsing what Panels UI scripts and foo_ui_panels.ini hand us
// (colours, option strings, $eval expressions, button actions, legacy panel types) and matching
// actions to main-menu paths. No foobar2000 SDK here — this file, like lyrics_parse and
// playlist_ops, is compiled straight into the host-side unit tests (`make test`).
#pragma once
#include "../gfx/canvas.h"
#include <string>
#include <vector>

namespace pui {

// $eval: integer + - * / % with () or {} grouping; division/modulo by zero give 0.
long eval_expr(const std::string& in);

// "r-g-b" / "r-g-b-a" from s (anything before the first digit skipped, alpha ignored).
gfx::Color parse_rgb(const char* s);
// foo_ui_panels.ini colours: "r g b", "r-g-b", "r,g,b" or "#rrggbb". False if malformed.
bool parse_config_color(const std::string& v, gfx::Color& out);
// "key-r-g-b" inside a spec ("brushcolor-10-20-30 pencolor-null"); "null" = transparent = false.
bool find_color(const std::string& spec, const char* key, gfx::Color& out);
// GDI+ style colour of $gp_set_brush/$gp_set_pen: "A-R-G-B", or "R-G-B" (opaque). Any non-digit
// separates. False with fewer than three numbers.
bool parse_argb(const std::string& s, gfx::Color& out, int& alpha);
// Columns UI style-script colours: the hex text's numeric value IS the COLORREF (R = low byte).
unsigned long parse_hex_colorref(const std::string& s);
std::string hex_colorref(unsigned long c);

// The visible label trailing a $button2 body's draw-command prefix ("" for a pure command body).
std::string trailing_literal(const std::string& body);
// A $button action argument: trimmed, surrounding single quotes removed.
std::string clean_action(std::string a);
// Surrounding single quotes removed (PanelsUI quotes WINDOWSIZE/PVAR values like '736').
std::string unquote(std::string s);
// Image option string ("alpha-200nokeepaspectROTATEFLIP-6"): only the keys present are written.
void parse_img_opts(const std::string& o, int& alpha, int& flip);
// $drawstring/$draw_text options -> gfx text flags ("vcenter" also matches "center", as legacy did).
unsigned text_opts(const std::string& o);
// "glowexpand-N" / "glowalpha-N" anywhere in a $font token tail.
void parse_glow_tail(const std::string& t, int& expand, int& alpha);
// Legacy panel type -> name of the Default UI element to host, nullptr for the native panels,
// the type itself (t.c_str()) when there's no mapping.
const char* map_type(const std::string& t);

// A menu name lower-cased with its '&' accelerators dropped ("&&" stays a literal '&').
std::string menu_label(const char* s);
// Index in `paths` (lower-cased "group/sub/command", as menu_label makes them) of the command a
// skin action names: the exact path, else the shortest path ending in "/<action>" — so a bare
// leaf prefers the top-level command over a same-named one deeper in a submenu. -1 if none.
int best_menu_match(const std::vector<std::string>& paths, const std::string& action);

} // namespace pui
