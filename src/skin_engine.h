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
#include "playlist_view.h"
#include "spectrum.h"
#include "album_list.h"
#include "lyrics_panel.h"
#include "quick_search.h"
#include "library_tree.h"
#include <vector>
#include <map>
#include <set>
#include <string>
#include <memory>

namespace pui {

// Posted to the top-level window to show/hide the native menu bar (wParam: 1 show, 0 hide).
// Triggered by the settings popup's "show menu bar" toggle (action "MENUBAR:toggle").
#define PUI_WM_TOGGLE_MENU (WM_USER + 0x501)
#define PUI_WM_SHOW_MAINMENU (WM_USER + 0x502) // wp = screen x, lp = screen y

// The full persisted pvar store (shared across all skins/panels — one process-wide cfg blob).
// Exposed so the Preferences page's Variables/Overrides tabs can edit it directly without
// needing a live SkinEngine instance (the page runs standalone, before/without a canvas).
// Reserved keys prefixed "_prefs_" (font/accent overrides) are consulted by SkinHook's own
// font selection and SkinEngine::theme_color() as a fallback when a skin doesn't set its own.
std::map<std::string, std::string> load_all_pvars();
void save_all_pvars(const std::map<std::string, std::string>& pvars);

class SkinEngine {
public:
    void set_parent(HWND parent) { m_parent = parent; }
    void set_base_dir(const std::string& dir) { m_base = dir; } // for resolving image paths
    bool load(const char* script);

    // Run `script` against the draw engine on `dc` (used by native panels like TrackDisplay).
    // If `track` is valid, built-in fields (%title% etc.) resolve from it.
    // If `capture` is set, clickable regions ($button/$textbutton/…) are recorded into it
    // (in the panel's own coordinate space) instead of the main button list.
    // hoverX/hoverY: current mouse position in this call's own coordinate space (panel-local
    // for a native panel, -1,-1 if the mouse isn't over it) — lets $button/$button2/$imagebutton
    // draw their HOVER image/subscript instead of the normal one. The caller (TrackDisplay etc.)
    // tracks its own WM_MOUSEMOVE/WM_MOUSELEAVE since each panel has its own coordinate space.
    // If `placementsOut` is set, $panel() calls made by THIS script are recorded there instead
    // of the master canvas's own list — a native panel (TrackDisplay) can then turn them into
    // real hosted sub-panels via host_child_panel(), since render()'s own placement loop only
    // ever sees the master canvas script, not a panel's per-panel script.
    void draw_script(HDC dc, int w, int h,
                     const service_ptr_t<titleformat_object>& script,
                     const metadb_handle_ptr& track,
                     std::vector<Button>* capture = nullptr,
                     int hoverX = -1, int hoverY = -1,
                     std::vector<Placement>* placementsOut = nullptr);

    // Turns a $panel() placement captured from a NATIVE panel's own per-panel script (via
    // draw_script's placementsOut) into a real hosted child window — same dispatch render()
    // uses for the master canvas, just reachable from a panel that isn't the top-level window.
    // offsetX/offsetY: that panel's own screen position within the main window (e.g. from
    // MapWindowPoints(panelWnd, mainWnd, ...)), added to the placement's own panel-local x/y.
    void host_child_panel(const Placement& p, int offsetX, int offsetY);

    // Hides a previously-hosted named panel (any type) that a native panel's script stopped
    // requesting this frame — e.g. Display.txt's mini.panels cycling back to "nothing".
    void hide_child_panel(const std::string& name);

    // Paint pass: run the script against `dc` (executes draw funcs + records placements),
    // then create/position hosted panel windows. Called from the canvas WM_PAINT.
    void render(HDC dc, int width, int height);

    // Persist setup variables (pvars) across sessions. save_pvars() on component shutdown.
    void save_pvars();

    // Hit-test the buttons recorded in the last render and run the clicked one's action.
    bool handle_click(int x, int y);

    // Track mouse position (canvas coordinates) for top-level $button/$button2 hover-state
    // rendering. Returns true if the hovered button changed (caller should repaint). Pass
    // (-1,-1) on WM_MOUSELEAVE.
    bool update_hover(int x, int y);

    // Execute a button action (PVAR:SET / WINDOWSIZE / play / pause / main-menu command).
    // Panels (TrackDisplay) call this after hit-testing their own captured buttons.
    bool run_button_action(const std::string& action);

    // Write the RATING tag (0..5; 0 clears) on a specific track — playlist star clicks.
    void set_rating(const metadb_handle_ptr& track, int stars);

    // Invalidate the canvas + every hosted panel so a pvar (theme/mode) change is reflected.
    void repaint_all();

    // Current theme accent colour (pvar "colour", "r-g-b"). Returns false if unset.
    bool theme_color(COLORREF& out) const;

    // Opening a settings popup ends the skin's first-run onboarding: sets every `first.*` pvar
    // to 1, which is what stops fooAvA blinking its settings button at 1Hz (the master script
    // only seeds first.boot=0, and legacy Panels UI is what used to clear it).
    void complete_onboarding();

    // Skin base dir + current theme image index (pvar "colour.b": 1 black/2 blue/3 red/4 green).
    // Used by the native seek/volume bars to draw with the skin's themed bar graphics.
    const std::string& base_dir() const { return m_base; }
    int colour_index() const;
    std::string get_pvar(const std::string& k) const { auto it = m_pvars.find(k); return it == m_pvars.end() ? std::string() : it->second; }

    // Shared canvas wallpaper (pvars "backgroundd"/"background"/"alpha.bgr") — lets native
    // panels (seekbar/volume/playlist) draw the SAME background the main canvas draws, cropped
    // to their own position within the window, instead of each inventing its own unaligned copy
    // (which is what made them look like isolated boxes rather than part of one skin).
    // background_path() is "" when the background is disabled or unset.
    std::string background_path() const;
    int background_alpha() const;
    // Draws it into `dc` (destW x destH, `panelWnd`'s own client size), cropped to whatever
    // `panelWnd` shows of the full canvas at its current position. False = caller should fall
    // back (background disabled, or the image failed to load).
    bool draw_canvas_background(HDC dc, HWND panelWnd, int destW, int destH) const;

    // The master canvas is drawn into an off-screen bitmap first; keep the last full render so
    // native panels that sit on top of it (seek/volume bars) can show exactly what the skin drew
    // beneath them — rail art, progress fill, knob — instead of inventing their own background.
    // The skin script itself draws those bars (from %playback_time_seconds%, %panel_volume%…);
    // the native windows only catch mouse input.
    void snapshot_canvas(HDC src, int w, int h);
    bool draw_canvas_snapshot(HDC dc, HWND panelWnd) const;
    void refresh_bars(); // repaint the seek/volume windows from the fresh snapshot

    // Read a persisted setup variable as int (loads pvars on first use). For main.cpp to
    // query e.g. the "menubar" toggle before the first paint.
    int pvar_int(const std::string& key, int def);
    std::string pvar_str(const std::string& key); // "" if unset
    void set_pvar(const std::string& key, const std::string& value); // persists immediately

    // Per-panel script source (panels/<name>.txt next to the skin) — the original DLL's
    // right-click "edit code" feature. read is also used internally to load a native panel's
    // script at creation; save is only ever called from that editor.
    std::string read_panel_script(const std::string& name);
    bool save_panel_script(const std::string& name, const std::string& text);

private:
    friend class SkinHook;
    void load_pvars();
    // Hides any top-level panel window not named in this frame's m_placements — panel windows
    // persist across frames (keyed by name), so a layout switch that stops requesting one (e.g.
    // MiniMode, one-panel vs two-panel) leaves it visible/stale unless told to hide explicitly.
    void hide_unrequested_panels();
    bool m_pvars_loaded = false;
    HWND m_parent = nullptr;
    std::string m_base;
    service_ptr_t<titleformat_object> m_script;
    std::map<std::string, std::string> m_pvars;
    // Panels UI's $puts/$get scratch pool (per-run, not persisted) and the cache of compiled
    // argument snippets we re-run to resolve them (skin: "$get(fontAVAsize_3)").
    std::map<std::string, std::string> m_tfvars;
    std::map<std::string, service_ptr_t<titleformat_object>> m_evalcache;
    HDC m_snapDc = nullptr; HBITMAP m_snapBmp = nullptr; HGDIOBJ m_snapOld = nullptr; int m_snapW = 0, m_snapH = 0;
    std::vector<Placement> m_placements;
    std::vector<Button> m_buttons;
    std::vector<Button>* m_capture = nullptr; // when set, buttons record here (panel-local)
    std::vector<Placement>* m_capturePlacements = nullptr; // when set, $panel() records here
    void dispatch_placement(const Placement& p, int offsetX, int offsetY); // shared by render()/host_child_panel
    int m_hoverX = -1, m_hoverY = -1; // canvas-space mouse pos, for top-level button hover
    std::map<std::string, service_ptr_t<titleformat_object>> m_subcache; // $button2 draw commands
    std::map<std::string, std::unique_ptr<PanelHost>> m_hosts;
    std::map<std::string, std::unique_ptr<LyricsPanel>> m_lyrics;
    std::map<std::string, std::unique_ptr<QuickSearch>> m_searches;
    std::map<std::string, std::unique_ptr<LibraryTree>> m_trees;
    std::set<std::string> m_childShown; // panels a per-panel script currently hosts; the master sweep must not hide them
    std::map<std::string, std::unique_ptr<TrackDisplay>> m_track_displays;
    std::map<std::string, std::unique_ptr<Seekbar>> m_seekbars;
    std::map<std::string, std::unique_ptr<Volume>> m_volumes;
    std::map<std::string, std::unique_ptr<PlaylistView>> m_playlists;
    std::map<std::string, std::unique_ptr<Spectrum>> m_spectra;
    std::map<std::string, std::unique_ptr<AlbumList>> m_album_lists;
    std::unique_ptr<Popup> m_popup; // settings/about popup ($button 'POPUP:<file.ava>')
};

} // namespace pui
