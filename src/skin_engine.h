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
#include "button.h"
#include "panel_host.h"
#include "track_display.h"
#include "seekbar.h"
#include "volume.h"
#include "popup.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace pui {

// Posted to the top-level window to show/hide the native menu bar (wParam: 1 show, 0 hide).
// Triggered by the settings popup's "show menu bar" toggle (action "MENUBAR:toggle").
#define PUI_WM_TOGGLE_MENU (WM_USER + 0x501)

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
    // If `capture` is set, clickable regions ($button/$textbutton/…) are recorded into it
    // (in the panel's own coordinate space) instead of the main button list.
    void draw_script(HDC dc, int w, int h,
                     const service_ptr_t<titleformat_object>& script,
                     const metadb_handle_ptr& track,
                     std::vector<Button>* capture = nullptr);

    // Paint pass: run the script against `dc` (executes draw funcs + records placements),
    // then create/position hosted panel windows. Called from the canvas WM_PAINT.
    void render(HDC dc, int width, int height);

    // Persist setup variables (pvars) across sessions. save_pvars() on component shutdown.
    void save_pvars();

    // Hit-test the buttons recorded in the last render and run the clicked one's action.
    bool handle_click(int x, int y);

    // Execute a button action (PVAR:SET / WINDOWSIZE / play / pause / main-menu command).
    // Panels (TrackDisplay) call this after hit-testing their own captured buttons.
    bool run_button_action(const std::string& action);

    // Invalidate the canvas + every hosted panel so a pvar (theme/mode) change is reflected.
    void repaint_all();

    // Current theme accent colour (pvar "colour", "r-g-b"). Returns false if unset.
    bool theme_color(COLORREF& out) const;

    // Skin base dir + current theme image index (pvar "colour.b": 1 black/2 blue/3 red/4 green).
    // Used by the native seek/volume bars to draw with the skin's themed bar graphics.
    const std::string& base_dir() const { return m_base; }
    int colour_index() const;

    // Read a persisted setup variable as int (loads pvars on first use). For main.cpp to
    // query e.g. the "menubar" toggle before the first paint.
    int pvar_int(const std::string& key, int def);

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
    std::vector<Button>* m_capture = nullptr; // when set, buttons record here (panel-local)
    std::map<std::string, service_ptr_t<titleformat_object>> m_subcache; // $button2 draw commands
    std::map<std::string, std::unique_ptr<PanelHost>> m_hosts;
    std::map<std::string, std::unique_ptr<TrackDisplay>> m_track_displays;
    std::map<std::string, std::unique_ptr<Seekbar>> m_seekbars;
    std::map<std::string, std::unique_ptr<Volume>> m_volumes;
    std::unique_ptr<Popup> m_popup; // settings/about popup ($button 'POPUP:<file.ava>')
};

} // namespace pui
