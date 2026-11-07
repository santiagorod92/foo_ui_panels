#include "panels_ui_api.h"
#include "skin_engine.h"

namespace pui {

namespace {

class panels_ui_api_impl : public panels_ui::panels_ui_api_v2 {
public:
    bool is_active() override { return !SkinEngine::live().empty(); }

    bool get_variable(const char* name, pfc::string_base& out) override {
        const auto& live = SkinEngine::live();
        if (live.empty() || !name) return false;
        const std::string v = live.front()->get_pvar(name);
        out = v.c_str();
        return !v.empty();
    }

    void set_variable(const char* name, const char* value) override {
        if (!name || !*name) return;
        const std::string v = value ? value : "";
        for (SkinEngine* e : SkinEngine::live()) {
            if (e->get_pvar(name) == v) continue;
            e->set_pvar(name, v);
            e->repaint_all();
        }
    }

    bool run_action(const char* action) override {
        const auto& live = SkinEngine::live();
        if (!action || live.empty()) return false;
        const std::string a = action;
        if (!replaces_panels(a)) return live.front()->run_actions(a);
        fb2k::inMainThread([a] {
            const auto& now = SkinEngine::live();
            if (!now.empty()) now.front()->run_actions(a);
        });
        return true;
    }

    bool panel_name(const void* native_view, pfc::string_base& out) override {
        for (SkinEngine* e : SkinEngine::live()) {
            const std::string n = e->panel_name(native_view);
            if (!n.empty()) { out = n.c_str(); return true; }
        }
        return false;
    }

    bool get_panel_rect(const char* name, int& x, int& y, int& w, int& h) override {
        const auto& live = SkinEngine::live();
        gfx::Rect r;
        if (!name || live.empty() || !live.front()->panel_rect(name, r)) return false;
        x = r.x; y = r.y; w = r.w; h = r.h;
        return true;
    }

    bool is_transparent(const char* name) override {
        const auto& live = SkinEngine::live();
        return name && !live.empty() && live.front()->panel_transparent(name);
    }

    bool get_backdrop(const char* name, t_uint32* pargb, int width, int height) override {
        const auto& live = SkinEngine::live();
        return name && !live.empty() && live.front()->panel_backdrop(name, pargb, width, height);
    }

    bool get_colour(const char* name, int kind, t_uint32& argb) override {
        const auto& live = SkinEngine::live();
        gfx::Color c;
        if (!name || live.empty() || !live.front()->panel_ui_colour(name, kind, c)) return false;
        argb = 0xFF000000u | ((t_uint32)c.r << 16) | ((t_uint32)c.g << 8) | c.b;
        return true;
    }

    bool get_font(const char* name, int kind, pfc::string_base& face, int& px, int& style) override {
        const auto& live = SkinEngine::live();
        ConfigFont f;
        if (!name || live.empty() || !live.front()->panel_ui_font(name, kind, f)) return false;
        face = f.face.c_str();
        px = f.px;
        style = f.style;
        return true;
    }

    bool eval(const char* expr, pfc::string_base& out) override {
        const auto& live = SkinEngine::live();
        if (!expr || live.empty()) return false;
        out = live.front()->eval_text(expr).c_str();
        return true;
    }

    bool get_value(const char* name, pfc::string_base& out) override {
        const auto& live = SkinEngine::live();
        if (!name || live.empty()) return false;
        const std::string v = live.front()->published_value(name);
        out = v.c_str();
        return !v.empty();
    }

    void set_value(const char* name, const char* value) override {
        if (!name || !*name) return;
        for (SkinEngine* e : SkinEngine::live()) e->publish_value(name, value ? value : "");
    }

private:
    static bool replaces_panels(const std::string& a) {
        return a.find("SKIN:") != std::string::npos || a.find("WIZARD:") != std::string::npos || a.find("MENU") != std::string::npos;
    }
};

FB2K_SERVICE_FACTORY(panels_ui_api_impl);

}

}
