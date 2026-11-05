#include "panels_ui_api.h"
#include "skin_engine.h"

namespace pui {

namespace {

class panels_ui_api_impl : public panels_ui::panels_ui_api {
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

private:
    static bool replaces_panels(const std::string& a) {
        return a.find("SKIN:") != std::string::npos || a.find("WIZARD:") != std::string::npos || a.find("MENU") != std::string::npos;
    }
};

FB2K_SERVICE_FACTORY(panels_ui_api_impl);

}

}
