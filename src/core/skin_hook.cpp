#include <cmath>
#include "skin_engine.h"
#include "image_cache.h"
#include <cstring>

// SkinEngine: running scripts. foobar2000's titleformat engine parses and runs them; the hook
// below answers the Panels UI fields itself and hands every $function call that is ours to the
// SDK-free ScriptRuntime (script_runtime.cpp), serving the titleformat, image and window needs it
// has through ScriptEnv. Also the skin diagnostics ("unsupported function").

namespace pui {

static bool eq(const char* name, size_t len, const char* lit) {
    return strlen(lit) == len && memcmp(name, lit, len) == 0;
}
static std::string param_str(titleformat_hook_function_params* p, size_t i) {
    const char* s = nullptr; size_t n = 0;
    p->get_param(i, s, n);
    return std::string(s ? s : "", s ? n : 0);
}

// Whether foobar2000's own titleformat engine knows $name: an unknown function renders a fixed
// error marker, learned from a name that can't exist. A known one called with the wrong number
// of arguments renders that same marker, so try 0..4 of them. Formatted against a (dummy) track:
// $info/$meta & co. come with track context, which a bare run() lacks. (The hook knows nothing;
// a null one crashes the core.)
static bool core_knows_function(const std::string& name) {
    struct NoHook : titleformat_hook {
        bool process_field(titleformat_text_out*, const char*, t_size, bool& found) override { found = false; return false; }
        bool process_function(titleformat_text_out*, const char*, t_size, titleformat_hook_function_params*, bool& found) override { found = false; return false; }
    };
    static std::map<std::string, bool> known;
    static std::string marker;
    static metadb_handle_ptr track;
    if (track.is_empty()) metadb::get()->handle_create(track, make_playable_location("pui-probe://", 0));
    auto render = [](const std::string& code) {
        titleformat_object::ptr obj; pfc::string8 out; NoHook hook;
        if (titleformat_compiler::get()->compile(obj, code.c_str())) track->format_title(&hook, out, obj, nullptr);
        return std::string(out.get_ptr());
    };
    if (marker.empty()) marker = render("$pui_no_such_function_qz()");
    auto it = known.find(name);
    if (it != known.end()) return it->second;
    std::string args;
    for (int n = 0; n <= 4; ++n) {
        if (render("$" + name + "(" + args + ")") != marker) return known[name] = true;
        args += args.empty() ? "a" : ",a";
    }
    return known[name] = false;
}

// titleformat output sink holding the script's literal text: the runtime reads this buffer when
// it flushes a text box (see ScriptRuntime::set_text_buffer — truncate-safe).
class DrawString : public pfc::string_base {
public:
    const std::string& buf() const { return m_buf; }
    const char* get_ptr() const override { return m_buf.c_str(); }
    void add_string(const char* s, t_size n = SIZE_MAX) override { m_buf.append(s, n == SIZE_MAX ? strlen(s) : n); }
    void truncate(t_size len) override { if (len < m_buf.size()) m_buf.resize(len); }
    t_size get_length() const override { return m_buf.size(); }
    char* lock_buffer(t_size req) override { m_buf.resize(req); return m_buf.empty() ? nullptr : &m_buf[0]; }
    void unlock_buffer() override { m_buf.resize(strlen(m_buf.c_str())); }
private:
    std::string m_buf;
};

// A function's output, written straight into the titleformat stream.
struct TfOut : ScriptOut {
    titleformat_text_out* tf;
    explicit TfOut(titleformat_text_out* o) : tf(o) {}
    void write(std::string_view s) override { tf->write(titleformat_inputtypes::unknown, s.data(), s.size()); }
};

// --- titleformat hook + the runtime's environment ---------------------------
class SkinHook : public titleformat_hook, public ScriptEnv {
public:
    SkinHook(SkinEngine* e, gfx::Canvas& cv, int w, int h, const metadb_handle_ptr& track = metadb_handle_ptr(),
             int hoverX = -1, int hoverY = -1)
        : m_e(e), m_track(track), m_rt(e->m_st, *this, cv, w, h, hoverX, hoverY) {}

    ScriptRuntime& runtime() { return m_rt; }

    bool process_field(titleformat_text_out* out, const char* name, t_size len, bool& found) override {
        found = true;
        TfOut o(out);
        if (eq(name, len, "_width")  || eq(name, len, "el_width"))  { o.write_int(m_rt.width()); return true; }
        if (eq(name, len, "_height") || eq(name, len, "el_height")) { o.write_int(m_rt.height()); return true; }
        if (eq(name, len, "_isplaying")) {
            if (playback_control::get()->is_playing()) o.write("1");
            return true;
        }
        if (eq(name, len, "_ispaused")) {
            if (playback_control::get()->is_paused()) o.write("1");
            return true;
        }
        // Volume fields used by the skin's own volume-bar drawing (foo_cwb_hooks / Panels UI):
        // cwb_volume = dB (-100 = muted), panel_volume = 0..1000 along the bar.
        if (eq(name, len, "cwb_volume")) {
            o.write_int(std::lround(playback_control::get()->get_volume())); return true;
        }
        if (eq(name, len, "panel_volume")) {
            o.write_int(std::lround((playback_control::get()->get_volume() + 100.0f) * 10.0f)); return true;
        }
        if (eq(name, len, "rating")) {
            // navidrome:// tracks store their rating under NAVIDROME_RATING, not the standard
            // RATING tag %rating% resolves (foo_navidrome deliberately avoids the field Playback
            // Statistics owns). fooAvA's rating-star widgets reference bare %rating%, which we
            // can't edit — intercept the field here instead, same fallback shape as
            // playlist_view.cpp's $if2(%navidrome_rating%,[%rating%]).
            // The track this run is for (a panel script's), else whatever is playing.
            metadb_handle_ptr t = m_track;
            if (t.is_empty()) playback_control::get()->get_now_playing(t);
            metadb_info_container::ptr info;
            if (t.is_valid() && t->get_info_ref(info) && info->info().meta_get_count_by_name("NAVIDROME_RATING") > 0) {
                o.write(info->info().meta_get("NAVIDROME_RATING", 0));
                return true;
            }
            found = false; return false; // not a navidrome track (or unrated) — native %rating%
        }
        if (eq(name, len, "cwb_playback_order")) {
            // fooAvA reads this (a foo_cwb_hooks field) to pick the repeat/shuffle icon + label.
            auto pm = playlist_manager::get();
            const char* nm = pm->playback_order_get_name(pm->playback_order_get_active());
            if (nm) o.write(nm);
            return true;
        }
        // foo_cwb_hooks playlist names. fooAvA's title bar is [$upper(%cwb_activelist%)] between
        // prev/next-playlist arrows ("<- LISTENING ->"); the side panels show %cwb_playinglist%.
        if (eq(name, len, "cwb_activelist") || eq(name, len, "cwb_playinglist")) {
            auto pm = playlist_manager::get();
            t_size idx = eq(name, len, "cwb_activelist") ? pm->get_active_playlist() : pm->get_playing_playlist();
            pfc::string8 nm;
            if (idx != pfc_infinite && pm->playlist_get_name(idx, nm)) o.write(nm.get_ptr());
            return true;
        }
        // The folder the engine took the skin from (the welcome screen says where it looked).
        if (eq(name, len, "pui_skin_folder")) { o.write(m_e->m_st.base); return true; }
        if (eq(name, len, "foobar_path")) {
            pfc::string8 p; filesystem::g_get_display_path(core_api::get_profile_path(), p);
            o.write(p.get_ptr()); return true;
        }
        found = false; return false;
    }

    bool process_function(titleformat_text_out* out, const char* name, t_size len,
                          titleformat_hook_function_params* p, bool& found) override {
        // Look the name up before touching the parameters: fetching one renders it, and the
        // core's own functions ($if…) must keep their lazy branches.
        const ScriptFunction* f = find_script_function(std::string_view(name, len));
        const t_size argc = p->get_param_count();
        if (!f || argc < f->minArgs) { found = false; return false; }
        ScriptArgs a;
        a.v.reserve(argc);
        for (t_size i = 0; i < argc; ++i) a.v.push_back(param_str(p, i));
        TfOut o(out);
        found = m_rt.call(std::string_view(name, len), a, o);
        return found;
    }

    // --- ScriptEnv ------------------------------------------------------------
    // Arguments are titleformat snippets the runtime sometimes has to run again. Objects are
    // cached per text (the skin reuses a handful of expressions).
    titleformat_object::ptr compile_cached(std::map<std::string, titleformat_object::ptr>& cache, const std::string& text) {
        auto it = cache.find(text);
        if (it == cache.end()) {
            it = cache.emplace(text, titleformat_object::ptr()).first;
            titleformat_compiler::get()->compile_safe(it->second, text.c_str());
        }
        return it->second;
    }

    std::string eval(const std::string& in) override {
        if (m_depth >= 8 || in.empty()) return in;
        auto obj = compile_cached(m_e->m_evalcache, in);
        if (obj.is_empty()) return in;
        pfc::string8 out;
        m_depth++;
        obj->run(this, out, nullptr);
        m_depth--;
        return std::string(out.get_ptr(), out.length());
    }

    // Replay a stored snippet into the live output stream (draw commands and all), so a stored
    // draw command ($puts(fs1,$font(...))) still executes when $get(fs1) replays it.
    struct PassThrough : public pfc::string_base {
        titleformat_text_out* out;
        explicit PassThrough(titleformat_text_out* o) : out(o) {}
        const char* get_ptr() const override { return ""; }
        void add_string(const char* s, t_size n) override { out->write(titleformat_inputtypes::unknown, s, n); }
        void truncate(t_size) override {}
        t_size get_length() const override { return 0; }
        char* lock_buffer(t_size) override { return nullptr; }
        void unlock_buffer() override {}
    };
    void replay(ScriptOut& out, const std::string& text) override {
        if (m_depth >= 8 || text.empty()) return; // a $puts(x,$get(x)) would loop forever
        auto obj = compile_cached(m_e->m_evalcache, text);
        if (obj.is_empty()) return;
        PassThrough sink(static_cast<TfOut&>(out).tf); // only ever our own TfOut
        m_depth++;
        obj->run(this, sink, nullptr);
        m_depth--;
    }

    void run_subscript(ScriptRuntime& rt, const std::string& text) override {
        auto obj = compile_cached(m_e->m_subcache, text);
        if (obj.is_empty()) return;
        DrawString out;
        rt.set_text_buffer(&out.buf());
        obj->run(this, out, nullptr);
        rt.set_text_buffer(nullptr);
    }

    bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) override {
        return pui::draw_image(cv, path, x, y, w, h, alpha, flip);
    }
    // m_track is set for draw_script() (TrackDisplay/Popup): enables the album_art_manager_v2
    // fallback for non-local sources.
    bool draw_cover(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) override {
        return draw_cover_art(cv, path, m_track, x, y, w, h, alpha, flip);
    }
    bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                         float sx, float sy, float sw, float sh, int alpha) override {
        return pui::draw_image_part(cv, path, dx, dy, dw, dh, sx, sy, sw, sh, alpha);
    }
    bool image_size(const std::string& path, int& w, int& h) override { return image_natural_size(path, w, h); }
    bool file_exists(const std::string& path) override { return pui::file_exists(path); }
    void report(const std::string& msg) override { m_e->report_once(msg); }
    void set_titlebar_visible(bool v) override { if (m_e->m_main) m_e->m_main->set_titlebar_visible(v); }
    void set_tray(const std::string& tip) override { if (m_e->m_main) m_e->m_main->set_tray(tip); }
    void set_title(const std::string& t) override { if (m_e->m_main) m_e->m_main->set_title(t); }

private:
    SkinEngine* m_e;
    metadb_handle_ptr m_track;
    int m_depth = 0; // nesting depth of snippet evaluation (eval/replay)
    ScriptRuntime m_rt;
};

void SkinEngine::report_once(const std::string& msg) {
    if (m_reported.insert(msg).second) console::print(msg.c_str());
}

void SkinEngine::diagnose_script(const std::string& where, const std::string& text) {
    m_problems.erase(where); // re-read: start over
    std::set<std::string> unknown, stubbed;
    for (const std::string& name : script_calls(text)) {
        const ScriptFunction* f = find_script_function(name);
        if (f && f->status == ScriptFunction::Status::Stub) stubbed.insert("$" + name);
        else if (!f && !core_knows_function(name)) unknown.insert("$" + name);
    }
    auto join = [](const std::set<std::string>& names) {
        std::string out;
        for (auto& n : names) { if (!out.empty()) out += ", "; out += n; }
        return out;
    };
    if (!unknown.empty()) {
        report_once("Panels UI: " + where + ": unsupported function(s), rendered as errors: " + join(unknown));
        m_problems[where].push_back({ "Uses functions nothing installed knows (drawn as errors if reached): " + join(unknown), false });
    }
    if (!stubbed.empty())
        report_once("Panels UI: " + where + ": not implemented yet, ignored: " + join(stubbed));
}

void SkinEngine::render(gfx::Canvas& cv, int width, int height) {
    if (m_script.is_empty() || !m_main) return;
    if (!m_pvars_loaded) { load_pvars(); m_pvars_loaded = true; }

    m_st.placements.clear();
    m_st.buttons.clear();
    { SkinHook hook(this, cv, width, height, metadb_handle_ptr(), m_hoverX, m_hoverY);
      DrawString out; hook.runtime().set_text_buffer(&out.buf());
      // While playing, evaluate with the playback fields (%playback_time_seconds%, %length%,
      // %isplaying%…) the skin's own progress bar / time readout are drawn from.
      bool ran = false;
      if (playback_control::get()->is_playing())
          ran = playback_control::get()->playback_format_title(&hook, out, m_script, nullptr, playback_control::display_level_all);
      if (!ran) m_script->run(&hook, out, nullptr);
      hook.runtime().finish();
      // Still inside the hook's scope, so it can blit over the frame just drawn: light the
      // panel tab that is currently showing (left column showPanel:*, right column showPane:*).
      gfx::Color accent;
      const bool haveAccent = theme_color(accent);
      hook.runtime().apply_selected_buttons(m_st.buttons, haveAccent, accent); }

    m_problemMarker = draw_problem_marker(cv, width, height, main_script_label(), true);

    for (auto& p : m_st.placements) p = remap_panel(p);
    hide_unrequested_panels();
    for (const auto& p : m_st.placements) dispatch_placement(p, 0, 0);
}

void SkinEngine::draw_script(gfx::Canvas& cv, int w, int h,
                             const service_ptr_t<titleformat_object>& script,
                             const metadb_handle_ptr& track,
                             std::vector<Button>* capture,
                             int hoverX, int hoverY,
                             std::vector<Placement>* placementsOut) {
    if (script.is_empty()) return;
    if (capture) { capture->clear(); m_st.capture = capture; }
    if (placementsOut) { placementsOut->clear(); m_st.capturePlacements = placementsOut; }
    // Panels UI hands every panel script its own $get(width)/$get(height).
    m_st.tfvars["width"] = std::to_string(w);
    m_st.tfvars["height"] = std::to_string(h);
    SkinHook hook(this, cv, w, h, track, hoverX, hoverY);
    DrawString out; hook.runtime().set_text_buffer(&out.buf());
    // Playback formatting so dynamic fields (%playback_time%, %isplaying%…) resolve — but that
    // only renders the playing item: for any other track (or once playback stopped under us)
    // format that track itself, and with no track at all just run the script.
    metadb_handle_ptr np;
    bool ran = false;
    if (track.is_valid() && playback_control::get()->get_now_playing(np) && np == track)
        ran = playback_control::get()->playback_format_title(
            &hook, out, script, nullptr, playback_control::display_level_all);
    if (!ran && track.is_valid()) { track->format_title(&hook, out, script, nullptr); ran = true; }
    if (!ran) script->run(&hook, out, nullptr);
    hook.runtime().finish();
    m_st.capture = nullptr;
    m_st.capturePlacements = nullptr;
    // After the run, while the canvas is still valid: light whichever panel tab is showing.
    gfx::Color accent;
    const bool haveAccent = theme_color(accent);
    if (capture) hook.runtime().apply_selected_buttons(*capture, haveAccent, accent);
    else if (!m_st.buttons.empty()) hook.runtime().apply_selected_buttons(m_st.buttons, haveAccent, accent);
}

} // namespace pui
