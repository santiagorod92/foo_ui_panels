#include "ui_settings.h"
#include "ui_logic.h"
#include "skin_engine.h"
#include "log.h"
#include <cmath>

namespace pui {

namespace {
const GUID g_zoom_guid = { 0x4c2e8a17, 0x6b3d, 0x4f91, { 0xa5, 0xe0, 0x3d, 0x7b, 0x9c, 0x1f, 0x2e, 0x68 } };
cfg_var_modern::cfg_int g_zoom_cfg(g_zoom_guid, 0);

const GUID g_ontop_guid = { 0x8e5a3c29, 0x1f7b, 0x4d06, { 0x9c, 0x42, 0x6a, 0x8e, 0x0b, 0x3d, 0x5f, 0x71 } };
cfg_var_modern::cfg_bool g_ontop_cfg(g_ontop_guid, false);

const GUID g_problems_guid = { 0x5d8b2e64, 0x3a17, 0x4c9f, { 0x8e, 0x21, 0x7b, 0x4d, 0x6f, 0x0a, 0x9c, 0x35 } };
cfg_var_modern::cfg_bool g_problems_cfg(g_problems_guid, true);

const GUID g_verbose_guid = { 0x2f6c9a41, 0x7d3e, 0x4b58, { 0x91, 0xa7, 0x0c, 0x5e, 0x3b, 0x82, 0xd4, 0x16 } };
cfg_var_modern::cfg_bool g_verbose_cfg(g_verbose_guid, false);

bool ontop_object(config_object::ptr& out) {
    return config_object::g_find(out, standard_config_objects::bool_ui_always_on_top);
}

void apply_on_top() {
    for (SkinEngine* e : SkinEngine::live())
        if (ui::MainWindow* w = e->main_window()) w->set_always_on_top(always_on_top());
}

class ontop_notify : public config_object_notify {
public:
    t_size get_watched_object_count() override { return 1; }
    GUID get_watched_object(t_size) override { return standard_config_objects::bool_ui_always_on_top; }
    void on_watched_object_changed(const config_object::ptr&) override { apply_on_top(); }
};
FB2K_SERVICE_FACTORY(ontop_notify);
}

int zoom_setting() { const int v = (int)g_zoom_cfg.get(); return v <= 0 ? 0 : clamp_zoom(v); }
void set_zoom_setting(int pct) { g_zoom_cfg.set(pct <= 0 ? 0 : clamp_zoom(pct)); }

bool always_on_top() {
    config_object::ptr o;
    return ontop_object(o) ? o->get_data_bool_simple(false) : g_ontop_cfg.get();
}
void set_always_on_top(bool on) {
    config_object::ptr o;
    if (ontop_object(o)) o->set_data_bool(on);
    else { g_ontop_cfg.set(on); apply_on_top(); }
}

bool show_script_problems() { return g_problems_cfg.get(); }
void set_show_script_problems(bool on) {
    g_problems_cfg.set(on);
    for (SkinEngine* e : SkinEngine::live()) e->repaint_all();
}

bool verbose_logging() { return g_verbose_cfg.get(); }
void set_verbose_logging(bool on) {
    if (on != g_verbose_cfg.get()) log::note("prefs", std::string("verbose logging ") + (on ? "on" : "off"));
    g_verbose_cfg.set(on);
    log::Logger::get().set_verbose(on);
}

int effective_zoom_percent() {
    for (SkinEngine* e : SkinEngine::live())
        if (ui::MainWindow* w = e->main_window()) return (int)std::lround(w->zoom() * 100);
    return 100;
}

void step_zoom(int dir) {
    if (dir == 0) set_zoom_setting(0);
    else {
        const int cur = zoom_setting() > 0 ? zoom_setting() : effective_zoom_percent();
        set_zoom_setting(zoom_step(cur, dir));
    }
    apply_view_settings();
}

void apply_view_settings() {
    for (SkinEngine* e : SkinEngine::live()) {
        if (ui::MainWindow* w = e->main_window()) {
            w->set_always_on_top(always_on_top());
            w->apply_zoom();
        }
        e->repaint_all();
    }
}

}
