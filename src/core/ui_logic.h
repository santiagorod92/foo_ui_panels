// Pure helpers behind the player-level UX features — zoom steps, mini mode, skin commands, the
// album browser's search and sort. No foobar2000 SDK here: compiled into the unit tests
// (`make test`), like script_util.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pui {

// --- zoom ------------------------------------------------------------------------------------
// The zoom setting is a percentage; 0 = automatic (the system scale on Windows, 100% on macOS
// where Retina is already handled by the canvas). Manual values are clamped to these bounds.
constexpr int kZoomMin = 50, kZoomMax = 400;
int clamp_zoom(int pct);
// Next preset above (dir > 0) / below (dir < 0) `pct` (75 90 100 110 125 150 175 200 250 300);
// stays put at the ends.
int zoom_step(int pct, int dir);
// Scale factor for a setting: 0 -> system_dpi / 96 (96 when unknown), else pct / 100.
double zoom_factor_for(int setting_pct, int system_dpi);
// Logical <-> device pixels: device = round(logical * f), logical = floor(device / f) so a
// click on the last device pixel never maps past the logical edge.
int to_device(int logical, double f);
int to_logical(int device, double f);

// --- skin commands / actions -----------------------------------------------------------------
// "PVAR:SET:a:1 ; WINDOWSIZE:430:172" -> the trimmed, non-empty actions, in order.
std::vector<std::string> split_actions(const std::string& s);
// "RIGHT TOP" / "RIGHT:TOP" / "right,top" -> halign, valign (upper-cased; "" when missing).
void parse_anchor(const std::string& s, std::string& halign, std::string& valign);
// Stable 16 bytes for a name (two FNV-1a 64-bit passes, different offsets) — a GUID per skin
// command / skin folder, so a keyboard shortcut bound to one keeps working across sessions.
void name_hash128(const std::string& name, uint8_t out[16]);

// --- mini mode -------------------------------------------------------------------------------
struct MiniPlan {
    bool act = false;   // false: nothing to do (no mini size, or no saved size to go back to)
    bool enter = false; // true: save the current size, then shrink; false: restore
    int w = 0, h = 0;   // client size to resize to
};
// cur: the client size now; mini: the skin's mini size; saved: the size remembered when mini
// mode was entered (0 = none). Already at the mini size = leave; anything else = enter.
MiniPlan mini_mode_plan(int curW, int curH, int miniW, int miniH, int savedW, int savedH);

// --- album browser ---------------------------------------------------------------------------
// Case-insensitive (ASCII) match: every space-separated word of `filter` occurs in `text`.
// An empty filter matches everything.
bool filter_matches(const std::string& filter, const std::string& text);

enum class AlbumSort { Artist, Album, Year, Added };
AlbumSort album_sort_from(const std::string& s); // "artist" / "album" / "year" / "added"
const char* album_sort_name(AlbumSort s);

} // namespace pui
