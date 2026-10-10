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

struct ScriptOut {
    virtual ~ScriptOut() = default;
    virtual void write(std::string_view text) = 0;
    void write_int(long v) { write(std::to_string(v)); }
};

struct ScriptArgs {
    std::vector<std::string> v;
    size_t size() const { return v.size(); }
    const std::string& str(size_t i) const { static const std::string none; return i < v.size() ? v[i] : none; }
    int num(size_t i) const;
};

class ScriptRuntime;

struct ScriptEnv {
    virtual ~ScriptEnv() = default;
    virtual std::string eval(const std::string& snippet) = 0;
    virtual void replay(ScriptOut& out, const std::string& snippet) = 0;
    virtual void run_subscript(ScriptRuntime& rt, const std::string& snippet) = 0;
    virtual bool draw_image(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) = 0;
    virtual bool draw_cover(gfx::Canvas& cv, const std::string& path, int x, int y, int w, int h, int alpha, int flip) = 0;
    virtual bool draw_image_part(gfx::Canvas& cv, const std::string& path, int dx, int dy, int dw, int dh,
                                 float sx, float sy, float sw, float sh, int alpha) = 0;
    virtual bool image_size(const std::string& path, int& w, int& h) = 0;
    virtual bool file_exists(const std::string& path) = 0;
    virtual void report(const std::string& msg) = 0;
    virtual void set_titlebar_visible(bool) {}
    virtual void set_tray(const std::string&) {}
    virtual void set_title(const std::string&) {}
};

struct ScriptState {
    PvarMap pvars;
    std::map<std::string, std::string> tfvars;
    std::vector<Placement> placements;
    std::vector<Button> buttons;
    std::vector<Button>* capture = nullptr;
    std::vector<Placement>* capturePlacements = nullptr;
    std::set<std::string> imagesChecked;
    SkinConfig cfg;
    std::string base;
};

struct ScriptFunction;

class ScriptRuntime {
public:
    ScriptRuntime(ScriptState& st, ScriptEnv& env, gfx::Canvas& cv, int w, int h, int hoverX = -1, int hoverY = -1)
        : m_st(st), m_env(env), m_cv(cv), m_w(w), m_h(h), m_hoverX(hoverX), m_hoverY(hoverY) {}
    ~ScriptRuntime() { flush_text(); }
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    bool call(std::string_view name, const ScriptArgs& args, ScriptOut& out);

    void set_text_buffer(const std::string* b) { m_buf = b; m_flushFrom = b ? b->size() : 0; }
    void finish() { flush_text(); m_buf = nullptr; }
    void run_subscript(const std::string& snippet, int ox, int oy);
    void apply_selected_buttons(std::vector<Button>& list, bool haveAccent, gfx::Color accent);

    std::string resolve_vars(const std::string& in) const;
    std::string resolve(std::string p) const;

    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    friend const std::vector<ScriptFunction>& script_functions();
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
    int m_hoverX, m_hoverY;
    int m_ox = 0, m_oy = 0;
    bool m_haveFont = false;
    struct FontState { std::string face; int size; std::string style; gfx::Color col; };
    std::vector<FontState> m_fontLog; size_t m_fontLogMark = 0;
    gfx::Color m_glowCol; int m_glowExpand = 0, m_glowAlpha = 0;
    gfx::Color m_gpBrush; int m_gpAlpha = 255;
    gfx::Color m_gpPen; int m_gpPenAlpha = 255, m_gpPenWidth = 1;
    struct LTRB { int left = 0, top = 0, right = 0, bottom = 0; };
    bool m_aligned = false; LTRB m_alignRect; unsigned m_alignFlags = 0;
    gfx::Color m_textcol{ 255, 255, 255 };
    const std::string* m_buf = nullptr; size_t m_flushFrom = 0;
};

struct ScriptFunction {
    enum class Status { Implemented, Stub };
    const char* name;
    size_t minArgs;
    Status status;
    bool (ScriptRuntime::*fn)(const ScriptArgs&, ScriptOut&);
    const char* signature;
    const char* summary;
};
const std::vector<ScriptFunction>& script_functions();
const ScriptFunction* find_script_function(std::string_view name);

std::vector<std::string> script_calls(const std::string& text);
std::string script_functions_markdown();

}
