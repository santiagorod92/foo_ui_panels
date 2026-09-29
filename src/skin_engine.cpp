#include <cmath>
#include <set>
#include "skin_engine.h"
#include <objidl.h>
#include <gdiplus.h>
#include "image.h"
#include "track_display.h"
#include "seekbar.h"
#include "volume.h"
#include "navidrome_rating_api.h"
#include <foobar2000/SDK/cfg_var.h>
#include <cstdlib>
#include <cstdio>

namespace pui {

// --- helpers ---------------------------------------------------------------
static bool eq(const char* name, size_t len, const char* lit) {
    return strlen(lit) == len && memcmp(name, lit, len) == 0;
}
static std::string param_str(titleformat_hook_function_params* p, size_t i) {
    const char* s = nullptr; size_t n = 0;
    p->get_param(i, s, n);
    return std::string(s ? s : "", s ? n : 0);
}
static int param_int(titleformat_hook_function_params* p, size_t i) {
    return atoi(param_str(p, i).c_str());
}

// fooAvA's text buttons are $button2(...,$font(face,size,style,colour)Label,...). The label that
// trails the draw-command prefix is the visible text; return it ("" for a pure-command body such
// as a replayed $imageabs2, which the button handler already drew).
static std::string trailing_literal(const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r')) ++i;
        if (i >= body.size()) break;
        if (body[i] != '$') return body.substr(i);
        size_t op = body.find('(', i);
        if (op == std::string::npos) return std::string();
        int depth = 1;
        size_t j = op + 1;
        for (; j < body.size() && depth > 0; ++j) {
            if (body[j] == '(') ++depth;
            else if (body[j] == ')') --depth;
        }
        if (depth > 0) return std::string();
        i = j;
    }
    return std::string();
}

// foobar text is UTF-8 — draw via DrawTextW (DrawTextA mojibakes non-ASCII, e.g. "’"->"â€™").
// Drop-in for dtW(dc,s,len,r,f): len is ignored (s is NUL-terminated).
static int dtW(HDC dc, const char* s, int, LPRECT r, UINT f) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return DrawTextW(dc, w.c_str(), -1, r, f);
}

// Parse a bare hex COLORREF string (no separators, e.g. "ff0000") as Columns UI style-script
// colours do: the numeric value of the hex text IS the COLORREF (R=low byte, G=mid, B=high byte).
static unsigned long parse_hex_colorref(const std::string& s) {
    return strtoul(s.c_str(), nullptr, 16);
}
static std::string hex_colorref(unsigned long c) {
    char buf[16]; snprintf(buf, sizeof buf, "%lx", c & 0xFFFFFFul); return buf;
}

// Parse "r-g-b" or "r-g-b-a" starting at s; returns COLORREF (alpha ignored for GDI).
static COLORREF parse_rgb(const char* s) {
    int v[4] = { 0,0,0,255 }, n = 0;
    while (*s && n < 4) {
        while (*s && (*s < '0' || *s > '9')) ++s; // skip non-digit (prefix/dashes)
        if (!*s) break;
        int x = 0; while (*s >= '0' && *s <= '9') { x = x * 10 + (*s - '0'); ++s; }
        v[n++] = x;
        if (*s == '-') ++s; else break;
    }
    return RGB(v[0], v[1], v[2]);
}
// Trim whitespace and surrounding single quotes from a $button action argument.
static std::string clean_action(std::string a) {
    size_t b = a.find_first_not_of(" \t"), e = a.find_last_not_of(" \t");
    if (b == std::string::npos) return {};
    a = a.substr(b, e - b + 1);
    if (a.size() >= 2 && a.front() == '\'' && a.back() == '\'') a = a.substr(1, a.size() - 2);
    return a;
}

// Parse image option string, e.g. "alpha-200nokeepaspectROTATEFLIP-6".
static void parse_img_opts(const std::string& o, int& alpha, int& flip) {
    auto a = o.find("alpha-");        if (a != std::string::npos) alpha = atoi(o.c_str() + a + 6);
    auto r = o.find("ROTATEFLIP-");   if (r != std::string::npos) flip  = atoi(o.c_str() + r + 11);
}

// Find "key-r-g-b" inside spec (e.g. brushcolor-..., pencolor-...).
static bool find_color(const std::string& spec, const char* key, COLORREF& out) {
    auto p = spec.find(key);
    if (p == std::string::npos) return false;
    const char* v = spec.c_str() + p + strlen(key);
    while (*v == '-' || *v == ' ') ++v;
    if (strncmp(v, "null", 4) == 0) return false; // transparent
    out = parse_rgb(v);
    return true;
}

// Tag-write filter: sets (or clears) one meta field on a track. Used by TAG:SET:rating:N.
class meta_set_filter : public file_info_filter {
public:
    meta_set_filter(const char* field, const char* value) : m_field(field), m_value(value) {}
    bool apply_filter(metadb_handle_ptr, t_filestats, file_info& info) override {
        if (m_value.is_empty() || m_value == "0") info.meta_remove_field(m_field);
        else info.meta_set(m_field, m_value);
        return true;
    }
private:
    pfc::string8 m_field, m_value;
};

// --- $eval integer expression evaluator ------------------------------------
namespace {
struct Expr {
    const char* s;
    void skip() { while (*s == ' ') ++s; }
    long number() {
        skip();
        // {} and () are both grouping in PanelsUI $eval.
        if (*s == '(' || *s == '{') {
            char close = (*s == '(') ? ')' : '}';
            ++s; long v = expr(); skip(); if (*s == close) ++s; return v;
        }
        long sign = 1; if (*s == '-') { sign = -1; ++s; }
        long v = 0; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
        return sign * v;
    }
    long term() { long v = number();
        for (;;) { skip();
            if (*s == '*') { ++s; v *= number(); }
            else if (*s == '/') { ++s; long d = number(); v = d ? v / d : 0; }
            else break; } return v; }
    long expr() { long v = term();
        for (;;) { skip();
            if (*s == '+') { ++s; v += term(); }
            else if (*s == '-') { ++s; v -= term(); }
            else break; } return v; }
};
long eval_expr(const std::string& in) { Expr e{ in.c_str() }; return e.expr(); }
}

// --- legacy panel type -> DUI element search string ------------------------
static const char* map_type(const std::string& t) {
    auto has = [&](const char* k) { return t.find(k) != std::string::npos; };
    if (has("Channel spectrum") || t == "Spectrum")      return "Spectrum";
    if (has("Single Column Playlist") || has("ELPlaylist")) return "Playlist View";
    if (has("Peakmeter") || has("Peak"))  return "Peak Meter";
    if (has("Album Art"))         return "Album Art";
    if (has("Track Display") || has("Lyric") || has("Playlist switcher") || has("Seek") || has("Volume") || has("Chronflow") ||
        has("Quick Search") || has("Album list") || has("Graphical Browser"))
        return nullptr; // native (Album list/Graphical Browser: no stock DUI element to host)
    return t.c_str();
}

// Pull "glowexpand-N"/"glowalpha-N" out of a token tail -- the skin sometimes glues them straight
// onto the colour ("glow-114-114-114glowexpand-0"), so they don't always arrive as their own token.
static void parse_glow_tail(const std::string& t, int& expand, int& alpha) {
    size_t e = t.find("glowexpand-");
    if (e != std::string::npos) expand = atoi(t.c_str() + e + 11);
    size_t a = t.find("glowalpha-");
    if (a != std::string::npos) alpha = atoi(t.c_str() + a + 10);
}

// The skin names three faces none of which ship with foobar2000 (or the Wine prefix): map them to
// the closest widely-installed stand-in so the layout metrics stay sane instead of GDI's arbitrary
// default. Drop the real .ttf files into the component folder to get the authentic look.
static const char* face_alias(const char* want) {
    struct { const char* want, *have; } table[] = {
        { "Swis721 Cn BT D-Type", "Nimbus Sans Narrow" }, // "Cn" = condensed; NNS is a narrow Helvetica clone
        { "Calibri",               "Carlito"     }, // metric-compatible Calibri clone
        { "HandelGotD",            "Nimbus Sans Narrow" }, // handwriting face, no close equivalent here
    };
    for (const auto& e : table) if (_stricmp(want, e.want) == 0) return e.have;
    return nullptr;
}
static bool same_face(const wchar_t* got, const char* want) {
    for (size_t i = 0;; ++i) {
        wchar_t g = got[i];
        char w = want[i];
        auto lower = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; };
        if (!g && !w) return true;
        if (!g || !w) return false;
        if (lower(g) != (wchar_t)((w >= 'A' && w <= 'Z') ? w + 32 : w)) return false;
    }
}

// --- titleformat hook: layout + immediate GDI drawing ----------------------
class SkinHook : public titleformat_hook {
    friend class SkinEngine; // draw_script() drives a hook from outside (see the class)
public:
    SkinHook(SkinEngine* e, HDC dc, int w, int h, const metadb_handle_ptr& track = metadb_handle_ptr(),
             int hoverX = -1, int hoverY = -1)
        : m_e(e), m_dc(dc), m_w(w), m_h(h), m_track(track), m_hoverX(hoverX), m_hoverY(hoverY) {
        SetBkMode(m_dc, TRANSPARENT);
    }
    ~SkinHook() {
        flush_text();
        if (m_font) { SelectObject(m_dc, m_oldFont); DeleteObject(m_font); }
    }

    // --- nested titleformat evaluation --------------------------------------
    // Panels UI arguments are themselves titleformat snippets ("$get(fontAVAsize_3)",
    // "$getpvar(colour)", "$if(%_isplaying%,a,b)"). The core renders a function's *arguments*
    // before handing them to the hook, so all we ever see is the raw text -- we have to run it
    // ourselves. Objects are cached per text (the skin reuses a handful of expressions) and the
    // result is written straight through, so a stored draw command ($puts(fs1,$font(...))) still
    // executes when $get(fs1) replays it.
    struct PassThrough : public pfc::string_base {
        titleformat_text_out* out;
        explicit PassThrough(titleformat_text_out* o) : out(o) {}
        const char* get_ptr() const override { return ""; }
        void add_string(const char* s, t_size n) override { out->write(titleformat_inputtypes::unknown, s, n); }
        void truncate(t_size) override {}
        t_size get_length() const override { return 0; }
        char* lock_buffer(t_size) override { return nullptr; }
        void unlock_buffer() override {}
    };

    titleformat_object::ptr compile_cached(const std::string& text) {
        auto& cache = m_e->m_evalcache;
        auto it = cache.find(text);
        if (it == cache.end())
            it = cache.emplace(text, titleformat_object::ptr()).first,
            titleformat_compiler::get()->compile_safe(it->second, text.c_str());
        return it->second;
    }

    // Replay a stored snippet into the live output stream (draw commands and all).
    void run_into(titleformat_text_out* out, const std::string& text) {
        if (m_depth >= 8 || text.empty()) return; // a $puts(x,$get(x)) would loop forever
        auto obj = compile_cached(text);
        if (obj.is_empty()) return;
        PassThrough sink(out);
        m_depth++;
        obj->run(this, sink, nullptr);
        m_depth--;
    }

    // Same, but capture the rendered text -- for arguments we then parse (a font face, a size).
    std::string eval_arg(const std::string& in) {
        if (m_depth >= 8 || in.empty() || in.find('$') == std::string::npos) return in;
        auto obj = compile_cached(in);
        if (obj.is_empty()) return in;
        pfc::string8 out;
        m_depth++;
        obj->run(this, out, nullptr);
        m_depth--;
        return std::string(out.get_ptr(), out.length());
    }

    // $eval's argument is arithmetic over already-rendered text, but the skin also nests $get()
    // calls directly in it ("$eval($get(pct)/100)"). Substitute those before parsing.
    std::string resolve_vars(const std::string& in) {
        std::string out;
        for (size_t i = 0; i < in.size();) {
            bool get = (in.compare(i, 5, "$get(") == 0), pv = (!get && in.compare(i, 8, "$getpvar(") == 0);
            size_t open = pv ? 8 : get ? 5 : 0;
            if (!open) { out.push_back(in[i++]); continue; }
            size_t end = in.find(')', i + open);
            if (end == std::string::npos) { out.append(in, i, std::string::npos); break; }
            std::string name = in.substr(i + open, end - i - open);
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (pv) {
                auto it = m_e->m_pvars.find(name);
                out += (it != m_e->m_pvars.end()) ? it->second : std::string();
            } else {
                auto it = m_e->m_tfvars.find(name);
                if (it != m_e->m_tfvars.end()) out += it->second;
                else {
                    auto p = m_e->m_pvars.find(name);
                    out += (p != m_e->m_pvars.end()) ? p->second : std::string();
                }
            }
            i = end + 1;
        }
        return out;
    }

    bool process_field(titleformat_text_out* out, const char* name, t_size len, bool& found) override {
        found = true;
        if (eq(name, len, "_width")  || eq(name, len, "el_width"))  { out->write_int(titleformat_inputtypes::unknown, m_w); return true; }
        if (eq(name, len, "_height") || eq(name, len, "el_height")) { out->write_int(titleformat_inputtypes::unknown, m_h); return true; }
        if (eq(name, len, "_isplaying")) {
            if (playback_control::get()->is_playing()) out->write(titleformat_inputtypes::unknown, "1", 1);
            return true;
        }
        if (eq(name, len, "_ispaused")) {
            if (playback_control::get()->is_paused()) out->write(titleformat_inputtypes::unknown, "1", 1);
            return true;
        }
        // Volume fields used by the skin's own volume-bar drawing (foo_cwb_hooks / Panels UI):
        // cwb_volume = dB (-100 = muted), panel_volume = 0..1000 along the bar.
        if (eq(name, len, "cwb_volume")) {
            out->write_int(titleformat_inputtypes::unknown, (int)std::lround(playback_control::get()->get_volume())); return true;
        }
        if (eq(name, len, "panel_volume")) {
            float v = playback_control::get()->get_volume();
            out->write_int(titleformat_inputtypes::unknown, (int)std::lround((v + 100.0f) * 10.0f)); return true;
        }
        if (eq(name, len, "rating")) {
            // navidrome:// tracks store their rating under NAVIDROME_RATING, not the standard
            // RATING tag %rating% resolves (see foo_navidrome's NavidromePlaylistSync.h — it
            // deliberately avoids the field Playback Statistics owns). fooAvA's rating-star
            // widgets (panels/Display.txt, MINI.txt) reference bare %rating%, which we can't
            // edit — intercept the field here instead, same fallback shape as playlist_view.cpp's
            // $if2(%navidrome_rating%,[%rating%]).
            metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
            if (np.is_valid()) {
                file_info_impl info;
                if (np->get_info(info) && info.meta_get_count_by_name("NAVIDROME_RATING") > 0) {
                    const char* v = info.meta_get("NAVIDROME_RATING", 0);
                    out->write(titleformat_inputtypes::unknown, v, strlen(v));
                    return true;
                }
            }
            found = false; return false; // not a navidrome track (or unrated) — native %rating%
        }
        if (eq(name, len, "cwb_playback_order")) {
            // fooAvA reads this (a foo_cwb_hooks field) to pick the repeat/shuffle icon + label.
            // Resolve it from foobar's own active playback order so the button reflects/stays in sync.
            auto pm = playlist_manager::get();
            const char* nm = pm->playback_order_get_name(pm->playback_order_get_active());
            if (nm) out->write(titleformat_inputtypes::unknown, nm, strlen(nm));
            return true;
        }
        // foo_cwb_hooks playlist names. fooAvA's title bar is [$upper(%cwb_activelist%)] between
        // prev/next-playlist arrows ("<- LISTENING ->"); the side panels show %cwb_playinglist%.
        if (eq(name, len, "cwb_activelist") || eq(name, len, "cwb_playinglist")) {
            auto pm = playlist_manager::get();
            t_size idx = eq(name, len, "cwb_activelist") ? pm->get_active_playlist()
                                                         : pm->get_playing_playlist();
            pfc::string8 nm;
            if (idx != pfc_infinite && pm->playlist_get_name(idx, nm))
                out->write(titleformat_inputtypes::unknown, nm.get_ptr(), nm.length());
            return true;
        }
        if (eq(name, len, "foobar_path")) {
            pfc::string8 p; filesystem::g_get_display_path(core_api::get_profile_path(), p);
            out->write(titleformat_inputtypes::unknown, p.get_ptr(), p.length()); return true;
        }
        found = false; return false;
    }

    bool process_function(titleformat_text_out* out, const char* name, t_size len,
                          titleformat_hook_function_params* p, bool& found) override {
        const t_size argc = p->get_param_count();
        found = true;

        if (eq(name, len, "panel") && argc >= 6) {
            (m_e->m_capturePlacements ? *m_e->m_capturePlacements : m_e->m_placements).push_back(
                { param_str(p,0), param_str(p,1),
                  param_int(p,2), param_int(p,3), param_int(p,4), param_int(p,5) });
            return true;
        }
        if (eq(name, len, "eval") && argc >= 1) {
            out->write_int(titleformat_inputtypes::unknown, eval_expr(resolve_vars(param_str(p, 0)))); return true;
        }
        // Panels UI's own scratch-variable pool, distinct from $setpvar's persistent pvars and
        // used all over the skin: font selection ($puts(fontAVA,...)), and the CD-case layout in
        // Display/MINI ($puts(lgx-cover,488) / $get(cx-cover) inside $eval). A value that is
        // itself a function call is kept verbatim so $get can replay it as a draw command.
        if (eq(name, len, "get") && argc >= 1) {
            std::string k = param_str(p, 0);
            auto it = m_e->m_tfvars.find(k);
            if (it != m_e->m_tfvars.end()) { run_into(out, it->second); return true; }
            auto pv = m_e->m_pvars.find(k); // panels seed some values as pvars
            if (pv != m_e->m_pvars.end()) out->write(titleformat_inputtypes::unknown, pv->second.c_str(), pv->second.size());
            return true;
        }
        if (eq(name, len, "puts") && argc >= 2) {
            std::string v = param_str(p, 1);
            m_e->m_tfvars[param_str(p, 0)] = (v.find('$') != std::string::npos) ? v : eval_arg(v);
            return true;
        }
        if (eq(name, len, "getpvar") && argc >= 1) {
            auto it = m_e->m_pvars.find(param_str(p, 0));
            if (it != m_e->m_pvars.end()) out->write(titleformat_inputtypes::unknown, it->second.c_str(), it->second.size());
            return true;
        }
        if (eq(name, len, "setpvar") && argc >= 2) {
            m_e->m_pvars[param_str(p, 0)] = param_str(p, 1); return true;
        }

        // --- colour helpers (Columns UI style-script semantics; fooAvA style-script blob) ---
        // $calculate_blend_target(colour) — black if mean(R,G,B) >= 128, else white.
        if (eq(name, len, "calculate_blend_target") && argc >= 1) {
            unsigned long c = parse_hex_colorref(param_str(p, 0));
            int total = (c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff);
            unsigned long target = (total >= 128 * 3) ? 0 : 0xffffffUL;
            std::string h = hex_colorref(target);
            out->write(titleformat_inputtypes::unknown, h.c_str(), h.size());
            return true;
        }
        // $offset_colour(colour_from, colour_to, amount) — shift colour_from toward colour_to.
        if (eq(name, len, "offset_colour") && argc >= 3) {
            unsigned long from = parse_hex_colorref(param_str(p, 0));
            unsigned long to   = parse_hex_colorref(param_str(p, 1));
            int amount = param_int(p, 2); if (amount < 0) amount = 0; if (amount > 255) amount = 255;

            int fr = from & 0xff, fg = (from >> 8) & 0xff, fb = (from >> 16) & 0xff;
            int tr = to & 0xff,   tg = (to >> 8) & 0xff,   tb = (to >> 16) & 0xff;
            int rdiff = tr - fr, gdiff = tg - fg, bdiff = tb - fb;
            int totaldiff = abs(rdiff) + abs(gdiff) + abs(bdiff);

            auto shift = [&](int base, int diff) {
                int v = base + (totaldiff ? (diff * amount * 3 / totaldiff) : 0);
                return v < 0 ? 0 : (v > 255 ? 255 : v);
            };
            unsigned long result = shift(fr, rdiff) | (shift(fg, gdiff) << 8) | (shift(fb, bdiff) << 16);
            std::string h = hex_colorref(result);
            out->write(titleformat_inputtypes::unknown, h.c_str(), h.size());
            return true;
        }

        // $windowstyle(hidetitlebar|showtitlebar) — toggles the host window's caption, e.g.
        // fooAvA's Background script: $ifequal($getpvar(Hidetitlebar),1,$windowstyle(hidetitlebar),$windowstyle(showtitlebar))
        if (eq(name, len, "windowstyle") && argc >= 1) {
            if (HWND w = m_e->m_parent) {
                std::string mode = param_str(p, 0);
                LONG_PTR style = GetWindowLongPtrW(w, GWL_STYLE);
                LONG_PTR next = style;
                if (mode.find("hidetitlebar") != std::string::npos) next = style & ~WS_CAPTION;
                else if (mode.find("showtitlebar") != std::string::npos) next = style | WS_CAPTION;
                if (next != style) {
                    SetWindowLongPtrW(w, GWL_STYLE, next);
                    SetWindowPos(w, nullptr, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                }
            }
            return true;
        }

        // --- drawing ---
        if ((eq(name, len, "font") || eq(name, len, "set_font")) && argc >= 2) {
            // Literal text since the last flush is drawn with the font/colour in force when it
            // was written — not with whatever $font we're about to make current. A skin defers
            // its title to the tail of an $alignabs box ($font(16)$if(%_isplaying%,$upper(%title%),…)
            // $char(10)$font(12,..)$…): without the flush here, everything would render with the
            // LAST font/colour (fooAvA's album line) — big text tiny, wrong glow, wrong colour.
            flush_text();
            // Every argument is itself a titleformat snippet here: face "$get(fontAVA)", size
            // "$get(fontAVAsize_3)", style "underline glow-$getpvar(colour) glowalpha-60".
            std::string face = eval_arg(param_str(p, 0));
            std::string size = eval_arg(param_str(p, 1));
            select_font(face.c_str(), atoi(size.c_str()),
                        argc >= 3 ? eval_arg(param_str(p, 2)) : std::string());
            // fooAvA packs the text colour as a 4th $font arg ($font(face,size,style,r-g-b))
            // instead of a separate $set_font_color call — apply it or text silently keeps
            // whatever colour was last set (invisible if that happens to match the background).
            // Many draw sites pass the 4th arg EMPTY ($font($get(fontAVA),$get(fontAVAsize_2),
            // glow-…,)) to mean "keep the current colour" — e.g. the bottom time/codec readout
            // inherits the 240-240-240 set just before the codec block. Treat empty as "don't
            // touch" rather than parse_rgb("") = black, which broke those labels.
            if (argc >= 4) {
                std::string col = eval_arg(param_str(p, 3));
                if (!col.empty()) m_textcol = parse_rgb(col.c_str());
            }
            m_fontLog.push_back({ face, atoi(size.c_str()),
                                  argc >= 3 ? eval_arg(param_str(p, 2)) : std::string(), m_textcol });
            return true;
        }
        if (eq(name, len, "drawrect") && argc >= 5) {
            std::string spec = param_str(p, 4);
            COLORREF brush = RGB(0,0,0), pen; bool hb = find_color(spec, "brushcolor", brush);
            bool hp = find_color(spec, "pencolor", pen);
            int alpha = 255; auto ap = spec.find("alpha-");
            if (ap != std::string::npos) alpha = atoi(spec.c_str() + ap + 6);
            RECT rc = mkrect(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3));
            if (hb) {
                if (alpha < 255) fill_alpha(rc, brush, alpha);
                else { HBRUSH b = CreateSolidBrush(brush); FillRect(m_dc, &rc, b); DeleteObject(b); }
            }
            if (hp) { HBRUSH b = CreateSolidBrush(pen); FrameRect(m_dc, &rc, b); DeleteObject(b); }
            return true;
        }
        if (eq(name, len, "drawroundrect") && argc >= 7) {
            int x = param_int(p,0) + m_ox, y = param_int(p,1) + m_oy, w = param_int(p,2), h = param_int(p,3);
            int aw = param_int(p,4), ah = param_int(p,5);
            COLORREF c = parse_rgb(param_str(p,6).c_str());
            HBRUSH b = CreateSolidBrush(c); HPEN pen = CreatePen(PS_SOLID, 1, c);
            HGDIOBJ ob = SelectObject(m_dc, b), op = SelectObject(m_dc, pen);
            RoundRect(m_dc, x, y, x + w, y + h, aw, ah);
            SelectObject(m_dc, ob); SelectObject(m_dc, op); DeleteObject(b); DeleteObject(pen);
            return true;
        }
        if (eq(name, len, "gradientrect") && argc >= 6) {
            draw_gradient(param_int(p,0) + m_ox, param_int(p,1) + m_oy, param_int(p,2), param_int(p,3),
                          parse_rgb(param_str(p,4).c_str()), parse_rgb(param_str(p,5).c_str()));
            return true;
        }
        if (eq(name, len, "drawstring") && argc >= 5) {
            flush_text(); draw_string(p, argc); return true;
        }
        // $draw_text(text,x,y,w,h,opts) — like $drawstring but colour comes from the ambient
        // $set_font_color/$textcolor state (fooAvA always calls set_font_color right before).
        if (eq(name, len, "draw_text") && argc >= 5) {
            flush_text();
            std::string text = param_str(p, 0);
            RECT rc = mkrect(param_int(p,1), param_int(p,2), param_int(p,3), param_int(p,4));
            std::string opts = (argc >= 6) ? param_str(p,5) : std::string();
            UINT fmt = DT_NOPREFIX | DT_END_ELLIPSIS;
            if (opts.find("center")  != std::string::npos) fmt |= DT_CENTER;
            if (opts.find("right")   != std::string::npos) fmt |= DT_RIGHT;
            if (opts.find("vcenter") != std::string::npos) fmt |= DT_VCENTER | DT_SINGLELINE;
            SetTextColor(m_dc, m_textcol);
            dtW(m_dc, text.c_str(), (int)text.size(), &rc, fmt);
            return true;
        }
        if (eq(name, len, "alignabs") && argc >= 4) {
            // $alignabs(left,top,right,bottom,halign,valign) — box for following literal text
            flush_text();
            m_alignRect = { param_int(p,0) + m_ox, param_int(p,1) + m_oy,
                            param_int(p,2) + m_ox, param_int(p,3) + m_oy };
            // A box whose right/bottom sit at or before left/top is really (x, y, width, height)
            // with 0 meaning "to the edge" — e.g. the panel titles' $alignabs(,31,0,14,center,middle).
            if (m_alignRect.right <= m_alignRect.left)
                m_alignRect.right = param_int(p,2) > 0 ? m_alignRect.left + param_int(p,2) : m_w + m_ox;
            if (m_alignRect.bottom <= m_alignRect.top)
                m_alignRect.bottom = param_int(p,3) > 0 ? m_alignRect.top + param_int(p,3) : m_h + m_oy;
            std::string ha = argc >= 5 ? param_str(p,4) : std::string();
            std::string va = argc >= 6 ? param_str(p,5) : std::string();
            UINT f = DT_NOPREFIX | DT_WORD_ELLIPSIS;
            if (ha.find("center") != std::string::npos) f |= DT_CENTER;
            else if (ha.find("right") != std::string::npos) f |= DT_RIGHT;
            if (va.find("center") != std::string::npos) f |= DT_VCENTER | DT_SINGLELINE;
            // A one-line-high box is a caption sized from $calcwidth(), which measures with a
            // slightly different font than we draw with — let it overflow instead of ellipsizing.
            if (m_alignRect.bottom - m_alignRect.top <= 24) { f &= ~DT_WORD_ELLIPSIS; f |= DT_NOCLIP | DT_SINGLELINE; }
            m_alignFlags = f; m_aligned = true;
            return true;
        }
        if ((eq(name, len, "textcolor") || eq(name, len, "set_font_color")) && argc >= 1) {
            flush_text(); // pending literal text keeps the colour it was written with
            m_textcol = parse_rgb(param_str(p,0).c_str()); return true;
        }
        if (eq(name, len, "imageabs") && argc >= 5) {
            // $imageabs(x,y,w,h,path,align)
            draw_cover_art(m_dc, resolve(param_str(p,4)), m_track, param_int(p,0) + m_ox, param_int(p,1) + m_oy,
                       param_int(p,2), param_int(p,3));
            return true;
        }
        if (eq(name, len, "draw_image") && argc >= 5) {
            // $draw_image(x,y,w,h,path,...)
            draw_cover_art(m_dc, resolve(param_str(p,4)), m_track, param_int(p,0) + m_ox, param_int(p,1) + m_oy,
                       param_int(p,2), param_int(p,3));
            return true;
        }
        // $cwb_fileexists — foo_cwb_hooks' file-exists check; same semantics as $fileexists here.
        if ((eq(name, len, "fileexists") || eq(name, len, "cwb_fileexists")) && argc >= 1) {
            std::string path = resolve(param_str(p,0));
            DWORD a = GetFileAttributesA(path.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
                out->write(titleformat_inputtypes::unknown, "1", 1);
            return true;
        }
        if (eq(name, len, "greater") && argc >= 2) {
            if (atoi(param_str(p,0).c_str()) > atoi(param_str(p,1).c_str()))
                out->write(titleformat_inputtypes::unknown, "1", 1);
            return true;
        }
        if (eq(name, len, "imageabs2") && argc >= 9) {
            // $button2's normal/hover states pass an $imageabs2 with all-zero args (0,0 sized and
            // positioned) as a deferred draw command. The SDK evaluates those args before the
            // button handler runs — the call would blit at the bare origin (0,0) and be
            // discarded, leaving the button invisible. Emit such calls back as a replayable
            // snippet; the button handler re-runs them at its own coords (and with all args
            // zero, the image blits at its natural size). Real draws — wallpaper, cd case, bars,
            // cover art, replayed button states — carry real sizes and/or an origin, so they
            // take the painting path below and are unaffected.
            bool plainDummy = false;
            if (m_ox == 0 && m_oy == 0) {
                plainDummy = true;
                for (int i = 0; plainDummy && i < 8; ++i)
                    if (param_int(p, i)) plainDummy = false;
            }
            if (plainDummy) {
                std::string s = "$imageabs2(";
                for (size_t i = 0; i < argc; ++i) {
                    s += param_str(p, i);
                    if (i + 1 < argc) s += ',';
                }
                s += ')';
                out->write(titleformat_inputtypes::unknown, s.c_str(), s.size());
                return true;
            }
            // $imageabs2(maxW,maxH,imgW,imgH,srcX,srcY,dstX,dstY,path,opts)
            int alpha = 255, flip = 0;
            if (argc >= 10) parse_img_opts(param_str(p,9), alpha, flip);
            // Arguments 2..5 are a crop of the (w,h)-scaled image — fooAvA's progress and volume
            // bars draw only the first N px of their fill graphic this way. Calls that ask for no
            // crop take the plain path below.
            //
            // Naming *both* of the first two arguments means "scale into that box" (the wallpaper
            // and cover-art calls, which all pass NOKEEPASPECT). Naming only one makes it a cap,
            // never an upscale: fooAvA's volume fill is $imageabs2(0,12,0,0,<level>,0,...) on the
            // 56x3 v<n>.png bar, and 12 is the knob's height, not the bar's — taking it as a
            // target stretched the fill 4x, so the volume bar rendered as a fat, colour-smeared
            // block instead of the thin bar the progress bar (s<n>.png, 1000x3) draws.
            {
                int W = param_int(p,0), H = param_int(p,1), SX = param_int(p,2), SY = param_int(p,3),
                    SW = param_int(p,4), SH = param_int(p,5);
                int iw = 0, ih = 0;
                std::string ip = resolve(param_str(p,8));
                if ((SX > 0 || SY > 0 || SW > 0 || SH > 0) && flip == 0 && image_natural_size(ip, iw, ih)) {
                    int scW, scH;
                    if (W > 0 && H > 0) { scW = W; scH = H; }
                    else { scW = W > 0 ? std::min(W, iw) : iw; scH = H > 0 ? std::min(H, ih) : ih; }
                    int cw = SW > 0 ? std::min(SW, scW - SX) : scW - SX;
                    int ch = SH > 0 ? std::min(SH, scH - SY) : scH - SY;
                    if (cw <= 0 || ch <= 0) return true;
                    // A crop that happens to cover the whole (uncapped) image still goes through
                    // here rather than the plain path, which would re-apply the unclamped H —
                    // that is the full-volume case, where the fill is the bar's entire 56 px.
                    draw_image_part(m_dc, ip, param_int(p,6) + m_ox, param_int(p,7) + m_oy, cw, ch,
                                    (float)SX * iw / scW, (float)SY * ih / scH,
                                    (float)cw * iw / scW, (float)ch * ih / scH, alpha);
                    return true;
                }
            }
            draw_cover_art(m_dc, resolve(param_str(p,8)), m_track, param_int(p,6) + m_ox, param_int(p,7) + m_oy,
                       param_int(p,0), param_int(p,1), alpha, flip);
            return true;
        }

        if ((eq(name, len, "button") || eq(name, len, "button2")) && argc >= 9) {
            // $button (x,y,?,?,w,h,imgpath_normal,imgpath_hover,'action',tooltip)
            // $button2(x,y,?,?,w,h,draw_normal,draw_hover,'action',tooltip)
            // For button2 the arg is a draw command (possibly '-quoted) run at the button
            // origin; for button it is an image path. Whichever state is active (hover if the
            // tracked mouse position — see m_hoverX/m_hoverY — is over the hit box, else normal)
            // is rendered; the other is skipped so the two don't stack.
            int x = param_int(p,0), y = param_int(p,1);
            int rawW = param_int(p,4), rawH = param_int(p,5);
            int hw = rawW > 0 ? rawW : 22, hh = rawH > 0 ? rawH : 22; // hit box
            if (rawW <= 0 || rawH <= 0) {
                // w/h omitted: the hit box is the normal image's own size (e.g. the 55px-wide
                // COVER FLOW pill), not a fixed 22x22 that only covers its left edge.
                std::string ip = param_str(p, 6);
                size_t b = ip.find_first_not_of(" \t\r\n"), e = ip.find_last_not_of(" \t\r\n");
                ip = b == std::string::npos ? std::string() : ip.substr(b, e - b + 1);
                int iw = 0, ih = 0;
                if (!ip.empty() && ip[0] != '$' && ip[0] != '\'' &&
                    (ip.find(".png") != std::string::npos || ip.find(".jpg") != std::string::npos) &&
                    image_natural_size(resolve(ip), iw, ih)) {
                    if (rawW <= 0) hw = iw;
                    if (rawH <= 0) hh = ih;
                }
            }
            bool hovered = m_hoverX >= x && m_hoverX < x + hw && m_hoverY >= y && m_hoverY < y + hh;
            // arg 6 = normal image, arg 7 = themed one. Trimmed/unquoted once, since the themed
            // one is also needed *after* the run, to relight a selected button (below).
            std::string dHover, d1;
            for (int pass = 0; pass < 2; ++pass) {
                std::string s = param_str(p, pass == 0 ? 7 : (hovered ? 7 : 6));
                size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
                s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
                if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
                (pass == 0 ? dHover : d1) = s;
            }
            bool drew = false;
            if (!d1.empty()) {
                if (d1[0] == '$') { run_subscript(d1, x, y); drew = true; } // draw command
                else if (d1.find(".png") != std::string::npos || d1.find(".jpg") != std::string::npos)
                    drew = draw_image(m_dc, resolve(d1), x, y, rawW > 0 ? rawW : 0, rawH > 0 ? rawH : 0);
            }
            // A text button whose normal state is "$font(...)Label": the argument pre-eval ran
            // the $font (setting the face/size/colour) and left the bare label behind, or the
            // label trails the $font(...) chunk of a kept snippet. Neither run above drew it, so
            // render the label centred in the hit box — the $textbutton ("EQ"/"INFO", playlist
            // pvar pills) and $button2-with-font cases in fooAvA all rely on this.
            if (!drew) {
                std::string lit;
                if (!d1.empty() && d1[0] == '$') lit = trailing_literal(d1);
                else if (!d1.empty() && d1[0] != '\'' &&
                         d1.find(".png") == std::string::npos && d1.find(".jpg") == std::string::npos)
                    lit = d1;
                if (!lit.empty()) {
                    // titleformat evaluates every argument before calling us, so both states'
                    // $font(...) have already run and the HOVER one (arg 7) is current — every
                    // text button would draw hover-styled (e.g. the title bar's playlist name in
                    // a coloured glow). When both states are text, the last two $font calls since
                    // the previous button are normal/hover: re-apply the one for this state.
                    const bool hoverText = !dHover.empty() && dHover[0] != '\'' &&
                        dHover.find(".png") == std::string::npos && dHover.find(".jpg") == std::string::npos;
                    if (hoverText && m_fontLog.size() >= m_fontLogMark + 2) {
                        const FontState& fs = m_fontLog[m_fontLog.size() - (hovered ? 1 : 2)];
                        select_font(fs.face.c_str(), fs.size, fs.style);
                        m_textcol = fs.col;
                    }
                    RECT r = mkrect(x, y, hw, hh);
                    UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP;
                    SetTextColor(m_dc, m_textcol);
                    draw_glow(lit, r, fmt);
                    dtW(m_dc, lit.c_str(), (int)lit.size(), &r, fmt);
                    drew = true;
                }
            }
            // No faint-frame fallback: a button whose image is missing stays invisible
            // (still clickable) instead of showing an empty border.
            std::string act = clean_action(param_str(p,8));
            if (!act.empty()) {
                Button btn;
                btn.x = x; btn.y = y; btn.w = hw; btn.h = hh; btn.action = act;
                // Selected-state candidates only: the themed image can be re-blitted after the
                // run, but a nested draw command (button2 running at an offset) cannot, and
                // drawing at the wrong offset would smear it somewhere else entirely.
                if (m_ox == 0 && m_oy == 0) {
                    const std::string kTag = "PVAR:SET:";
                    if (act.compare(0, kTag.size(), kTag) == 0) {
                        std::string rest = act.substr(kTag.size());
                        size_t c = rest.find(':');
                        if (c != std::string::npos) { btn.pvarKey = rest.substr(0, c); btn.pvarValue = rest.substr(c + 1); }
                    }
                    if (!dHover.empty() && dHover[0] != '$' &&
                        (dHover.find(".png") != std::string::npos || dHover.find(".jpg") != std::string::npos)) {
                        btn.litImage = dHover; btn.litW = rawW; btn.litH = rawH;
                    }
                }
                (m_e->m_capture ? *m_e->m_capture : m_e->m_buttons).push_back(btn);
            }
            m_fontLogMark = m_fontLog.size();
            return true;
        }

        // $calcwidth(text) -> pixel width of text in the current font (used for centering)
        if (eq(name, len, "calcwidth") && argc >= 1) {
            std::string t = param_str(p, 0);
            SIZE sz{}; GetTextExtentPoint32A(m_dc, t.c_str(), (int)t.size(), &sz);
            out->write_int(titleformat_inputtypes::unknown, sz.cx);
            return true;
        }
        // $textbutton(left,top,width,height,str_normal,str_hover,action,"TOOLTIP",tip)
        // Draws the (already-evaluated) normal text into the box + records a clickable region.
        if (eq(name, len, "textbutton") && argc >= 5) {
            int x = param_int(p,0), y = param_int(p,1), w = param_int(p,2), h = param_int(p,3);
            std::string text = param_str(p,4);
            RECT r = mkrect(x, y, w > 0 ? w : 240, h > 0 ? h : 18);
            SetTextColor(m_dc, m_textcol);
            dtW(m_dc, text.c_str(), (int)text.size(), &r,
                      DT_NOPREFIX | DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
            if (argc >= 7) { std::string a = clean_action(param_str(p,6));
                if (!a.empty()) (m_e->m_capture ? *m_e->m_capture : m_e->m_buttons).push_back({ x, y, (w>0?w:240), (h>0?h:18), a }); }
            return true;
        }
        // $imagebutton(left,top,image_normal,image_hover,action,"TOOLTIP",tip) — rating stars etc.
        if (eq(name, len, "imagebutton") && argc >= 5) {
            int x = param_int(p,0), y = param_int(p,1);
            const int hw = 11, hh = 15; // ~star-sized hit box
            bool hovered = m_hoverX >= x && m_hoverX < x + hw && m_hoverY >= y && m_hoverY < y + hh;
            draw_image(m_dc, resolve(param_str(p, hovered ? 3 : 2)), x, y, 0, 0); // natural size
            std::string a = clean_action(param_str(p,4));
            if (!a.empty()) (m_e->m_capture ? *m_e->m_capture : m_e->m_buttons).push_back({ x, y, hw, hh, a });
            return true;
        }

        // accepted-but-not-yet-rendered functions
        static const char* stubs[] = { "scplsetlayout","gp_set_brush","gp_set_pen","gp_fill_rectangle","settitle","settray" };
        for (auto s : stubs) if (eq(name, len, s)) return true;

        found = false; return false;
    }

private:
    // Panels UI $font style glow: "glow-r-g-b [glowexpand-N] [glowalpha-N]" — a soft halo of
    // the glyphs. Built from the SAME GDI layout the text itself is drawn with (DrawTextW into a
    // mask, dilated by `expand` px, faded outward, alpha-blended under the text). Stamping the
    // text through GDI+ instead lays glyphs out differently from GDI (padding, advance widths),
    // so the halo sat offset from the letters and read as a smeared ghost copy of the title.
    void draw_glow(const std::string& s, const RECT& r, UINT fmt) {
        if (!m_glowAlpha || m_glowExpand <= 0 || s.empty() || !m_font) return;
        const int e = m_glowExpand > 6 ? 6 : m_glowExpand;
        // Work area: the text rect grown by the glow radius, clamped to the target bitmap.
        int bw = m_w + m_ox, bh = m_h + m_oy;
        if (HGDIOBJ cur = GetCurrentObject(m_dc, OBJ_BITMAP)) {
            BITMAP bm{}; if (GetObject(cur, sizeof(bm), &bm)) { bw = bm.bmWidth; bh = bm.bmHeight; }
        }
        int x0 = std::max(0, (int)r.left - e), y0 = std::max(0, (int)r.top - e);
        int x1 = std::min(bw, (int)r.right + e), y1 = std::min(bh, (int)r.bottom + e);
        if (fmt & DT_NOCLIP) { x0 = 0; y0 = 0; x1 = bw; y1 = bh; } // text may spill past its box
        const int W = x1 - x0, H = y1 - y0;
        if (W <= 0 || H <= 0) return;

        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC md = CreateCompatibleDC(m_dc);
        void* mbits = nullptr;
        HBITMAP mb = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &mbits, nullptr, 0);
        if (!mb) { DeleteDC(md); return; }
        HGDIOBJ omb = SelectObject(md, mb);
        memset(mbits, 0, (size_t)W * H * 4);
        HGDIOBJ of = SelectObject(md, m_font);
        SetBkMode(md, TRANSPARENT); SetTextColor(md, RGB(255, 255, 255));
        RECT tr = { r.left - x0, r.top - y0, r.right - x0, r.bottom - y0 };
        dtW(md, s.c_str(), (int)s.size(), &tr, fmt);
        SelectObject(md, of);

        // Coverage mask (max channel: ClearType renders coloured fringes) + its bounding box.
        const uint32_t* px = (const uint32_t*)mbits;
        std::vector<uint8_t> mask((size_t)W * H);
        int mx0 = W, my0 = H, mx1 = -1, my1 = -1;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            uint32_t c = px[(size_t)y * W + x];
            uint8_t v = (uint8_t)std::max({ (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF });
            mask[(size_t)y * W + x] = v;
            if (v) { mx0 = std::min(mx0, x); my0 = std::min(my0, y); mx1 = std::max(mx1, x); my1 = std::max(my1, y); }
        }
        if (mx1 < 0) { SelectObject(md, omb); DeleteObject(mb); DeleteDC(md); return; }
        mx0 = std::max(0, mx0 - e); my0 = std::max(0, my0 - e);
        mx1 = std::min(W - 1, mx1 + e); my1 = std::min(H - 1, my1 + e);

        // Dilate: each pixel takes the strongest nearby glyph coverage, weighted so the ring
        // hugging the glyphs is strongest and the outermost ring faintest.
        const int cr = GetRValue(m_glowCol), cg = GetGValue(m_glowCol), cb = GetBValue(m_glowCol);
        uint32_t* out = (uint32_t*)mbits;
        std::vector<uint32_t> res((size_t)W * H, 0);
        for (int y = my0; y <= my1; ++y) for (int x = mx0; x <= mx1; ++x) {
            int best = 0;
            for (int dy = -e; dy <= e; ++dy) {
                int yy = y + dy; if (yy < 0 || yy >= H) continue;
                for (int dx = -e; dx <= e; ++dx) {
                    int xx = x + dx; if (xx < 0 || xx >= W) continue;
                    int d2 = dx * dx + dy * dy;
                    if (d2 > e * e + e) continue;
                    int m = mask[(size_t)yy * W + xx]; if (!m) continue;
                    int d = d2 == 0 ? 0 : (int)std::ceil(std::sqrt((double)d2));
                    if (d < 1) d = 1;
                    int wgt = m * (e - d + 1) / (e + 1);
                    if (wgt > best) best = wgt;
                }
            }
            int a = best * m_glowAlpha / 255;
            if (a > 0) res[(size_t)y * W + x] = ((uint32_t)a << 24) | ((uint32_t)(cr * a / 255) << 16) |
                                                ((uint32_t)(cg * a / 255) << 8) | (uint32_t)(cb * a / 255);
        }
        memcpy(out, res.data(), res.size() * 4);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(m_dc, x0 + mx0, y0 + my0, mx1 - mx0 + 1, my1 - my0 + 1,
                   md, mx0, my0, mx1 - mx0 + 1, my1 - my0 + 1, bf);
        SelectObject(md, omb); DeleteObject(mb); DeleteDC(md);
    }

    void flush_text() {
        if (!m_buf) return;
        if (m_aligned && m_buf->size() > m_flushFrom) {
            std::string s; // buffer slice since the last flush, control chars ($char(N)) dropped
            for (size_t i = m_flushFrom; i < m_buf->size(); ++i) {
                unsigned char c = (unsigned char)(*m_buf)[i];
                if (c >= 32 || c == '\n') s.push_back((char)c);
            }
if (!s.empty()) {
                SetTextColor(m_dc, m_textcol);
                RECT r = m_alignRect;
                draw_glow(s, r, m_alignFlags);
                dtW(m_dc, s.c_str(), (int)s.size(), &r, m_alignFlags);
            }
        }
        m_flushFrom = m_buf->size();
    }

    // Draw origin: normally (0,0); set to a button's (x,y) while running a $button2
    // sub-draw command so its relative coords land at the button position.
    int m_ox = 0, m_oy = 0;
    RECT mkrect(int x, int y, int w, int h) { RECT r = { x + m_ox, y + m_oy, x + m_ox + w, y + m_oy + h }; return r; }

    // Fill a rect with a solid colour at `alpha` (0..255) via AlphaBlend (msimg32).
    void fill_alpha(const RECT& r, COLORREF c, int alpha) {
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) return;
        HDC md = CreateCompatibleDC(m_dc);
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bits) { unsigned char* px = (unsigned char*)bits;
            px[0] = GetBValue(c); px[1] = GetGValue(c); px[2] = GetRValue(c); px[3] = 255; }
        HGDIOBJ ob = SelectObject(md, bmp);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 }; // const alpha, no per-pixel
        AlphaBlend(m_dc, r.left, r.top, w, h, md, 0, 0, 1, 1, bf);
        SelectObject(md, ob); DeleteObject(bmp); DeleteDC(md);
    }

    // Run a $button2 draw-command (titleformat) at offset (ox,oy). Compiled scripts cached.
    void run_subscript(const std::string& text, int ox, int oy);

    // Relight the selected member of every pvar radio group in `list` (themed image + themed
    // outline). Must run while m_dc is still valid — i.e. inside draw_script, after the run.
    void apply_selected_buttons(std::vector<Button>& list);

    // Resolve a skin image path: normalize separators, strip leading ./ or /,
    // and make relative paths absolute against the skin base dir.
    std::string resolve(std::string p) {
        for (auto& c : p) if (c == '\\') c = '/';
        bool absolute = (p.size() >= 2 && p[1] == ':') || p.compare(0, 2, "//") == 0;
        if (absolute) return p;
        while (!p.empty() && (p[0] == '.' || p[0] == '/')) p.erase(0, 1);
        if (m_e->m_base.empty()) return p;
        return m_e->m_base + "/" + p;
    }

    void select_font(const char* face, int size, const std::string& style) {
        // The skin asks for "$get(fontAVA)" but $get/$puts are the SDK's native titleformat
        // scratch vars, scoped per compiled script/eval -- disjoint from our own $setpvar/$getpvar
        // pool that the skin actually used to set fontAVA at script start. `face` arrives empty;
        // recover the real face from m_pvars before falling back to a generic UI font.
        const char* f0 = face;
        while (f0 && (*f0 == ' ' || *f0 == '\t')) ++f0;
        std::string fromPvar;
        if (!f0 || !*f0) {
            auto it = m_e->m_pvars.find("fontAVA");
            if (it != m_e->m_pvars.end() && !it->second.empty()) { fromPvar = it->second; f0 = fromPvar.c_str(); }
        }
        // Neither the script nor its own pvar named a face — try the Preferences-page global
        // font override (reserved pvar "_prefs_font_face") before the hardcoded fallback.
        if (!f0 || !*f0) {
            auto it = m_e->m_pvars.find("_prefs_font_face");
            if (it != m_e->m_pvars.end() && !it->second.empty()) { fromPvar = it->second; f0 = fromPvar.c_str(); }
        }
        if (!f0 || !*f0) f0 = "Tahoma";
        if (size <= 0) {
            auto it = m_e->m_pvars.find("_prefs_font_size");
            if (it != m_e->m_pvars.end() && !it->second.empty()) size = atoi(it->second.c_str());
        }
        int h = -MulDiv(size > 0 ? size : 9, GetDeviceCaps(m_dc, LOGPIXELSY), 72);
        // Style is a space-separated list; match whole tokens only -- `style.find('b')` also hits
        // "glow", which made every glowing label bold, and `find('i')` would hit "glow" too.
        bool bold = false, italic = false, underline = false;
        COLORREF glow = RGB(0,0,0);
        int expand = 2, alpha = 255; // Panels UI defaults; `glowexpand-0` means no glow at all
        bool hasGlow = false;
        for (size_t i = 0; i < style.size();) {
            while (i < style.size() && (style[i] == ' ' || style[i] == '\t')) ++i;
            size_t j = i;
            while (j < style.size() && style[j] != ' ' && style[j] != '\t') ++j;
            std::string tok = style.substr(i, j - i);
            i = j;
            if (tok == "bold") bold = true;
            else if (tok == "italic") italic = true;
            else if (tok == "underline") underline = true;
            else if (tok.compare(0, 5, "glow-") == 0 && tok.size() > 5) {
                // the skin also writes this with no space: "glow-114-114-114glowexpand-0"
                hasGlow = true;
                glow = parse_rgb(tok.c_str() + 5);
                size_t tail = tok.find_first_of("gG", 5);
                if (tail != std::string::npos) { parse_glow_tail(tok.substr(tail), expand, alpha); }
            } else if (tok.compare(0, 11, "glowexpand-") == 0) expand = atoi(tok.c_str() + 11);
            else if (tok.compare(0, 10, "glowalpha-") == 0) alpha = atoi(tok.c_str() + 10);
        }
        m_glowCol = glow;
        m_glowExpand = hasGlow ? expand : 0;
        m_glowAlpha = hasGlow ? alpha : 0;
        auto make = [&](const char* face) {
            return CreateFontA(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic ? TRUE : FALSE,
                underline ? TRUE : FALSE, 0, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        };
        HFONT f = make(f0);
        if (!f) return;
        HGDIOBJ old = SelectObject(m_dc, f);
        // GDI silently swaps in an arbitrary default for a face it can't find, so ask the DC which
        // one it actually got and, if it isn't what was asked for, retry with the closest face we
        // do have. Drop the real TTFs into the fb2k dir to get the skin's own look.
        wchar_t got[LF_FACESIZE]; got[0] = 0;
        GetTextFaceW(m_dc, LF_FACESIZE, got);
        if (!same_face(got, f0)) {
            if (const char* alias = face_alias(f0)) {
                HFONT f2 = make(alias);
                if (f2) { SelectObject(m_dc, old); DeleteObject(f); f = f2; old = SelectObject(m_dc, f); }
            }
        }
        if (m_font) DeleteObject(m_font); else m_oldFont = old;
        m_font = f;
    }

    void draw_string(titleformat_hook_function_params* p, t_size argc) {
        std::string text = param_str(p, 0);
        RECT rc = mkrect(param_int(p,1), param_int(p,2), param_int(p,3), param_int(p,4));
        COLORREF col = (argc >= 6) ? parse_rgb(param_str(p,5).c_str()) : RGB(0,0,0);
        std::string flags = (argc >= 7) ? param_str(p,6) : std::string();
        UINT fmt = DT_NOPREFIX | DT_END_ELLIPSIS;
        if (flags.find("center")  != std::string::npos) fmt |= DT_CENTER;
        if (flags.find("right")   != std::string::npos) fmt |= DT_RIGHT;
        if (flags.find("vcenter") != std::string::npos) fmt |= DT_VCENTER | DT_SINGLELINE;
        SetTextColor(m_dc, col);
        draw_glow(text, rc, fmt);
        dtW(m_dc, text.c_str(), (int)text.size(), &rc, fmt);
    }

    void draw_gradient(int x, int y, int w, int h, COLORREF c1, COLORREF c2) {
        TRIVERTEX v[2] = {
            { x,     y,     (COLOR16)(GetRValue(c1)<<8), (COLOR16)(GetGValue(c1)<<8), (COLOR16)(GetBValue(c1)<<8), 0 },
            { x + w, y + h, (COLOR16)(GetRValue(c2)<<8), (COLOR16)(GetGValue(c2)<<8), (COLOR16)(GetBValue(c2)<<8), 0 },
        };
        GRADIENT_RECT gr = { 0, 1 };
        GradientFill(m_dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    }

    SkinEngine* m_e; HDC m_dc; int m_w, m_h;
    metadb_handle_ptr m_track; // set for draw_script() (TrackDisplay/Popup) — enables the
                                // album_art_manager_v2 fallback in draw_cover_art for non-local sources.
    int m_hoverX, m_hoverY; // mouse pos in this call's coordinate space, -1,-1 if not hovering
    HFONT m_font = nullptr; HGDIOBJ m_oldFont = nullptr;
    struct FontState { std::string face; int size; std::string style; COLORREF col; };
    // Every $font this pass, and its length after the last $button/$button2 — lets a text
    // button recover the normal-state font (see the button handler).
    std::vector<FontState> m_fontLog; size_t m_fontLogMark = 0;
    int m_depth = 0; // nesting depth for snippet evaluation (see run_into/eval_arg)
    COLORREF m_glowCol = RGB(0,0,0); int m_glowExpand = 0, m_glowAlpha = 0;

    // positional literal-text state. Text is read from the DrawString buffer (m_buf) rather than
    // accumulated per-write: titleformat appends a $if/$ifequal condition's value to the buffer then
    // truncates it away — reading the live buffer at flush time means those (e.g. %_isplaying% -> "1")
    // never leak into the drawn text (which a separate pending buffer would keep).
    bool m_aligned = false; RECT m_alignRect = {}; UINT m_alignFlags = 0;
    COLORREF m_textcol = RGB(255, 255, 255);
    const std::string* m_buf = nullptr; size_t m_flushFrom = 0;
public:
    void set_buf(const std::string* b) { m_buf = b; m_flushFrom = b ? b->size() : 0; }
    // Flush the final text box while the DrawString buffer is still alive (it is destroyed
    // before this hook), then detach so ~SkinHook doesn't read a dangling buffer.
    void finish() { flush_text(); m_buf = nullptr; }
};

// --- SkinEngine ------------------------------------------------------------
bool SkinEngine::load(const char* script) {
    if (!titleformat_compiler::get()->compile(m_script, script)) {
        console::print("Panels UI: skin script failed to compile");
        return false;
    }
    return true;
}

// titleformat output sink that routes written text to the hook for positional drawing,
// while still satisfying string_base (so titleformat_object::run can write to it).
class DrawString : public pfc::string_base {
public:
    explicit DrawString(SkinHook* h) : m_h(h) {}
    const std::string& buf() const { return m_buf; }
    const char* get_ptr() const override { return m_buf.c_str(); }
    void add_string(const char* s, t_size n = SIZE_MAX) override {
        size_t len = (n == SIZE_MAX) ? strlen(s) : n;
        m_buf.append(s, len); // SkinHook reads this buffer at flush (truncate-safe)
    }
    void truncate(t_size len) override { if (len < m_buf.size()) m_buf.resize(len); }
    t_size get_length() const override { return m_buf.size(); }
    char* lock_buffer(t_size req) override { m_buf.resize(req); return m_buf.empty() ? nullptr : &m_buf[0]; }
    void unlock_buffer() override { m_buf.resize(strlen(m_buf.c_str())); }
private:
    SkinHook* m_h; std::string m_buf;
};

// Run a $button2 draw-command (compiled+cached) with the draw origin set to (ox,oy)
// so the command's relative coordinates land at the button position.
void SkinHook::run_subscript(const std::string& text, int ox, int oy) {
    auto& cache = m_e->m_subcache;
    auto it = cache.find(text);
    if (it == cache.end()) {
        service_ptr_t<titleformat_object> obj;
        titleformat_compiler::get()->compile_safe(obj, text.c_str());
        it = cache.emplace(text, obj).first;
    }
    if (it->second.is_empty()) return;
    int sx = m_ox, sy = m_oy; m_ox = ox; m_oy = oy;
    const std::string* sbuf = m_buf; size_t sfrom = m_flushFrom;
    DrawString out(this);
    set_buf(&out.buf());
    it->second->run(this, out, nullptr);
    m_buf = sbuf; m_flushFrom = sfrom;
    m_ox = sx; m_oy = sy;
}

// Persistent pvar store (serialized "key=value" lines).
namespace {
// {1B5C9A40-7E2D-4C8A-9F31-6A0B2D4E8C70}
const GUID g_pvars_guid =
    { 0x1b5c9a40, 0x7e2d, 0x4c8a, { 0x9f, 0x31, 0x6a, 0x0b, 0x2d, 0x4e, 0x8c, 0x70 } };
cfg_var_modern::cfg_string g_pvars_cfg(g_pvars_guid, "");
}

std::map<std::string, std::string> load_all_pvars() {
    pfc::string8 data = g_pvars_cfg.get();
    std::map<std::string, std::string> out;
    std::string s(data.get_ptr(), data.length());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(pos, nl - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
        pos = nl + 1;
    }
    return out;
}

void save_all_pvars(const std::map<std::string, std::string>& pvars) {
    std::string out;
    for (auto& kv : pvars) { out += kv.first; out += '='; out += kv.second; out += '\n'; }
    g_pvars_cfg.set(out.c_str());
}

void SkinEngine::load_pvars() {
    m_pvars = load_all_pvars();
    // The cover-flow button and view used to need the (long dead) foo_chronflow plugin, so fooAvA
    // ships with them off. The panel is native now: enable them once, then leave the user's
    // choice alone.
    // ("3": re-run once to undo complete_onboarding() having set first.cf=1 — see there.)
    if (m_pvars["_cf_native"] != "3") {
        m_pvars["cfbutton"] = "1"; m_pvars["first.cf"] = "0"; m_pvars["_cf_native"] = "3";
    }
}

void SkinEngine::save_pvars() {
    save_all_pvars(m_pvars);
}

void SkinEngine::render(HDC dc, int width, int height) {
    if (m_script.is_empty() || !m_parent) return;
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }

    m_placements.clear();
    m_buttons.clear();
    { SkinHook hook(this, dc, width, height, metadb_handle_ptr(), m_hoverX, m_hoverY);
      DrawString out(&hook); hook.set_buf(&out.buf());
      // While playing, evaluate with the playback fields (%playback_time_seconds%, %length%,
      // %isplaying%…) the skin's own progress bar / time readout are drawn from.
      bool ran = false;
      if (playback_control::get()->is_playing())
          ran = playback_control::get()->playback_format_title(&hook, out, m_script, nullptr, playback_control::display_level_all);
      if (!ran) m_script->run(&hook, out, nullptr);
      hook.finish();
      // Still inside the hook's scope, so it can blit over the frame just drawn: light the
      // panel tab that is currently showing (left column showPanel:*, right column showPane:*).
      hook.apply_selected_buttons(m_buttons); }

    hide_unrequested_panels();
    for (const auto& p : m_placements) {
        dispatch_placement(p, 0, 0);
    }
}

void SkinEngine::hide_unrequested_panels() {
    auto wanted = [&](const std::string& name) {
        if (m_childShown.count(name)) return true;
        for (auto& p : m_placements) if (p.name == name) return true;
        return false;
    };
    auto sweep = [&](auto& map) {
        for (auto& kv : map) if (kv.second && !wanted(kv.first)) {
            if (HWND w = kv.second->wnd()) ShowWindow(w, SW_HIDE);
        }
    };
    sweep(m_track_displays); sweep(m_seekbars); sweep(m_volumes);
    sweep(m_playlists); sweep(m_spectra); sweep(m_album_lists); sweep(m_lyrics); sweep(m_searches); sweep(m_trees); sweep(m_hosts);
}

// Create/position/show the hosted window for one $panel() placement, offset by (offsetX,offsetY)
// — 0,0 for the master canvas (render()'s own loop), or a native panel's own screen position
// within the main window when the placement came from that panel's per-panel script instead
// (host_child_panel below). Shared so both paths get the exact same per-type dispatch.
void SkinEngine::dispatch_placement(const Placement& p, int offsetX, int offsetY) {
    int x = p.x + offsetX, y = p.y + offsetY;
    // Native panels (no DUI equivalent) get our own window.
    if (p.type.find("Playlist switcher") != std::string::npos) {
        auto& lt = m_trees[p.name];
        if (!lt) { lt = std::make_unique<LibraryTree>(); lt->create(m_parent, this); }
        if (HWND w = lt->wnd()) {
            ShowWindow(w, SW_SHOW);
            if (m_childShown.count(p.name)) SetWindowPos(w, HWND_TOP, x, y, p.w, p.h, SWP_NOACTIVATE);
            else MoveWindow(w, x, y, p.w, p.h, TRUE);
        }
        return;
    }
    if (p.type.find("Quick Search") != std::string::npos) {
        auto& qs = m_searches[p.name];
        if (!qs) { qs = std::make_unique<QuickSearch>(); qs->create(m_parent, this); }
        if (HWND w = qs->wnd()) {
            ShowWindow(w, SW_SHOW);
            SetWindowPos(w, HWND_TOP, x, y, p.w, p.h, SWP_NOACTIVATE);
        }
        return;
    }
    if (p.type.find("Lyric") != std::string::npos) {
        auto& ly = m_lyrics[p.name];
        if (!ly) { ly = std::make_unique<LyricsPanel>(); ly->create(m_parent, this); }
        if (HWND w = ly->wnd()) {
            ShowWindow(w, SW_SHOW);
            if (m_childShown.count(p.name)) SetWindowPos(w, HWND_TOP, x, y, p.w, p.h, SWP_NOACTIVATE);
            else MoveWindow(w, x, y, p.w, p.h, TRUE);
        }
        return;
    }
    if (p.type.find("Track Display") != std::string::npos) {
        auto& td = m_track_displays[p.name];
        if (!td) {
            td = std::make_unique<TrackDisplay>();
            td->create(m_parent, this);
            td->set_name(p.name); // for the right-click "Edit code..." editor
            std::string sc = read_panel_script(p.name); // real fooAvA per-panel script
            if (!sc.empty()) td->set_script(sc.c_str());
        }
        if (HWND w = td->wnd()) {
            ShowWindow(w, SW_SHOW);
            if (m_childShown.count(p.name)) SetWindowPos(w, HWND_TOP, x, y, p.w, p.h, SWP_NOACTIVATE);
            else MoveWindow(w, x, y, p.w, p.h, TRUE);
        }
        return;
    }
    if (p.type.find("Seek") != std::string::npos) {
        auto& sb = m_seekbars[p.name];
        if (!sb) { sb = std::make_unique<Seekbar>(); sb->create(m_parent, this); }
        if (HWND w = sb->wnd()) { ShowWindow(w, SW_SHOW); MoveWindow(w, x, y, p.w, p.h, TRUE); }
        return;
    }
    if (p.type.find("Volume") != std::string::npos) {
        auto& vol = m_volumes[p.name];
        if (!vol) { vol = std::make_unique<Volume>(); vol->create(m_parent, this); }
        if (HWND w = vol->wnd()) { ShowWindow(w, SW_SHOW); MoveWindow(w, x, y, p.w, p.h, TRUE); }
        return;
    }
    if (p.type.find("Single Column Playlist") != std::string::npos ||
        p.type.find("ELPlaylist") != std::string::npos) {
        auto& pv = m_playlists[p.name];
        if (!pv) { pv = std::make_unique<PlaylistView>(); pv->create(m_parent, this); }
        if (HWND w = pv->wnd()) { ShowWindow(w, SW_SHOW); MoveWindow(w, x, y, p.w, p.h, TRUE); }
        return;
    }
    if (p.type.find("Album list") != std::string::npos || p.type.find("Graphical Browser") != std::string::npos ||
        p.type.find("Chronflow") != std::string::npos) {
        auto& al = m_album_lists[p.name];
        if (!al) { al = std::make_unique<AlbumList>(); al->create(m_parent, this, p.type.find("Chronflow") != std::string::npos); }
        if (HWND w = al->wnd()) {
            ShowWindow(w, SW_SHOW);
            if (m_childShown.count(p.name)) SetWindowPos(w, HWND_TOP, x, y, p.w, p.h, SWP_NOACTIVATE);
            else MoveWindow(w, x, y, p.w, p.h, TRUE);
        }
        return;
    }
    if (p.type.find("spectrum") != std::string::npos || p.type.find("Spectrum") != std::string::npos) {
        // Always our native themed bars. An installed DUI "Spectrum"-named element (e.g.
        // foo_vis_spectrum_analyzer) was previously preferred when present, but hosting it through
        // PanelHost's bare ui_element_instance (no real DUI host services behind it) renders a
        // blank window — invisible against the old solid-black canvas, but a visible empty gap now
        // that the canvas shows the skin wallpaper. Native draws its own skin-background+bars (see
        // Spectrum::paint()) and is the one path known to actually render.
        auto& sp = m_spectra[p.name];
        if (!sp) { sp = std::make_unique<Spectrum>(); sp->create(m_parent, this); }
        // Two stacked strips in the skin: the tall (35px) upper one is the analyser whose bars
        // rise; the short (20px) lower one is the inverted reflection. Assign the role here so
        // the strip knows which half to draw (see Spectrum::paint).
        sp->set_mirror(p.h < 30);
        // The skin places the two strips at the very bottom of the cover; lift the whole block a
        // bit so there's breathing room between the analyser/mirror and whatever sits below it
        // (bottom bar / now-playing text), closer to its original position. The analyser strip
        // keeps its top edge and grows downward; the reflection slides down and grows so the pair
        // stays adjacent, then the whole block is raised.
        int hh = p.h, yy = y;
        constexpr int kRaise = 26;
        if (p.h < 30) { yy += 8; hh += 8; } else { hh += 8; }
        yy -= kRaise;
        // HWND_TOP (not a plain MoveWindow, which leaves z-order untouched): a sibling panel
        // created later — e.g. Display.txt's mini.playlist/miniinfo cover overlay — would
        // otherwise sit in front of an already-existing spectrum window and hide it entirely.
        if (HWND w = sp->wnd()) {
            ShowWindow(w, SW_SHOW);
            SetWindowPos(w, HWND_TOP, x, yy, p.w, hh, SWP_NOACTIVATE);
        }
        return;
    }
    const char* dui = map_type(p.type);
    if (!dui) return;
    auto& host = m_hosts[p.name];
    if (!host) { host = std::make_unique<PanelHost>(); host->create(m_parent, dui); }
    if (HWND w = host->wnd()) { ShowWindow(w, SW_SHOW); MoveWindow(w, x, y, p.w, p.h, TRUE); }
}

void SkinEngine::host_child_panel(const Placement& p, int offsetX, int offsetY) {
    m_childShown.insert(p.name);
    dispatch_placement(p, offsetX, offsetY);
}

void SkinEngine::hide_child_panel(const std::string& name) {
    m_childShown.erase(name);
    auto hide = [](HWND w) { if (w) ShowWindow(w, SW_HIDE); };
    if (auto it = m_track_displays.find(name); it != m_track_displays.end() && it->second) hide(it->second->wnd());
    if (auto it = m_seekbars.find(name);       it != m_seekbars.end()       && it->second) hide(it->second->wnd());
    if (auto it = m_volumes.find(name);        it != m_volumes.end()        && it->second) hide(it->second->wnd());
    if (auto it = m_playlists.find(name);      it != m_playlists.end()      && it->second) hide(it->second->wnd());
    if (auto it = m_spectra.find(name);        it != m_spectra.end()        && it->second) hide(it->second->wnd());
    if (auto it = m_album_lists.find(name);    it != m_album_lists.end()    && it->second) hide(it->second->wnd());
    if (auto it = m_lyrics.find(name);         it != m_lyrics.end()         && it->second) hide(it->second->wnd());
    if (auto it = m_searches.find(name);       it != m_searches.end()       && it->second) hide(it->second->wnd());
    if (auto it = m_trees.find(name);          it != m_trees.end()          && it->second) hide(it->second->wnd());
    if (auto it = m_hosts.find(name);          it != m_hosts.end()          && it->second) hide(it->second->wnd());
}

std::string SkinEngine::read_panel_script(const std::string& name) {
    if (m_base.empty()) return {};
    std::string path = m_base + "/panels/" + name + ".txt";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        // Silent failure here used to just leave the panel showing its placeholder script with
        // no clue why — e.g. pointing the Preferences page's skin folder at skins/fooava's raw
        // extraction source (no panels/ subfolder there, only the deployed component folder has
        // one) blanks every native panel with the master canvas script loading fine regardless.
        console::printf("Panels UI: panel script not found: %s", path.c_str());
        return {};
    }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) { size_t r = fread(&s[0], 1, n, f); s.resize(r); }
    fclose(f);
    // Simple cover-art resolution (replaces fooAvA's fragile init): point MyCoverPath at
    // the track folder's cover; the image loader resolves the wildcard (Folder.jpg/png/...).
    static const char* kCoverInit =
        "$setpvar(MyCoverPath,$replace(%path%,%filename_ext%,*folder*.*))";
    return std::string(kCoverInit) + s;
}

bool SkinEngine::save_panel_script(const std::string& name, const std::string& text) {
    if (m_base.empty()) return false;
    std::string path = m_base + "/panels/" + name + ".txt";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

// Run a fooAvA button action ("Playback/Random", "Previous", "New Playlist", …) by
// matching the leaf name against registered main-menu commands.
static bool run_action(const std::string& action) {
    std::string leaf = action;
    auto s = leaf.find_last_of('/');
    if (s != std::string::npos) leaf = leaf.substr(s + 1);

    service_enum_t<mainmenu_commands> e;
    service_ptr_t<mainmenu_commands> p;
    while (e.next(p)) {
        const t_uint32 n = p->get_command_count();
        for (t_uint32 i = 0; i < n; ++i) {
            pfc::string8 nm;
            p->get_name(i, nm);
            if (stricmp_utf8(nm.get_ptr(), leaf.c_str()) == 0) {
                p->execute(i, service_ptr_t<service_base>());
                return true;
            }
        }
    }
    console::printf("Panels UI: no command for action '%s'", action.c_str());
    return false;
}

// Strip surrounding single quotes (PanelsUI quotes WINDOWSIZE/PVAR values like '736').
static std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
    return s;
}

bool SkinEngine::run_button_action(const std::string& a) {
    // Transport buttons — handle via playback_control directly. (Going through the main menu by
    // leaf name is ambiguous: "Random" matches both Playback/Random AND the Random playback ORDER,
    // so the play-random button would wrongly change the order.)
    auto pc = playback_control::get();
    if (a == "Previous")          { pc->previous(); return true; }
    if (a == "Next")              { pc->next(); return true; }
    if (a == "Stop")              { pc->stop(); return true; }
    if (a == "Play" || a == "Pause" || a == "play" || a == "pause") { pc->play_or_pause(); return true; }
    if (a == "Playback/Random")   { pc->start(playback_control::track_command_rand, false); return true; }

    // PVAR:SET:key:value — set a setup variable, persist, and re-layout (mode/theme switch).
    if (a.compare(0, 9, "PVAR:SET:") == 0) {
        std::string rest = a.substr(9);
        size_t c = rest.find(':');
        if (c != std::string::npos) {
            m_pvars[unquote(rest.substr(0, c))] = unquote(rest.substr(c + 1));
            save_pvars();
            repaint_all();
        }
        return true;
    }
    // WINDOWSIZE:w:h[:halign:valign] — resize the top-level player window, optionally anchored
    // at a corner/edge (halign LEFT/RIGHT, valign TOP/BOTTOM) instead of the default top-left.
    if (a.compare(0, 11, "WINDOWSIZE:") == 0) {
        std::vector<std::string> tok; std::string rest = a.substr(11), cur;
        for (char ch : rest) { if (ch == ':') { tok.push_back(cur); cur.clear(); } else cur += ch; }
        tok.push_back(cur);
        if (tok.size() >= 2) {
            int w = atoi(unquote(tok[0]).c_str());
            int h = atoi(unquote(tok[1]).c_str());
            std::string halign = tok.size() >= 3 ? tok[2] : std::string();
            std::string valign = tok.size() >= 4 ? tok[3] : std::string();
            HWND top = m_parent ? GetAncestor(m_parent, GA_ROOT) : nullptr;
            if (top && w > 0 && h > 0) {
                // w/h are a CLIENT size (computed from %_width%/%_height%, which is always
                // client-space) but SetWindowPos takes the OUTER window rect — convert via
                // AdjustWindowRectEx, or every resize silently loses the title bar/menu/border
                // overhead from the requested client size. Uncorrected, repeated clicks on a
                // script's own toggle button (e.g. fooAvA's onepanel one/two-panel switch) drift
                // smaller by that overhead each time instead of landing on a stable pair of sizes.
                RECT rc = { 0, 0, w, h };
                LONG_PTR style = GetWindowLongPtrW(top, GWL_STYLE);
                LONG_PTR exstyle = GetWindowLongPtrW(top, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, (DWORD)style, GetMenu(top) != nullptr, (DWORD)exstyle);
                int outerW = rc.right - rc.left, outerH = rc.bottom - rc.top;
                // halign/valign name which corner/edge of the CURRENT window stays fixed while
                // it grows/shrinks — e.g. fooAvA's RIGHT:TOP keeps the top-right corner (where
                // its own min/mini/exit buttons sit) in place instead of the window drifting
                // left as it shrinks. Default (no flags, or an unrecognised value) keeps top-left.
                RECT cur_rc{}; GetWindowRect(top, &cur_rc);
                int x = cur_rc.left, y = cur_rc.top;
                if (halign == "RIGHT")  x = cur_rc.right - outerW;
                else if (halign == "CENTER") x = cur_rc.left + (cur_rc.right - cur_rc.left - outerW) / 2;
                if (valign == "BOTTOM") y = cur_rc.top - outerH + (cur_rc.bottom - cur_rc.top);
                else if (valign == "CENTER") y = cur_rc.top + (cur_rc.bottom - cur_rc.top - outerH) / 2;
                SetWindowPos(top, nullptr, x, y, outerW, outerH, SWP_NOZORDER);
            }
        }
        return true;
    }
    // POPUP:<file.ava> — open a PanelsUI script (settings/about) in a floating window.
    if (a.compare(0, 6, "POPUP:") == 0) {
        std::string sc = read_panel_script(unquote(a.substr(6)));
        if (!sc.empty()) {
            if (!m_popup) m_popup = std::make_unique<Popup>();
            m_popup->show(m_parent ? GetAncestor(m_parent, GA_ROOT) : nullptr,
                          // 360 wide: the settings layout puts the theme swatches (x 45..225)
                          // left of the font column at %_width%-117 — narrower overlaps them.
                          this, sc.c_str(), 360, 500, L"fooAvA Settings");
            // Stops the skin's own 1Hz settings-button onboarding blink (see below).
            complete_onboarding();
        }
        return true;
    }
    // MENU — the logo button: classic File/Edit/View/Playback/Library/Help menu, popped up at the cursor.
    if (a == "MENU") {
        if (m_parent) {
            POINT pt; GetCursorPos(&pt);
            PostMessageW(GetAncestor(m_parent, GA_ROOT), PUI_WM_SHOW_MAINMENU, (WPARAM)pt.x, (LPARAM)pt.y);
        }
        return true;
    }
    // Title-bar playlist switcher: arrows cycle the active playlist (wrapping), the name opens
    // a menu of all playlists.
    if (a == "Previous playlist" || a == "Next playlist") {
        auto pm = playlist_manager::get();
        const t_size n = pm->get_playlist_count();
        if (n) {
            t_size cur = pm->get_active_playlist();
            if (cur == pfc_infinite) cur = 0;
            else cur = (a == "Next playlist") ? (cur + 1) % n : (cur + n - 1) % n;
            pm->set_active_playlist(cur);
        }
        repaint_all();
        return true;
    }
    if (a == "PLAYLISTS-MENU") {
        auto pm = playlist_manager::get();
        const t_size n = pm->get_playlist_count(), active = pm->get_active_playlist();
        HMENU m = CreatePopupMenu();
        for (t_size i = 0; i < n; ++i) {
            pfc::string8 nm; pm->playlist_get_name(i, nm);
            pfc::stringcvt::string_wide_from_utf8 w(nm.get_ptr());
            AppendMenuW(m, MF_STRING | (i == active ? MF_CHECKED : 0), (UINT_PTR)(i + 1), w.get_ptr());
        }
        HWND owner = m_parent ? GetAncestor(m_parent, GA_ROOT) : core_api::get_main_window();
        POINT pt; GetCursorPos(&pt);
        ReleaseCapture();
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                 pt.x, pt.y, 0, owner, nullptr);
        DestroyMenu(m);
        if (cmd > 0 && (t_size)cmd <= n) pm->set_active_playlist((t_size)cmd - 1);
        repaint_all();
        return true;
    }
    // MENUBAR:toggle — flip the menubar pvar and tell the top-level window to show/hide its menu.
    if (a == "MENUBAR:toggle") {
        int cur = m_pvars.count("menubar") ? atoi(m_pvars["menubar"].c_str()) : 1;
        int nv = cur ? 0 : 1; m_pvars["menubar"] = std::to_string(nv); save_pvars();
        if (m_parent) PostMessageW(GetAncestor(m_parent, GA_ROOT), PUI_WM_TOGGLE_MENU, (WPARAM)nv, 0);
        repaint_all();
        return true;
    }
    // Playback order by name ("Default", "Repeat (track)", "Shuffle (tracks)", …) — set it on
    // playlist_manager directly so the order buttons stay in sync with the player.
    {
        auto pm = playlist_manager::get();
        const t_size n = pm->playback_order_get_count();
        for (t_size i = 0; i < n; ++i)
            if (stricmp_utf8(pm->playback_order_get_name(i), a.c_str()) == 0) {
                pm->playback_order_set_active(i); repaint_all(); return true;
            }
    }
    // TAG:SET:field:value — write a tag on the now-playing track (e.g. the rating stars).
    if (a.compare(0, 8, "TAG:SET:") == 0) {
        std::string rest = a.substr(8); size_t c = rest.find(':');
        if (c != std::string::npos) {
            std::string field = rest.substr(0, c), value = rest.substr(c + 1);
            metadb_handle_ptr track; playback_control::get()->get_now_playing(track);
            if (track.is_valid()) {
                if (field == "rating") {
                    // set_rating() routes navidrome:// tracks through foo_navidrome's own API
                    // instead of the file-tag write below, which fails for them (no real file).
                    set_rating(track, atoi(value.c_str()));
                } else {
                    metadb_handle_list list; list.add_item(track);
                    service_ptr_t<file_info_filter> f =
                        new service_impl_t<meta_set_filter>(field.c_str(), value.c_str());
                    metadb_io_v2::get()->update_info_async(
                        list, f, core_api::get_main_window(), 0, nullptr);
                }
            }
        }
        repaint_all();
        return true;
    }
    return run_action(a);
}

bool SkinEngine::handle_click(int x, int y) {
    // fooAvA stacks multiple $button calls at the SAME rect to chain multiple actions off one
    // click (e.g. the onepanel toggle: an invisible PVAR:SET button plus a visible WINDOWSIZE
    // button, same x/y/w/h) — run every match, not just the first, or the later ones never fire.
    bool handled = false;
    for (const auto& b : m_buttons) {
        if (button_hit(b, x, y)) { run_button_action(b.action); handled = true; }
    }
    return handled;
}

bool SkinEngine::update_hover(int x, int y) {
    int before = -1, after = -1;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        if (button_hit(m_buttons[i], m_hoverX, m_hoverY)) before = (int)i;
        if (button_hit(m_buttons[i], x, y)) after = (int)i;
    }
    m_hoverX = x; m_hoverY = y;
    return before != after;
}

void SkinEngine::repaint_all() {
    if (m_parent) InvalidateRect(GetAncestor(m_parent, GA_ROOT), nullptr, TRUE);
    for (auto& kv : m_track_displays) if (kv.second && kv.second->wnd()) InvalidateRect(kv.second->wnd(), nullptr, TRUE);
    for (auto& kv : m_seekbars)       if (kv.second && kv.second->wnd()) InvalidateRect(kv.second->wnd(), nullptr, TRUE);
    for (auto& kv : m_volumes)        if (kv.second && kv.second->wnd()) InvalidateRect(kv.second->wnd(), nullptr, TRUE);
}

int SkinEngine::pvar_int(const std::string& key, int def) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    auto it = m_pvars.find(key);
    return (it != m_pvars.end() && !it->second.empty()) ? atoi(it->second.c_str()) : def;
}

void SkinEngine::set_rating(const metadb_handle_ptr& track, int stars) {
    if (track.is_empty()) return;
    if (stars < 0) stars = 0; if (stars > 5) stars = 5;

    // navidrome:// tracks have no real file to tag — metadb_io_v2 fails with "Tagging of this
    // file format is not supported". foo_navidrome (if installed) exposes a service that pushes
    // the rating to the server instead; use it when the track is one of its.
    service_enum_t<navidrome::navidrome_rating_api> e;
    service_ptr_t<navidrome::navidrome_rating_api> api;
    while (e.next(api)) {
        if (api->is_navidrome_track(track)) { api->set_rating_async(track, stars); return; }
    }

    char v[8] = ""; if (stars > 0) sprintf(v, "%d", stars);
    metadb_handle_list list; list.add_item(track);
    service_ptr_t<file_info_filter> f = new service_impl_t<meta_set_filter>("RATING", v);
    metadb_io_v2::get()->update_info_async(list, f, core_api::get_main_window(), 0, nullptr);
}

std::string SkinEngine::pvar_str(const std::string& key) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    auto it = m_pvars.find(key);
    return it != m_pvars.end() ? it->second : std::string();
}

void SkinEngine::set_pvar(const std::string& key, const std::string& value) {
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    m_pvars[key] = value;
    save_pvars();
}

std::string SkinEngine::background_path() const {
    auto bd = m_pvars.find("backgroundd");
    if (bd == m_pvars.end() || bd->second != "1") return {};
    auto bg = m_pvars.find("background");
    if (bg == m_pvars.end() || bg->second.empty()) return {};
    std::string wp = bg->second;
    for (auto& c : wp) if (c == '\\') c = '/';
    return m_base + "/images/fooAVA/" + wp;
}

int SkinEngine::background_alpha() const {
    auto it = m_pvars.find("alpha.bgr");
    return (it != m_pvars.end() && !it->second.empty()) ? atoi(it->second.c_str()) : 195;
}

bool SkinEngine::draw_canvas_background(HDC dc, HWND panelWnd, int destW, int destH) const {
    (void)destW; (void)destH; // dest DC's own bounds already clip the draw; kept for callers' clarity
    std::string path = background_path();
    if (path.empty() || !m_parent) return false;
    RECT prc; GetClientRect(m_parent, &prc);
    POINT org = { 0, 0 }; MapWindowPoints(panelWnd, m_parent, &org, 1);
    // Same rect the canvas itself draws into (dst 0,24, size %_width% x %_height%-24 —
    // see fooava.txt's own $imageabs2 background call), just shifted by this panel's own
    // offset within the window so the visible slice lines up pixel-for-pixel.
    return draw_image(dc, path, -org.x, 24 - org.y, prc.right, prc.bottom - 24, background_alpha());
}

void SkinEngine::snapshot_canvas(HDC src, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!m_snapDc || m_snapW != w || m_snapH != h) {
        if (m_snapDc) { SelectObject(m_snapDc, m_snapOld); DeleteObject(m_snapBmp); DeleteDC(m_snapDc); }
        m_snapDc = CreateCompatibleDC(src);
        m_snapBmp = CreateCompatibleBitmap(src, w, h);
        m_snapOld = SelectObject(m_snapDc, m_snapBmp);
        m_snapW = w; m_snapH = h;
    }
    BitBlt(m_snapDc, 0, 0, w, h, src, 0, 0, SRCCOPY);
}

bool SkinEngine::draw_canvas_snapshot(HDC dc, HWND panelWnd) const {
    if (!m_snapDc || !m_parent) return false;
    RECT rc; GetClientRect(panelWnd, &rc);
    POINT org = { 0, 0 }; MapWindowPoints(panelWnd, m_parent, &org, 1);
    return BitBlt(dc, 0, 0, rc.right, rc.bottom, m_snapDc, org.x, org.y, SRCCOPY) != 0;
}

void SkinEngine::refresh_bars() {
    for (auto& kv : m_seekbars) if (kv.second && kv.second->wnd()) InvalidateRect(kv.second->wnd(), nullptr, FALSE);
    for (auto& kv : m_volumes)  if (kv.second && kv.second->wnd()) InvalidateRect(kv.second->wnd(), nullptr, FALSE);
}

int SkinEngine::colour_index() const {
    auto it = m_pvars.find("colour.b");
    if (it != m_pvars.end()) { int v = atoi(it->second.c_str()); if (v >= 1 && v <= 4) return v; }
    return 2; // blue default
}

bool SkinEngine::theme_color(COLORREF& out) const {
    auto it = m_pvars.find("colour");
    if (it == m_pvars.end() || it->second.empty()) {
        // Skin doesn't expose its own accent pvar — fall back to the Preferences-page global
        // accent override (reserved pvar "_prefs_accent_color", same "r-g-b" format).
        it = m_pvars.find("_prefs_accent_color");
        if (it == m_pvars.end() || it->second.empty()) return false;
    }
    out = parse_rgb(it->second.c_str());
    return true;
}

// Relight the selected member of every pvar radio group in `list`: themed image over the
// normal one the run already drew, plus a 1px outline in the skin's theme accent (with a faint
// halo, so it reads as "lit" rather than as a drawn box).
//
// The group is detected structurally, not by hard-coding a list of pvars: a group is a pvar
// that 2+ buttons in THIS frame set to 2+ *different* values. fooAvA's panel tabs qualify —
// the left column sets showPanel:1..5, the right column showPane:1..4 — and so do the settings
// popup's radio rows (set.colour:1..3, MyFont:0..1). Its toggles (CoverFlow, pp, not, codec,
// onepanel, showsr) only ever draw ONE branch per frame, so they stay a group of one and are
// left alone; without this guard the INFO/search/mode buttons would sit permanently lit.
void SkinHook::apply_selected_buttons(std::vector<Button>& list) {
    std::map<std::string, std::vector<Button*>> groups;
    for (auto& b : list)
        if (!b.pvarKey.empty()) groups[b.pvarKey].push_back(&b);
    for (auto& [key, members] : groups) {
        if (members.size() < 2) continue;
        bool distinct = false;
        for (size_t i = 1; i < members.size(); ++i)
            if (members[i]->pvarValue != members[0]->pvarValue) { distinct = true; break; }
        if (!distinct) continue;
        std::string cur = m_e->pvar_str(key);
        if (cur.empty()) continue;
        for (Button* b : members)
            if (b->pvarValue == cur) { b->selected = true; break; }
    }

    COLORREF accent = RGB(0, 140, 220); // matches fooAvA's default blue accent
    if (!m_e->theme_color(accent)) return; // no theme colour to outline with
    for (auto& b : list) {
        if (!b.selected) continue;
        if (!b.litImage.empty())
            draw_image(m_dc, resolve(b.litImage), b.x, b.y, b.litW, b.litH);
        RECT r = mkrect(b.x, b.y, b.w, b.h);
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) continue;
        // Soft halo one step out, so the 1px contour reads as "lit" rather than as a hard box.
        fill_alpha(mkrect(r.left - 1, r.top - 1, w + 2, 1), accent, 90);
        fill_alpha(mkrect(r.left - 1, r.bottom, w + 2, 1), accent, 90);
        fill_alpha(mkrect(r.left - 1, r.top, 1, h), accent, 90);
        fill_alpha(mkrect(r.right, r.top, 1, h), accent, 90);
        HBRUSH br = CreateSolidBrush(accent);
        if (br) { FrameRect(m_dc, &r, br); DeleteObject(br); }
    }
}

// The skin's own "first run" onboarding. fooAvA seeds `first.boot=0` and, while it stays 0, its
// master script blinks the settings button at 1Hz (a $select on %_time_elapsed_seconds% picking
// between the themed and the grey wrench art). Legacy Panels UI cleared the flag when the user
// opened the settings popup; nothing in the extracted script does, so it blinks forever. Opening
// any popup counts as "configured": every `first.*` flag goes to 1. Set them all rather than just
// first.boot so a skin with more first-run flags works the same way; skins without any are
// unaffected. Except first.cf: there 1 means SHOW the cover-flow intro page ("install
// foo_chronflow"), which would replace our native carousel — load_pvars() keeps it at 0.
void SkinEngine::complete_onboarding() {
    bool changed = false;
    for (auto& [k, v] : m_pvars) {
        if (k.compare(0, 6, "first.") == 0 && k != "first.cf" && v != "1") { v = "1"; changed = true; }
    }
    if (!changed) return;
    save_pvars();
    repaint_all();
}

void SkinEngine::draw_script(HDC dc, int w, int h,
                             const service_ptr_t<titleformat_object>& script,
                             const metadb_handle_ptr& track,
                             std::vector<Button>* capture,
                             int hoverX, int hoverY,
                             std::vector<Placement>* placementsOut) {
    if (script.is_empty()) return;
    if (capture) { capture->clear(); m_capture = capture; }
    if (placementsOut) { placementsOut->clear(); m_capturePlacements = placementsOut; }
    // Panels UI hands every panel script its own $get(width)/$get(height).
    m_tfvars["width"] = std::to_string(w);
    m_tfvars["height"] = std::to_string(h);
    SkinHook hook(this, dc, w, h, track, hoverX, hoverY);
    DrawString out(&hook); hook.set_buf(&out.buf());
    // Use playback formatting so dynamic fields (%playback_time%, %isplaying%…) resolve.
    if (track.is_valid())
        playback_control::get()->playback_format_title(
            &hook, out, script, nullptr, playback_control::display_level_all);
    else
        script->run(&hook, out, nullptr);
    hook.finish();
    m_capture = nullptr;
    m_capturePlacements = nullptr;
    // After the run, while the hook's DC is still valid: light whichever panel tab is showing.
    // Same DCO (the painting bitmap, not the on-screen one), so it lands in the composite.
    if (capture) hook.apply_selected_buttons(*capture);
    else if (!m_buttons.empty()) hook.apply_selected_buttons(m_buttons);
}

} // namespace pui
