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
#include "pvars.h"
#include "script_runtime.h"
#include "script_util.h"
#include "skin_config.h"
#include <vector>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <memory>

namespace pui {

// The full persisted pvar store (shared across all skins/panels — one process-wide cfg blob).
// Exposed so the Preferences page's Variables/Overrides tabs can edit it directly without
// needing a live SkinEngine instance (the page runs standalone, before/without a canvas).
// Reserved keys prefixed "_prefs_" (font/accent overrides) are consulted by the runtime's own
// font selection and SkinEngine::theme_color() as a fallback when a skin doesn't set its own.
PvarMap load_all_pvars();
void save_all_pvars(const PvarMap& pvars);

// Built-in fallback skin script (no main script found in the skin folder).
const char* builtin_test_skin();


class PopupView;

class SkinEngine {
public:
    SkinEngine();
    ~SkinEngine();

    // Also starts the engine's playback/album-art notifications (the engine itself can be a
    // static-lifetime member, constructed before the core's services exist).
    void set_main_window(ui::MainWindow* w);
    ui::MainWindow* main_window() const { return m_main; }
    void set_base_dir(const std::string& dir) { m_st.base = dir; } // for resolving image paths
    bool load(const char* script); // compiles the master script; a failure keeps the previous one

    // Loads the skin in `dir`: its foo_ui_panels.ini (SkinConfig) and main script (see
    // resolve_main_script; the built-in test skin if there is none). Problems the scripts contain
    // are reported to the console (see below).
    bool load_skin(const std::string& dir);
    // Hot reload. Call periodically (the platforms do, from their canvas timer; it throttles
    // itself to once a second): if the main script, foo_ui_panels.ini or a panel script in use
    // changed on disk, it is reloaded and everything repaints.
    void check_skin_changes();
    const SkinConfig& config() const { return m_st.cfg; }

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

    // Every engine attached to a window (main thread only) — for what acts on "the player"
    // rather than one canvas: the View > Panels UI menu, the Preferences page.
    static const std::vector<SkinEngine*>& live();
    // Re-resolves the skin folder (Preferences or View > Panels UI > Skin changed it) and loads
    // that skin in place: the old skin's panels go, the new one's are created on the next paint.
    void reload_skin();
    static void reload_all();

    // Mini mode — the skin's compact layout, declared in its config: `mini.size` (client W H),
    // `mini.anchor` (the corner that stays put, e.g. "RIGHT TOP") and where the full size is
    // remembered (`mini.saved_w_pvar` / `mini.saved_h_pvar`, default reserved pvars) so the
    // skin's own restore button and the menu command agree. Not declared: not available.
    bool mini_mode_available() const;
    bool in_mini_mode() const;
    void toggle_mini_mode();

    // The skin's own named commands — `command.<label> = <action>[; <action>...]` — listed under
    // View > Panels UI > Skin commands, so they can get keyboard shortcuts. Sorted by label.
    std::vector<std::pair<std::string, std::string>> skin_commands() const;
    // Runs a `;`-separated action list (each one as a button would).
    bool run_actions(const std::string& actions);

    // Keyboard focus to the next (back: previous) panel that takes keys — playlist, album browser,
    // playlist switcher, search, lyrics — in reading order (top to bottom, left to right), after
    // the panel `from` ("" = from the canvas). Tab / Shift+Tab.
    void focus_next_panel(const std::string& from, bool back);

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

    // Invalidate the canvas + every hosted panel so a pvar (theme/mode) change, a playback event
    // or newly arrived album art shows.
    void repaint_all();

    // True while something is actually playing (not stopped, not paused): the only time the
    // periodic progress/time repaint has anything to advance. Discrete changes (new track,
    // seek, pause, volume) repaint through the engine's own play_callback instead.
    static bool playback_ticking();

    // The $settray icon's menu (transport, show/hide, exit), at the mouse cursor.
    void show_tray_menu();

    // Files/folders dropped from the OS file manager: resolved by foobar2000 (folders expanded,
    // playlists parsed, the user's sort/filter settings applied) and inserted into the active
    // playlist at `at` (pfc_infinite = appended), selected.
    static void add_files(const std::vector<std::string>& paths, t_size at = pfc_infinite);

    // Current theme accent colour: the "r-g-b" pvar the config names `theme.accent_pvar`, else the
    // Preferences accent override. Returns false if neither is set.
    bool theme_color(gfx::Color& out) const;

    // Opening a settings popup ends the skin's first-run onboarding: sets the pvars the config
    // lists in `onboarding.pvars` to 1 (legacy Panels UI cleared those flags itself; fooAvA blinks
    // its settings button at 1Hz until first.boot is set).
    void complete_onboarding();

    const std::string& base_dir() const { return m_st.base; }
    // Theme number: the pvar the config names `theme.index_pvar` (fooAvA: colour.b, 1..4), else
    // `theme.index_default` (1). Substituted for {theme} in asset paths.
    int theme_index() const;
    // Art a native panel draws, from the config's `asset.<key>` (relative to `images`), with
    // {theme} -> theme_index() and {n} -> n. "" when the skin declares none: draw without it.
    std::string asset(const std::string& key, int n = 0) const;
    // A native panel's colour for `role` ("text", "background", …): the config's
    // `color.<panel>.<role>`, else `color.<role>`, else `def` (the panel's built-in look). Values
    // are "r g b", "r-g-b" or "#rrggbb".
    gfx::Color color(const char* panel, const char* role, gfx::Color def) const;
    bool configured_color(const char* panel, const char* role, gfx::Color& out) const; // false = not set
    // A $panel() the config renames/retypes (`panel.remap.<name> = <new name>|<new type>`).
    Placement remap_panel(const Placement& p) const;
    bool is_lyrics_panel(const std::string& name) const; // a hosted native Lyric Show panel
    std::string get_pvar(const std::string& k) const { auto it = m_st.pvars.find(k); return it == m_st.pvars.end() ? std::string() : it->second; }

    // Shared canvas wallpaper — lets native panels draw the SAME background the main canvas
    // draws, cropped to their own position within the window. Which pvars hold it is the
    // config's `background.*`; background_path() is "" when the skin has none, or it's off.
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

    // A panel's own view state (a playlist's scroll position, the album browser's album...):
    // reserved pvars "_view.<key>", kept in memory and saved with the pvars when foobar2000
    // closes — so a view comes back as it was, without a config write per scroll step.
    std::string view_state(const std::string& key) { return pvar_str("_view." + key); }
    void set_view_state(const std::string& key, const std::string& value) {
        if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
        m_st.pvars["_view." + key] = value;
    }

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

    // Script problems the skin itself shows (when View > Panels UI > Show script problems is on):
    // a script that doesn't compile or is missing marks its panel; functions nothing knows are
    // listed with those but don't mark it on their own (old skins call dead plugins' functions
    // in branches that never run). Keyed by the script's label (its path in the skin folder, e.g.
    // "panels/Display.txt"); also printed to the console once. A script's list is rebuilt
    // whenever it is (re)read.
    struct ScriptProblem { std::string msg; bool serious; };
    const std::vector<ScriptProblem>& script_problems(const std::string& label) const;
    void add_script_problem(const std::string& label, const std::string& msg, bool serious = true);
    std::string panel_script_label(const std::string& name) const { return script_label(panels_dir() + "/" + name + ".txt"); }
    std::string main_script_label() const { return m_mainPath.empty() ? std::string() : script_label(m_mainPath); }
    // Draws the "!" marker in a corner of a w x h panel whose script `label` has problems (top
    // right; bottom right for the master canvas, whose top right holds the window buttons);
    // returns where (for clicks and the tooltip), an empty rect when there's none.
    gfx::Rect draw_problem_marker(gfx::Canvas& cv, int w, int h, const std::string& label, bool bottom = false);
    // The marker's tooltip: the problems, and what a click does.
    std::string problem_tooltip(const std::string& label) const;
    // The main script in the code editor (the master canvas's marker opens it).
    void open_main_script_editor();

    // Drops every hosted panel and stops the notifications (component shutdown, before the main
    // window goes away).
    void destroy_panels();

private:
    friend class SkinHook;
    // Skin diagnostics, in the foobar2000 console: functions neither the core nor this engine
    // know, ones accepted but not implemented yet, missing skin images, panels no installed
    // element can host. Each message once per (re)load, so a repaint loop can't flood it.
    void diagnose_script(const std::string& where, const std::string& text);
    void report_once(const std::string& msg);
    std::set<std::string> m_reported;
    std::map<std::string, std::vector<ScriptProblem>> m_problems; // see script_problems()
    gfx::Rect m_problemMarker; // the master canvas's, from the last render

    // Hot reload: last seen modification time per skin file (main script, config, panel scripts).
    using FileTimes = std::map<std::string, std::filesystem::file_time_type>;
    FileTimes scan_skin_files() const;
    bool load_main_script(); // (re)reads m_mainPath; reports + keeps the old script on failure
    std::string panels_dir() const { return m_st.base + "/" + m_st.cfg.str("panels", "panels"); }
    std::string script_label(const std::string& path) const; // path relative to the skin folder
    void seed_pvars(); // the config's `pvar.once.*`
    std::string m_mainPath; // "" = built-in test skin
    FileTimes m_fileTimes;
    unsigned long long m_nextScan = 0;
    std::string m_popupFile; // panels/<this>.txt is the open popup's script
    unsigned long long m_previewAt = 0, m_previewBy = 0; // when to save the skin's preview (0 = done), see save_preview()
    void save_preview();

    void load_pvars();
    // Hides any top-level panel not named in this frame's m_st.placements — panels persist across
    // frames (keyed by name), so a layout switch that stops requesting one (e.g. MiniMode,
    // one-panel vs two-panel) leaves it visible/stale unless told to hide explicitly.
    void hide_unrequested_panels();
    void dispatch_placement(const Placement& p, int offsetX, int offsetY); // shared by render()/host_child_panel

    // One hosted panel: a native view in a platform host, or an embedded foreign element.
    using Kind = PanelKind;
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
    service_ptr_t<titleformat_object> m_script;
    // What the scripts read and write (pvars, $puts pool, placements, buttons, config, skin
    // folder) — see script_runtime.h.
    ScriptState m_st;
    // Compiled argument snippets re-run to resolve them (skin: "$get(fontAVAsize_3)").
    std::map<std::string, service_ptr_t<titleformat_object>> m_evalcache;
    gfx::ImagePtr m_snapshot; // last master canvas frame
    struct PanelFrame { gfx::ImagePtr img; gfx::Rect bounds; };
    std::map<std::string, PanelFrame> m_frames; mutable std::mutex m_framesMx;
    int m_hoverX = -1, m_hoverY = -1; // canvas-space mouse pos, for top-level button hover
    std::map<std::string, service_ptr_t<titleformat_object>> m_subcache; // $button2 draw commands
    std::map<std::string, Slot> m_panels;
    std::set<std::string> m_childShown; // panels a per-panel script currently hosts; the master sweep must not hide them
    // Settings/about popup ($button 'POPUP:<file.ava>').
    std::unique_ptr<PopupView> m_popup;
    std::unique_ptr<ui::ViewHost> m_popupHost;

    // Repaints on playback events instead of waiting for the next timer tick.
    struct PlayEvents : play_callback_impl_base {
        explicit PlayEvents(SkinEngine* e);
        SkinEngine* m_e;
        void on_playback_new_track(metadb_handle_ptr) override { m_e->repaint_all(); }
        void on_playback_stop(play_control::t_stop_reason) override { m_e->repaint_all(); }
        void on_playback_seek(double) override { m_e->repaint_all(); }
        void on_playback_pause(bool) override { m_e->repaint_all(); }
        void on_playback_edited(metadb_handle_ptr) override { m_e->repaint_all(); }
        void on_playback_dynamic_info_track(const file_info&) override { m_e->repaint_all(); }
        void on_volume_change(float) override { m_e->repaint_all(); }
    };
    std::unique_ptr<PlayEvents> m_playEvents;
};

} // namespace pui
