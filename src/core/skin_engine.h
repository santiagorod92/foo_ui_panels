#pragma once
#include "../fb2k.h"
#include "../gfx/canvas.h"
#include "../ui/view.h"
#include "builtin_skin.h"
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

PvarMap load_all_pvars();
void save_all_pvars(const PvarMap& pvars);

class PopupView;
class PrefsModel;
struct DiagnosticsInfo;

class SkinEngine {
public:
    SkinEngine();
    ~SkinEngine();

    void set_main_window(ui::MainWindow* w);
    ui::MainWindow* main_window() const { return m_main; }
    void set_base_dir(const std::string& dir) { m_st.base = dir; }
    bool load(const char* script);

    bool load_skin(const std::string& dir);
    void check_skin_changes();
    const SkinConfig& config() const { return m_st.cfg; }

    void draw_script(gfx::Canvas& cv, int w, int h,
                     const service_ptr_t<titleformat_object>& script,
                     const metadb_handle_ptr& track,
                     std::vector<Button>* capture = nullptr,
                     int hoverX = -1, int hoverY = -1,
                     std::vector<Placement>* placementsOut = nullptr);

    void host_child_panel(const Placement& p, int offsetX, int offsetY);

    void hide_child_panel(const std::string& name);

    void render(gfx::Canvas& cv, int width, int height);

    void save_pvars();

    static const std::vector<SkinEngine*>& live();
    void reload_skin();
    static void reload_all();

    void show_layout_wizard();
    void close_layout_wizard();
    bool in_layout_wizard() const { return m_wizard; }

    bool mini_mode_available() const;
    bool in_mini_mode() const;
    void toggle_mini_mode();

    std::vector<std::pair<std::string, std::string>> skin_commands() const;
    bool run_actions(const std::string& actions);

    void focus_next_panel(const std::string& from, bool back);

    bool handle_click(int x, int y);

    bool update_hover(int x, int y);

    bool run_button_action(const std::string& action);

    void set_rating(const metadb_handle_ptr& track, int stars);

    void repaint_all();

    static bool playback_ticking();

    void show_tray_menu();

    static void add_files(const std::vector<std::string>& paths, t_size at = pfc_infinite);

    bool theme_color(gfx::Color& out) const;

    void complete_onboarding();

    const std::string& base_dir() const { return m_st.base; }
    int theme_index() const;
    std::string asset(const std::string& key, int n = 0) const;
    gfx::Color color(const char* panel, const char* role, gfx::Color def) const;
    bool configured_color(const char* panel, const char* role, gfx::Color& out) const;
    Placement remap_panel(const Placement& p) const;
    bool is_lyrics_panel(const std::string& name) const;
    std::string get_pvar(const std::string& k) const { auto it = m_st.pvars.find(k); return it == m_st.pvars.end() ? std::string() : it->second; }

    std::string background_path() const;
    int background_alpha() const;
    bool draw_canvas_background(gfx::Canvas& cv, const ui::ViewHost& host) const;

    void snapshot_canvas(gfx::Canvas& cv, int w, int h);
    bool draw_canvas_snapshot(gfx::Canvas& cv, const ui::ViewHost& host) const;
    void refresh_bars();

    void store_panel_frame(const std::string& name, gfx::ImagePtr frame, const gfx::Rect& bounds);
    gfx::ImagePtr backdrop_for(const gfx::Rect& r, int& originX, int& originY) const;

    std::string view_state(const std::string& key) { return pvar_str("_view." + key); }
    void set_view_state(const std::string& key, const std::string& value) {
        if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }
        m_st.pvars["_view." + key] = value;
    }

    int pvar_int(const std::string& key, int def);
    std::string pvar_str(const std::string& key);
    void set_pvar(const std::string& key, const std::string& value);

    std::string read_panel_script(const std::string& name);
    std::string read_panel_script_raw(const std::string& name);
    bool save_panel_script(const std::string& name, const std::string& text);

    struct ScriptProblem { std::string msg; bool serious; };
    const std::vector<ScriptProblem>& script_problems(const std::string& label) const;
    void add_script_problem(const std::string& label, const std::string& msg, bool serious = true);
    std::string panel_script_label(const std::string& name) const { return script_label(panels_dir() + "/" + name + ".txt"); }
    std::string main_script_label() const { return m_mainPath.empty() ? std::string() : script_label(m_mainPath); }
    gfx::Rect draw_problem_marker(gfx::Canvas& cv, int w, int h, const std::string& label, bool bottom = false);
    std::string problem_tooltip(const std::string& label) const;
    void open_main_script_editor();

    void destroy_panels();

    void describe(DiagnosticsInfo& d) const;

private:
    friend class SkinHook;
    void diagnose_script(const std::string& where, const std::string& text);
    void report_once(const std::string& msg);
    std::set<std::string> m_reported;
    std::map<std::string, std::vector<ScriptProblem>> m_problems;
    gfx::Rect m_problemMarker;

    using FileTimes = std::map<std::string, std::filesystem::file_time_type>;
    FileTimes scan_skin_files() const;
    bool load_main_script();
    std::string panels_dir() const { return m_st.base + "/" + m_st.cfg.str("panels", "panels"); }
    std::string script_label(const std::string& path) const;
    void seed_pvars();
    std::string m_mainPath;
    FileTimes m_fileTimes;
    unsigned long long m_nextScan = 0;
    std::string m_popupFile;
    unsigned long long m_previewAt = 0, m_previewBy = 0;
    void save_preview();

    void load_pvars();
    void hide_unrequested_panels();
    void dispatch_placement(const Placement& p, int offsetX, int offsetY);

    using Kind = PanelKind;
    struct Slot {
        Kind kind = Kind::Embedded;
        std::unique_ptr<ui::View> view;
        std::unique_ptr<ui::ViewHost> host;
        std::unique_ptr<ui::EmbeddedPanel> embedded;
        void show(bool v) { if (host) host->show(v); if (embedded) embedded->show(v); }
        void place(const gfx::Rect& r, bool top) { if (host) host->set_bounds(r, top); if (embedded) embedded->set_bounds(r, top); }
    };
    Slot* ensure_slot(const std::string& name, Kind kind, const Placement& p);

    bool m_pvars_loaded = false;
    bool m_wizard = false;
    std::string first_run_setup(const std::string& dir);
    void apply_skin_choice(PrefsModel& prefs);
    ui::MainWindow* m_main = nullptr;
    service_ptr_t<titleformat_object> m_script;
    ScriptState m_st;
    std::map<std::string, service_ptr_t<titleformat_object>> m_evalcache;
    gfx::ImagePtr m_snapshot;
    struct PanelFrame { gfx::ImagePtr img; gfx::Rect bounds; };
    std::map<std::string, PanelFrame> m_frames; mutable std::mutex m_framesMx;
    int m_hoverX = -1, m_hoverY = -1;
    std::map<std::string, service_ptr_t<titleformat_object>> m_subcache;
    std::map<std::string, Slot> m_panels;
    std::set<std::string> m_childShown;
    std::unique_ptr<PopupView> m_popup;
    std::unique_ptr<ui::ViewHost> m_popupHost;

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

}
