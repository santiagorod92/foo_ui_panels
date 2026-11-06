#pragma once
#include "../fb2k.h"

namespace js_panel {

inline const GUID element_guid = { 0x5EB08677, 0x0D6E, 0x48CB, { 0x9F, 0x0E, 0x2F, 0xBF, 0x6E, 0x80, 0xA3, 0x98 } };

inline constexpr const char* element_name = "JS Panel";

inline constexpr const char* script_key = "script";

class NOVTABLE js_panel_notify_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(js_panel_notify_api);
public:
    virtual void notify(const char* name, const char* info) = 0;
    virtual void variable_changed(const char* name, const char* value) = 0;
};

inline const GUID js_panel_notify_api::class_guid =
    { 0x91029014, 0x7430, 0x4bc7, { 0xb0, 0x6e, 0xc4, 0xdb, 0x71, 0x53, 0xb0, 0x8c } };

#ifdef _WIN32
inline ui_element_config::ptr make_config(const char* scriptPath) {
    return ui_element_config::g_create(element_guid, scriptPath, strlen(scriptPath));
}
#endif

}
