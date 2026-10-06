// The Panels UI script functions ($panel, $font, $drawrect, $button, $get/$puts, …) and the draw
// state they share — the interpreter half of the skin engine that doesn't need foobar2000.
//
// foobar2000's titleformat engine parses and runs the script; for every $function it doesn't know
// it asks the skin hook (skin_hook.cpp), which hands the call to ScriptRuntime::call(). Everything
// that does need the SDK or the platform — nested titleformat evaluation, image decoding, album
// art, the main window — is reached through ScriptEnv, so the runtime builds into the host-side
// unit tests (`make test`) and the offline skin linter.
//
// The function table (script_functions()) is the single list of what the engine implements: it
// drives the dispatch, the skin diagnostics ("unsupported function") and the generated reference
// docs/SCRIPT_FUNCTIONS.md.
#pragma once
#include "../gfx/canvas.h"
#include "button.h"
#include "pvars.h"
#include "skin_config.h"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pui {

// Text a function writes back into the script's output (e.g. $eval's result, $get's value).
struct ScriptOut {
    virtual ~ScriptOut() = default;
    virtual void write(std::string_view text) = 0;
    void write_int(long v) { write(std::to_string(v)); }
};

// The rendered arguments of one call. titleformat renders each argument before the hook sees it.
struct ScriptArgs {
    std::vector<std::string> v;
    size_t size() const { return v.size(); }
    const std::string& str(size_t i) const { static const std::string none; return i < v.size() ? v[i] : none; }
    int num(size_t i) const;
};

class ScriptRuntime;

// What the runtime asks of the world outside it.
struct ScriptEnv {
    virtual ~ScriptEnv() = default;
    // titleformat: run `snippet` (whose own $functions come back through the runtime) and return
    // its text; replay it into `out`; run it as a sub-draw into a fresh text buffer (it must hand
    // that buffer to rt.set_text_buffer() for the duration of the run, see $button2).
    virtual std::string eval(const std::string& snippet) = 0;
    virtual void replay(ScriptOut& out, const std::string& snippet) = 0;
    virtual void run_subscript(ScriptRuntime& rt, const std::string& snippet) = 0;
    // Images (image_cache on the real engine). draw_cover may fall back to the track's album art.
    virtual bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) = 0;
    virtual bool draw_cover(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) = 0;
    virtual bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                                 float sx, float sy, float sw, float sh, int alpha) = 0;
    virtual bool image_size(const std::string& path, int& w, int& h) = 0;
    virtual bool file_exists(const std::string& path) = 0;
    // A problem worth one console line (missing image…). The engine de-duplicates.
    virtual void report(const std::string& msg) = 0;
    // The player window ($windowstyle, $settray, $settitle). No-ops for a panel without one.
    virtual void set_titlebar_visible(bool) {}
    virtual void set_tray(const std::string&) {}
    virtual void set_title(const std::string&) {}
};

// Engine state the functions read and write — owned by the SkinEngine, outlives a run.
struct ScriptState {
    PvarMap pvars;                                 // persistent ($setpvar, PVAR:SET)
    std::map<std::string, std::string> tfvars;     // $puts/$get scratch pool
    std::vector<Placement> placements;             // $panel() calls of the master script
    std::vector<Button> buttons;                   // clickable regions of the master script
    std::vector<Button>* capture = nullptr;        // when set, buttons record here instead
    std::vector<Placement>* capturePlacements = nullptr; // when set, $panel() records here
    std::set<std::string> imagesChecked;           // skin images already checked for existence
    SkinConfig cfg;
    std::string base;                              // skin folder (resolves relative paths)
};

struct ScriptFunction;

// One run of a script against one canvas (a paint of the master canvas or of a panel).
class ScriptRuntime {
public:
    ScriptRuntime(ScriptState& st, ScriptEnv& env, gfx::Canvas& cv, int w, int h, int hoverX = -1, int hoverY = -1)
        : m_st(st), m_env(env), m_cv(cv), m_w(w), m_h(h), m_hoverX(hoverX), m_hoverY(hoverY) {}
    ~ScriptRuntime() { flush_text(); }
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Runs $name. False if the name isn't one of ours or has too few arguments (the core then
    // renders its "unknown function" marker, as the original did).
    bool call(std::string_view name, const ScriptArgs& args, ScriptOut& out);

    // The script's literal output so far. Text written after an $alignabs is drawn in its box at
    // the next flush (a $font/colour change, another box, the end of the run): it is read from the
    // live buffer then, because titleformat appends $if conditions' values and truncates them away
    // again, and those must never be drawn.
    void set_text_buffer(const std::string* b) { m_buf = b; m_flushFrom = b ? b->size() : 0; }
    // End of the run: flush the last text box while the buffer is alive, then detach from it.
    void finish() { flush_text(); m_buf = nullptr; }
    // $button2's sub-draw: runs `snippet` with the draw origin moved to (ox,oy).
    void run_subscript(const std::string& snippet, int ox, int oy);
    // After the run: light the selected member of every pvar radio group in `list`.
    void apply_selected_buttons(std::vector<Button>& list, bool haveAccent, gfx::Color accent);

    // $eval's argument with $get(x)/$getpvar(x) substituted (it is arithmetic over text the core
    // already rendered, but the skin nests $get calls in it).
    std::string resolve_vars(const std::string& in) const;
    // A skin path: separators normalised, ./ and / stripped, relative to the skin folder.
    std::string resolve(std::string p) const;

    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    friend const std::vector<ScriptFunction>& script_functions();
    // --- the functions (see the table in script_runtime.cpp for their signatures) ---
    bool f_panel(const ScriptArgs&, ScriptOut&);
    bool f_eval(const ScriptArgs&, ScriptOut&);
    bool f_get(const ScriptArgs&, ScriptOut&);
    bool f_puts(const ScriptArgs&, ScriptOut&);
    bool f_getpvar(const ScriptArgs&, ScriptOut&);
    bool f_setpvar(const ScriptArgs&, ScriptOut&);
    bool f_blend_target(const ScriptArgs&, ScriptOut&);
    bool f_offset_colour(const ScriptArgs&, ScriptOut&);
    bool f_windowstyle(const ScriptArgs&, ScriptOut&);
    bool f_font(const ScriptArgs&, ScriptOut&);
    bool f_drawrect(const ScriptArgs&, ScriptOut&);
    bool f_drawroundrect(const ScriptArgs&, ScriptOut&);
    bool f_gradientrect(const ScriptArgs&, ScriptOut&);
    bool f_drawstring(const ScriptArgs&, ScriptOut&);
    bool f_draw_text(const ScriptArgs&, ScriptOut&);
    bool f_alignabs(const ScriptArgs&, ScriptOut&);
    bool f_textcolor(const ScriptArgs&, ScriptOut&);
    bool f_imageabs(const ScriptArgs&, ScriptOut&);
    bool f_fileexists(const ScriptArgs&, ScriptOut&);
    bool f_greater(const ScriptArgs&, ScriptOut&);
    bool f_imageabs2(const ScriptArgs&, ScriptOut&);
    bool f_button(const ScriptArgs&, ScriptOut&);
    bool f_calcwidth(const ScriptArgs&, ScriptOut&);
    bool f_textbutton(const ScriptArgs&, ScriptOut&);
    bool f_imagebutton(const ScriptArgs&, ScriptOut&);
    bool f_settray(const ScriptArgs&, ScriptOut&);
    bool f_settitle(const ScriptArgs&, ScriptOut&);
    bool f_gp_set_brush(const ScriptArgs&, ScriptOut&);
    bool f_gp_fill_rectangle(const ScriptArgs&, ScriptOut&);
    bool f_gp_set_pen(const ScriptArgs&, ScriptOut&);
    bool f_gp_draw_rectangle(const ScriptArgs&, ScriptOut&);
    bool f_stub(const ScriptArgs&, ScriptOut&);

    std::string eval_arg(const std::string& in);
    std::string image_path(const std::string& raw);
    void select_font(const std::string& face, int size, const std::string& style);
    void draw_glow(const std::string& s, const gfx::Rect& r, unsigned fmt);
    void flush_text();
    std::vector<Button>& button_list() { return m_st.capture ? *m_st.capture : m_st.buttons; }
    gfx::Rect mkrect(int x, int y, int w, int h) const { return gfx::Rect{ x + m_ox, y + m_oy, w, h }; }

    ScriptState& m_st; ScriptEnv& m_env; gfx::Canvas& m_cv;
    int m_w, m_h;
    int m_hoverX, m_hoverY; // mouse position in this run's coordinates, -1,-1 if not over it
    // Draw origin: (0,0), or a button's (x,y) while its $button2 sub-draw runs.
    int m_ox = 0, m_oy = 0;
    bool m_haveFont = false; // a $font has been selected this run (glow needs one)
    struct FontState { std::string face; int size; std::string style; gfx::Color col; };
    // Every $font this run, and its length after the last $button/$button2 — lets a text button
    // recover the normal-state font (see f_button).
    std::vector<FontState> m_fontLog; size_t m_fontLogMark = 0;
    gfx::Color m_glowCol; int m_glowExpand = 0, m_glowAlpha = 0;
    gfx::Color m_gpBrush; int m_gpAlpha = 255;                    // $gp_set_brush
    gfx::Color m_gpPen; int m_gpPenAlpha = 255, m_gpPenWidth = 1; // $gp_set_pen
    struct LTRB { int left = 0, top = 0, right = 0, bottom = 0; };
    bool m_aligned = false; LTRB m_alignRect; unsigned m_alignFlags = 0;
    gfx::Color m_textcol{ 255, 255, 255 };
    const std::string* m_buf = nullptr; size_t m_flushFrom = 0;
};

// One entry of the function table.
struct ScriptFunction {
    enum class Status { Implemented, Stub }; // Stub: accepted, renders nothing (not implemented)
    const char* name;
    size_t minArgs;
    Status status;
    bool (ScriptRuntime::*fn)(const ScriptArgs&, ScriptOut&);
    const char* signature; // for the docs, e.g. "$drawrect(x,y,w,h,spec)"
    const char* summary;
};
const std::vector<ScriptFunction>& script_functions();
const ScriptFunction* find_script_function(std::string_view name);

// Every `$name(` a script calls, outside '…' literals, in order of first use.
std::vector<std::string> script_calls(const std::string& text);
// docs/SCRIPT_FUNCTIONS.md, generated from the table (tools/skin_lint --functions-md).
std::string script_functions_markdown();

} // namespace pui
