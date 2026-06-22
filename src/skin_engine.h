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
#include "seekbar.h"
#include "volume.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace pui {

struct Placement {
    std::string name, type;
    int x = 0, y = 0, w = 0, h = 0;
};

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action; // main-menu command path, e.g. "Playback/Random"
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

    // Persist setup variables (pvars) across sessions. save_pvars() on component shutdown.
    void save_pvars();

    // Hit-test the buttons recorded in the last render and run the clicked one's action.
    bool handle_click(int x, int y);

private:
    friend class SkinHook;
    void load_pvars();
    std::string read_panel_script(const std::string& name); // panels/<name>.txt next to the DLL
    bool m_pvars_loaded = false;
    HWND m_parent = nullptr;
    std::string m_base;
    service_ptr_t<titleformat_object> m_script;
    std::map<std::string, std::string> m_pvars;
    std::vector<Placement> m_placements;
    std::vector<Button> m_buttons;
    std::map<std::string, std::unique_ptr<PanelHost>> m_hosts;
    std::map<std::string, std::unique_ptr<TrackDisplay>> m_track_displays;
    std::map<std::string, std::unique_ptr<Seekbar>> m_seekbars;
    std::map<std::string, std::unique_ptr<Volume>> m_volumes;
};

} // namespace pui
