#include "skin_engine.h"
#include "image.h"
#include "track_display.h"
#include "seekbar.h"
#include "volume.h"
#include "../sdk/foobar2000/SDK/cfg_var.h"
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
        if (eq(name, len, "draw_image") && argc >= 5) {
            // $draw_image(x,y,w,h,path,...)
            draw_image(m_dc, resolve(param_str(p,4)), param_int(p,0), param_int(p,1),
                       param_int(p,2), param_int(p,3));
            return true;
        }
        if (eq(name, len, "fileexists") && argc >= 1) {
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
            // $imageabs2(maxW,maxH,imgW,imgH,srcX,srcY,dstX,dstY,path,opts)
            int alpha = 255, flip = 0;
            if (argc >= 10) parse_img_opts(param_str(p,9), alpha, flip);
            draw_image(m_dc, resolve(param_str(p,8)), param_int(p,6), param_int(p,7),
                       param_int(p,0), param_int(p,1), alpha, flip);
            return true;
        }

        if ((eq(name, len, "button") || eq(name, len, "button2")) && argc >= 9) {
            // $button(x,y,?,?,w,h,img1,img2,'action',tooltip)
            // $button2(x,y,?,?,w,h,draw1,draw2,'action',tooltip) — draws happen during arg eval
            int x = param_int(p,0), y = param_int(p,1), w = param_int(p,4), h = param_int(p,5);
            if (w <= 0) w = 22; if (h <= 0) h = 22;
            bool drew = eq(name, len, "button") && draw_image(m_dc, resolve(param_str(p,6)), x, y, w, h);
            if (!drew) { // icon PNG missing -> faint clickable marker so the button is visible
                RECT r = mkrect(x, y, w, h);
                HBRUSH b = CreateSolidBrush(RGB(70, 80, 110)); FrameRect(m_dc, &r, b); DeleteObject(b);
            }
            std::string act = clean_action(param_str(p,8));
            if (!act.empty()) m_e->m_buttons.push_back({ x, y, w, h, act });
            return true;
        }

        // accepted-but-not-yet-rendered functions
        static const char* stubs[] = { "draw_text","set_font_color",
            "textcolor","offset_colour","calculate_blend_target","alignabs","calcwidth","scplsetlayout",
            "imagebutton","textbutton","windowstyle","gp_set_brush","gp_set_pen",
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

// Persistent pvar store (serialized "key=value" lines).
namespace {
// {1B5C9A40-7E2D-4C8A-9F31-6A0B2D4E8C70}
const GUID g_pvars_guid =
    { 0x1b5c9a40, 0x7e2d, 0x4c8a, { 0x9f, 0x31, 0x6a, 0x0b, 0x2d, 0x4e, 0x8c, 0x70 } };
cfg_var_modern::cfg_string g_pvars_cfg(g_pvars_guid, "");
}

void SkinEngine::load_pvars() {
    pfc::string8 data = g_pvars_cfg.get();
    m_pvars.clear();
    std::string s(data.get_ptr(), data.length());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(pos, nl - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) m_pvars[line.substr(0, eq)] = line.substr(eq + 1);
        pos = nl + 1;
    }
}

void SkinEngine::save_pvars() {
    std::string out;
    for (auto& kv : m_pvars) { out += kv.first; out += '='; out += kv.second; out += '\n'; }
    g_pvars_cfg.set(out.c_str());
}

void SkinEngine::render(HDC dc, int width, int height) {
    if (m_script.is_empty() || !m_parent) return;
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }

    m_placements.clear();
    m_buttons.clear();
    pfc::string8 dump;
    { SkinHook hook(this, dc, width, height);
      m_script->run(&hook, dump, nullptr); }

    static bool logged = false;
    if (!logged) {
        logged = true;
        console::printf("Panels UI: render %dx%d -> %u placements, out len=%u",
                        width, height, (unsigned)m_placements.size(), (unsigned)dump.length());
        for (size_t i = 0; i < m_placements.size() && i < 6; ++i) {
            auto& p = m_placements[i];
            console::printf("  [%u] '%s' type='%s' %d,%d,%d,%d",
                (unsigned)i, p.name.c_str(), p.type.c_str(), p.x, p.y, p.w, p.h);
        }
    }

    for (const auto& p : m_placements) {
        // Native panels (no DUI equivalent) get our own window.
        if (p.type.find("Track Display") != std::string::npos) {
            auto& td = m_track_displays[p.name];
            if (!td) {
                td = std::make_unique<TrackDisplay>();
                td->create(m_parent, this);
                std::string sc = read_panel_script(p.name); // real fooAvA per-panel script
                if (!sc.empty()) td->set_script(sc.c_str());
            }
            if (HWND w = td->wnd()) MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
            continue;
        }
        if (p.type.find("Seek") != std::string::npos) {
            auto& sb = m_seekbars[p.name];
            if (!sb) { sb = std::make_unique<Seekbar>(); sb->create(m_parent); }
            if (HWND w = sb->wnd()) MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
            continue;
        }
        if (p.type.find("Volume") != std::string::npos) {
            auto& vol = m_volumes[p.name];
            if (!vol) { vol = std::make_unique<Volume>(); vol->create(m_parent); }
            if (HWND w = vol->wnd()) MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
            continue;
        }
        const char* dui = map_type(p.type);
        if (!dui) continue;
        auto& host = m_hosts[p.name];
        if (!host) { host = std::make_unique<PanelHost>(); host->create(m_parent, dui); }
        if (HWND w = host->wnd()) MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
    }
}

std::string SkinEngine::read_panel_script(const std::string& name) {
    if (m_base.empty()) return {};
    std::string path = m_base + "/panels/" + name + ".txt";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
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

bool SkinEngine::handle_click(int x, int y) {
    for (const auto& b : m_buttons) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
            return run_action(b.action);
    }
    return false;
}

void SkinEngine::draw_script(HDC dc, int w, int h,
                             const service_ptr_t<titleformat_object>& script,
                             const metadb_handle_ptr& track) {
    if (script.is_empty()) return;
    SkinHook hook(this, dc, w, h);
    pfc::string8 dump;
    // Use playback formatting so dynamic fields (%playback_time%, %isplaying%…) resolve.
    if (track.is_valid())
        playback_control::get()->playback_format_title(
            &hook, dump, script, nullptr, playback_control::display_level_all);
    else
        script->run(&hook, dump, nullptr);
}

} // namespace pui
