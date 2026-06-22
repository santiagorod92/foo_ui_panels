// Phase 3/5 — Panels UI skin interpreter + draw engine.
// The skin is a titleformat-style script re-evaluated every paint. We reuse
// foobar2000's titleformat compiler (gets $if/$sub/$add/… for free) and supply the
// Panels-UI-specific functions/fields via a titleformat_hook:
//   %_width% %_height% %el_width% %el_height% %foobar_path%
//   $panel(name,type,x,y,w,h)   -> records a panel placement (host window)
//   $eval(expr)                 -> integer arithmetic over { } expressions
//   $getpvar/$setpvar           -> persistent variables (setup state)
//   $font/$drawrect/$drawstring/$drawroundrect/$gradientrect -> immediate GDI drawing
#pragma once
#include "win_sdk.h"
#include "panel_host.h"
#include "track_display.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace pui {

struct Placement {
    std::string name, type;
    int x = 0, y = 0, w = 0, h = 0;
};

class SkinEngine {
public:
    void set_parent(HWND parent) { m_parent = parent; }
    void set_base_dir(const std::string& dir) { m_base = dir; } // for resolving image paths
    bool load(const char* script);

    // Run `script` against the draw engine on `dc` (used by native panels like TrackDisplay).
    // If `track` is valid, built-in fields (%title% etc.) resolve from it.
    void draw_script(HDC dc, int w, int h,
                     const service_ptr_t<titleformat_object>& script,
                     const metadb_handle_ptr& track);

    // Paint pass: run the script against `dc` (executes draw funcs + records placements),
    // then create/position hosted panel windows. Called from the canvas WM_PAINT.
    void render(HDC dc, int width, int height);

private:
    friend class SkinHook;
    HWND m_parent = nullptr;
    std::string m_base;
    service_ptr_t<titleformat_object> m_script;
    std::map<std::string, std::string> m_pvars;
    std::vector<Placement> m_placements;
    std::map<std::string, std::unique_ptr<PanelHost>> m_hosts;
    std::map<std::string, std::unique_ptr<TrackDisplay>> m_track_displays;
};

} // namespace pui
