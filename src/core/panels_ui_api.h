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

class NOVTABLE panels_ui_api_v2 : public panels_ui_api {
    FB2K_MAKE_SERVICE_INTERFACE(panels_ui_api_v2, panels_ui_api);
public:
    virtual bool panel_name(const void* native_view, pfc::string_base& out) = 0;
    virtual bool get_panel_rect(const char* name, int& x, int& y, int& w, int& h) = 0;
    virtual bool is_transparent(const char* name) = 0;
    virtual bool get_backdrop(const char* name, t_uint32* pargb, int width, int height) = 0;
    virtual bool get_colour(const char* name, int kind, t_uint32& argb) = 0;
    virtual bool get_font(const char* name, int kind, pfc::string_base& face, int& px, int& style) = 0;
    virtual bool eval(const char* expr, pfc::string_base& out) = 0;
    virtual bool get_value(const char* name, pfc::string_base& out) = 0;
    virtual void set_value(const char* name, const char* value) = 0;
};

inline const GUID panels_ui_api::class_guid =
    { 0x2f6a91c4, 0x7d3b, 0x4e58, { 0xa1, 0x0c, 0x5b, 0x96, 0xe2, 0x47, 0x83, 0xd1 } };

inline const GUID panels_ui_api_v2::class_guid =
    { 0xf75f0990, 0x8aff, 0x47da, { 0x81, 0xc4, 0x02, 0xd4, 0xe3, 0xb6, 0x51, 0x5b } };

}
