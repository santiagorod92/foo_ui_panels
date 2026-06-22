#include "skin_engine.h"
#include "image.h"
#include <cstdlib>

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
// Find "key-r-g-b" inside spec (e.g. brushcolor-..., pencolor-...).
static bool find_color(const std::string& spec, const char* key, COLORREF& out) {
    auto p = spec.find(key);
    if (p == std::string::npos) return false;
    out = parse_rgb(spec.c_str() + p + strlen(key));
    return true;
}

// --- $eval integer expression evaluator ------------------------------------
namespace {
struct Expr {
    const char* s;
    void skip() { while (*s == ' ' || *s == '{' || *s == '}') ++s; }
    long number() {
        skip();
        if (*s == '(') { ++s; long v = expr(); skip(); if (*s == ')') ++s; return v; }
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
    if (has("Lyric"))             return "OpenLyrics";
    if (has("Playlist switcher")) return "Playlist Tabs";
    if (has("Peakmeter") || has("Peak"))  return "Peak Meter";
    if (has("Album list") || has("Graphical Browser")) return "Album List";
    if (has("Album Art"))         return "Album Art";
    if (has("Track Display") || has("Seek") || has("Volume") || has("Chronflow") || has("Quick Search"))
        return nullptr; // native, not implemented yet
    return t.c_str();
}

// --- titleformat hook: layout + immediate GDI drawing ----------------------
class SkinHook : public titleformat_hook {
public:
    SkinHook(SkinEngine* e, HDC dc, int w, int h)
        : m_e(e), m_dc(dc), m_w(w), m_h(h) {
        SetBkMode(m_dc, TRANSPARENT);
    }
    ~SkinHook() {
        if (m_font) { SelectObject(m_dc, m_oldFont); DeleteObject(m_font); }
    }

    bool process_field(titleformat_text_out* out, const char* name, t_size len, bool& found) override {
        found = true;
        if (eq(name, len, "_width")  || eq(name, len, "el_width"))  { out->write_int(titleformat_inputtypes::unknown, m_w); return true; }
        if (eq(name, len, "_height") || eq(name, len, "el_height")) { out->write_int(titleformat_inputtypes::unknown, m_h); return true; }
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
            m_e->m_placements.push_back({ param_str(p,0), param_str(p,1),
                param_int(p,2), param_int(p,3), param_int(p,4), param_int(p,5) });
            return true;
        }
        if (eq(name, len, "eval") && argc >= 1) {
            out->write_int(titleformat_inputtypes::unknown, eval_expr(param_str(p, 0))); return true;
        }
        if (eq(name, len, "getpvar") && argc >= 1) {
            auto it = m_e->m_pvars.find(param_str(p, 0));
            if (it != m_e->m_pvars.end()) out->write(titleformat_inputtypes::unknown, it->second.c_str(), it->second.size());
            return true;
        }
        if (eq(name, len, "setpvar") && argc >= 2) {
            m_e->m_pvars[param_str(p, 0)] = param_str(p, 1); return true;
        }

        // --- drawing ---
        if (eq(name, len, "font") && argc >= 2) {
            select_font(param_str(p, 0).c_str(), param_int(p, 1),
                        argc >= 3 ? param_str(p, 2) : std::string()); return true;
        }
        if (eq(name, len, "drawrect") && argc >= 5) {
            std::string spec = param_str(p, 4);
            COLORREF brush = RGB(0,0,0), pen; bool hb = find_color(spec, "brushcolor", brush);
            bool hp = find_color(spec, "pencolor", pen);
            RECT rc = mkrect(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3));
            if (hb) { HBRUSH b = CreateSolidBrush(brush); FillRect(m_dc, &rc, b); DeleteObject(b); }
            if (hp) { HBRUSH b = CreateSolidBrush(pen); FrameRect(m_dc, &rc, b); DeleteObject(b); }
            return true;
        }
        if (eq(name, len, "drawroundrect") && argc >= 7) {
            int x = param_int(p,0), y = param_int(p,1), w = param_int(p,2), h = param_int(p,3);
            int aw = param_int(p,4), ah = param_int(p,5);
            COLORREF c = parse_rgb(param_str(p,6).c_str());
            HBRUSH b = CreateSolidBrush(c); HPEN pen = CreatePen(PS_SOLID, 1, c);
            HGDIOBJ ob = SelectObject(m_dc, b), op = SelectObject(m_dc, pen);
            RoundRect(m_dc, x, y, x + w, y + h, aw, ah);
            SelectObject(m_dc, ob); SelectObject(m_dc, op); DeleteObject(b); DeleteObject(pen);
            return true;
        }
        if (eq(name, len, "gradientrect") && argc >= 6) {
            draw_gradient(param_int(p,0), param_int(p,1), param_int(p,2), param_int(p,3),
                          parse_rgb(param_str(p,4).c_str()), parse_rgb(param_str(p,5).c_str()));
            return true;
        }
        if (eq(name, len, "drawstring") && argc >= 5) {
            draw_string(p, argc); return true;
        }
        if (eq(name, len, "imageabs") && argc >= 5) {
            // $imageabs(x,y,w,h,path,align)
            draw_image(m_dc, resolve(param_str(p,4)), param_int(p,0), param_int(p,1),
                       param_int(p,2), param_int(p,3));
            return true;
        }
        if (eq(name, len, "imageabs2") && argc >= 9) {
            // $imageabs2(maxW,maxH,imgW,imgH,srcX,srcY,dstX,dstY,path,opts)
            int alpha = 255;
            if (argc >= 10) { std::string o = param_str(p,9);
                auto a = o.find("alpha-"); if (a != std::string::npos) alpha = atoi(o.c_str() + a + 6); }
            draw_image(m_dc, resolve(param_str(p,8)), param_int(p,6), param_int(p,7),
                       param_int(p,0), param_int(p,1), alpha);
            return true;
        }

        // accepted-but-not-yet-rendered functions
        static const char* stubs[] = { "draw_text","draw_image","set_font_color",
            "textcolor","offset_colour","calculate_blend_target","alignabs","calcwidth","scplsetlayout",
            "button","button2","imagebutton","textbutton","windowstyle","gp_set_brush","gp_set_pen",
            "gp_fill_rectangle" };
        for (auto s : stubs) if (eq(name, len, s)) return true;

        found = false; return false;
    }

private:
    RECT mkrect(int x, int y, int w, int h) { RECT r = { x, y, x + w, y + h }; return r; }

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
        int h = -MulDiv(size > 0 ? size : 9, GetDeviceCaps(m_dc, LOGPIXELSY), 72);
        int weight = (style.find('b') != std::string::npos) ? FW_BOLD : FW_NORMAL;
        BYTE italic = (style.find('i') != std::string::npos) ? TRUE : FALSE;
        HFONT f = CreateFontA(h, 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        if (!f) return;
        HGDIOBJ old = SelectObject(m_dc, f);
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
        DrawTextA(m_dc, text.c_str(), (int)text.size(), &rc, fmt);
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
    HFONT m_font = nullptr; HGDIOBJ m_oldFont = nullptr;
};

// --- SkinEngine ------------------------------------------------------------
bool SkinEngine::load(const char* script) {
    if (!titleformat_compiler::get()->compile(m_script, script)) {
        console::print("Panels UI: skin script failed to compile");
        return false;
    }
    return true;
}

void SkinEngine::render(HDC dc, int width, int height) {
    if (m_script.is_empty() || !m_parent) return;

    m_placements.clear();
    { SkinHook hook(this, dc, width, height);
      pfc::string8 dump;
      m_script->run(&hook, dump, nullptr); }

    for (const auto& p : m_placements) {
        const char* dui = map_type(p.type);
        if (!dui) continue;
        auto& host = m_hosts[p.name];
        if (!host) { host = std::make_unique<PanelHost>(); host->create(m_parent, dui); }
        if (HWND w = host->wnd()) MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
    }
}

} // namespace pui
