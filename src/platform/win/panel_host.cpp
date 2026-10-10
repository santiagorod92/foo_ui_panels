#include "panel_host.h"
#include "../../core/log.h"

namespace pui {

static bool name_contains(const char* haystack, const char* needle) {
    pfc::string8 h, n;
    for (const char* p = haystack; p && *p; ++p) h.add_char((char)tolower((unsigned char)*p));
    for (const char* p = needle;   p && *p; ++p) n.add_char((char)tolower((unsigned char)*p));
    return strstr(h.get_ptr(), n.get_ptr()) != nullptr;
}

void PanelHost::log_available() {
    std::string line = "available UI elements:";
    service_enum_t<ui_element> e;
    service_ptr_t<ui_element> p;
    while (e.next(p)) {
        pfc::string8 nm; p->get_name(nm);
        line += std::string("\n  - ") + nm.get_ptr();
    }
    log::console(log::Level::Info, "panel", line);
}

HWND PanelHost::create(HWND parent, const char* name) {
    service_enum_t<ui_element> e;
    service_ptr_t<ui_element> p, found;
    while (e.next(p)) {
        pfc::string8 nm; p->get_name(nm);
        if (name_contains(nm, name)) { found = p; break; }
    }
    if (found.is_empty()) {
        log::console(log::Level::Warn, "panel", std::string("no UI element matching \"") + name + "\"");
        return nullptr;
    }
    try {
        ui_element_config::ptr cfg = found->get_default_configuration();
        m_inst = found->instantiate(parent, cfg, ui_element_instance_callback_get_ptr());
    } catch (std::exception const& ex) {
        log::console(log::Level::Error, "panel", std::string("failed to host \"") + name + "\": " + ex.what());
        return nullptr;
    }
    return wnd();
}

}
