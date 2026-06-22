#include "skin_engine.h"
#include <cstdlib>

namespace pui {

// --- name helpers ----------------------------------------------------------
static bool eq(const char* name, size_t len, const char* lit) {
    return strlen(lit) == len && memcmp(name, lit, len) == 0;
}
static std::string param_str(titleformat_hook_function_params* p, size_t i) {
    const char* s = nullptr; size_t n = 0;
    p->get_param(i, s, n);
    return std::string(s ? s : "", s ? n : 0);
}

// --- tiny integer expression evaluator for $eval (+ - * /, parens, braces) --
namespace {
struct Expr {
    const char* s;
    void skip() { while (*s == ' ' || *s == '{' || *s == '}') ++s; }
    long number() {
        skip();
        if (*s == '(') { ++s; long v = expr(); skip(); if (*s == ')') ++s; return v; }
        long sign = 1;
        if (*s == '-') { sign = -1; ++s; }
        long v = 0; bool any = false;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; any = true; }
        (void)any;
        return sign * v;
    }
    long term() {
        long v = number();
        for (;;) { skip();
            if (*s == '*') { ++s; v *= number(); }
            else if (*s == '/') { ++s; long d = number(); v = d ? v / d : 0; }
            else break;
        }
        return v;
    }
    long expr() {
        long v = term();
        for (;;) { skip();
            if (*s == '+') { ++s; v += term(); }
            else if (*s == '-') { ++s; v -= term(); }
            else break;
        }
        return v;
    }
};
long eval_expr(const std::string& in) { Expr e{ in.c_str() }; return e.expr(); }
}

// --- legacy panel type -> DUI element search string ------------------------
static const char* map_type(const std::string& t) {
    auto has = [&](const char* k) { return t.find(k) != std::string::npos; };
    if (has("Channel spectrum") || t == "Spectrum")      return "Spectrum";
    if (has("Single Column Playlist") || has("ELPlaylist")) return "Playlist View";
    if (has("Lyric"))            return "OpenLyrics";
    if (has("Playlist switcher"))return "Playlist Tabs";
    if (has("Peakmeter") || has("Peak"))  return "Peak Meter";
    if (has("Album list") || has("Graphical Browser")) return "Album List";
    if (has("Album Art"))        return "Album Art";
    // Native-only legacy types we have no element for yet: skip (don't host garbage).
    if (has("Track Display") || has("Seek") || has("Volume") || has("Chronflow") || has("Quick Search"))
        return nullptr;
    // Otherwise assume it's already a DUI element name and pass it through to PanelHost.
    return t.c_str();
}

// --- the titleformat hook --------------------------------------------------
class SkinHook : public titleformat_hook {
public:
    SkinHook(SkinEngine* e, int w, int h) : m_e(e), m_w(w), m_h(h) {}

    bool process_field(titleformat_text_out* out, const char* name, t_size len, bool& found) override {
        if (eq(name, len, "_width"))  { out->write_int(titleformat_inputtypes::unknown, m_w); found = true; return true; }
        if (eq(name, len, "_height")) { out->write_int(titleformat_inputtypes::unknown, m_h); found = true; return true; }
        found = false; return false;
    }

    bool process_function(titleformat_text_out* out, const char* name, t_size len,
                          titleformat_hook_function_params* params, bool& found) override {
        const t_size argc = params->get_param_count();
        found = true;

        if (eq(name, len, "panel")) {
            // $panel(name, type, x, y, w, h [, ...])
            if (argc >= 6) {
                Placement p;
                p.name = param_str(params, 0);
                p.type = param_str(params, 1);
                p.x = atoi(param_str(params, 2).c_str());
                p.y = atoi(param_str(params, 3).c_str());
                p.w = atoi(param_str(params, 4).c_str());
                p.h = atoi(param_str(params, 5).c_str());
                m_e->m_placements.push_back(std::move(p));
            }
            return true;
        }
        if (eq(name, len, "eval") && argc >= 1) {
            out->write_int(titleformat_inputtypes::unknown, eval_expr(param_str(params, 0)));
            return true;
        }
        if (eq(name, len, "getpvar") && argc >= 1) {
            auto it = m_e->m_pvars.find(param_str(params, 0));
            if (it != m_e->m_pvars.end())
                out->write(titleformat_inputtypes::unknown, it->second.c_str(), it->second.size());
            return true;
        }
        if (eq(name, len, "setpvar") && argc >= 2) {
            m_e->m_pvars[param_str(params, 0)] = param_str(params, 1);
            return true;
        }
        // Drawing / control functions: accepted but not yet rendered.
        static const char* stubs[] = { "drawrect","drawroundrect","drawstring","draw_text","draw_image",
            "imageabs","imageabs2","gradientrect","font","set_font_color","textcolor","offset_colour",
            "calculate_blend_target","alignabs","calcwidth","scplsetlayout","button","button2",
            "imagebutton","textbutton","windowstyle","gp_set_brush","gp_set_pen","gp_fill_rectangle" };
        for (auto s : stubs) if (eq(name, len, s)) return true;

        found = false; return false; // let titleformat handle built-ins it knows
    }

private:
    SkinEngine* m_e; int m_w, m_h;
};

// --- SkinEngine ------------------------------------------------------------
bool SkinEngine::load(const char* script) {
    if (!titleformat_compiler::get()->compile(m_script, script)) {
        console::print("Panels UI: skin script failed to compile");
        return false;
    }
    return true;
}

void SkinEngine::layout(int width, int height) {
    if (m_script.is_empty() || !m_parent) return;

    m_placements.clear();
    SkinHook hook(this, width, height);
    pfc::string8 dump;
    m_script->run(&hook, dump, nullptr);

    // Create/position a hosted panel per placement.
    for (const auto& p : m_placements) {
        const char* dui = map_type(p.type);
        if (!dui) continue; // native panel types not implemented yet
        auto& host = m_hosts[p.name];
        if (!host) {
            host = std::make_unique<PanelHost>();
            host->create(m_parent, dui);
        }
        if (HWND w = host->wnd())
            MoveWindow(w, p.x, p.y, p.w, p.h, TRUE);
    }
}

} // namespace pui
