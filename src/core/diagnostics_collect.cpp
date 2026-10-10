#include "../fb2k.h"
#include "diagnostics.h"
#include "log.h"
#include "pvars.h"
#include "skin_engine.h"
#include "ui_settings.h"
#include <algorithm>

#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef PUI_VERSION
#define PUI_VERSION "0.0.0-dev"
#endif

namespace pui {

namespace {

const char* platform_name() {
#ifdef _WIN32
    return "Windows";
#else
    return "macOS";
#endif
}

std::vector<std::string> installed_components() {
    std::vector<std::string> out;
    service_enum_t<componentversion> e;
    service_ptr_t<componentversion> c;
    while (e.next(c)) {
        pfc::string8 file, name, version;
        try {
            c->get_file_name(file);
            c->get_component_name(name);
            c->get_component_version(version);
        } catch (...) { continue; }
        std::string line = std::string(file.get_ptr());
        if (!name.is_empty()) line += " (" + std::string(name.get_ptr()) + ")";
        if (!version.is_empty()) line += " " + std::string(version.get_ptr());
        out.push_back(line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string session_summary() {
    const std::string wine = ui::wine_version();
    return std::string("foo_ui_panels ") + PUI_VERSION + " (" + build_arch() + ") on " +
           core_version_info::g_get_version_string() + ", " + platform_name() + " " + ui::os_version() +
           (wine.empty() ? "" : ", Wine " + wine) + ", verbose=" + (verbose_logging() ? "on" : "off");
}

class LogInit : public initquit {
public:
    void on_init() override { start_logging(); }
    void on_quit() override { log::note("env", "foobar2000 is closing"); }
};
FB2K_SERVICE_FACTORY(LogInit);

}

void start_logging() {
    static bool started = false;
    if (started) return;
    started = true;
    log::Logger& l = log::Logger::get();
    l.set_verbose(verbose_logging());
    l.set_console([](const std::string& msg) { console::print(("Panels UI: " + msg).c_str()); });
    try {
        const pfc::string8 native = filesystem::g_get_native_path(core_api::pathInProfile("foo_ui_panels.log"));
        l.configure(native.c_str());
    } catch (const std::exception& e) {
        console::printf("Panels UI: can't open the component log: %s", e.what());
    }
    log::note("env", session_summary());
}

std::string collect_diagnostics() {
    DiagnosticsInfo d;
    d.componentVersion = PUI_VERSION;
    d.foobarVersion = core_version_info::g_get_version_string();
    d.platform = platform_name();
    d.osVersion = ui::os_version();
    d.arch = build_arch();
    d.wineVersion = ui::wine_version();
    d.home = ui::home_dir();
    for (SkinEngine* e : SkinEngine::live()) e->describe(d);
    d.zoomSetting = zoom_setting();
    d.effectiveZoom = effective_zoom_percent();
    d.onTop = always_on_top();
    d.showProblems = show_script_problems();
    d.verboseLogging = verbose_logging();
    for (const auto& [k, v] : load_all_pvars()) if (!k.empty() && k[0] != '_') ++d.pvarCount;
    d.components = installed_components();
    d.navidrome = std::any_of(d.components.begin(), d.components.end(),
                              [](const std::string& c) { return c.find("foo_navidrome") != std::string::npos; });
    d.logPath = log::Logger::get().path();
    log::info("env", "diagnostics collected: " + std::to_string(d.playerWindows) + " player window(s), " +
                         std::to_string(d.panels.size()) + " panel(s)");
    d.logLines = log::Logger::get().recent();
    return build_diagnostics(d);
}

}
