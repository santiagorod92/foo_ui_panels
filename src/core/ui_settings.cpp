#include "ui_settings.h"
#include "ui_logic.h"
#include "skin_engine.h"
#include <cmath>

namespace pui {

namespace {
// {4C2E8A17-6B3D-4F91-A5E0-3D7B9C1F2E68} — int: zoom percentage, 0 = automatic.
const GUID g_zoom_guid = { 0x4c2e8a17, 0x6b3d, 0x4f91, { 0xa5, 0xe0, 0x3d, 0x7b, 0x9c, 0x1f, 0x2e, 0x68 } };
cfg_var_modern::cfg_int g_zoom_cfg(g_zoom_guid, 0);

// {8E5A3C29-1F7B-4D06-9C42-6A8E0B3D5F71} — bool: always on top, only where the core has no
// standard "always on top" config object (see below).
const GUID g_ontop_guid = { 0x8e5a3c29, 0x1f7b, 0x4d06, { 0x9c, 0x42, 0x6a, 0x8e, 0x0b, 0x3d, 0x5f, 0x71 } };
cfg_var_modern::cfg_bool g_ontop_cfg(g_ontop_guid, false);

// {5D8B2E64-3A17-4C9F-8E21-7B4D6F0A9C35} — bool: mark script problems on the skin.
const GUID g_problems_guid = { 0x5d8b2e64, 0x3a17, 0x4c9f, { 0x8e, 0x21, 0x7b, 0x4d, 0x6f, 0x0a, 0x9c, 0x35 } };
cfg_var_modern::cfg_bool g_problems_cfg(g_problems_guid, true);

// Always on top is foobar2000's own setting (View > Always on Top, Alt+A): the core keeps it in
// a standard config object and leaves applying it to the UI module, like Columns UI does.
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
} // namespace

int zoom_setting() { const int v = (int)g_zoom_cfg.get(); return v <= 0 ? 0 : clamp_zoom(v); }
void set_zoom_setting(int pct) { g_zoom_cfg.set(pct <= 0 ? 0 : clamp_zoom(pct)); }

bool always_on_top() {
    config_object::ptr o;
    return ontop_object(o) ? o->get_data_bool_simple(false) : g_ontop_cfg.get();
}
void set_always_on_top(bool on) {
    config_object::ptr o;
    if (ontop_object(o)) o->set_data_bool(on); // the notify above applies it
    else { g_ontop_cfg.set(on); apply_on_top(); }
}

bool show_script_problems() { return g_problems_cfg.get(); }
void set_show_script_problems(bool on) {
    g_problems_cfg.set(on);
    for (SkinEngine* e : SkinEngine::live()) e->repaint_all();
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

} // namespace pui
