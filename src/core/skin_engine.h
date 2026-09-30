// Panels UI skin interpreter + draw engine (platform-free).
// The skin is a titleformat-style script re-evaluated every paint. We reuse
// foobar2000's titleformat compiler (gets $if/$sub/$add/… for free) and supply the
// Panels-UI-specific functions/fields via a titleformat_hook:
//   %_width% %_height% %el_width% %el_height% %foobar_path%
//   $panel(name,type,x,y,w,h)   -> records a panel placement (hosted native panel / DUI element)
//   $eval(expr)                 -> integer arithmetic over { } expressions
//   $getpvar/$setpvar           -> persistent variables (setup state)
//   $font/$drawrect/$drawstring/$drawroundrect/$gradientrect -> immediate drawing on a gfx::Canvas
// Windowing (the main window, panel hosts, menus) goes through ui:: (src/ui/view.h).
#pragma once
#include "../fb2k.h"
#include "../gfx/canvas.h"
#include "../ui/view.h"
#include "button.h"
#include <vector>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <memory>

namespace pui {

// The full persisted pvar store (shared across all skins/panels — one process-wide cfg blob).
// Exposed so the Preferences page's Variables/Overrides tabs can edit it directly without
// needing a live SkinEngine instance (the page runs standalone, before/without a canvas).
// Reserved keys prefixed "_prefs_" (font/accent overrides) are consulted by SkinHook's own
// font selection and SkinEngine::theme_color() as a fallback when a skin doesn't set its own.
// Panels UI matched pvar names without regard to case: fooAvA's settings popup writes
// PVAR:SET:hidetitlebar while its master script reads $getpvar(Hidetitlebar), and the toggle
// only works if those are the same variable.
struct PvarNameLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return stricmp_utf8(a.c_str(), b.c_str()) < 0;
    }
};
using PvarMap = std::map<std::string, std::string, PvarNameLess>;

PvarMap load_all_pvars();
void save_all_pvars(const PvarMap& pvars);

// Built-in fallback skin script (no fooava.txt in the skin folder).
const char* builtin_test_skin();

// Parse "r-g-b" / "r-g-b-a" (any non-digit prefix skipped; alpha ignored).
gfx::Color parse_rgb(const char* s);

class PopupView;

class SkinEngine {
public:
    SkinEngine();
    ~SkinEngine();

    void set_main_window(ui::MainWindow* w) { m_main = w; }
    ui::MainWindow* main_window() const { return m_main; }
    void set_base_dir(const std::string& dir) { m_base = dir; } // for resolving image paths
    bool load(const char* script);

    // Run `script` against the draw engine on `cv` (used by native panels like TrackDisplay).
    // If `track` is valid, built-in fields (%title% etc.) resolve from it.
    // If `capture` is set, clickable regions ($button/$textbutton/…) are recorded into it
    // (in the panel's own coordinate space) instead of the main button list.
    // hoverX/hoverY: current mouse position in this call's own coordinate space (panel-local
    // for a native panel, -1,-1 if the mouse isn't over it) — lets $button/$button2/$imagebutton
    // draw their HOVER image/subscript instead of the normal one.
    // If `placementsOut` is set, $panel() calls made by THIS script are recorded there instead
    // of the master canvas's own list — a native panel (TrackDisplay) can then turn them into
    // real hosted sub-panels via host_child_panel(), since render()'s own placement loop only
    // ever sees the master canvas script, not a panel's per-panel script.
    void draw_script(gfx::Canvas& cv, int w, int h,
                     const service_ptr_t<titleformat_object>& script,
                     const metadb_handle_ptr& track,
                     std::vector<Button>* capture = nullptr,
                     int hoverX = -1, int hoverY = -1,
                     std::vector<Placement>* placementsOut = nullptr);

    // Turns a $panel() placement captured from a NATIVE panel's own per-panel script (via
    // draw_script's placementsOut) into a real hosted panel — same dispatch render() uses for
    // the master canvas. offsetX/offsetY: that panel's own position within the main window,
    // added to the placement's own panel-local x/y.
    void host_child_panel(const Placement& p, int offsetX, int offsetY);

    // Hides a previously-hosted named panel (any type) that a native panel's script stopped
    // requesting this frame — e.g. Display.txt's mini.panels cycling back to "nothing".
    void hide_child_panel(const std::string& name);

    // Paint pass: run the master script against `cv` (executes draw funcs + records placements),
    // then create/position the hosted panels. Called from the main window's paint.
    void render(gfx::Canvas& cv, int width, int height);

    // Persist setup variables (pvars) across sessions. save_pvars() on component shutdown.
    void save_pvars();

    // Hit-test the buttons recorded in the last render and run the clicked one's action.
    bool handle_click(int x, int y);

    // Track mouse position (canvas coordinates) for top-level $button/$button2 hover-state
    // rendering. Returns true if the hovered button changed (caller should repaint). Pass
    // (-1,-1) when the mouse leaves.
    bool update_hover(int x, int y);

    // Execute a button action (PVAR:SET / WINDOWSIZE / play / pause / main-menu command).
    // Panels (TrackDisplay) call this after hit-testing their own captured buttons.
    bool run_button_action(const std::string& action);

    // Write the RATING tag (0..5; 0 clears) on a specific track — playlist star clicks.
    void set_rating(const metadb_handle_ptr& track, int stars);

    // Invalidate the canvas + the script-driven panels so a pvar (theme/mode) change shows.
    void repaint_all();

    // Current theme accent colour (pvar "colour", "r-g-b"). Returns false if unset.
    bool theme_color(gfx::Color& out) const;

    // Opening a settings popup ends the skin's first-run onboarding: sets every `first.*` pvar
    // to 1, which is what stops fooAvA blinking its settings button at 1Hz (the master script
    // only seeds first.boot=0, and legacy Panels UI is what used to clear it).
    void complete_onboarding();

    // Skin base dir + current theme image index (pvar "colour.b": 1 black/2 blue/3 red/4 green).
    const std::string& base_dir() const { return m_base; }
    int colour_index() const;
    std::string get_pvar(const std::string& k) const { auto it = m_pvars.find(k); return it == m_pvars.end() ? std::string() : it->second; }

    // Shared canvas wallpaper (pvars "backgroundd"/"background"/"alpha.bgr") — lets native
    // panels draw the SAME background the main canvas draws, cropped to their own position
    // within the window. background_path() is "" when the background is disabled or unset.
    std::string background_path() const;
    int background_alpha() const;
    // Draws it into `cv` for a panel hosted at `host`'s position. False = caller should fall
    // back (background disabled, or the image failed to load).
    bool draw_canvas_background(gfx::Canvas& cv, const ui::ViewHost& host) const;

    // The master canvas frame is kept after every render so panels that sit on top of it
    // (seek/volume bars, playlist) can show exactly what the skin drew beneath them instead of
    // inventing their own background. The skin script itself draws those bars; the native
    // views only catch mouse input.
    void snapshot_canvas(gfx::Canvas& cv, int w, int h);
    bool draw_canvas_snapshot(gfx::Canvas& cv, const ui::ViewHost& host) const;
    void refresh_bars(); // repaint the seek/volume views from the fresh snapshot

    // Last frame a panel drew (keyed by panel name), so a panel stacked on top of another one
    // (the spectrum strips over the cover) can show it through. backdrop_for() returns the
    // frame of the panel under `r` (root coordinates) and that panel's origin, else the master
    // canvas snapshot at (0,0). UI thread only (the spectrum polls it from its UI-thread timer).
    void store_panel_frame(const std::string& name, gfx::ImagePtr frame, const gfx::Rect& bounds);
    gfx::ImagePtr backdrop_for(const gfx::Rect& r, int& originX, int& originY) const;

    // Read a persisted setup variable as int (loads pvars on first use).
    int pvar_int(const std::string& key, int def);
    std::string pvar_str(const std::string& key); // "" if unset
    void set_pvar(const std::string& key, const std::string& value); // persists immediately

    // Per-panel script source (panels/<name>.txt next to the skin) — the original DLL's
    // right-click "edit code" feature. read is also used internally to load a native panel's
    // script at creation; save is only ever called from that editor.
    std::string read_panel_script(const std::string& name);
    std::string read_panel_script_raw(const std::string& name); // as on disk, no init prefix
    bool save_panel_script(const std::string& name, const std::string& text);

    // Drops every hosted panel (component shutdown, before the main window goes away).
    void destroy_panels();

private:
    friend class SkinHook;
    void load_pvars();
    // Hides any top-level panel not named in this frame's m_placements — panels persist across
    // frames (keyed by name), so a layout switch that stops requesting one (e.g. MiniMode,
    // one-panel vs two-panel) leaves it visible/stale unless told to hide explicitly.
    void hide_unrequested_panels();
    void dispatch_placement(const Placement& p, int offsetX, int offsetY); // shared by render()/host_child_panel

    // One hosted panel: a native view in a platform host, or an embedded foreign element.
    enum class Kind { TrackDisplay, Seekbar, Volume, Playlist, Spectrum, AlbumList, Lyrics,
                      QuickSearch, LibraryTree, Embedded };
    struct Slot {
        Kind kind = Kind::Embedded;
        std::unique_ptr<ui::View> view;               // destroyed after the host (declared first)
        std::unique_ptr<ui::ViewHost> host;
        std::unique_ptr<ui::EmbeddedPanel> embedded;
        void show(bool v) { if (host) host->show(v); if (embedded) embedded->show(v); }
        void place(const gfx::Rect& r, bool top) { if (host) host->set_bounds(r, top); if (embedded) embedded->set_bounds(r, top); }
    };
    Slot* ensure_slot(const std::string& name, Kind kind, const Placement& p);

    bool m_pvars_loaded = false;
    ui::MainWindow* m_main = nullptr;
    std::string m_base;
    service_ptr_t<titleformat_object> m_script;
    PvarMap m_pvars;
    // Panels UI's $puts/$get scratch pool (per-run, not persisted) and the cache of compiled
    // argument snippets we re-run to resolve them (skin: "$get(fontAVAsize_3)").
    std::map<std::string, std::string> m_tfvars;
    std::map<std::string, service_ptr_t<titleformat_object>> m_evalcache;
    gfx::ImagePtr m_snapshot; // last master canvas frame
    struct PanelFrame { gfx::ImagePtr img; gfx::Rect bounds; };
    std::map<std::string, PanelFrame> m_frames; mutable std::mutex m_framesMx;
    std::vector<Placement> m_placements;
    std::vector<Button> m_buttons;
    std::vector<Button>* m_capture = nullptr; // when set, buttons record here (panel-local)
    std::vector<Placement>* m_capturePlacements = nullptr; // when set, $panel() records here
    int m_hoverX = -1, m_hoverY = -1; // canvas-space mouse pos, for top-level button hover
    std::map<std::string, service_ptr_t<titleformat_object>> m_subcache; // $button2 draw commands
    std::map<std::string, Slot> m_panels;
    std::set<std::string> m_childShown; // panels a per-panel script currently hosts; the master sweep must not hide them
    // Settings/about popup ($button 'POPUP:<file.ava>').
    std::unique_ptr<PopupView> m_popup;
    std::unique_ptr<ui::ViewHost> m_popupHost;
};

} // namespace pui
