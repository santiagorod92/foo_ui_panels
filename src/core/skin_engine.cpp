#include <algorithm>
#include "skin_engine.h"
#include "image_cache.h"
#include "script_util.h"
#include "fs_util.h"
#include "ui_logic.h"
#include "ui_settings.h"
#include "../panels/track_display.h"
#include "../panels/seekbar.h"
#include "../panels/volume.h"
#include "../panels/popup.h"
#include "../panels/playlist_view.h"
#include "../panels/spectrum.h"
#include "../panels/peak_meter.h"
#include "../panels/album_art.h"
#include "../panels/album_list.h"
#include "../panels/lyrics_panel.h"
#include "../panels/quick_search.h"
#include "../panels/library_tree.h"

namespace pui {

SkinEngine::PlayEvents::PlayEvents(SkinEngine* e)
    : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_seek |
                              flag_on_playback_pause | flag_on_playback_edited |
                              flag_on_playback_dynamic_info_track | flag_on_volume_change),
      m_e(e) {}

static std::vector<SkinEngine*> g_live;

const std::vector<SkinEngine*>& SkinEngine::live() { return g_live; }

SkinEngine::SkinEngine() = default;
SkinEngine::~SkinEngine() {
    g_live.erase(std::remove(g_live.begin(), g_live.end(), this), g_live.end());
    destroy_panels();
}

void SkinEngine::set_main_window(ui::MainWindow* w) {
    m_main = w;
    auto it = std::find(g_live.begin(), g_live.end(), this);
    if (w && it == g_live.end()) g_live.push_back(this);
    else if (!w && it != g_live.end()) g_live.erase(it);
    if (w && !m_playEvents) {
        m_playEvents = std::make_unique<PlayEvents>(this);
        set_art_ready_callback([this] { repaint_all(); });
    }
}

bool SkinEngine::playback_ticking() {
    auto pc = playback_control::get();
    return pc->is_playing() && !pc->is_paused();
}

void SkinEngine::destroy_panels() {
    if (m_playEvents) { m_playEvents.reset(); set_art_ready_callback(nullptr); }
    m_popupHost.reset(); m_popup.reset();
    m_panels.clear();
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_frames.clear();
}

bool SkinEngine::mini_mode_available() const { return m_st.cfg.nums("mini.size").size() == 2; }

bool SkinEngine::in_mini_mode() const {
    const std::vector<int> s = m_st.cfg.nums("mini.size");
    if (s.size() != 2 || !m_main) return false;
    const gfx::Rect r = m_main->client_rect();
    return r.w == s[0] && r.h == s[1];
}

void SkinEngine::toggle_mini_mode() {
    const std::vector<int> s = m_st.cfg.nums("mini.size");
    if (s.size() != 2 || !m_main) return;
    const std::string wKey = m_st.cfg.str("mini.saved_w_pvar", "_mini.w");
    const std::string hKey = m_st.cfg.str("mini.saved_h_pvar", "_mini.h");
    const gfx::Rect r = m_main->client_rect();
    const MiniPlan p = mini_mode_plan(r.w, r.h, s[0], s[1], pvar_int(wKey, 0), pvar_int(hKey, 0));
    if (!p.act) return;
    if (p.enter) {
        m_st.pvars[wKey] = std::to_string(r.w);
        m_st.pvars[hKey] = std::to_string(r.h);
        save_pvars();
    }
    std::string halign, valign;
    parse_anchor(m_st.cfg.str("mini.anchor"), halign, valign);
    m_main->resize_client(p.w, p.h, halign, valign);
    repaint_all();
}

std::vector<std::pair<std::string, std::string>> SkinEngine::skin_commands() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& [label, actions] : m_st.cfg.with_prefix("command."))
        if (!label.empty() && !split_actions(actions).empty()) out.emplace_back(label, actions);
    return out;
}

bool SkinEngine::run_actions(const std::string& actions) {
    bool any = false;
    for (const std::string& a : split_actions(actions)) any = run_button_action(a) || any;
    return any;
}

void SkinEngine::hide_unrequested_panels() {
    auto wanted = [&](const std::string& name) {
        if (m_childShown.count(name)) return true;
        for (auto& p : m_st.placements) if (p.name == name) return true;
        return false;
    };
    for (auto& [name, slot] : m_panels) if (!wanted(name)) slot.show(false);
}

static bool takes_keys(PanelKind k) {
    using K = PanelKind;
    return k == K::Playlist || k == K::AlbumList || k == K::LibraryTree || k == K::QuickSearch || k == K::Lyrics;
}

void SkinEngine::focus_next_panel(const std::string& from, bool back) {
    struct Stop { std::string name; gfx::Rect r; ui::ViewHost* host; };
    std::vector<Stop> stops;
    for (auto& [name, slot] : m_panels)
        if (takes_keys(slot.kind) && slot.host && slot.host->visible()) stops.push_back({ name, slot.host->bounds(), slot.host.get() });
    if (stops.empty()) return;
    std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) {
        return a.r.y != b.r.y ? a.r.y < b.r.y : a.r.x < b.r.x;
    });
    int cur = -1;
    for (size_t i = 0; i < stops.size(); ++i) if (stops[i].name == from) cur = (int)i;
    const int n = (int)stops.size();
    const int next = cur < 0 ? (back ? n - 1 : 0) : (cur + (back ? n - 1 : 1)) % n;
    stops[(size_t)next].host->focus();
    stops[(size_t)next].host->set_focus_ring(true);
}

SkinEngine::Slot* SkinEngine::ensure_slot(const std::string& name, Kind kind, const Placement& p) {
    auto it = m_panels.find(name);
    if (it != m_panels.end() && it->second.kind == kind) return &it->second;
    if (it != m_panels.end()) m_panels.erase(it);
    if (!m_main) return nullptr;
    Slot s; s.kind = kind;
    ui::ViewOptions opts;
    switch (kind) {
    case Kind::TrackDisplay: {
        auto td = std::make_unique<TrackDisplay>(this, name);
        std::string sc = read_panel_script(name);
        if (!sc.empty()) td->set_script(sc.c_str());
        s.view = std::move(td);
        break;
    }
    case Kind::Seekbar:     s.view = std::make_unique<Seekbar>(this); opts.cursor = ui::Cursor::Hand; break;
    case Kind::Volume:      s.view = std::make_unique<Volume>(this); opts.cursor = ui::Cursor::Hand; break;
    case Kind::Playlist:    s.view = std::make_unique<PlaylistView>(this); opts.double_clicks = true; opts.accept_files = true; break;
    case Kind::Spectrum:    s.view = std::make_unique<Spectrum>(this); opts.render_fps = 60; break;
    case Kind::PeakMeter:   s.view = std::make_unique<PeakMeter>(this); opts.render_fps = 60; break;
    case Kind::AlbumArt:    s.view = std::make_unique<AlbumArt>(this); break;
    case Kind::AlbumList:
        s.view = std::make_unique<AlbumList>(this, p.type.find("Chronflow") != std::string::npos);
        opts.double_clicks = true;
        break;
    case Kind::Lyrics:      s.view = std::make_unique<LyricsPanel>(this); break;
    case Kind::QuickSearch: s.view = std::make_unique<QuickSearch>(this); opts.cursor = ui::Cursor::IBeam; break;
    case Kind::LibraryTree: s.view = std::make_unique<LibraryTree>(this); opts.double_clicks = true; break;
    case Kind::Embedded: {
        const char* dui = map_type(p.type);
        if (!dui) return nullptr;
        s.embedded = ui::create_embedded_ui_element(*m_main, dui);
        if (!s.embedded)
            report_once("panel '" + name + "' (" + p.type +
                        "): no installed UI element can host it, left empty");
        break;
    }
    }
    static const char* const kNames[] = { "Now playing", "Seek bar", "Volume", "Playlist", "Spectrum analyser",
                                          "Peak meter", "Album art", "Album browser", "Lyrics", "Search",
                                          "Playlists", "Panel" };
    opts.accessible_name = kNames[(int)kind];
    if (takes_keys(kind)) opts.on_tab = [this, name](bool back) { focus_next_panel(name, back); };
    if (s.view) {
        s.host = ui::create_child_view(*m_main, s.view.get(), opts);
        if (!s.host) return nullptr;
    }
    return &m_panels.emplace(name, std::move(s)).first->second;
}

void SkinEngine::dispatch_placement(const Placement& p, int offsetX, int offsetY) {
    int x = p.x + offsetX, y = p.y + offsetY;
    const bool child = m_childShown.count(p.name) != 0;
    PanelZ z;
    const Kind kind = panel_kind(p.type, &z);
    const bool top = z == PanelZ::Top || (z == PanelZ::Child && child);

    Slot* s = ensure_slot(p.name, kind, p);
    if (!s) return;
    int w = p.w, h = p.h;
    if (kind == Kind::Spectrum) {
        const bool mirror = p.h < m_st.cfg.num("spectrum.mirror_below", 0);
        static_cast<Spectrum*>(s->view.get())->set_mirror(mirror);
        const int grow = m_st.cfg.num("spectrum.grow", 0);
        if (mirror) y += grow;
        h += grow;
        y -= m_st.cfg.num("spectrum.raise", 0);
    }
    s->show(true);
    s->place(gfx::Rect{ x, y, w, h }, top);
}

void SkinEngine::host_child_panel(const Placement& p, int offsetX, int offsetY) {
    m_childShown.insert(p.name);
    dispatch_placement(p, offsetX, offsetY);
}

void SkinEngine::hide_child_panel(const std::string& name) {
    m_childShown.erase(name);
    auto it = m_panels.find(name);
    if (it != m_panels.end()) it->second.show(false);
}

const std::vector<SkinEngine::ScriptProblem>& SkinEngine::script_problems(const std::string& label) const {
    static const std::vector<ScriptProblem> none;
    auto it = m_problems.find(label);
    return it == m_problems.end() ? none : it->second;
}

void SkinEngine::add_script_problem(const std::string& label, const std::string& msg, bool serious) {
    auto& list = m_problems[label];
    if (std::none_of(list.begin(), list.end(), [&](const ScriptProblem& p) { return p.msg == msg; }))
        list.push_back({ msg, serious });
    report_once("" + label + ": " + msg);
}

gfx::Rect SkinEngine::draw_problem_marker(gfx::Canvas& cv, int w, int h, const std::string& label, bool bottom) {
    const auto& problems = script_problems(label);
    if (label.empty() || !show_script_problems() ||
        std::none_of(problems.begin(), problems.end(), [](const ScriptProblem& p) { return p.serious; })) return {};
    const int d = 16;
    const gfx::Rect r{ w - d - 4, bottom ? h - d - 4 : 4, d, d };
    if (r.x < 0 || r.y < 0) return {};
    cv.fill_round_rect(r, d, d, gfx::Color(214, 64, 36));
    gfx::FontSpec f; f.face = "Arial"; f.size = 8; f.bold = true;
    cv.set_font(f);
    cv.draw_text("!", r, gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine | gfx::kNoClip, gfx::Color(255, 255, 255));
    return r;
}

std::string SkinEngine::problem_tooltip(const std::string& label) const {
    std::string t = "Script problems in " + label + ":";
    for (auto& p : script_problems(label)) t += "\n\xe2\x80\xa2 " + p.msg;
    return t + "\nClick to edit the code (View \xe2\x80\xba Panels UI \xe2\x80\xba Show script problems hides this).";
}

void SkinEngine::repaint_all() {
    if (m_main) m_main->invalidate();
    for (auto& [name, s] : m_panels)
        if (s.host && s.host->visible()) s.host->invalidate();
}

static std::string images_dir(const std::string& base, const SkinConfig& cfg) {
    std::string d = cfg.str("images");
    for (auto& c : d) if (c == '\\') c = '/';
    while (!d.empty() && (d.back() == '/')) d.pop_back();
    return d.empty() ? base : base + "/" + d;
}

std::string SkinEngine::background_path() const {
    const std::string onKey = m_st.cfg.str("background.enabled_pvar"), imgKey = m_st.cfg.str("background.image_pvar");
    if (imgKey.empty()) return {};
    if (!onKey.empty()) {
        auto on = m_st.pvars.find(onKey);
        if (on == m_st.pvars.end() || on->second != "1") return {};
    }
    auto bg = m_st.pvars.find(imgKey);
    if (bg == m_st.pvars.end() || bg->second.empty()) return {};
    std::string wp = bg->second;
    for (auto& c : wp) if (c == '\\') c = '/';
    return images_dir(m_st.base, m_st.cfg) + "/" + wp;
}

int SkinEngine::background_alpha() const {
    const std::string key = m_st.cfg.str("background.alpha_pvar");
    auto it = key.empty() ? m_st.pvars.end() : m_st.pvars.find(key);
    return (it != m_st.pvars.end() && !it->second.empty()) ? atoi(it->second.c_str())
                                                        : m_st.cfg.num("background.alpha", 255);
}

int SkinEngine::theme_index() const {
    const std::string key = m_st.cfg.str("theme.index_pvar");
    auto it = key.empty() ? m_st.pvars.end() : m_st.pvars.find(key);
    if (it != m_st.pvars.end() && !it->second.empty()) return atoi(it->second.c_str());
    return m_st.cfg.num("theme.index_default", 1);
}

bool SkinEngine::configured_color(const char* panel, const char* role, gfx::Color& out) const {
    return parse_config_color(m_st.cfg.str(std::string("color.") + panel + "." + role), out)
        || parse_config_color(m_st.cfg.str(std::string("color.") + role), out);
}

gfx::Color SkinEngine::color(const char* panel, const char* role, gfx::Color def) const {
    gfx::Color c;
    return configured_color(panel, role, c) ? c : def;
}

std::string SkinEngine::asset(const std::string& key, int n) const {
    std::string a = m_st.cfg.str("asset." + key);
    if (a.empty() || m_st.base.empty()) return {};
    auto put = [&](const std::string& tag, int v) {
        for (size_t p; (p = a.find(tag)) != std::string::npos;) a.replace(p, tag.size(), std::to_string(v));
    };
    put("{theme}", theme_index());
    put("{n}", n);
    for (auto& c : a) if (c == '\\') c = '/';
    return images_dir(m_st.base, m_st.cfg) + "/" + a;
}

bool SkinEngine::is_lyrics_panel(const std::string& name) const {
    auto it = m_panels.find(name);
    return it != m_panels.end() && it->second.kind == Kind::Lyrics;
}

Placement SkinEngine::remap_panel(const Placement& p) const {
    const std::string to = m_st.cfg.str("panel.remap." + p.name);
    if (to.empty()) return p;
    Placement q = p;
    size_t bar = to.find('|');
    q.name = to.substr(0, bar);
    if (bar != std::string::npos) q.type = to.substr(bar + 1);
    return q;
}

bool SkinEngine::draw_canvas_background(gfx::Canvas& cv, const ui::ViewHost& host) const {
    std::string path = background_path();
    if (path.empty() || !m_main) return false;
    gfx::Rect prc = m_main->client_rect(), b = host.bounds();
    const int top = m_st.cfg.num("background.top", 0);
    return draw_image(cv, path, -b.x, top - b.y, prc.w, prc.h - top, background_alpha());
}

void SkinEngine::snapshot_canvas(gfx::Canvas& cv, int w, int h) {
    if (w <= 0 || h <= 0) return;
    gfx::ImagePtr snap = cv.snapshot(gfx::Rect{ 0, 0, w, h });
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_snapshot = std::move(snap);
}

bool SkinEngine::draw_canvas_snapshot(gfx::Canvas& cv, const ui::ViewHost& host) const {
    gfx::ImagePtr snap;
    { std::lock_guard<std::mutex> lk(m_framesMx); snap = m_snapshot; }
    if (!snap) return false;
    gfx::Rect b = host.bounds();
    return draw_image_region(cv, *snap, b.x, b.y, b.w, b.h);
}

void SkinEngine::store_panel_frame(const std::string& name, gfx::ImagePtr frame, const gfx::Rect& bounds) {
    std::lock_guard<std::mutex> lk(m_framesMx);
    m_frames[name] = PanelFrame{ std::move(frame), bounds };
}

gfx::ImagePtr SkinEngine::backdrop_for(const gfx::Rect& r, int& originX, int& originY) const {
    std::lock_guard<std::mutex> lk(m_framesMx);
    for (auto& [name, f] : m_frames) {
        auto slot = m_panels.find(name);
        if (slot == m_panels.end() || !slot->second.host || !slot->second.host->visible()) continue;
        const gfx::Rect& b = f.bounds;
        if (!f.img || b.right() <= r.x || b.x >= r.right() || b.bottom() <= r.y || b.y >= r.bottom()) continue;
        originX = b.x; originY = b.y;
        return f.img;
    }
    originX = originY = 0;
    return m_snapshot;
}

void SkinEngine::refresh_bars() {
    for (auto& [name, s] : m_panels)
        if (s.host && (s.kind == Kind::Seekbar || s.kind == Kind::Volume)) s.host->invalidate();
}

bool SkinEngine::theme_color(gfx::Color& out) const {
    const std::string key = m_st.cfg.str("theme.accent_pvar");
    auto it = key.empty() ? m_st.pvars.end() : m_st.pvars.find(key);
    if (it == m_st.pvars.end() || it->second.empty()) {
        it = m_st.pvars.find("_prefs_accent_color");
        if (it == m_st.pvars.end() || it->second.empty()) return false;
    }
    out = parse_rgb(it->second.c_str());
    return true;
}

}
