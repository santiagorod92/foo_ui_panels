#include <cmath>
#include <set>
#include <algorithm>
#include "skin_engine.h"
#include "image_cache.h"
#include "script_util.h"
#include "fs_util.h"
#include "navidrome_rating_api.h"
#include "../panels/track_display.h"
#include "../panels/seekbar.h"
#include "../panels/volume.h"
#include "../panels/popup.h"
#include "../panels/playlist_view.h"
#include "../panels/spectrum.h"
#include "../panels/peak_meter.h"
#include "../panels/album_art.h"
#include "../panels/album_list.h"
#include "../panels/lyrics_panel.h"
#include "../panels/quick_search.h"
#include "../panels/library_tree.h"
#include <foobar2000/SDK/cfg_var.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

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

// A button's tooltip argument at index i: TOOLTIP:"text" in one argument ($button/$button2), or
// TOOLTIP followed by the text as the next argument ($imagebutton/$textbutton). Quotes stripped.
static std::string tooltip_arg(titleformat_hook_function_params* p, size_t argc, size_t i) {
    if (i >= argc) return {};
    std::string t = clean_action(param_str(p, i));
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    if (t.compare(0, 8, "TOOLTIP:") == 0) t = t.substr(8);
    else if (t == "TOOLTIP") t = i + 1 < argc ? clean_action(param_str(p, i + 1)) : std::string();
    else return {};
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    return t;
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

// A face the skin asks for that isn't installed: the stand-in the skin config names for it
// (`font.alias.<face> = <installed face>`, matched case-insensitively), so layout metrics stay sane
// instead of the platform's arbitrary default. "" if none.
static std::string face_alias(const SkinConfig& cfg, const char* want) {
    for (auto& [face, have] : cfg.with_prefix("font.alias."))
        if (pfc::stricmp_ascii(want, face.c_str()) == 0) return have;
    return {};
}

// $functions SkinHook::process_function handles — keep in sync with it (the skin diagnostics
// treat anything that is neither here nor a core titleformat function as unsupported).
static const char* const kHookFunctions[] = {
    "panel", "eval", "get", "puts", "getpvar", "setpvar", "calculate_blend_target", "offset_colour",
    "windowstyle", "font", "set_font", "drawrect", "drawroundrect", "gradientrect", "drawstring",
    "draw_text", "alignabs", "textcolor", "set_font_color", "imageabs", "draw_image", "fileexists",
    "cwb_fileexists", "greater", "imageabs2", "button", "button2", "calcwidth", "textbutton",
    "imagebutton", "settitle", "settray", "gp_set_brush", "gp_fill_rectangle", "gp_set_pen", "gp_draw_rectangle",
};
// Accepted (so they render nothing instead of an error) but not implemented: $scplsetlayout
// (Single Column Playlist's own layout scripts, which the native playlist doesn't run).
static const char* const kStubFunctions[] = { "scplsetlayout" };

static bool listed(const char* const* list, size_t n, const std::string& name) {
    for (size_t i = 0; i < n; ++i) if (name == list[i]) return true;
    return false;
}

// Whether foobar2000's own titleformat engine knows $name: an unknown function renders a fixed
// error marker, learned from a name that can't exist. A known one called with the wrong number
// of arguments renders that same marker, so try 0..4 of them. Formatted against a (dummy) track:
// $info/$meta & co. come with track context, which a bare run() lacks. (The hook knows nothing;
// a null one crashes the core.)
static bool core_knows_function(const std::string& name) {
    struct NoHook : titleformat_hook {
        bool process_field(titleformat_text_out*, const char*, t_size, bool& found) override { found = false; return false; }
        bool process_function(titleformat_text_out*, const char*, t_size, titleformat_hook_function_params*, bool& found) override { found = false; return false; }
    };
    static std::map<std::string, bool> known;
    static std::string marker;
    static metadb_handle_ptr track;
    if (track.is_empty()) metadb::get()->handle_create(track, make_playable_location("pui-probe://", 0));
    auto render = [](const std::string& code) {
        titleformat_object::ptr obj; pfc::string8 out; NoHook hook;
        if (titleformat_compiler::get()->compile(obj, code.c_str())) track->format_title(&hook, out, obj, nullptr);
        return std::string(out.get_ptr());
    };
    if (marker.empty()) marker = render("$pui_no_such_function_qz()");
    auto it = known.find(name);
    if (it != known.end()) return it->second;
    std::string args;
    for (int n = 0; n <= 4; ++n) {
        if (render("$" + name + "(" + args + ")") != marker) return known[name] = true;
        args += args.empty() ? "a" : ",a";
    }
    return known[name] = false;
}

// --- titleformat hook: layout + immediate drawing --------------------------
class SkinHook : public titleformat_hook {
    friend class SkinEngine; // draw_script() drives a hook from outside (see the class)
public:
    SkinHook(SkinEngine* e, gfx::Canvas& cv, int w, int h, const metadb_handle_ptr& track = metadb_handle_ptr(),
             int hoverX = -1, int hoverY = -1)
        : m_e(e), m_cv(cv), m_w(w), m_h(h), m_track(track), m_hoverX(hoverX), m_hoverY(hoverY) {}
    ~SkinHook() { flush_text(); }

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
            // The track this run is for (a panel script's), else whatever is playing.
            metadb_handle_ptr t = m_track;
            if (t.is_empty()) playback_control::get()->get_now_playing(t);
            metadb_info_container::ptr info;
            if (t.is_valid() && t->get_info_ref(info) && info->info().meta_get_count_by_name("NAVIDROME_RATING") > 0) {
                const char* v = info->info().meta_get("NAVIDROME_RATING", 0);
                out->write(titleformat_inputtypes::unknown, v, strlen(v));
                return true;
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
            if (ui::MainWindow* w = m_e->m_main) {
                std::string mode = param_str(p, 0);
                if (mode.find("hidetitlebar") != std::string::npos) w->set_titlebar_visible(false);
                else if (mode.find("showtitlebar") != std::string::npos) w->set_titlebar_visible(true);
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
            gfx::Color brush, pen; bool hb = find_color(spec, "brushcolor", brush);
            bool hp = find_color(spec, "pencolor", pen);
            int alpha = 255; auto ap = spec.find("alpha-");
            if (ap != std::string::npos) alpha = atoi(spec.c_str() + ap + 6);
            gfx::Rect rc = mkrect(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3));
            if (hb) {
                if (alpha < 255) m_cv.fill_rect_alpha(rc, brush, alpha);
                else m_cv.fill_rect(rc, brush);
            }
            if (hp) m_cv.frame_rect(rc, pen);
            return true;
        }
        if (eq(name, len, "drawroundrect") && argc >= 7) {
            int x = param_int(p,0) + m_ox, y = param_int(p,1) + m_oy, w = param_int(p,2), h = param_int(p,3);
            int aw = param_int(p,4), ah = param_int(p,5);
            m_cv.fill_round_rect(gfx::Rect{ x, y, w, h }, aw, ah, parse_rgb(param_str(p,6).c_str()));
            return true;
        }
        if (eq(name, len, "gradientrect") && argc >= 6) {
            m_cv.gradient_v(gfx::Rect{ param_int(p,0) + m_ox, param_int(p,1) + m_oy, param_int(p,2), param_int(p,3) },
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
            gfx::Rect rc = mkrect(param_int(p,1), param_int(p,2), param_int(p,3), param_int(p,4));
            std::string opts = (argc >= 6) ? param_str(p,5) : std::string();
            m_cv.draw_text(text, rc, text_opts(opts), m_textcol);
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
            unsigned f = gfx::kWordEllipsis;
            if (ha.find("center") != std::string::npos) f |= gfx::kAlignCenter;
            else if (ha.find("right") != std::string::npos) f |= gfx::kAlignRight;
            if (va.find("center") != std::string::npos) f |= gfx::kVCenter | gfx::kSingleLine;
            // A one-line-high box is a caption sized from $calcwidth(), which measures with a
            // slightly different font than we draw with — let it overflow instead of ellipsizing.
            if (m_alignRect.bottom - m_alignRect.top <= 24) { f &= ~gfx::kWordEllipsis; f |= gfx::kNoClip | gfx::kSingleLine; }
            m_alignFlags = f; m_aligned = true;
            return true;
        }
        if ((eq(name, len, "textcolor") || eq(name, len, "set_font_color")) && argc >= 1) {
            flush_text(); // pending literal text keeps the colour it was written with
            m_textcol = parse_rgb(param_str(p,0).c_str()); return true;
        }
        if (eq(name, len, "imageabs") && argc >= 5) {
            // $imageabs(x,y,w,h,path,align)
            draw_cover_art(m_cv, image_path(param_str(p,4)), m_track, param_int(p,0) + m_ox, param_int(p,1) + m_oy,
                       param_int(p,2), param_int(p,3));
            return true;
        }
        if (eq(name, len, "draw_image") && argc >= 5) {
            // $draw_image(x,y,w,h,path,...)
            draw_cover_art(m_cv, image_path(param_str(p,4)), m_track, param_int(p,0) + m_ox, param_int(p,1) + m_oy,
                       param_int(p,2), param_int(p,3));
            return true;
        }
        // $cwb_fileexists — foo_cwb_hooks' file-exists check; same semantics as $fileexists here.
        if ((eq(name, len, "fileexists") || eq(name, len, "cwb_fileexists")) && argc >= 1) {
            if (file_exists(resolve(param_str(p,0))))
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
                std::string ip = image_path(param_str(p,8));
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
                    draw_image_part(m_cv, ip, param_int(p,6) + m_ox, param_int(p,7) + m_oy, cw, ch,
                                    (float)SX * iw / scW, (float)SY * ih / scH,
                                    (float)cw * iw / scW, (float)ch * ih / scH, alpha);
                    return true;
                }
            }
            draw_cover_art(m_cv, image_path(param_str(p,8)), m_track, param_int(p,6) + m_ox, param_int(p,7) + m_oy,
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
                    drew = draw_image(m_cv, image_path(d1), x, y, rawW > 0 ? rawW : 0, rawH > 0 ? rawH : 0);
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
                    gfx::Rect r = mkrect(x, y, hw, hh);
                    unsigned fmt = gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine | gfx::kNoClip;
                    draw_glow(lit, r, fmt);
                    m_cv.draw_text(lit, r, fmt, m_textcol);
                    drew = true;
                }
            }
            // No faint-frame fallback: a button whose image is missing stays invisible
            // (still clickable) instead of showing an empty border.
            std::string act = clean_action(param_str(p,8));
            if (!act.empty()) {
                Button btn;
                btn.x = x; btn.y = y; btn.w = hw; btn.h = hh; btn.action = act;
                btn.tooltip = tooltip_arg(p, argc, 9);
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
            out->write_int(titleformat_inputtypes::unknown, m_cv.text_width(t));
            return true;
        }
        // $textbutton(left,top,width,height,str_normal,str_hover,action,"TOOLTIP",tip)
        // Draws the (already-evaluated) normal text into the box + records a clickable region.
        if (eq(name, len, "textbutton") && argc >= 5) {
            int x = param_int(p,0), y = param_int(p,1), w = param_int(p,2), h = param_int(p,3);
            std::string text = param_str(p,4);
            gfx::Rect r = mkrect(x, y, w > 0 ? w : 240, h > 0 ? h : 18);
            m_cv.draw_text(text, r, gfx::kAlignCenter | gfx::kSingleLine | gfx::kVCenter | gfx::kNoClip, m_textcol);
            if (argc >= 7) { std::string a = clean_action(param_str(p,6));
                if (!a.empty()) (m_e->m_capture ? *m_e->m_capture : m_e->m_buttons).push_back({ x, y, (w>0?w:240), (h>0?h:18), a, tooltip_arg(p, argc, 7) }); }
            return true;
        }
        // $imagebutton(left,top,image_normal,image_hover,action,"TOOLTIP",tip) — rating stars etc.
        if (eq(name, len, "imagebutton") && argc >= 5) {
            int x = param_int(p,0), y = param_int(p,1);
            const int hw = 11, hh = 15; // ~star-sized hit box
            bool hovered = m_hoverX >= x && m_hoverX < x + hw && m_hoverY >= y && m_hoverY < y + hh;
            draw_image(m_cv, image_path(param_str(p, hovered ? 3 : 2)), x, y, 0, 0); // natural size
            std::string a = clean_action(param_str(p,4));
            if (!a.empty()) (m_e->m_capture ? *m_e->m_capture : m_e->m_buttons).push_back({ x, y, hw, hh, a, tooltip_arg(p, argc, 5) });
            return true;
        }

        // $settray(tooltip): the skin wants a tray icon (see ui::MainWindow::set_tray).
        if (eq(name, len, "settray")) {
            if (m_e->m_main) m_e->m_main->set_tray(argc >= 1 ? param_str(p, 0) : std::string("foobar2000"));
            return true;
        }
        // $settitle(text) — the window/taskbar title; fooAvA: "fooAvA" while stopped/paused, else
        // "%artist% - %title%". Re-run every paint; the platform skips unchanged text.
        if (eq(name, len, "settitle") && argc >= 1) {
            if (m_e->m_main) m_e->m_main->set_title(param_str(p, 0));
            return true;
        }
        // GDI+ brush fills: $gp_set_brush(A-R-G-B) (or R-G-B, opaque), then
        // $gp_fill_rectangle(x,y,w,h) — e.g. a translucent black veil, $gp_set_brush(85-0-0-0).
        if (eq(name, len, "gp_set_brush") && argc >= 1) {
            parse_argb(param_str(p, 0), m_gpBrush, m_gpAlpha);
            return true;
        }
        // GDI+ outlines: $gp_set_pen(A-R-G-B,width[,dash,join]) — dash/join ignored — then
        // $gp_draw_rectangle(x,y,w,h), the pen centred on the rectangle's edges like GDI+.
        if (eq(name, len, "gp_set_pen") && argc >= 1) {
            parse_argb(param_str(p, 0), m_gpPen, m_gpPenAlpha);
            m_gpPenWidth = argc >= 2 ? std::max(1, param_int(p, 1)) : 1;
            return true;
        }
        if (eq(name, len, "gp_draw_rectangle") && argc >= 4 && m_gpPenAlpha > 0) {
            const int w = m_gpPenWidth, lo = w / 2;
            gfx::Rect r = mkrect(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3));
            const gfx::Rect o{ r.x - lo, r.y - lo, r.w + w, r.h + w }; // outer edge of the stroke
            // Four non-overlapping bands, so a translucent pen doesn't double up at the corners.
            const gfx::Rect bands[] = { { o.x, o.y, o.w, w }, { o.x, o.y + o.h - w, o.w, w },
                                        { o.x, o.y + w, w, o.h - 2 * w }, { o.x + o.w - w, o.y + w, w, o.h - 2 * w } };
            for (const gfx::Rect& b : bands) {
                if (b.w <= 0 || b.h <= 0) continue;
                if (m_gpPenAlpha >= 255) m_cv.fill_rect(b, m_gpPen);
                else m_cv.fill_rect_alpha(b, m_gpPen, m_gpPenAlpha);
            }
            return true;
        }
        if (eq(name, len, "gp_fill_rectangle") && argc >= 4) {
            gfx::Rect rc = mkrect(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3));
            if (m_gpAlpha >= 255) m_cv.fill_rect(rc, m_gpBrush);
            else if (m_gpAlpha > 0) m_cv.fill_rect_alpha(rc, m_gpBrush, m_gpAlpha);
            return true;
        }

        for (auto s : kStubFunctions) if (eq(name, len, s)) return true;

        found = false; return false;
    }

private:
    // Panels UI $font style glow: "glow-r-g-b [glowexpand-N] [glowalpha-N]" — a soft halo of
    // the glyphs. Built from the SAME text layout the text itself is drawn with (the canvas
    // renders the glyph coverage into a mask), dilated by `expand` px, faded outward, and
    // alpha-blended under the text. Stamping the text through a different renderer lays glyphs
    // out differently (padding, advance widths), so the halo sat offset from the letters and
    // read as a smeared ghost copy of the title.
    void draw_glow(const std::string& s, const gfx::Rect& r, unsigned fmt) {
        if (!m_glowAlpha || m_glowExpand <= 0 || s.empty() || !m_haveFont) return;
        const int e = m_glowExpand > 6 ? 6 : m_glowExpand;
        // Work area: the text rect grown by the glow radius, clamped to the target surface.
        const int bw = m_cv.width(), bh = m_cv.height();
        gfx::Rect box = r;
        if (fmt & gfx::kNoClip) {
            // Text may spill past its box: grow it by the measured overflow on both sides (the
            // alignment decides which side it actually lands on). Not the whole canvas — the
            // dilation below is O(area * radius²) and this runs for every glowing label per paint.
            const int tw = m_cv.text_width(s);
            const int th = m_cv.text_height(s, std::max(r.w, tw), fmt);
            const int sx = std::max(0, tw - r.w), sy = std::max(0, th - r.h);
            box = gfx::Rect{ r.x - sx, r.y - sy, r.w + 2 * sx, r.h + 2 * sy };
        }
        int x0 = std::max(0, box.x - e), y0 = std::max(0, box.y - e);
        int x1 = std::min(bw, box.right() + e), y1 = std::min(bh, box.bottom() + e);
        const int W = x1 - x0, H = y1 - y0;
        if (W <= 0 || H <= 0) return;

        std::vector<uint8_t> mask;
        if (!m_cv.text_coverage(s, r, fmt, gfx::Rect{ x0, y0, W, H }, mask)) return;
        // Bounding box of the coverage.
        int mx0 = W, my0 = H, mx1 = -1, my1 = -1;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            if (mask[(size_t)y * W + x]) { mx0 = std::min(mx0, x); my0 = std::min(my0, y); mx1 = std::max(mx1, x); my1 = std::max(my1, y); }
        }
        if (mx1 < 0) return;
        mx0 = std::max(0, mx0 - e); my0 = std::max(0, my0 - e);
        mx1 = std::min(W - 1, mx1 + e); my1 = std::min(H - 1, my1 + e);

        // Dilate: each pixel takes the strongest nearby glyph coverage, weighted so the ring
        // hugging the glyphs is strongest and the outermost ring faintest.
        const int cr = m_glowCol.r, cg = m_glowCol.g, cb = m_glowCol.b;
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
        m_cv.blend_argb(gfx::Rect{ x0 + mx0, y0 + my0, mx1 - mx0 + 1, my1 - my0 + 1 },
                        res.data() + (size_t)my0 * W + mx0, W);
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
                gfx::Rect r = gfx::Rect::ltrb(m_alignRect.left, m_alignRect.top, m_alignRect.right, m_alignRect.bottom);
                draw_glow(s, r, m_alignFlags);
                m_cv.draw_text(s, r, m_alignFlags, m_textcol);
            }
        }
        m_flushFrom = m_buf->size();
    }

    // Draw origin: normally (0,0); set to a button's (x,y) while running a $button2
    // sub-draw command so its relative coords land at the button position.
    int m_ox = 0, m_oy = 0;
    gfx::Rect mkrect(int x, int y, int w, int h) { return gfx::Rect{ x + m_ox, y + m_oy, w, h }; }

    // Run a $button2 draw-command (titleformat) at offset (ox,oy). Compiled scripts cached.
    void run_subscript(const std::string& text, int ox, int oy);

    // Relight the selected member of every pvar radio group in `list` (themed image + themed
    // outline). Must run while the canvas is still valid — i.e. inside draw_script, after the run.
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

    // resolve() for an image the skin draws. The first time a path inside the skin folder turns
    // up missing it is reported (cover art and anything else outside the folder may come and go).
    std::string image_path(const std::string& raw) {
        std::string path = resolve(raw);
        const std::string& base = m_e->m_base;
        if (!base.empty() && path.size() > base.size() + 1 && path.compare(0, base.size(), base) == 0 &&
            path.back() != '/' && path.find_first_of("*?") == std::string::npos &&
            m_e->m_imagesChecked.insert(path).second && !file_exists(path))
            m_e->report_once("Panels UI: skin image not found: " + path);
        return path;
    }

    void select_font(const char* face, int size, const std::string& style) {
        // A skin can name its face through a variable ($font($get(fontAVA),...)) whose value it
        // set with $setpvar: `face` then arrives empty. The config's `font.face_pvar` names that
        // pvar, so the real face is recovered before falling back to a generic one.
        const char* f0 = face;
        while (f0 && (*f0 == ' ' || *f0 == '\t')) ++f0;
        std::string fromPvar;
        const std::string facePvar = m_e->m_cfg.str("font.face_pvar");
        if ((!f0 || !*f0) && !facePvar.empty()) {
            auto it = m_e->m_pvars.find(facePvar);
            if (it != m_e->m_pvars.end() && !it->second.empty()) { fromPvar = it->second; f0 = fromPvar.c_str(); }
        }
        // Neither the script nor its own pvar named a face — try the Preferences-page global
        // font override (reserved pvar "_prefs_font_face") before the hardcoded fallback.
        if (!f0 || !*f0) {
            auto it = m_e->m_pvars.find("_prefs_font_face");
            if (it != m_e->m_pvars.end() && !it->second.empty()) { fromPvar = it->second; f0 = fromPvar.c_str(); }
        }
        std::string defFace;
        if (!f0 || !*f0) { defFace = m_e->m_cfg.str("font.default", "Tahoma"); f0 = defFace.c_str(); }
        if (size <= 0) {
            auto it = m_e->m_pvars.find("_prefs_font_size");
            if (it != m_e->m_pvars.end() && !it->second.empty()) size = atoi(it->second.c_str());
        }
        // Style is a space-separated list; match whole tokens only -- `style.find('b')` also hits
        // "glow", which made every glowing label bold, and `find('i')` would hit "glow" too.
        bool bold = false, italic = false, underline = false;
        gfx::Color glow;
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
        gfx::FontSpec spec;
        spec.face = f0; spec.size = (float)(size > 0 ? size : 9); spec.points = true;
        spec.bold = bold; spec.italic = italic; spec.underline = underline;
        // The platform silently swaps in an arbitrary default for a face it can't find: if it
        // isn't what was asked for, retry with the closest face we do have. Drop the real TTFs
        // into the fb2k dir to get the skin's own look.
        if (!m_cv.set_font(spec)) {
            std::string alias = face_alias(m_e->m_cfg, f0);
            if (!alias.empty()) { spec.face = alias; m_cv.set_font(spec); }
        }
        m_haveFont = true;
    }

    void draw_string(titleformat_hook_function_params* p, t_size argc) {
        std::string text = param_str(p, 0);
        gfx::Rect rc = mkrect(param_int(p,1), param_int(p,2), param_int(p,3), param_int(p,4));
        gfx::Color col = (argc >= 6) ? parse_rgb(param_str(p,5).c_str()) : gfx::Color();
        std::string flags = (argc >= 7) ? param_str(p,6) : std::string();
        unsigned fmt = text_opts(flags);
        draw_glow(text, rc, fmt);
        m_cv.draw_text(text, rc, fmt, col);
    }

    SkinEngine* m_e; gfx::Canvas& m_cv; int m_w, m_h;
    metadb_handle_ptr m_track; // set for draw_script() (TrackDisplay/Popup) — enables the
                                // album_art_manager_v2 fallback in draw_cover_art for non-local sources.
    int m_hoverX, m_hoverY; // mouse pos in this call's coordinate space, -1,-1 if not hovering
    bool m_haveFont = false; // a $font has been selected this pass (glow needs one)
    struct FontState { std::string face; int size; std::string style; gfx::Color col; };
    // Every $font this pass, and its length after the last $button/$button2 — lets a text
    // button recover the normal-state font (see the button handler).
    std::vector<FontState> m_fontLog; size_t m_fontLogMark = 0;
    int m_depth = 0; // nesting depth for snippet evaluation (see run_into/eval_arg)
    gfx::Color m_glowCol; int m_glowExpand = 0, m_glowAlpha = 0;
    gfx::Color m_gpBrush; int m_gpAlpha = 255; // $gp_set_brush state for $gp_fill_rectangle
    gfx::Color m_gpPen; int m_gpPenAlpha = 255, m_gpPenWidth = 1; // $gp_set_pen for $gp_draw_rectangle

    // positional literal-text state. Text is read from the DrawString buffer (m_buf) rather than
    // accumulated per-write: titleformat appends a $if/$ifequal condition's value to the buffer then
    // truncates it away — reading the live buffer at flush time means those (e.g. %_isplaying% -> "1")
    // never leak into the drawn text (which a separate pending buffer would keep).
    struct LTRB { int left = 0, top = 0, right = 0, bottom = 0; };
    bool m_aligned = false; LTRB m_alignRect; unsigned m_alignFlags = 0;
    gfx::Color m_textcol{ 255, 255, 255 };
    const std::string* m_buf = nullptr; size_t m_flushFrom = 0;
public:
    void set_buf(const std::string* b) { m_buf = b; m_flushFrom = b ? b->size() : 0; }
    // Flush the final text box while the DrawString buffer is still alive (it is destroyed
    // before this hook), then detach so ~SkinHook doesn't read a dangling buffer.
    void finish() { flush_text(); m_buf = nullptr; }
};

// --- SkinEngine ------------------------------------------------------------
SkinEngine::PlayEvents::PlayEvents(SkinEngine* e)
    : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_seek |
                              flag_on_playback_pause | flag_on_playback_edited |
                              flag_on_playback_dynamic_info_track | flag_on_volume_change),
      m_e(e) {}

SkinEngine::SkinEngine() = default;
SkinEngine::~SkinEngine() { destroy_panels(); }

void SkinEngine::set_main_window(ui::MainWindow* w) {
    m_main = w;
    if (w && !m_playEvents) {
        m_playEvents = std::make_unique<PlayEvents>(this);
        set_art_ready_callback([this] { repaint_all(); });
    }
}

void SkinEngine::add_files(const std::vector<std::string>& paths, t_size at) {
    if (paths.empty()) return;
    pfc::list_t<const char*> urls;
    for (auto& p : paths) urls.add_item(p.c_str()); // process_locations_async copies them
    auto notify = process_locations_notify::create([at](metadb_handle_list_cref items) {
        auto pm = playlist_manager::get();
        t_size pl = pm->get_active_playlist();
        if (pl == pfc_infinite) { pl = pm->create_playlist_autoname(); pm->set_active_playlist(pl); }
        const t_size n = pm->playlist_get_item_count(pl);
        const t_size base = (at == pfc_infinite || at > n) ? n : at;
        pm->playlist_undo_backup(pl);
        pm->playlist_clear_selection(pl);
        pm->playlist_insert_items(pl, base, items, bit_array_true());
    });
    playlist_incoming_item_filter_v2::get()->process_locations_async(
        urls, playlist_incoming_item_filter_v2::op_flag_delay_ui, nullptr, nullptr,
        core_api::get_main_window(), notify);
}

void SkinEngine::show_tray_menu() {
    if (!m_main) return;
    enum { kPlayPause = 1, kStop, kPrev, kNext, kShow, kHide, kExit };
    auto pc = playback_control::get();
    auto item = [](const char* label, int id) { ui::MenuItem m; m.label = label; m.id = id; return m; };
    ui::Menu menu;
    menu.push_back(item(pc->is_playing() && !pc->is_paused() ? "Pause" : "Play", kPlayPause));
    menu.push_back(item("Stop", kStop));
    menu.push_back(item("Previous", kPrev));
    menu.push_back(item("Next", kNext));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Show foobar2000", kShow));
    menu.push_back(item("Hide foobar2000", kHide));
    menu.push_back(ui::MenuItem::sep());
    menu.push_back(item("Exit", kExit));
    switch (ui::popup_menu_at_cursor(*m_main, menu)) {
    case kPlayPause: pc->play_or_pause(); break;
    case kStop:      pc->stop(); break;
    case kPrev:      pc->previous(); break;
    case kNext:      pc->next(); break;
    case kShow:      standard_commands::main_activate(); break;
    case kHide:      standard_commands::main_hide(); break;
    case kExit:      standard_commands::main_exit(); break;
    default: break;
    }
}

bool SkinEngine::playback_ticking() {
    auto pc = playback_control::get();
    return pc->is_playing() && !pc->is_paused();
}

void SkinEngine::destroy_panels() {
    if (m_playEvents) { m_playEvents.reset(); set_art_ready_callback(nullptr); }
    m_popupHost.reset(); m_popup.reset();
    m_panels.clear();
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_frames.clear();
}

bool SkinEngine::load(const char* script) {
    service_ptr_t<titleformat_object> obj;
    if (!titleformat_compiler::get()->compile(obj, script)) {
        console::print("Panels UI: skin script failed to compile");
        return false;
    }
    m_script = obj;
    return true;
}

bool SkinEngine::load_skin(const std::string& dir) {
    m_base = dir;
    if (m_main) m_main->set_tray(""); // only while the (new) skin keeps asking for it
    m_cfg.load(dir);
    if (!m_cfg.empty()) console::printf("Panels UI: skin config %s loaded", SkinConfig::kFileName);
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
    seed_pvars();
    std::string why;
    m_mainPath = resolve_main_script(dir, m_cfg, &why);
    if (!why.empty()) console::printf("Panels UI: %s", why.c_str());
    bool ok = load_main_script();
    m_fileTimes = scan_skin_files();
    m_nextScan = tick_ms() + 1000;
    return ok;
}

bool SkinEngine::load_main_script() {
    std::string skin = m_mainPath.empty() ? std::string() : read_file(m_mainPath);
    if (m_mainPath.empty() || skin.empty())
        console::printf("Panels UI: no main script in %s, using the built-in test skin", m_base.c_str());
    else {
        console::printf("Panels UI: main script %s (%u bytes)", m_mainPath.c_str(), (unsigned)skin.size());
        diagnose_script(script_label(m_mainPath), skin);
    }
    bool ok = load(skin.empty() ? builtin_test_skin() : skin.c_str());
    if (!ok) console::print("Panels UI: main script failed to compile");
    return ok;
}

std::string SkinEngine::script_label(const std::string& path) const {
    if (path.compare(0, m_base.size() + 1, m_base + "/") == 0) return path.substr(m_base.size() + 1);
    return path;
}

SkinEngine::FileTimes SkinEngine::scan_skin_files() const {
    FileTimes out;
    if (m_base.empty()) return out;
    std::error_code ec;
    for (const std::string& f : { m_mainPath, m_base + "/" + SkinConfig::kFileName }) {
        if (f.empty()) continue;
        auto t = std::filesystem::last_write_time(fs_path(f), ec);
        if (!ec) out[f] = t;
    }
    const std::string pdir = panels_dir();
    std::filesystem::directory_iterator it(fs_path(pdir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->path().extension() != ".txt") continue;
        auto ft = it->last_write_time(e2);
        if (!e2) out[pdir + "/" + fs_utf8(it->path().filename())] = ft;
    }
    return out;
}

void SkinEngine::check_skin_changes() {
    if (m_base.empty() || tick_ms() < m_nextScan) return;
    m_nextScan = tick_ms() + 1000;
    FileTimes now = scan_skin_files();
    if (now == m_fileTimes) return;
    std::set<std::string> changed;
    for (auto& [path, t] : now) {
        auto it = m_fileTimes.find(path);
        if (it == m_fileTimes.end() || it->second != t) changed.insert(path);
    }
    for (auto& [path, t] : m_fileTimes) if (!now.count(path)) changed.insert(path); // deleted
    m_fileTimes = std::move(now);
    if (changed.empty()) return;

    m_reported.clear(); m_imagesChecked.clear(); // a reload re-reports what's still wrong
    bool any = false;
    if (changed.count(m_base + "/" + SkinConfig::kFileName)) {
        // The config can name another main script, art folder, panel folder...: start over.
        console::printf("Panels UI: reloaded %s", SkinConfig::kFileName);
        m_cfg.load(m_base);
        seed_pvars();
        std::string why;
        m_mainPath = resolve_main_script(m_base, m_cfg, &why);
        if (!why.empty()) console::printf("Panels UI: %s", why.c_str());
        load_main_script();
        m_fileTimes = scan_skin_files();
        changed.clear();
        for (auto& [path, t] : m_fileTimes) changed.insert(path); // re-read every panel script
        any = true;
    } else if (changed.count(m_mainPath)) {
        if (load_main_script()) {
            console::printf("Panels UI: reloaded %s", script_label(m_mainPath).c_str());
            any = true;
        }
    }
    auto panel_file = [&](const std::string& name) { return panels_dir() + "/" + name + ".txt"; };
    for (auto& [name, slot] : m_panels) {
        if (slot.kind != Kind::TrackDisplay || !changed.count(panel_file(name))) continue;
        std::string sc = read_panel_script(name);
        static_cast<TrackDisplay*>(slot.view.get())->set_script(sc.empty() ? nullptr : sc.c_str());
        console::printf("Panels UI: reloaded %s", script_label(panel_file(name)).c_str());
        any = true;
    }
    if (m_popup && !m_popupFile.empty() && changed.count(panel_file(m_popupFile))) {
        std::string sc = read_panel_script(m_popupFile);
        if (!sc.empty()) {
            m_popup->set_script(sc.c_str());
            if (m_popupHost) m_popupHost->invalidate();
            console::printf("Panels UI: reloaded %s", script_label(panel_file(m_popupFile)).c_str());
            any = true;
        }
    }
    if (!any) return;
    // Snippets cached by text and $puts values belong to the old scripts.
    m_evalcache.clear(); m_subcache.clear(); m_tfvars.clear();
    repaint_all();
}

void SkinEngine::report_once(const std::string& msg) {
    if (m_reported.insert(msg).second) console::print(msg.c_str());
}

void SkinEngine::diagnose_script(const std::string& where, const std::string& text) {
    std::set<std::string> unknown, stubbed;
    bool quoted = false; // '...' is literal text in titleformat
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') { quoted = !quoted; continue; }
        if (quoted || c != '$') continue;
        size_t j = i + 1;
        while (j < text.size() && (isalnum((unsigned char)text[j]) || text[j] == '_')) ++j;
        if (j == i + 1 || j >= text.size() || text[j] != '(') continue;
        std::string name = text.substr(i + 1, j - i - 1);
        if (listed(kStubFunctions, std::size(kStubFunctions), name)) stubbed.insert("$" + name);
        else if (!listed(kHookFunctions, std::size(kHookFunctions), name) && !core_knows_function(name))
            unknown.insert("$" + name);
    }
    auto join = [](const std::set<std::string>& names) {
        std::string out;
        for (auto& n : names) { if (!out.empty()) out += ", "; out += n; }
        return out;
    };
    if (!unknown.empty())
        report_once("Panels UI: " + where + ": unsupported function(s), rendered as errors: " + join(unknown));
    if (!stubbed.empty())
        report_once("Panels UI: " + where + ": not implemented yet, ignored: " + join(stubbed));
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

PvarMap load_all_pvars() {
    pfc::string8 data = g_pvars_cfg.get();
    PvarMap out;
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

void save_all_pvars(const PvarMap& pvars) {
    std::string out;
    for (auto& kv : pvars) { out += kv.first; out += '='; out += kv.second; out += '\n'; }
    g_pvars_cfg.set(out.c_str());
}

void SkinEngine::load_pvars() {
    m_pvars = load_all_pvars();
}

// `pvar.once.<name> = <value>`: set once, then left to the user — e.g. a skin turning on a view
// whose original plugin is dead but which a native panel now provides. The reserved
// `_once.<name>` pvar remembers it was done.
void SkinEngine::seed_pvars() {
    bool changed = false;
    for (auto& [name, value] : m_cfg.with_prefix("pvar.once.")) {
        std::string& mark = m_pvars["_once." + name];
        if (mark == "1") continue;
        m_pvars[name] = value; mark = "1"; changed = true;
    }
    if (changed) save_pvars();
}

void SkinEngine::save_pvars() {
    save_all_pvars(m_pvars);
}

void SkinEngine::render(gfx::Canvas& cv, int width, int height) {
    if (m_script.is_empty() || !m_main) return;
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }

    m_placements.clear();
    m_buttons.clear();
    { SkinHook hook(this, cv, width, height, metadb_handle_ptr(), m_hoverX, m_hoverY);
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

    for (auto& p : m_placements) p = remap_panel(p);
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
    for (auto& [name, slot] : m_panels) if (!wanted(name)) slot.show(false);
}

// The slot for panel `name`, created on first use. A name re-used for another panel type
// replaces the old panel.
SkinEngine::Slot* SkinEngine::ensure_slot(const std::string& name, Kind kind, const Placement& p) {
    auto it = m_panels.find(name);
    if (it != m_panels.end() && it->second.kind == kind) return &it->second;
    if (it != m_panels.end()) m_panels.erase(it);
    if (!m_main) return nullptr;
    Slot s; s.kind = kind;
    ui::ViewOptions opts;
    switch (kind) {
    case Kind::TrackDisplay: {
        auto td = std::make_unique<TrackDisplay>(this, name);
        std::string sc = read_panel_script(name); // real fooAvA per-panel script
        if (!sc.empty()) td->set_script(sc.c_str());
        s.view = std::move(td);
        break;
    }
    case Kind::Seekbar:     s.view = std::make_unique<Seekbar>(this); opts.cursor = ui::Cursor::Hand; break;
    case Kind::Volume:      s.view = std::make_unique<Volume>(this); opts.cursor = ui::Cursor::Hand; break;
    case Kind::Playlist:    s.view = std::make_unique<PlaylistView>(this); opts.double_clicks = true; opts.accept_files = true; break;
    case Kind::Spectrum:    s.view = std::make_unique<Spectrum>(this); opts.render_fps = 60; break;
    case Kind::PeakMeter:   s.view = std::make_unique<PeakMeter>(this); opts.render_fps = 60; break;
    case Kind::AlbumArt:    s.view = std::make_unique<AlbumArt>(this); break;
    case Kind::AlbumList:
        s.view = std::make_unique<AlbumList>(this, p.type.find("Chronflow") != std::string::npos);
        opts.double_clicks = true;
        break;
    case Kind::Lyrics:      s.view = std::make_unique<LyricsPanel>(this); break;
    case Kind::QuickSearch: s.view = std::make_unique<QuickSearch>(this); opts.cursor = ui::Cursor::IBeam; break;
    case Kind::LibraryTree: s.view = std::make_unique<LibraryTree>(this); opts.double_clicks = true; break;
    case Kind::Embedded: {
        const char* dui = map_type(p.type);
        if (!dui) return nullptr;
        s.embedded = ui::create_embedded_ui_element(*m_main, dui);
        // Nothing installed can host it: keep an empty slot (show/place are no-ops) so the
        // lookup isn't retried on every paint, and say so once.
        if (!s.embedded)
            report_once("Panels UI: panel '" + name + "' (" + p.type +
                        "): no installed UI element can host it, left empty");
        break;
    }
    }
    if (s.view) {
        s.host = ui::create_child_view(*m_main, s.view.get(), opts);
        if (!s.host) return nullptr;
    }
    return &m_panels.emplace(name, std::move(s)).first->second;
}

// Create/position/show the hosted panel for one $panel() placement, offset by (offsetX,offsetY)
// — 0,0 for the master canvas (render()'s own loop), or a native panel's own position within
// the main window when the placement came from that panel's per-panel script instead
// (host_child_panel below). Shared so both paths get the exact same per-type dispatch.
void SkinEngine::dispatch_placement(const Placement& p, int offsetX, int offsetY) {
    int x = p.x + offsetX, y = p.y + offsetY;
    const bool child = m_childShown.count(p.name) != 0;
    auto has = [&](const char* k) { return p.type.find(k) != std::string::npos; };
    // Native panels (no DUI equivalent) get our own view. A panel a per-panel script hosts
    // (child) is raised above its host panel; top-level ones keep their z-order.
    Kind kind;
    bool top = child;
    if (has("Playlist switcher"))                        kind = Kind::LibraryTree;
    else if (has("Quick Search"))                        { kind = Kind::QuickSearch; top = true; }
    else if (has("Lyric"))                               kind = Kind::Lyrics;
    else if (has("Track Display"))                       kind = Kind::TrackDisplay;
    else if (has("Seek"))                                { kind = Kind::Seekbar; top = false; }
    else if (has("Volume"))                              { kind = Kind::Volume; top = false; }
    else if (has("Single Column Playlist") || has("ELPlaylist")) { kind = Kind::Playlist; top = false; }
    else if (has("Album list") || has("Graphical Browser") || has("Chronflow")) kind = Kind::AlbumList;
    else if (has("Peakmeter") || has("Peak Meter"))      { kind = Kind::PeakMeter; top = true; }
    else if (has("Album Art"))                           kind = Kind::AlbumArt;
    else if (has("spectrum") || has("Spectrum"))         { kind = Kind::Spectrum; top = true; }
    else                                                 { kind = Kind::Embedded; top = false; }

    Slot* s = ensure_slot(p.name, kind, p);
    if (!s) return;
    int w = p.w, h = p.h;
    if (kind == Kind::Spectrum) {
        // Always our native themed bars. An installed DUI "Spectrum"-named element (e.g.
        // foo_vis_spectrum_analyzer) was previously preferred when present, but hosting it
        // without real DUI host services behind it renders a blank window.
        // A skin can stack two strips: the analyser and, below it, an inverted reflection — any
        // strip shorter than `spectrum.mirror_below` px (0 = none) takes that role (see
        // Spectrum::paint). `spectrum.grow` makes each strip taller (a reflection grows downward,
        // staying adjacent) and `spectrum.raise` lifts them, for skins whose original analyser
        // drew differently from the placement. Raised to the top: a panel created later (e.g. a
        // cover overlay) would otherwise sit in front of an existing spectrum and hide it.
        const bool mirror = p.h < m_cfg.num("spectrum.mirror_below", 0);
        static_cast<Spectrum*>(s->view.get())->set_mirror(mirror);
        const int grow = m_cfg.num("spectrum.grow", 0);
        if (mirror) y += grow;
        h += grow;
        y -= m_cfg.num("spectrum.raise", 0);
    }
    s->show(true);
    s->place(gfx::Rect{ x, y, w, h }, top);
}

void SkinEngine::host_child_panel(const Placement& p, int offsetX, int offsetY) {
    m_childShown.insert(p.name);
    dispatch_placement(p, offsetX, offsetY);
}

void SkinEngine::hide_child_panel(const std::string& name) {
    m_childShown.erase(name);
    auto it = m_panels.find(name);
    if (it != m_panels.end()) it->second.show(false);
}

std::string SkinEngine::read_panel_script_raw(const std::string& name) {
    if (m_base.empty()) return {};
    return read_file(panels_dir() + "/" + name + ".txt");
}

std::string SkinEngine::read_panel_script(const std::string& name) {
    if (m_base.empty()) return {};
    std::string path = panels_dir() + "/" + name + ".txt";
    std::string s = read_file(path);
    if (s.empty() && !file_exists(path)) {
        // Said once: a skin folder without its panel scripts otherwise just shows placeholder
        // panels with no clue why, while the main script loads fine.
        report_once("Panels UI: panel script not found: " + path);
        return {};
    }
    diagnose_script(script_label(path), s);
    // `cover.pvar`: set that pvar to the track folder's cover (`cover.pattern`, wildcards
    // resolved by the image loader) before the script runs — for skins whose own cover lookup
    // relied on a plugin that's gone.
    const std::string coverPvar = m_cfg.str("cover.pvar");
    if (coverPvar.empty()) return s;
    return "$setpvar(" + coverPvar + ",$replace(%path%,%filename_ext%," +
           m_cfg.str("cover.pattern", "*folder*.*") + "))" + s;
}

bool SkinEngine::save_panel_script(const std::string& name, const std::string& text) {
    if (m_base.empty()) return false;
    const std::string path = panels_dir() + "/" + name + ".txt";
    if (!write_file(path, text)) return false;
    // The editor applies it itself: don't have the hot reload pick it up a second time.
    std::error_code ec;
    auto t = std::filesystem::last_write_time(fs_path(path), ec);
    if (!ec) m_fileTimes[path] = t;
    m_reported.clear();
    return true;
}

// Main-menu commands by lower-cased full path ("playback/random", "library/album list"),
// built once: the group chain comes from mainmenu_group(_popup) display names (menu_label).
// Components can't register commands after startup, so no invalidation.
struct MenuCommands { std::vector<std::string> paths; std::vector<GUID> guids; };
static const MenuCommands& menu_commands() {
    static MenuCommands mc;
    if (!mc.paths.empty()) return mc;
    struct Group { GUID parent; std::string name; };
    struct Less { bool operator()(const GUID& a, const GUID& b) const { return memcmp(&a, &b, sizeof a) < 0; } };
    std::map<GUID, Group, Less> groups;
    for (auto g : mainmenu_group::enumerate()) {
        Group gr{ g->get_parent(), {} };
        mainmenu_group_popup::ptr pop;
        if (g->service_query_t(pop)) { pfc::string8 n; pop->get_display_string(n); gr.name = menu_label(n); }
        groups[g->get_guid()] = gr;
    }
    auto group_path = [&](GUID id) {
        std::string path;
        for (int depth = 0; id != pfc::guid_null && depth < 16; ++depth) {
            auto it = groups.find(id);
            if (it == groups.end()) break;
            if (!it->second.name.empty()) path = it->second.name + (path.empty() ? "" : "/") + path;
            id = it->second.parent;
        }
        return path;
    };
    for (auto p : mainmenu_commands::enumerate()) {
        const std::string prefix = group_path(p->get_parent());
        const t_uint32 n = p->get_command_count();
        for (t_uint32 i = 0; i < n; ++i) {
            pfc::string8 nm; p->get_name(i, nm);
            mc.paths.push_back(prefix.empty() ? menu_label(nm) : prefix + "/" + menu_label(nm));
            mc.guids.push_back(p->get_command(i));
        }
    }
    return mc;
}

// Run a skin button action ("Playback/Random", "Library/Album List", "New Playlist", …) through
// the main-menu command best_menu_match() picks for it.
static bool run_action(const std::string& action) {
    const MenuCommands& mc = menu_commands();
    const int i = best_menu_match(mc.paths, action);
    if (i >= 0 && mainmenu_commands::g_execute(mc.guids[(size_t)i])) return true;
    console::printf("Panels UI: no command for action '%s'", action.c_str());
    return false;
}

bool SkinEngine::run_button_action(const std::string& action) {
    // `action.remap.<action> = <action>`: a skin action replaced by another — e.g. a step of a
    // cycle that led to a view the native panels don't provide.
    const std::string a = m_cfg.str("action.remap." + action, action);
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
            // w/h are a CLIENT size (computed from %_width%/%_height%, which is always
            // client-space); halign/valign name which corner/edge of the CURRENT window stays
            // fixed while it grows/shrinks — e.g. fooAvA's RIGHT:TOP keeps the top-right corner
            // (where its own min/mini/exit buttons sit) in place instead of the window drifting
            // left as it shrinks. Default (no flags, or an unrecognised value) keeps top-left.
            if (m_main && w > 0 && h > 0) m_main->resize_client(w, h, halign, valign);
        }
        return true;
    }
    // POPUP:<file.ava> — open a PanelsUI script (settings/about) in a floating window.
    if (a.compare(0, 6, "POPUP:") == 0) {
        const std::string file = unquote(a.substr(6));
        std::string sc = read_panel_script(file);
        if (!sc.empty()) m_popupFile = file;
        if (!sc.empty() && m_main) {
            if (!m_popup) m_popup = std::make_unique<PopupView>(this);
            m_popup->set_script(sc.c_str());
            if (m_popupHost) {
                m_popupHost->invalidate(); // already open: refresh (the platform raises it)
                m_popupHost->focus();
            } else {
                // Size: `popup.size.<file>`, else `popup.size` ("W H"), else 400x500 — a popup
                // script lays itself out for one size, which only the skin knows.
                std::vector<int> sz = m_cfg.nums("popup.size." + file);
                if (sz.size() != 2) sz = m_cfg.nums("popup.size");
                if (sz.size() != 2 || sz[0] <= 0 || sz[1] <= 0) sz = { 400, 500 };
                // Window title from the script's own name ("FOOAvA_settings.ava" -> "FOOAvA settings").
                std::string title = file;
                if (title.size() > 4 && pfc::stricmp_ascii(title.c_str() + title.size() - 4, ".ava") == 0)
                    title.resize(title.size() - 4);
                for (auto& c : title) if (c == '_') c = ' ';
                m_popupHost = ui::create_popup_window(*m_main, m_popup.get(), sz[0], sz[1], title,
                                                      [this] { m_popupHost.reset(); });
            }
            // Stops the skin's own 1Hz settings-button onboarding blink (see below).
            complete_onboarding();
        }
        return true;
    }
    // MENU — the logo button: classic File/Edit/View/Playback/Library/Help menu, popped up at the cursor.
    if (a == "MENU") {
        if (m_main) m_main->show_main_menu();
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
        ui::Menu m;
        for (t_size i = 0; i < n; ++i) {
            pfc::string8 nm; pm->playlist_get_name(i, nm);
            ui::MenuItem it; it.label = nm.get_ptr(); it.id = (int)(i + 1); it.checked = (i == active);
            m.push_back(it);
        }
        int cmd = m_main ? ui::popup_menu_at_cursor(*m_main, m) : 0;
        if (cmd > 0 && (t_size)cmd <= n) pm->set_active_playlist((t_size)cmd - 1);
        repaint_all();
        return true;
    }
    // MENUBAR:toggle — flip the menubar pvar and tell the top-level window to show/hide its menu.
    if (a == "MENUBAR:toggle") {
        int cur = m_pvars.count("menubar") ? atoi(m_pvars["menubar"].c_str()) : 1;
        int nv = cur ? 0 : 1; m_pvars["menubar"] = std::to_string(nv); save_pvars();
        if (m_main) m_main->set_menubar_visible(nv != 0);
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
    // Collect first: an action can pump messages (a menu, a resize) and repaint, which rebuilds
    // m_buttons under the loop.
    std::vector<std::string> actions;
    for (const auto& b : m_buttons) if (button_hit(b, x, y)) actions.push_back(b.action);
    for (const auto& a : actions) run_button_action(a);
    return !actions.empty();
}

bool SkinEngine::update_hover(int x, int y) {
    int before = -1, after = -1;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        if (button_hit(m_buttons[i], m_hoverX, m_hoverY)) before = (int)i;
        if (button_hit(m_buttons[i], x, y)) after = (int)i;
    }
    m_hoverX = x; m_hoverY = y;
    if (m_main) m_main->set_tooltip(tooltip_at(m_buttons, x, y));
    return before != after;
}

void SkinEngine::repaint_all() {
    if (m_main) m_main->invalidate();
    for (auto& [name, s] : m_panels)
        if (s.host && s.host->visible()) s.host->invalidate();
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

// The folder the config's `images` names (relative to the skin), else the skin folder itself.
static std::string images_dir(const std::string& base, const SkinConfig& cfg) {
    std::string d = cfg.str("images");
    for (auto& c : d) if (c == '\\') c = '/';
    while (!d.empty() && (d.back() == '/')) d.pop_back();
    return d.empty() ? base : base + "/" + d;
}

std::string SkinEngine::background_path() const {
    // `background.image_pvar`: pvar holding the wallpaper file (relative to `images`);
    // `background.enabled_pvar` (optional): pvar that must be 1 for it to show.
    const std::string onKey = m_cfg.str("background.enabled_pvar"), imgKey = m_cfg.str("background.image_pvar");
    if (imgKey.empty()) return {};
    if (!onKey.empty()) {
        auto on = m_pvars.find(onKey);
        if (on == m_pvars.end() || on->second != "1") return {};
    }
    auto bg = m_pvars.find(imgKey);
    if (bg == m_pvars.end() || bg->second.empty()) return {};
    std::string wp = bg->second;
    for (auto& c : wp) if (c == '\\') c = '/';
    return images_dir(m_base, m_cfg) + "/" + wp;
}

int SkinEngine::background_alpha() const {
    // `background.alpha_pvar` (0..255), else `background.alpha` (default opaque).
    const std::string key = m_cfg.str("background.alpha_pvar");
    auto it = key.empty() ? m_pvars.end() : m_pvars.find(key);
    return (it != m_pvars.end() && !it->second.empty()) ? atoi(it->second.c_str())
                                                        : m_cfg.num("background.alpha", 255);
}

int SkinEngine::theme_index() const {
    const std::string key = m_cfg.str("theme.index_pvar");
    auto it = key.empty() ? m_pvars.end() : m_pvars.find(key);
    if (it != m_pvars.end() && !it->second.empty()) return atoi(it->second.c_str());
    return m_cfg.num("theme.index_default", 1);
}

bool SkinEngine::configured_color(const char* panel, const char* role, gfx::Color& out) const {
    return parse_config_color(m_cfg.str(std::string("color.") + panel + "." + role), out)
        || parse_config_color(m_cfg.str(std::string("color.") + role), out);
}

gfx::Color SkinEngine::color(const char* panel, const char* role, gfx::Color def) const {
    gfx::Color c;
    return configured_color(panel, role, c) ? c : def;
}

std::string SkinEngine::asset(const std::string& key, int n) const {
    std::string a = m_cfg.str("asset." + key);
    if (a.empty() || m_base.empty()) return {};
    auto put = [&](const std::string& tag, int v) {
        for (size_t p; (p = a.find(tag)) != std::string::npos;) a.replace(p, tag.size(), std::to_string(v));
    };
    put("{theme}", theme_index());
    put("{n}", n);
    for (auto& c : a) if (c == '\\') c = '/';
    return images_dir(m_base, m_cfg) + "/" + a;
}

bool SkinEngine::is_lyrics_panel(const std::string& name) const {
    auto it = m_panels.find(name);
    return it != m_panels.end() && it->second.kind == Kind::Lyrics;
}

Placement SkinEngine::remap_panel(const Placement& p) const {
    const std::string to = m_cfg.str("panel.remap." + p.name);
    if (to.empty()) return p;
    Placement q = p;
    size_t bar = to.find('|');
    q.name = to.substr(0, bar);
    if (bar != std::string::npos) q.type = to.substr(bar + 1);
    return q;
}

bool SkinEngine::draw_canvas_background(gfx::Canvas& cv, const ui::ViewHost& host) const {
    std::string path = background_path();
    if (path.empty() || !m_main) return false;
    gfx::Rect prc = m_main->client_rect(), b = host.bounds();
    // Same rect the canvas script draws its wallpaper into — the client area below
    // `background.top` px — shifted by this panel's own offset within the window so the visible
    // slice lines up pixel-for-pixel.
    const int top = m_cfg.num("background.top", 0);
    return draw_image(cv, path, -b.x, top - b.y, prc.w, prc.h - top, background_alpha());
}

void SkinEngine::snapshot_canvas(gfx::Canvas& cv, int w, int h) {
    if (w <= 0 || h <= 0) return;
    gfx::ImagePtr snap = cv.snapshot(gfx::Rect{ 0, 0, w, h });
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_snapshot = std::move(snap);
}

bool SkinEngine::draw_canvas_snapshot(gfx::Canvas& cv, const ui::ViewHost& host) const {
    gfx::ImagePtr snap;
    { std::lock_guard<std::mutex> lk(m_framesMx); snap = m_snapshot; }
    if (!snap) return false;
    gfx::Rect b = host.bounds();
    return draw_image_region(cv, *snap, b.x, b.y, b.w, b.h);
}

void SkinEngine::store_panel_frame(const std::string& name, gfx::ImagePtr frame, const gfx::Rect& bounds) {
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_frames[name] = PanelFrame{ std::move(frame), bounds };
}

gfx::ImagePtr SkinEngine::backdrop_for(const gfx::Rect& r, int& originX, int& originY) const {
    std::lock_guard<std::mutex> lk(m_framesMx);
    for (auto& [name, f] : m_frames) {
        auto slot = m_panels.find(name); // a hidden panel's last frame is stale
        if (slot == m_panels.end() || !slot->second.host || !slot->second.host->visible()) continue;
        const gfx::Rect& b = f.bounds;
        if (!f.img || b.right() <= r.x || b.x >= r.right() || b.bottom() <= r.y || b.y >= r.bottom()) continue;
        originX = b.x; originY = b.y;
        return f.img;
    }
    originX = originY = 0;
    return m_snapshot;
}

void SkinEngine::refresh_bars() {
    for (auto& [name, s] : m_panels)
        if (s.host && (s.kind == Kind::Seekbar || s.kind == Kind::Volume)) s.host->invalidate();
}


bool SkinEngine::theme_color(gfx::Color& out) const {
    const std::string key = m_cfg.str("theme.accent_pvar");
    auto it = key.empty() ? m_pvars.end() : m_pvars.find(key);
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

    gfx::Color accent;
    if (!m_e->theme_color(accent)) return; // no theme colour to outline with
    for (auto& b : list) {
        if (!b.selected) continue;
        if (!b.litImage.empty())
            draw_image(m_cv, resolve(b.litImage), b.x, b.y, b.litW, b.litH);
        gfx::Rect r = mkrect(b.x, b.y, b.w, b.h);
        const int w = r.w, h = r.h;
        if (w <= 0 || h <= 0) continue;
        // Soft halo one step out, so the 1px contour reads as "lit" rather than as a hard box.
        // (mkrect adds the draw origin again — as the legacy code did; it is 0,0 here.)
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.y - 1, w + 2, 1), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.bottom(), w + 2, 1), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.y, 1, h), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.right(), r.y, 1, h), accent, 90);
        m_cv.frame_rect(r, accent);
    }
}

// The skin's own "first run" onboarding. Legacy Panels UI cleared a skin's first-run flags when
// the user opened its settings popup; nothing in a skin's scripts does (fooAvA blinks its settings
// button at 1Hz while first.boot is 0, forever). Opening any popup counts as "configured": the
// pvars `onboarding.pvars` lists go to 1 — space-separated names, `prefix*` patterns, and
// `!name` exclusions (fooAvA: `first.* !first.cf`, as first.cf=1 means "show the page asking
// to install foo_chronflow", which would replace the native cover flow).
void SkinEngine::complete_onboarding() {
    auto lower = [](std::string t) { for (auto& c : t) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return t; };
    std::vector<std::string> want, skip;
    {
        std::string list = lower(m_cfg.str("onboarding.pvars"));
        for (char& c : list) if (c == '\t') c = ' ';
        size_t i = 0;
        while (i < list.size()) {
            size_t j = list.find(' ', i);
            if (j == std::string::npos) j = list.size();
            std::string tok = list.substr(i, j - i);
            if (!tok.empty() && tok[0] == '!') skip.push_back(tok.substr(1));
            else if (!tok.empty()) want.push_back(tok);
            i = j + 1;
        }
    }
    if (want.empty()) return;
    auto match = [&](const std::string& pat, const std::string& key) { // pvar names ignore case
        const std::string k = lower(key);
        if (!pat.empty() && pat.back() == '*') return k.compare(0, pat.size() - 1, pat, 0, pat.size() - 1) == 0;
        return k == pat;
    };
    auto any = [&](const std::vector<std::string>& pats, const std::string& k) {
        for (auto& p : pats) if (match(p, k)) return true;
        return false;
    };
    bool changed = false;
    for (auto& [k, v] : m_pvars) {
        if (any(want, k) && !any(skip, k) && v != "1") { v = "1"; changed = true; }
    }
    if (!changed) return;
    save_pvars();
    repaint_all();
}

void SkinEngine::draw_script(gfx::Canvas& cv, int w, int h,
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
    SkinHook hook(this, cv, w, h, track, hoverX, hoverY);
    DrawString out(&hook); hook.set_buf(&out.buf());
    // Playback formatting so dynamic fields (%playback_time%, %isplaying%…) resolve — but that
    // only renders the playing item: for any other track (or once playback stopped under us)
    // format that track itself, and with no track at all just run the script.
    metadb_handle_ptr np;
    bool ran = false;
    if (track.is_valid() && playback_control::get()->get_now_playing(np) && np == track)
        ran = playback_control::get()->playback_format_title(
            &hook, out, script, nullptr, playback_control::display_level_all);
    if (!ran && track.is_valid()) { track->format_title(&hook, out, script, nullptr); ran = true; }
    if (!ran) script->run(&hook, out, nullptr);
    hook.finish();
    m_capture = nullptr;
    m_capturePlacements = nullptr;
    // After the run, while the hook's DC is still valid: light whichever panel tab is showing.
    // Same DCO (the painting bitmap, not the on-screen one), so it lands in the composite.
    if (capture) hook.apply_selected_buttons(*capture);
    else if (!m_buttons.empty()) hook.apply_selected_buttons(m_buttons);
}

} // namespace pui
