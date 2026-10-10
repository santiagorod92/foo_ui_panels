#pragma once

namespace pui {

int zoom_setting();
void set_zoom_setting(int pct);
void step_zoom(int dir);
int effective_zoom_percent();

bool always_on_top();
void set_always_on_top(bool on);

bool show_script_problems();
void set_show_script_problems(bool on);

bool verbose_logging();
void set_verbose_logging(bool on);

void apply_view_settings();

}
