#include "panel_host.h"

namespace pui {

// ASCII case-insensitive "does haystack contain needle?"
static bool name_contains(const char* haystack, const char* needle) {
    pfc::string8 h, n;
    for (const char* p = haystack; p && *p; ++p) h.add_char((char)tolower((unsigned char)*p));
    for (const char* p = needle;   p && *p; ++p) n.add_char((char)tolower((unsigned char)*p));
    return strstr(h.get_ptr(), n.get_ptr()) != nullptr;
}

void PanelHost::log_available() {
    pfc::string8 line = "Panels UI: available UI elements:";
    service_enum_t<ui_element> e;
    service_ptr_t<ui_element> p;
    while (e.next(p)) {
        pfc::string8 nm; p->get_name(nm);
        line << "\n  - " << nm;
    }
    console::print(line);
}

HWND PanelHost::create(HWND parent, const char* name) {
    service_enum_t<ui_element> e;
    service_ptr_t<ui_element> p, found;
    while (e.next(p)) {
        pfc::string8 nm; p->get_name(nm);
        if (name_contains(nm, name)) { found = p; break; }
    }
    if (found.is_empty()) {
        console::printf("Panels UI: no UI element matching \"%s\"", name);
        return nullptr;
    }
    try {
        ui_element_config::ptr cfg = found->get_default_configuration();
        m_inst = found->instantiate(parent, cfg, ui_element_instance_callback_get_ptr());
    } catch (std::exception const& ex) {
        console::printf("Panels UI: failed to host \"%s\": %s", name, ex.what());
        return nullptr;
    }
    return wnd();
}

} // namespace pui
