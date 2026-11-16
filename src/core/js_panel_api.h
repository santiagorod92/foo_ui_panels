#pragma once
#include "../fb2k.h"

namespace js_panel {

inline const GUID element_guid = { 0x5EB08677, 0x0D6E, 0x48CB, { 0x9F, 0x0E, 0x2F, 0xBF, 0x6E, 0x80, 0xA3, 0x98 } };

inline constexpr const char* element_name = "JS Panel";

inline constexpr const char* script_key = "script";

inline constexpr const char* drag_type_tracks = "com.santiagorod92.foo-js-panel.tracks";

class NOVTABLE js_panel_notify_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(js_panel_notify_api);
public:
    virtual void notify(const char* name, const char* info) = 0;
    virtual void variable_changed(const char* name, const char* value) = 0;
};

class NOVTABLE js_panel_notify_api_v2 : public js_panel_notify_api {
    FB2K_MAKE_SERVICE_INTERFACE(js_panel_notify_api_v2, js_panel_notify_api);
public:
    virtual void state_changed(const char* name, const char* value) = 0;
};

class js_panel_windowless_host {
public:
    virtual void* native_window() = 0;
    virtual void to_native(int& x, int& y) = 0;
    virtual bool visible() = 0;
    virtual void invalidate(int x, int y, int w, int h) = 0;
    virtual void set_cursor(int idc) = 0;
    virtual void set_tooltip(const char* text) = 0;
    virtual void capture_mouse(bool on) = 0;
    virtual void take_focus() = 0;

protected:
    ~js_panel_windowless_host() = default;
};

class NOVTABLE js_panel_windowless : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE(js_panel_windowless, service_base);
public:
    enum { mouse_move, mouse_leave, mouse_left_down, mouse_left_up, mouse_left_double, mouse_right_down, mouse_right_up,
           mouse_right_double, mouse_middle_down, mouse_middle_up, mouse_middle_double };
    virtual void paint(t_uint32* pargb, int width, int height, double scale) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void mouse(int kind, int x, int y, unsigned mask) = 0;
    virtual void wheel(int step, bool horizontal) = 0;
    virtual bool key(const void* native_event) = 0;
    virtual void focus(bool focused) = 0;
    virtual void detach() = 0;
};

class NOVTABLE js_panel_windowless_v2 : public js_panel_windowless {
    FB2K_MAKE_SERVICE_INTERFACE(js_panel_windowless_v2, js_panel_windowless);
public:
    enum { drag_enter, drag_over, drag_leave, drag_drop };
    virtual int drag(int kind, int x, int y, unsigned keys, int effects, const void* native_data) = 0;
};

class NOVTABLE js_panel_windowless_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(js_panel_windowless_api);
public:
    virtual service_ptr_t<js_panel_windowless> create(const char* name, const char* script, js_panel_windowless_host* host) = 0;
};

inline const GUID js_panel_notify_api::class_guid =
    { 0x91029014, 0x7430, 0x4bc7, { 0xb0, 0x6e, 0xc4, 0xdb, 0x71, 0x53, 0xb0, 0x8c } };

inline const GUID js_panel_notify_api_v2::class_guid =
    { 0x70b866f9, 0x1fe6, 0x4ff4, { 0xbe, 0xf5, 0xa7, 0x45, 0xf2, 0x07, 0x2b, 0x55 } };

inline const GUID js_panel_windowless::class_guid =
    { 0x767764da, 0x7dd0, 0x4e87, { 0x8f, 0x53, 0x9f, 0xcc, 0x1f, 0x7a, 0x2b, 0x15 } };

inline const GUID js_panel_windowless_v2::class_guid =
    { 0x2c51e0a4, 0x6f3b, 0x4d18, { 0x9a, 0x77, 0x41, 0x0e, 0xc3, 0x5d, 0x92, 0xb6 } };

inline const GUID js_panel_windowless_api::class_guid =
    { 0x93740c3b, 0x8b72, 0x45eb, { 0xb7, 0xe2, 0x31, 0xa4, 0x2a, 0xc6, 0x06, 0xae } };

#ifdef _WIN32
inline ui_element_config::ptr make_config(const char* scriptPath) {
    return ui_element_config::g_create(element_guid, scriptPath, strlen(scriptPath));
}
#endif

}
