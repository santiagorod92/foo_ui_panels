#pragma once
#include "../fb2k.h"

namespace panels_ui {

class NOVTABLE panels_ui_api : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(panels_ui_api);
public:
    virtual bool is_active() = 0;
    virtual bool get_variable(const char* name, pfc::string_base& out) = 0;
    virtual void set_variable(const char* name, const char* value) = 0;
    virtual bool run_action(const char* action) = 0;
};

inline const GUID panels_ui_api::class_guid =
    { 0x2f6a91c4, 0x7d3b, 0x4e58, { 0xa1, 0x0c, 0x5b, 0x96, 0xe2, 0x47, 0x83, 0xd1 } };

}
