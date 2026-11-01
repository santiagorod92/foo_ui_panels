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
        return action && !live.empty() && live.front()->run_actions(action);
    }
};

FB2K_SERVICE_FACTORY(panels_ui_api_impl);

}

}
