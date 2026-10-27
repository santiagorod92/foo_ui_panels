#pragma once
#include "../fb2k.h"

namespace js_panel {

inline const GUID element_guid = { 0x5EB08677, 0x0D6E, 0x48CB, { 0x9F, 0x0E, 0x2F, 0xBF, 0x6E, 0x80, 0xA3, 0x98 } };

inline constexpr const char* element_name = "JS Panel";

inline constexpr const char* script_key = "script";

#ifdef _WIN32
inline ui_element_config::ptr make_config(const char* scriptPath) {
    return ui_element_config::g_create(element_guid, scriptPath, strlen(scriptPath));
}
#endif

}
