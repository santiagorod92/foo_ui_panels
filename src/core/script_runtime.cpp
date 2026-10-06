#include "script_runtime.h"
#include "script_util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace pui {

int ScriptArgs::num(size_t i) const { return atoi(str(i).c_str()); }

PvarMap parse_pvars(const std::string& s) {
    PvarMap out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(pos, nl - pos);
        const size_t eq = line.find('=');
        if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
        pos = nl + 1;
    }
    return out;
}

std::string serialize_pvars(const PvarMap& pvars) {
    std::string out;
    for (auto& kv : pvars) { out += kv.first; out += '='; out += kv.second; out += '\n'; }
    return out;
}

// --- helpers ---------------------------------------------------------------
static bool iequal_ascii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return false;
    }
    return true;
}

static std::string trim_ws(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

static Button make_button(int x, int y, int w, int h, std::string action, std::string tooltip) {
    Button b;
    b.x = x; b.y = y; b.w = w; b.h = h; b.action = std::move(action); b.tooltip = std::move(tooltip);
    return b;
}

static bool is_image_file(const std::string& s) {
    return s.find(".png") != std::string::npos || s.find(".jpg") != std::string::npos;
}

// A button's tooltip argument at index i: TOOLTIP:"text" in one argument ($button/$button2), or
// TOOLTIP followed by the text as the next argument ($imagebutton/$textbutton). Quotes stripped.
static std::string tooltip_arg(const ScriptArgs& a, size_t i) {
    if (i >= a.size()) return {};
    std::string t = clean_action(a.str(i));
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    if (t.compare(0, 8, "TOOLTIP:") == 0) t = t.substr(8);
    else if (t == "TOOLTIP") t = i + 1 < a.size() ? clean_action(a.str(i + 1)) : std::string();
    else return {};
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    return t;
}

// A face the skin asks for that isn't installed: the stand-in the skin config names for it
// (`font.alias.<face> = <installed face>`, matched case-insensitively), so layout metrics stay sane
// instead of the platform's arbitrary default. "" if none.
static std::string face_alias(const SkinConfig& cfg, const std::string& want) {
    for (auto& [face, have] : cfg.with_prefix("font.alias."))
        if (iequal_ascii(want, face)) return have;
    return {};
}

// --- the function table ----------------------------------------------------
using S = ScriptFunction::Status;
using R = ScriptRuntime;

const std::vector<ScriptFunction>& script_functions() {
    static const std::vector<ScriptFunction> table = {
        // Layout / hosting
        { "panel", 6, S::Implemented, &R::f_panel, "$panel(name,type,x,y,w,h)",
          "Hosts a panel (native view or UI element) at the rect." },
        { "windowstyle", 1, S::Implemented, &R::f_windowstyle, "$windowstyle(hidetitlebar|showtitlebar)",
          "Shows or hides the player window's title bar." },
        { "settitle", 1, S::Implemented, &R::f_settitle, "$settitle(text)", "The window/taskbar title." },
        { "settray", 0, S::Implemented, &R::f_settray, "$settray(tooltip)",
          "Asks for a tray (Windows) / menu-bar (macOS) icon." },
        // Variables / arithmetic
        { "eval", 1, S::Implemented, &R::f_eval, "$eval(expr)",
          "Integer arithmetic (+ - * / %, () or {} grouping); $get/$getpvar may be nested." },
        { "get", 1, S::Implemented, &R::f_get, "$get(name)",
          "A $puts value (replayed, so a stored draw command runs), else the pvar of that name." },
        { "puts", 2, S::Implemented, &R::f_puts, "$puts(name,value)", "Sets a per-paint scratch variable." },
        { "getpvar", 1, S::Implemented, &R::f_getpvar, "$getpvar(name)",
          "A persistent variable (names ignore case)." },
        { "setpvar", 2, S::Implemented, &R::f_setpvar, "$setpvar(name,value)", "Sets a persistent variable." },
        { "greater", 2, S::Implemented, &R::f_greater, "$greater(a,b)", "1 if a > b (integers), else nothing." },
        { "fileexists", 1, S::Implemented, &R::f_fileexists, "$fileexists(path)",
          "1 if the file exists (relative to the skin folder)." },
        { "cwb_fileexists", 1, S::Implemented, &R::f_fileexists, "$cwb_fileexists(path)",
          "foo_cwb_hooks' spelling of $fileexists." },
        // Colours
        { "calculate_blend_target", 1, S::Implemented, &R::f_blend_target, "$calculate_blend_target(colour)",
          "Black if the hex colour is light, else white (Columns UI)." },
        { "offset_colour", 3, S::Implemented, &R::f_offset_colour, "$offset_colour(from,to,amount)",
          "Shifts a hex colour toward another by amount 0..255 (Columns UI)." },
        // Text
        { "font", 2, S::Implemented, &R::f_font, "$font(face,size[,style[,r-g-b]])",
          "Selects the font; style: bold italic underline glow-r-g-b glowexpand-N glowalpha-N." },
        { "set_font", 2, S::Implemented, &R::f_font, "$set_font(face,size[,style[,r-g-b]])", "Same as $font." },
        { "textcolor", 1, S::Implemented, &R::f_textcolor, "$textcolor(r-g-b)", "Colour of following text." },
        { "set_font_color", 1, S::Implemented, &R::f_textcolor, "$set_font_color(r-g-b)", "Same as $textcolor." },
        { "alignabs", 4, S::Implemented, &R::f_alignabs, "$alignabs(x,y,w,h[,halign[,valign]])",
          "Box for the literal text that follows (left/center/right, top/center)." },
        { "drawstring", 5, S::Implemented, &R::f_drawstring, "$drawstring(text,x,y,w,h[,r-g-b[,opts]])",
          "Draws text in a box; opts: center right vcenter, and wrap (several lines — this engine's addition)." },
        { "draw_text", 5, S::Implemented, &R::f_draw_text, "$draw_text(text,x,y,w,h[,opts])",
          "Draws text in a box in the current text colour." },
        { "calcwidth", 1, S::Implemented, &R::f_calcwidth, "$calcwidth(text)",
          "Pixel width of text in the current font." },
        // Shapes
        { "drawrect", 5, S::Implemented, &R::f_drawrect, "$drawrect(x,y,w,h,spec)",
          "Rectangle; spec: brushcolor-r-g-b pencolor-r-g-b alpha-N (null = none)." },
        { "drawroundrect", 7, S::Implemented, &R::f_drawroundrect, "$drawroundrect(x,y,w,h,rx,ry,r-g-b)",
          "Filled rounded rectangle." },
        { "gradientrect", 6, S::Implemented, &R::f_gradientrect, "$gradientrect(x,y,w,h,top,bottom)",
          "Vertical gradient between two r-g-b colours." },
        { "gp_set_brush", 1, S::Implemented, &R::f_gp_set_brush, "$gp_set_brush(a-r-g-b)",
          "Brush for $gp_fill_rectangle (r-g-b = opaque)." },
        { "gp_fill_rectangle", 4, S::Implemented, &R::f_gp_fill_rectangle, "$gp_fill_rectangle(x,y,w,h)",
          "Fills with the GDI+ brush." },
        { "gp_set_pen", 1, S::Implemented, &R::f_gp_set_pen, "$gp_set_pen(a-r-g-b[,width])",
          "Pen for $gp_draw_rectangle (dash/join arguments ignored)." },
        { "gp_draw_rectangle", 4, S::Implemented, &R::f_gp_draw_rectangle, "$gp_draw_rectangle(x,y,w,h)",
          "Outline centred on the rectangle's edges, like GDI+." },
        // Images
        { "imageabs", 5, S::Implemented, &R::f_imageabs, "$imageabs(x,y,w,h,path[,align])",
          "Draws an image (wildcards and the track's album art allowed)." },
        { "draw_image", 5, S::Implemented, &R::f_imageabs, "$draw_image(x,y,w,h,path,…)", "Same as $imageabs." },
        { "imageabs2", 9, S::Implemented, &R::f_imageabs2,
          "$imageabs2(w,h,srcX,srcY,srcW,srcH,x,y,path[,opts])",
          "Draws an image scaled into w×h (0 = natural / cap), optionally cropped; opts: alpha-N ROTATEFLIP-N." },
        // Controls
        { "button", 9, S::Implemented, &R::f_button,
          "$button(x,y,?,?,w,h,normal,hover,action[,TOOLTIP:text])",
          "Image button (w/h 0 = the image's size)." },
        { "button2", 9, S::Implemented, &R::f_button,
          "$button2(x,y,?,?,w,h,normal,hover,action[,TOOLTIP:text])",
          "Button whose states are draw commands or a text label." },
        { "textbutton", 5, S::Implemented, &R::f_textbutton,
          "$textbutton(x,y,w,h,normal,hover[,action[,TOOLTIP,text]])", "Text button." },
        { "imagebutton", 5, S::Implemented, &R::f_imagebutton,
          "$imagebutton(x,y,normal,hover,action[,TOOLTIP,text])", "Small image button (rating stars)." },
        // Accepted, not implemented: Single Column Playlist's own layout scripts, which the
        // native playlist doesn't run.
        { "scplsetlayout", 0, S::Stub, &R::f_stub, "$scplsetlayout(…)",
          "Single Column Playlist layout — ignored (the native playlist has its own)." },
    };
    return table;
}

const ScriptFunction* find_script_function(std::string_view name) {
    static const std::map<std::string, const ScriptFunction*, std::less<>> index = [] {
        std::map<std::string, const ScriptFunction*, std::less<>> m;
        for (auto& f : script_functions()) m.emplace(f.name, &f);
        return m;
    }();
    auto it = index.find(name);
    return it == index.end() ? nullptr : it->second;
}

bool ScriptRuntime::call(std::string_view name, const ScriptArgs& args, ScriptOut& out) {
    const ScriptFunction* f = find_script_function(name);
    if (!f || args.size() < f->minArgs) return false;
    return (this->*(f->fn))(args, out);
}

std::vector<std::string> script_calls(const std::string& text) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    bool quoted = false; // '...' is literal text in titleformat
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') { quoted = !quoted; continue; }
        if (quoted || c != '$') continue;
        size_t j = i + 1;
        while (j < text.size() && (isalnum((unsigned char)text[j]) || text[j] == '_')) ++j;
        if (j == i + 1 || j >= text.size() || text[j] != '(') continue;
        std::string name = text.substr(i + 1, j - i - 1);
        if (seen.insert(name).second) out.push_back(std::move(name));
    }
    return out;
}

std::string script_functions_markdown() {
    std::string md =
        "# Panels UI script functions\n\n"
        "<!-- Generated from the table in src/core/script_runtime.cpp by `make docs` — do not edit. -->\n\n"
        "The `$functions` this engine adds to foobar2000's titleformat. Every standard titleformat\n"
        "function (`$if`, `$sub`, `$upper`, …) and field works as well. Coordinates are in skin\n"
        "pixels; colours are `r-g-b` unless noted.\n\n"
        "| Function | Status | What it does |\n|---|---|---|\n";
    for (auto& f : script_functions()) {
        std::string sig = f.signature;
        for (size_t p = 0; (p = sig.find('|', p)) != std::string::npos; p += 2) sig.replace(p, 1, "\\|");
        md += "| `" + sig + "` | " + (f.status == ScriptFunction::Status::Stub ? "ignored" : "yes") +
              " | " + f.summary + " |\n";
    }
    return md;
}

// --- nested evaluation -----------------------------------------------------
// Panels UI arguments are themselves titleformat snippets ("$get(fontAVAsize_3)",
// "$getpvar(colour)"). The core renders a function's arguments before handing them over, but a
// value we parse further (a font face, a size) is run again through the env to be sure.
std::string ScriptRuntime::eval_arg(const std::string& in) {
    if (in.empty() || in.find('$') == std::string::npos) return in;
    return m_env.eval(in);
}

std::string ScriptRuntime::resolve_vars(const std::string& in) const {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        bool get = (in.compare(i, 5, "$get(") == 0), pv = (!get && in.compare(i, 9, "$getpvar(") == 0);
        size_t open = pv ? 9 : get ? 5 : 0;
        if (!open) { out.push_back(in[i++]); continue; }
        size_t end = in.find(')', i + open);
        if (end == std::string::npos) { out.append(in, i, std::string::npos); break; }
        std::string name = in.substr(i + open, end - i - open);
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        auto pvar = [&] { auto p = m_st.pvars.find(name); return p != m_st.pvars.end() ? p->second : std::string(); };
        if (pv) out += pvar();
        else {
            auto it = m_st.tfvars.find(name);
            out += it != m_st.tfvars.end() ? it->second : pvar();
        }
        i = end + 1;
    }
    return out;
}

std::string ScriptRuntime::resolve(std::string p) const {
    for (auto& c : p) if (c == '\\') c = '/';
    bool absolute = (p.size() >= 2 && p[1] == ':') || p.compare(0, 2, "//") == 0;
    if (absolute) return p;
    while (!p.empty() && (p[0] == '.' || p[0] == '/')) p.erase(0, 1);
    if (m_st.base.empty()) return p;
    return m_st.base + "/" + p;
}

// resolve() for an image the skin draws. The first time a path inside the skin folder turns up
// missing it is reported (cover art and anything else outside the folder may come and go).
std::string ScriptRuntime::image_path(const std::string& raw) {
    std::string path = resolve(raw);
    const std::string& base = m_st.base;
    if (!base.empty() && path.size() > base.size() + 1 && path.compare(0, base.size(), base) == 0 &&
        path.back() != '/' && path.find_first_of("*?") == std::string::npos &&
        m_st.imagesChecked.insert(path).second && !m_env.file_exists(path))
        m_env.report("Panels UI: skin image not found: " + path);
    return path;
}

// --- layout / variables ----------------------------------------------------
bool ScriptRuntime::f_panel(const ScriptArgs& a, ScriptOut&) {
    (m_st.capturePlacements ? *m_st.capturePlacements : m_st.placements).push_back(
        { a.str(0), a.str(1), a.num(2), a.num(3), a.num(4), a.num(5) });
    return true;
}

bool ScriptRuntime::f_eval(const ScriptArgs& a, ScriptOut& out) {
    out.write_int(eval_expr(resolve_vars(a.str(0))));
    return true;
}

// Panels UI's own scratch-variable pool, distinct from $setpvar's persistent pvars and used all
// over the skin: font selection ($puts(fontAVA,...)), and the CD-case layout in Display/MINI
// ($puts(lgx-cover,488) / $get(cx-cover) inside $eval). A value that is itself a function call is
// kept verbatim so $get can replay it as a draw command.
bool ScriptRuntime::f_get(const ScriptArgs& a, ScriptOut& out) {
    const std::string& k = a.str(0);
    auto it = m_st.tfvars.find(k);
    if (it != m_st.tfvars.end()) { m_env.replay(out, it->second); return true; }
    auto pv = m_st.pvars.find(k); // panels seed some values as pvars
    if (pv != m_st.pvars.end()) out.write(pv->second);
    return true;
}

bool ScriptRuntime::f_puts(const ScriptArgs& a, ScriptOut&) {
    const std::string& v = a.str(1);
    m_st.tfvars[a.str(0)] = (v.find('$') != std::string::npos) ? v : eval_arg(v);
    return true;
}

bool ScriptRuntime::f_getpvar(const ScriptArgs& a, ScriptOut& out) {
    auto it = m_st.pvars.find(a.str(0));
    if (it != m_st.pvars.end()) out.write(it->second);
    return true;
}

bool ScriptRuntime::f_setpvar(const ScriptArgs& a, ScriptOut&) {
    m_st.pvars[a.str(0)] = a.str(1);
    return true;
}

bool ScriptRuntime::f_greater(const ScriptArgs& a, ScriptOut& out) {
    if (a.num(0) > a.num(1)) out.write("1");
    return true;
}

bool ScriptRuntime::f_fileexists(const ScriptArgs& a, ScriptOut& out) {
    if (m_env.file_exists(resolve(a.str(0)))) out.write("1");
    return true;
}

// --- colours (Columns UI style-script semantics) ---------------------------
// $calculate_blend_target(colour) — black if mean(R,G,B) >= 128, else white.
bool ScriptRuntime::f_blend_target(const ScriptArgs& a, ScriptOut& out) {
    unsigned long c = parse_hex_colorref(a.str(0));
    int total = (c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff);
    out.write(hex_colorref((total >= 128 * 3) ? 0 : 0xffffffUL));
    return true;
}

// $offset_colour(colour_from, colour_to, amount) — shift colour_from toward colour_to.
bool ScriptRuntime::f_offset_colour(const ScriptArgs& a, ScriptOut& out) {
    unsigned long from = parse_hex_colorref(a.str(0));
    unsigned long to   = parse_hex_colorref(a.str(1));
    int amount = std::clamp(a.num(2), 0, 255);
    int fr = from & 0xff, fg = (from >> 8) & 0xff, fb = (from >> 16) & 0xff;
    int tr = to & 0xff,   tg = (to >> 8) & 0xff,   tb = (to >> 16) & 0xff;
    int rdiff = tr - fr, gdiff = tg - fg, bdiff = tb - fb;
    int totaldiff = abs(rdiff) + abs(gdiff) + abs(bdiff);
    auto shift = [&](int base, int diff) {
        int v = base + (totaldiff ? (diff * amount * 3 / totaldiff) : 0);
        return std::clamp(v, 0, 255);
    };
    unsigned long result = shift(fr, rdiff) | (shift(fg, gdiff) << 8) | (shift(fb, bdiff) << 16);
    out.write(hex_colorref(result));
    return true;
}

// --- window ----------------------------------------------------------------
// e.g. fooAvA's Background script:
// $ifequal($getpvar(Hidetitlebar),1,$windowstyle(hidetitlebar),$windowstyle(showtitlebar))
bool ScriptRuntime::f_windowstyle(const ScriptArgs& a, ScriptOut&) {
    const std::string& mode = a.str(0);
    if (mode.find("hidetitlebar") != std::string::npos) m_env.set_titlebar_visible(false);
    else if (mode.find("showtitlebar") != std::string::npos) m_env.set_titlebar_visible(true);
    return true;
}

bool ScriptRuntime::f_settray(const ScriptArgs& a, ScriptOut&) {
    m_env.set_tray(a.size() >= 1 ? a.str(0) : std::string("foobar2000"));
    return true;
}

// fooAvA: "fooAvA" while stopped/paused, else "%artist% - %title%". Re-run every paint; the
// platform skips unchanged text.
bool ScriptRuntime::f_settitle(const ScriptArgs& a, ScriptOut&) {
    m_env.set_title(a.str(0));
    return true;
}

bool ScriptRuntime::f_stub(const ScriptArgs&, ScriptOut&) { return true; }

// --- text ------------------------------------------------------------------
bool ScriptRuntime::f_font(const ScriptArgs& a, ScriptOut&) {
    // Literal text since the last flush is drawn with the font/colour in force when it was
    // written — not with whatever $font we're about to make current. A skin defers its title to
    // the tail of an $alignabs box ($font(16)$if(%_isplaying%,$upper(%title%),…)$char(10)
    // $font(12,..)$…): without the flush here, everything would render with the LAST font/colour.
    flush_text();
    // Every argument is itself a titleformat snippet here: face "$get(fontAVA)", size
    // "$get(fontAVAsize_3)", style "underline glow-$getpvar(colour) glowalpha-60".
    const std::string face = eval_arg(a.str(0));
    const int size = atoi(eval_arg(a.str(1)).c_str());
    const std::string style = a.size() >= 3 ? eval_arg(a.str(2)) : std::string();
    select_font(face, size, style);
    // fooAvA packs the text colour as a 4th $font arg ($font(face,size,style,r-g-b)) instead of a
    // separate $set_font_color call. Many draw sites pass it EMPTY to mean "keep the current
    // colour" (e.g. the bottom time/codec readout inherits the 240-240-240 set just before): empty
    // is "don't touch", not parse_rgb("") = black.
    if (a.size() >= 4) {
        const std::string col = eval_arg(a.str(3));
        if (!col.empty()) m_textcol = parse_rgb(col.c_str());
    }
    m_fontLog.push_back({ face, size, style, m_textcol });
    return true;
}

bool ScriptRuntime::f_textcolor(const ScriptArgs& a, ScriptOut&) {
    flush_text(); // pending literal text keeps the colour it was written with
    m_textcol = parse_rgb(a.str(0).c_str());
    return true;
}

// $alignabs(left,top,right,bottom,halign,valign) — box for the following literal text.
bool ScriptRuntime::f_alignabs(const ScriptArgs& a, ScriptOut&) {
    flush_text();
    m_alignRect = { a.num(0) + m_ox, a.num(1) + m_oy, a.num(2) + m_ox, a.num(3) + m_oy };
    // A box whose right/bottom sit at or before left/top is really (x, y, width, height) with 0
    // meaning "to the edge" — e.g. the panel titles' $alignabs(,31,0,14,center,middle).
    if (m_alignRect.right <= m_alignRect.left)
        m_alignRect.right = a.num(2) > 0 ? m_alignRect.left + a.num(2) : m_w + m_ox;
    if (m_alignRect.bottom <= m_alignRect.top)
        m_alignRect.bottom = a.num(3) > 0 ? m_alignRect.top + a.num(3) : m_h + m_oy;
    const std::string& ha = a.str(4);
    const std::string& va = a.str(5);
    unsigned f = gfx::kWordEllipsis;
    if (ha.find("center") != std::string::npos) f |= gfx::kAlignCenter;
    else if (ha.find("right") != std::string::npos) f |= gfx::kAlignRight;
    if (va.find("center") != std::string::npos) f |= gfx::kVCenter | gfx::kSingleLine;
    // A one-line-high box is a caption sized from $calcwidth(), which measures with a slightly
    // different font than we draw with — let it overflow instead of ellipsizing.
    if (m_alignRect.bottom - m_alignRect.top <= 24) { f &= ~gfx::kWordEllipsis; f |= gfx::kNoClip | gfx::kSingleLine; }
    m_alignFlags = f; m_aligned = true;
    return true;
}

bool ScriptRuntime::f_drawstring(const ScriptArgs& a, ScriptOut&) {
    flush_text();
    const std::string& text = a.str(0);
    gfx::Rect rc = mkrect(a.num(1), a.num(2), a.num(3), a.num(4));
    gfx::Color col = a.size() >= 6 ? parse_rgb(a.str(5).c_str()) : gfx::Color();
    unsigned fmt = text_opts(a.str(6));
    draw_glow(text, rc, fmt);
    m_cv.draw_text(text, rc, fmt, col);
    return true;
}

// Like $drawstring, but the colour comes from the ambient $set_font_color/$textcolor state
// (fooAvA always calls set_font_color right before).
bool ScriptRuntime::f_draw_text(const ScriptArgs& a, ScriptOut&) {
    flush_text();
    gfx::Rect rc = mkrect(a.num(1), a.num(2), a.num(3), a.num(4));
    m_cv.draw_text(a.str(0), rc, text_opts(a.str(5)), m_textcol);
    return true;
}

bool ScriptRuntime::f_calcwidth(const ScriptArgs& a, ScriptOut& out) {
    out.write_int(m_cv.text_width(a.str(0)));
    return true;
}

// --- shapes ----------------------------------------------------------------
bool ScriptRuntime::f_drawrect(const ScriptArgs& a, ScriptOut&) {
    const std::string& spec = a.str(4);
    gfx::Color brush, pen;
    bool hb = find_color(spec, "brushcolor", brush);
    bool hp = find_color(spec, "pencolor", pen);
    int alpha = 255;
    auto ap = spec.find("alpha-");
    if (ap != std::string::npos) alpha = atoi(spec.c_str() + ap + 6);
    gfx::Rect rc = mkrect(a.num(0), a.num(1), a.num(2), a.num(3));
    if (hb) {
        if (alpha < 255) m_cv.fill_rect_alpha(rc, brush, alpha);
        else m_cv.fill_rect(rc, brush);
    }
    if (hp) m_cv.frame_rect(rc, pen);
    return true;
}

bool ScriptRuntime::f_drawroundrect(const ScriptArgs& a, ScriptOut&) {
    m_cv.fill_round_rect(mkrect(a.num(0), a.num(1), a.num(2), a.num(3)), a.num(4), a.num(5),
                         parse_rgb(a.str(6).c_str()));
    return true;
}

bool ScriptRuntime::f_gradientrect(const ScriptArgs& a, ScriptOut&) {
    m_cv.gradient_v(mkrect(a.num(0), a.num(1), a.num(2), a.num(3)),
                    parse_rgb(a.str(4).c_str()), parse_rgb(a.str(5).c_str()));
    return true;
}

// GDI+ brush fills: $gp_set_brush(A-R-G-B) (or R-G-B, opaque), then $gp_fill_rectangle(x,y,w,h)
// — e.g. a translucent black veil, $gp_set_brush(85-0-0-0).
bool ScriptRuntime::f_gp_set_brush(const ScriptArgs& a, ScriptOut&) {
    parse_argb(a.str(0), m_gpBrush, m_gpAlpha);
    return true;
}

bool ScriptRuntime::f_gp_fill_rectangle(const ScriptArgs& a, ScriptOut&) {
    gfx::Rect rc = mkrect(a.num(0), a.num(1), a.num(2), a.num(3));
    if (m_gpAlpha >= 255) m_cv.fill_rect(rc, m_gpBrush);
    else if (m_gpAlpha > 0) m_cv.fill_rect_alpha(rc, m_gpBrush, m_gpAlpha);
    return true;
}

bool ScriptRuntime::f_gp_set_pen(const ScriptArgs& a, ScriptOut&) {
    parse_argb(a.str(0), m_gpPen, m_gpPenAlpha);
    m_gpPenWidth = a.size() >= 2 ? std::max(1, a.num(1)) : 1;
    return true;
}

// The pen centred on the rectangle's edges, like GDI+. A fully transparent pen draws nothing.
bool ScriptRuntime::f_gp_draw_rectangle(const ScriptArgs& a, ScriptOut&) {
    if (m_gpPenAlpha <= 0) return true;
    const int w = m_gpPenWidth, lo = w / 2;
    gfx::Rect r = mkrect(a.num(0), a.num(1), a.num(2), a.num(3));
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

// --- images ----------------------------------------------------------------
// $imageabs(x,y,w,h,path,align) / $draw_image(x,y,w,h,path,...)
bool ScriptRuntime::f_imageabs(const ScriptArgs& a, ScriptOut&) {
    m_env.draw_cover(m_cv, image_path(a.str(4)), a.num(0) + m_ox, a.num(1) + m_oy, a.num(2), a.num(3), 255, 0);
    return true;
}

bool ScriptRuntime::f_imageabs2(const ScriptArgs& a, ScriptOut& out) {
    // $button2's normal/hover states pass an $imageabs2 with all-zero args (0,0 sized and
    // positioned) as a deferred draw command. The core evaluates those args before the button
    // handler runs — the call would blit at the bare origin and be discarded, leaving the button
    // invisible. Emit such calls back as a replayable snippet; the button handler re-runs them at
    // its own coords (and with all args zero, the image blits at its natural size). Real draws
    // carry real sizes and/or an origin, so they take the painting path below.
    bool plainDummy = false;
    if (m_ox == 0 && m_oy == 0) {
        plainDummy = true;
        for (size_t i = 0; plainDummy && i < 8; ++i)
            if (a.num(i)) plainDummy = false;
    }
    if (plainDummy) {
        std::string s = "$imageabs2(";
        for (size_t i = 0; i < a.size(); ++i) {
            s += a.str(i);
            if (i + 1 < a.size()) s += ',';
        }
        s += ')';
        out.write(s);
        return true;
    }
    // $imageabs2(maxW,maxH,imgW,imgH,srcX,srcY,dstX,dstY,path,opts)
    int alpha = 255, flip = 0;
    if (a.size() >= 10) parse_img_opts(a.str(9), alpha, flip);
    // Arguments 2..5 are a crop of the (w,h)-scaled image — fooAvA's progress and volume bars draw
    // only the first N px of their fill graphic this way. Calls that ask for no crop take the
    // plain path below.
    //
    // Naming *both* of the first two arguments means "scale into that box" (wallpaper and cover
    // art, which all pass NOKEEPASPECT). Naming only one makes it a cap, never an upscale:
    // fooAvA's volume fill is $imageabs2(0,12,0,0,<level>,0,...) on the 56x3 v<n>.png bar, and 12
    // is the knob's height, not the bar's — taking it as a target stretched the fill 4x.
    const std::string ip = image_path(a.str(8));
    {
        int W = a.num(0), H = a.num(1), SX = a.num(2), SY = a.num(3), SW = a.num(4), SH = a.num(5);
        int iw = 0, ih = 0;
        if ((SX > 0 || SY > 0 || SW > 0 || SH > 0) && flip == 0 && m_env.image_size(ip, iw, ih)) {
            int scW, scH;
            if (W > 0 && H > 0) { scW = W; scH = H; }
            else { scW = W > 0 ? std::min(W, iw) : iw; scH = H > 0 ? std::min(H, ih) : ih; }
            int cw = SW > 0 ? std::min(SW, scW - SX) : scW - SX;
            int ch = SH > 0 ? std::min(SH, scH - SY) : scH - SY;
            if (cw <= 0 || ch <= 0) return true;
            // A crop that happens to cover the whole (uncapped) image still goes through here
            // rather than the plain path, which would re-apply the unclamped H — that is the
            // full-volume case, where the fill is the bar's entire 56 px.
            m_env.draw_image_part(m_cv, ip, a.num(6) + m_ox, a.num(7) + m_oy, cw, ch,
                                  (float)SX * iw / scW, (float)SY * ih / scH,
                                  (float)cw * iw / scW, (float)ch * ih / scH, alpha);
            return true;
        }
    }
    m_env.draw_cover(m_cv, ip, a.num(6) + m_ox, a.num(7) + m_oy, a.num(0), a.num(1), alpha, flip);
    return true;
}

// --- controls --------------------------------------------------------------
// $button (x,y,?,?,w,h,imgpath_normal,imgpath_hover,'action',tooltip)
// $button2(x,y,?,?,w,h,draw_normal,draw_hover,'action',tooltip)
// For button2 the state is a draw command (possibly '-quoted) run at the button origin; for
// button an image path. Whichever state is active (hover if the mouse is over the hit box, else
// normal) is rendered; the other is skipped so the two don't stack.
bool ScriptRuntime::f_button(const ScriptArgs& a, ScriptOut&) {
    int x = a.num(0), y = a.num(1);
    int rawW = a.num(4), rawH = a.num(5);
    int hw = rawW > 0 ? rawW : 22, hh = rawH > 0 ? rawH : 22; // hit box
    if (rawW <= 0 || rawH <= 0) {
        // w/h omitted: the hit box is the normal image's own size (e.g. the 55px-wide COVER FLOW
        // pill), not a fixed 22x22 that only covers its left edge.
        const std::string ip = trim_ws(a.str(6));
        int iw = 0, ih = 0;
        if (!ip.empty() && ip[0] != '$' && ip[0] != '\'' && is_image_file(ip) && m_env.image_size(resolve(ip), iw, ih)) {
            if (rawW <= 0) hw = iw;
            if (rawH <= 0) hh = ih;
        }
    }
    const bool hovered = m_hoverX >= x && m_hoverX < x + hw && m_hoverY >= y && m_hoverY < y + hh;
    // arg 6 = normal image, arg 7 = themed one. Trimmed/unquoted once, since the themed one is
    // also needed *after* the run, to relight a selected button.
    auto state = [&](size_t i) {
        std::string s = trim_ws(a.str(i));
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
        return s;
    };
    const std::string dHover = state(7), d1 = state(hovered ? 7 : 6);
    bool drew = false;
    if (!d1.empty()) {
        if (d1[0] == '$') { run_subscript(d1, x, y); drew = true; } // draw command
        else if (is_image_file(d1))
            drew = m_env.draw_image(m_cv, image_path(d1), x, y, rawW > 0 ? rawW : 0, rawH > 0 ? rawH : 0, 255, 0);
    }
    // A text button whose normal state is "$font(...)Label": the argument pre-eval ran the $font
    // and left the bare label behind, or the label trails the $font(...) chunk of a kept snippet.
    // Neither run above drew it, so render the label centred in the hit box — $textbutton
    // ("EQ"/"INFO", playlist pvar pills) and $button2-with-font in fooAvA rely on this.
    if (!drew) {
        std::string lit;
        if (!d1.empty() && d1[0] == '$') lit = trailing_literal(d1);
        else if (!d1.empty() && d1[0] != '\'' && !is_image_file(d1)) lit = d1;
        if (!lit.empty()) {
            // titleformat evaluates every argument before calling us, so both states' $font(...)
            // have already run and the HOVER one (arg 7) is current — every text button would draw
            // hover-styled. When both states are text, the last two $font calls since the previous
            // button are normal/hover: re-apply the one for this state.
            const bool hoverText = !dHover.empty() && dHover[0] != '\'' && !is_image_file(dHover);
            if (hoverText && m_fontLog.size() >= m_fontLogMark + 2) {
                const FontState fs = m_fontLog[m_fontLog.size() - (hovered ? 1 : 2)];
                select_font(fs.face, fs.size, fs.style);
                m_textcol = fs.col;
            }
            gfx::Rect r = mkrect(x, y, hw, hh);
            unsigned fmt = gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine | gfx::kNoClip;
            draw_glow(lit, r, fmt);
            m_cv.draw_text(lit, r, fmt, m_textcol);
            drew = true;
        }
    }
    // No faint-frame fallback: a button whose image is missing stays invisible (still clickable)
    // instead of showing an empty border.
    std::string act = clean_action(a.str(8));
    if (!act.empty()) {
        Button btn = make_button(x, y, hw, hh, act, tooltip_arg(a, 9));
        // Selected-state candidates only: the themed image can be re-blitted after the run, but a
        // nested draw command (button2 running at an offset) cannot.
        if (m_ox == 0 && m_oy == 0) {
            const std::string kTag = "PVAR:SET:";
            if (act.compare(0, kTag.size(), kTag) == 0) {
                std::string rest = act.substr(kTag.size());
                size_t c = rest.find(':');
                if (c != std::string::npos) { btn.pvarKey = rest.substr(0, c); btn.pvarValue = rest.substr(c + 1); }
            }
            if (!dHover.empty() && dHover[0] != '$' && is_image_file(dHover)) {
                btn.litImage = dHover; btn.litW = rawW; btn.litH = rawH;
            }
        }
        button_list().push_back(btn);
    }
    m_fontLogMark = m_fontLog.size();
    return true;
}

// $textbutton(left,top,width,height,str_normal,str_hover,action,"TOOLTIP",tip) — draws the
// (already-evaluated) normal text into the box and records a clickable region.
bool ScriptRuntime::f_textbutton(const ScriptArgs& a, ScriptOut&) {
    int x = a.num(0), y = a.num(1), w = a.num(2) > 0 ? a.num(2) : 240, h = a.num(3) > 0 ? a.num(3) : 18;
    m_cv.draw_text(a.str(4), mkrect(x, y, w, h), gfx::kAlignCenter | gfx::kSingleLine | gfx::kVCenter | gfx::kNoClip, m_textcol);
    if (a.size() >= 7) {
        std::string act = clean_action(a.str(6));
        if (!act.empty()) button_list().push_back(make_button(x, y, w, h, act, tooltip_arg(a, 7)));
    }
    return true;
}

// $imagebutton(left,top,image_normal,image_hover,action,"TOOLTIP",tip) — rating stars etc.
bool ScriptRuntime::f_imagebutton(const ScriptArgs& a, ScriptOut&) {
    int x = a.num(0), y = a.num(1);
    const int hw = 11, hh = 15; // ~star-sized hit box
    const bool hovered = m_hoverX >= x && m_hoverX < x + hw && m_hoverY >= y && m_hoverY < y + hh;
    m_env.draw_image(m_cv, image_path(a.str(hovered ? 3 : 2)), x, y, 0, 0, 255, 0); // natural size
    std::string act = clean_action(a.str(4));
    if (!act.empty()) button_list().push_back(make_button(x, y, hw, hh, act, tooltip_arg(a, 5)));
    return true;
}

void ScriptRuntime::run_subscript(const std::string& snippet, int ox, int oy) {
    int sx = m_ox, sy = m_oy;
    const std::string* sbuf = m_buf; size_t sfrom = m_flushFrom;
    m_ox = ox; m_oy = oy;
    m_env.run_subscript(*this, snippet);
    m_buf = sbuf; m_flushFrom = sfrom;
    m_ox = sx; m_oy = sy;
}

// Relight the selected member of every pvar radio group in `list`: themed image over the normal
// one the run already drew, plus a 1px outline in the theme accent (with a faint halo, so it reads
// as "lit" rather than as a drawn box).
//
// A group is detected structurally: a pvar that 2+ buttons in THIS frame set to 2+ *different*
// values. fooAvA's panel tabs qualify (showPanel:1..5, showPane:1..4) and so do the settings
// popup's radio rows. Its toggles only ever draw ONE branch per frame, so they stay a group of one
// and are left alone; without this guard the INFO/search/mode buttons would sit permanently lit.
void ScriptRuntime::apply_selected_buttons(std::vector<Button>& list, bool haveAccent, gfx::Color accent) {
    std::map<std::string, std::vector<Button*>> groups;
    for (auto& b : list)
        if (!b.pvarKey.empty()) groups[b.pvarKey].push_back(&b);
    for (auto& [key, members] : groups) {
        if (members.size() < 2) continue;
        bool distinct = false;
        for (size_t i = 1; i < members.size(); ++i)
            if (members[i]->pvarValue != members[0]->pvarValue) { distinct = true; break; }
        if (!distinct) continue;
        auto it = m_st.pvars.find(key);
        if (it == m_st.pvars.end() || it->second.empty()) continue;
        for (Button* b : members)
            if (b->pvarValue == it->second) { b->selected = true; break; }
    }
    if (!haveAccent) return; // no theme colour to outline with
    for (auto& b : list) {
        if (!b.selected) continue;
        if (!b.litImage.empty())
            m_env.draw_image(m_cv, resolve(b.litImage), b.x, b.y, b.litW, b.litH, 255, 0);
        gfx::Rect r = mkrect(b.x, b.y, b.w, b.h);
        const int w = r.w, h = r.h;
        if (w <= 0 || h <= 0) continue;
        // Soft halo one step out, so the 1px contour reads as "lit" rather than as a hard box.
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.y - 1, w + 2, 1), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.bottom(), w + 2, 1), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.x - 1, r.y, 1, h), accent, 90);
        m_cv.fill_rect_alpha(mkrect(r.right(), r.y, 1, h), accent, 90);
        m_cv.frame_rect(r, accent);
    }
}

// --- draw state ------------------------------------------------------------
void ScriptRuntime::select_font(const std::string& faceIn, int size, const std::string& style) {
    // A skin can name its face through a variable ($font($get(fontAVA),...)) whose value it set
    // with $setpvar: `face` then arrives empty. The config's `font.face_pvar` names that pvar, so
    // the real face is recovered before falling back to a generic one; then the Preferences-page
    // override (reserved pvar "_prefs_font_face"), then `font.default`.
    std::string face = trim_ws(faceIn);
    auto pvar = [&](const std::string& k) {
        auto it = k.empty() ? m_st.pvars.end() : m_st.pvars.find(k);
        return it != m_st.pvars.end() ? it->second : std::string();
    };
    if (face.empty()) face = pvar(m_st.cfg.str("font.face_pvar"));
    if (face.empty()) face = pvar("_prefs_font_face");
    if (face.empty()) face = m_st.cfg.str("font.default", "Tahoma");
    if (size <= 0) size = atoi(pvar("_prefs_font_size").c_str());
    // Style is a space-separated list; match whole tokens only (`find('b')` also hits "glow").
    bool bold = false, italic = false, underline = false, hasGlow = false;
    gfx::Color glow;
    int expand = 2, alpha = 255; // Panels UI defaults; `glowexpand-0` means no glow at all
    for (size_t i = 0; i < style.size();) {
        while (i < style.size() && (style[i] == ' ' || style[i] == '\t')) ++i;
        size_t j = i;
        while (j < style.size() && style[j] != ' ' && style[j] != '\t') ++j;
        const std::string tok = style.substr(i, j - i);
        i = j;
        if (tok == "bold") bold = true;
        else if (tok == "italic") italic = true;
        else if (tok == "underline") underline = true;
        else if (tok.compare(0, 5, "glow-") == 0 && tok.size() > 5) {
            // the skin also writes this with no space: "glow-114-114-114glowexpand-0"
            hasGlow = true;
            glow = parse_rgb(tok.c_str() + 5);
            size_t tail = tok.find_first_of("gG", 5);
            if (tail != std::string::npos) parse_glow_tail(tok.substr(tail), expand, alpha);
        } else if (tok.compare(0, 11, "glowexpand-") == 0) expand = atoi(tok.c_str() + 11);
        else if (tok.compare(0, 10, "glowalpha-") == 0) alpha = atoi(tok.c_str() + 10);
    }
    m_glowCol = glow;
    m_glowExpand = hasGlow ? expand : 0;
    m_glowAlpha = hasGlow ? alpha : 0;
    gfx::FontSpec spec;
    spec.face = face; spec.size = (float)(size > 0 ? size : 9); spec.points = true;
    spec.bold = bold; spec.italic = italic; spec.underline = underline;
    // The platform silently swaps in an arbitrary default for a face it can't find: if it isn't
    // what was asked for, retry with the closest face we do have.
    if (!m_cv.set_font(spec)) {
        std::string alias = face_alias(m_st.cfg, face);
        if (!alias.empty()) { spec.face = alias; m_cv.set_font(spec); }
    }
    m_haveFont = true;
}

// Panels UI $font style glow: "glow-r-g-b [glowexpand-N] [glowalpha-N]" — a soft halo of the
// glyphs. Built from the SAME text layout the text is drawn with (the canvas renders the glyph
// coverage into a mask), dilated by `expand` px, faded outward, and alpha-blended under the text.
void ScriptRuntime::draw_glow(const std::string& s, const gfx::Rect& r, unsigned fmt) {
    if (!m_glowAlpha || m_glowExpand <= 0 || s.empty() || !m_haveFont) return;
    const int e = m_glowExpand > 6 ? 6 : m_glowExpand;
    const int bw = m_cv.width(), bh = m_cv.height();
    gfx::Rect box = r;
    if (fmt & gfx::kNoClip) {
        // Text may spill past its box: grow it by the measured overflow on both sides. Not the
        // whole canvas — the dilation below is O(area * radius²) and runs per glowing label.
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
    int mx0 = W, my0 = H, mx1 = -1, my1 = -1;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        if (mask[(size_t)y * W + x]) { mx0 = std::min(mx0, x); my0 = std::min(my0, y); mx1 = std::max(mx1, x); my1 = std::max(my1, y); }
    }
    if (mx1 < 0) return;
    mx0 = std::max(0, mx0 - e); my0 = std::max(0, my0 - e);
    mx1 = std::min(W - 1, mx1 + e); my1 = std::min(H - 1, my1 + e);

    // Dilate: each pixel takes the strongest nearby glyph coverage, weighted so the ring hugging
    // the glyphs is strongest and the outermost ring faintest.
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
        int al = best * m_glowAlpha / 255;
        if (al > 0) res[(size_t)y * W + x] = ((uint32_t)al << 24) | ((uint32_t)(cr * al / 255) << 16) |
                                             ((uint32_t)(cg * al / 255) << 8) | (uint32_t)(cb * al / 255);
    }
    m_cv.blend_argb(gfx::Rect{ x0 + mx0, y0 + my0, mx1 - mx0 + 1, my1 - my0 + 1 },
                    res.data() + (size_t)my0 * W + mx0, W);
}

void ScriptRuntime::flush_text() {
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

} // namespace pui
