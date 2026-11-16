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
    return k == K::Playlist || k == K::Seekbar || k == K::Volume || k == K::AlbumList || k == K::LibraryTree || k == K::QuickSearch || k == K::Lyrics;
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
    case Kind::Volume:      s.view = std::make_unique<Volume>(this); opts.cursor = ui::Cursor::Hand; opts.double_clicks = true; break;
    case Kind::Playlist:    s.view = std::make_unique<PlaylistView>(this); opts.double_clicks = true; opts.accept_files = true; break;
    case Kind::Spectrum:    s.view = std::make_unique<Spectrum>(this); opts.render_fps = 60; break;
    case Kind::PeakMeter:   s.view = std::make_unique<PeakMeter>(this); opts.render_fps = 60; break;
    case Kind::AlbumArt:    s.view = std::make_unique<AlbumArt>(this, name); break;
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
        if (panel_windowless(name, p.type) && (s.windowless = create_windowless(name))) break;
        m_creatingPanel = name;
        s.embedded = ui::create_embedded_ui_element(*m_main, dui, panel_script(name));
        m_creatingPanel.clear();
        if (!s.embedded)
            report_once("panel '" + name + "' (" + p.type +
                        "): no installed UI element can host it, left empty");
        break;
    }
    }
    static const char* const kNames[] = { N_("Now playing"), N_("Seek bar"), N_("Volume"), N_("Playlist"), N_("Spectrum analyser"),
                                          N_("Peak meter"), N_("Album art"), N_("Album browser"), N_("Lyrics"), N_("Search"),
                                          N_("Playlists"), N_("Panel") };
    opts.accessible_name = tr(kNames[(int)kind]);
    if (takes_keys(kind)) opts.on_tab = [this, name](bool back) { focus_next_panel(name, back); };
    if (s.view) {
        s.host = ui::create_child_view(*m_main, s.view.get(), opts);
        if (!s.host) return nullptr;
    }
    return &m_panels.emplace(name, std::move(s)).first->second;
}

bool SkinEngine::panel_windowless(const std::string& name, const std::string& type) const {
    if (name.empty() || m_st.cfg.num("panel." + name + ".windowless", 0) == 0) return false;
    std::string t = type;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return t == "js panel" || t == "foo_js_panel";
}

std::unique_ptr<SkinEngine::Windowless> SkinEngine::create_windowless(const std::string& name) {
    service_enum_t<js_panel::js_panel_windowless_api> e;
    service_ptr_t<js_panel::js_panel_windowless_api> api;
    if (!e.next(api)) {
        report_once("panel '" + name + "': this JS Panel can't draw without a window, so it gets one");
        return nullptr;
    }
    auto w = std::make_unique<Windowless>();
    w->host = std::make_unique<WindowlessHost>(this, name);
    m_creatingPanel = name;
    w->panel = api->create(name.c_str(), panel_script(name).c_str(), w->host.get());
    m_creatingPanel.clear();
    if (w->panel.is_empty()) return nullptr;
    log::info("panel", "'" + name + "' is a windowless JS Panel");
    return w;
}

void SkinEngine::paint_windowless(gfx::Canvas& cv, const Placement& placed) {
    if (!m_rendering || !panel_windowless(placed.name, placed.type)) return;
    const Placement p = remap_panel(placed);
    int x = p.x, y = p.y, w = p.w, h = p.h;
    if (!apply_panel_override(m_panelOverrides, p.name, x, y, w, h) || w <= 0 || h <= 0) return;
    Slot* s = ensure_slot(p.name, Kind::Embedded, p);
    if (!s || !s->windowless) return;
    const gfx::Rect r{ x, y, w, h };
    s->rect = r;
    m_wlOrder.push_back(p.name);
    gfx::ImagePtr below = cv.snapshot(r);
    gfx::Pixels px;
    if (!below || !gfx::image_pixels(*below, px) || px.w <= 0 || px.h <= 0) return;
    auto panel = s->windowless->panel;
    panel->resize(w, h);
    panel->paint(px.pargb.data(), px.w, px.h, (double)px.w / w);
    if (gfx::ImagePtr img = gfx::image_from_pixels(px))
        cv.draw_image(*img, gfx::RectF{ (float)x, (float)y, (float)w, (float)h },
                      gfx::RectF{ 0, 0, (float)img->width(), (float)img->height() }, 255, false, gfx::Interp::Bilinear);
}

SkinEngine::Slot* SkinEngine::windowless_slot(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = m_panels.find(name);
    return it != m_panels.end() && it->second.windowless && it->second.shown ? &it->second : nullptr;
}

std::string SkinEngine::windowless_at(int x, int y) const {
    for (auto it = m_wlOrder.rbegin(); it != m_wlOrder.rend(); ++it) {
        auto s = m_panels.find(*it);
        if (s == m_panels.end() || !s->second.windowless || !s->second.shown) continue;
        const gfx::Rect& r = s->second.rect;
        if (x >= r.x && x < r.right() && y >= r.y && y < r.bottom()) return *it;
    }
    return {};
}

void SkinEngine::focus_windowless(const std::string& name) {
    if (name == m_wlFocus) return;
    const std::string old = m_wlFocus;
    m_wlFocus = name;
    if (Slot* s = windowless_slot(old)) {
        auto panel = s->windowless->panel;
        panel->focus(false);
    }
    if (Slot* s = windowless_slot(name)) {
        auto panel = s->windowless->panel;
        panel->focus(true);
    }
}

bool SkinEngine::windowless_mouse(int kind, int x, int y, unsigned mask) {
    using W = js_panel::js_panel_windowless;
    const bool down = kind == W::mouse_left_down || kind == W::mouse_left_double || kind == W::mouse_right_down ||
                      kind == W::mouse_right_double || kind == W::mouse_middle_down || kind == W::mouse_middle_double;
    const bool up = kind == W::mouse_left_up || kind == W::mouse_right_up || kind == W::mouse_middle_up;
    if (kind == W::mouse_move && !(mask & 0x13)) m_wlCapture.clear();
    std::string target = m_wlCapture;
    if (target.empty() && kind != W::mouse_leave && !over_button(x, y)) target = windowless_at(x, y);
    if (target != m_wlHover) {
        const std::string old = m_wlHover;
        m_wlHover = target;
        m_wlCursor = 0;
        if (Slot* s = windowless_slot(old)) {
            auto panel = s->windowless->panel;
            panel->mouse(W::mouse_leave, 0, 0, 0);
        }
    }
    if (down) focus_windowless(target);
    Slot* s = windowless_slot(target);
    if (!s) return false;
    if (down) m_wlCapture = target;
    if (up) m_wlCapture.clear();
    if (kind == W::mouse_move && update_hover(-1, -1) && m_main) m_main->invalidate();
    const gfx::Rect r = s->rect;
    auto panel = s->windowless->panel;
    panel->mouse(kind, x - r.x, y - r.y, mask);
    return true;
}

bool SkinEngine::windowless_wheel(int x, int y, int step) {
    if (step == 0 || over_button(x, y)) return false;
    Slot* s = windowless_slot(windowless_at(x, y));
    if (!s) return false;
    auto panel = s->windowless->panel;
    panel->wheel(step, false);
    return true;
}

bool SkinEngine::windowless_key(const void* native_event) {
    Slot* s = windowless_slot(m_wlFocus);
    if (!s) return false;
    auto panel = s->windowless->panel;
    return panel->key(native_event);
}

int SkinEngine::windowless_drag(int kind, int x, int y, unsigned keys, int effects, const void* native_data) {
    using W = js_panel::js_panel_windowless_v2;
    const auto v2_of = [this](const std::string& name) {
        service_ptr_t<W> out;
        if (Slot* s = windowless_slot(name)) s->windowless->panel->service_query_t(out);
        return out;
    };
    std::string target = kind == W::drag_leave ? std::string() : windowless_at(x, y);
    service_ptr_t<W> panel = v2_of(target);
    if (panel.is_empty()) target.clear();
    const bool entered = !target.empty() && target != m_wlDrag;
    if (target != m_wlDrag) {
        if (auto old = v2_of(m_wlDrag); old.is_valid()) old->drag(W::drag_leave, 0, 0, 0, 0, native_data);
        m_wlDrag = target;
    }
    if (panel.is_empty()) return -1;
    const gfx::Rect r = windowless_slot(target)->rect;
    if (entered && kind == W::drag_drop) panel->drag(W::drag_enter, x - r.x, y - r.y, keys, effects, native_data);
    const int effect = panel->drag(entered && kind == W::drag_over ? W::drag_enter : kind, x - r.x, y - r.y, keys, effects, native_data);
    if (kind == W::drag_drop) m_wlDrag.clear();
    return effect;
}

int SkinEngine::windowless_cursor() const { return m_wlHover.empty() ? 0 : m_wlCursor; }

void* SkinEngine::WindowlessHost::native_window() { return m_e->m_main ? m_e->m_main->native() : nullptr; }

void SkinEngine::WindowlessHost::to_native(int& x, int& y) {
    auto it = m_e->m_panels.find(m_name);
    if (it != m_e->m_panels.end()) {
        x += it->second.rect.x;
        y += it->second.rect.y;
    }
    if (m_e->m_main) m_e->m_main->logical_to_native(x, y);
}

bool SkinEngine::WindowlessHost::visible() {
    auto it = m_e->m_panels.find(m_name);
    return it != m_e->m_panels.end() && it->second.shown;
}

void SkinEngine::WindowlessHost::invalidate(int, int, int, int) {
    if (m_e->m_main) m_e->m_main->invalidate();
}

void SkinEngine::WindowlessHost::set_cursor(int idc) {
    if (m_e->m_wlHover == m_name) m_e->m_wlCursor = idc;
}

void SkinEngine::WindowlessHost::set_tooltip(const char* text) {
    if (m_e->m_main) m_e->m_main->set_tooltip(text ? text : "");
}

void SkinEngine::WindowlessHost::take_focus() { m_e->focus_windowless(m_name); }

void SkinEngine::dispatch_placement(const Placement& p, int offsetX, int offsetY) {
    int x = p.x + offsetX, y = p.y + offsetY, w = p.w, h = p.h;
    if (!apply_panel_override(m_panelOverrides, p.name, x, y, w, h)) {
        auto it = m_panels.find(p.name);
        if (it != m_panels.end()) it->second.show(false);
        return;
    }
    const bool child = m_childShown.count(p.name) != 0;
    PanelZ z;
    const Kind kind = panel_kind(p.type, &z);
    const bool fresh = m_panels.find(p.name) == m_panels.end();
    const bool top = z == PanelZ::Top || (z == PanelZ::Child && child) || (fresh && kind == Kind::Embedded && panel_transparent(p.name));

    Slot* s = ensure_slot(p.name, kind, p);
    if (!s) return;
    if (kind == Kind::Spectrum) {
        const bool mirror = h < m_st.cfg.num("spectrum.mirror_below", 0);
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

bool SkinEngine::panel_command(const std::string& args) {
    if (!panel_action(m_panelOverrides, args)) return false;
    repaint_all();
    return true;
}

std::string SkinEngine::panel_name(const void* native) const {
    if (!native) return {};
    for (auto& [name, s] : m_panels)
        if (s.embedded && s.embedded->native() == native) return name;
    return m_creatingPanel;
}

bool SkinEngine::panel_rect(const std::string& name, gfx::Rect& out) const {
    auto it = m_panels.find(name);
    if (it == m_panels.end() || !it->second.shown) return false;
    out = it->second.rect;
    return true;
}

bool SkinEngine::panel_transparent(const std::string& name) const {
    return !name.empty() && m_st.cfg.num("panel." + name + ".transparent", 0) != 0;
}

bool SkinEngine::panel_backdrop(const std::string& name, uint32_t* pargb, int w, int h) const {
    gfx::Rect r;
    if (!pargb || w <= 0 || h <= 0 || !panel_transparent(name) || !panel_rect(name, r) || r.w <= 0 || r.h <= 0) return false;
    int ox = 0, oy = 0;
    gfx::ImagePtr img = backdrop_for(r, ox, oy);
    if (!img) return false;
    gfx::Pixels px;
    px.w = w; px.h = h;
    px.pargb.assign((size_t)w * h, 0xFF000000u);
    {
        auto cv = gfx::pixels_canvas(px, (double)w / r.w);
        if (!cv) return false;
        draw_image_region(*cv, *img, r.x - ox, r.y - oy, r.w, r.h);
    }
    std::copy(px.pargb.begin(), px.pargb.end(), pargb);
    return true;
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
    std::string t = trf("Script problems in {0}:", { label });
    for (auto& p : script_problems(label)) t += "\n\xe2\x80\xa2 " + p.shown();
    return t + "\n" + tr("Click to edit the code (View \xe2\x80\xba Panels UI \xe2\x80\xba Show script problems hides this).");
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
    {
        std::lock_guard<std::mutex> lk(m_framesMx);
        m_frames[name] = PanelFrame{ std::move(frame), bounds };
    }
    for (auto& [other, s] : m_panels)
        if (s.embedded && s.shown && s.rect.x < bounds.right() && bounds.x < s.rect.right() && s.rect.y < bounds.bottom() &&
            bounds.y < s.rect.bottom() && panel_transparent(other))
            s.embedded->invalidate();
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

bool SkinEngine::panel_ui_colour(const std::string& name, int kind, gfx::Color& out) const {
    for (const char* role : ui_colour_roles(kind))
        if (configured_color(name.c_str(), role, out)) return true;
    return (kind == 2 || kind == 3) && theme_color(out);
}

bool SkinEngine::panel_ui_font(const std::string& name, int kind, ConfigFont& out) const {
    const char* k = ui_font_kind(kind);
    if (!k) return false;
    for (const std::string& key : { "font." + name + "." + k, std::string("font.") + k, std::string("font.default") })
        if (parse_config_font(m_st.cfg.str(key), out)) return true;
    return false;
}

void SkinEngine::refresh_overlays() {
    for (auto& [name, s] : m_panels) {
        if (s.host && (s.kind == Kind::Seekbar || s.kind == Kind::Volume)) s.host->invalidate();
        if (s.embedded && s.shown && panel_transparent(name)) s.embedded->invalidate();
    }
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
