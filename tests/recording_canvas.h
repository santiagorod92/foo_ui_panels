// Test doubles for the script runtime: a gfx::Canvas that records every call as a line of text
// ("fill_rect 1,2,3,4 #ff0000") and a ScriptEnv with no titleformat engine behind it (snippets
// evaluate to themselves unless the test maps them) that records image draws the same way.
#pragma once
#include "core/script_runtime.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace t {

inline std::string col(pui::gfx::Color c) {
    char b[8]; std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b); return b;
}
inline std::string rect(const pui::gfx::Rect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h);
}

class RecordingCanvas : public pui::gfx::Canvas {
public:
    std::vector<std::string> ops;
    int charWidth = 7; // text_width = chars * charWidth
    bool fontAvailable = true; // set_font's result (false = "substituted")

    int width() const override { return 800; }
    int height() const override { return 600; }
    int dpi() const override { return 96; }
    void fill_rect(const pui::gfx::Rect& r, pui::gfx::Color c) override { ops.push_back("fill_rect " + rect(r) + " " + col(c)); }
    void fill_rect_alpha(const pui::gfx::Rect& r, pui::gfx::Color c, int a) override {
        ops.push_back("fill_rect_alpha " + rect(r) + " " + col(c) + " a" + std::to_string(a));
    }
    void frame_rect(const pui::gfx::Rect& r, pui::gfx::Color c) override { ops.push_back("frame_rect " + rect(r) + " " + col(c)); }
    void fill_round_rect(const pui::gfx::Rect& r, int rx, int ry, pui::gfx::Color c) override {
        ops.push_back("round_rect " + rect(r) + " " + std::to_string(rx) + "," + std::to_string(ry) + " " + col(c));
    }
    void gradient_v(const pui::gfx::Rect& r, pui::gfx::Color a, pui::gfx::Color b) override {
        ops.push_back("gradient " + rect(r) + " " + col(a) + ">" + col(b));
    }
    void line(int, int, int, int, pui::gfx::Color) override { ops.push_back("line"); }
    void fill_polygon(const pui::gfx::PointF*, int, pui::gfx::Color) override { ops.push_back("polygon"); }
    bool set_font(const pui::gfx::FontSpec& s) override {
        ops.push_back("font " + s.face + " " + std::to_string((int)s.size) + (s.bold ? " bold" : "") +
                      (s.italic ? " italic" : "") + (s.underline ? " underline" : ""));
        return fontAvailable;
    }
    void draw_text(std::string_view text, const pui::gfx::Rect& r, unsigned flags, pui::gfx::Color c) override {
        ops.push_back("text \"" + std::string(text) + "\" " + rect(r) + " f" + std::to_string(flags) + " " + col(c));
    }
    int text_width(std::string_view text) override { return (int)text.size() * charWidth; }
    int text_height(std::string_view, int, unsigned) override { return 12; }
    bool text_coverage(std::string_view, const pui::gfx::Rect& r, unsigned, const pui::gfx::Rect& area,
                       std::vector<uint8_t>& mask) override {
        // A solid block where the text box overlaps the area.
        mask.assign((size_t)area.w * area.h, 0);
        for (int y = 0; y < area.h; ++y) for (int x = 0; x < area.w; ++x)
            if (r.contains(area.x + x, area.y + y)) mask[(size_t)y * area.w + x] = 255;
        return true;
    }
    void blend_argb(const pui::gfx::Rect& r, const uint32_t*, int) override { ops.push_back("glow " + rect(r)); }
    void draw_image(const pui::gfx::Image&, const pui::gfx::RectF&, const pui::gfx::RectF&, int, bool,
                    pui::gfx::Interp) override { ops.push_back("image"); }
    pui::gfx::ImagePtr snapshot(const pui::gfx::Rect&) override { return nullptr; }
};

struct StringOut : pui::ScriptOut {
    std::string text;
    void write(std::string_view s) override { text += s; }
};

class FakeEnv : public pui::ScriptEnv {
public:
    std::vector<std::string>& ops;        // shared with the canvas, so the order is one log
    std::map<std::string, std::string> evals; // snippet -> rendered text (else itself)
    std::map<std::string, std::pair<int, int>> images; // path -> natural size (exists)
    std::vector<std::string> reports, subscripts;
    bool titlebar = true; std::string tray = "-", title;

    explicit FakeEnv(std::vector<std::string>& log) : ops(log) {}
    std::string eval(const std::string& s) override { auto it = evals.find(s); return it != evals.end() ? it->second : s; }
    void replay(pui::ScriptOut& out, const std::string& s) override { out.write("<" + s + ">"); }
    void run_subscript(pui::ScriptRuntime&, const std::string& s) override { subscripts.push_back(s); ops.push_back("sub " + s); }
    bool draw_image(pui::gfx::Canvas&, const std::string& p, int x, int y, int w, int h, int a, int f) override {
        ops.push_back("img " + p + " " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                      std::to_string(h) + (a != 255 ? " a" + std::to_string(a) : "") + (f ? " flip" + std::to_string(f) : ""));
        return images.count(p) != 0;
    }
    bool draw_cover(pui::gfx::Canvas& cv, const std::string& p, int x, int y, int w, int h, int a, int f) override {
        return draw_image(cv, p, x, y, w, h, a, f);
    }
    bool draw_image_part(pui::gfx::Canvas&, const std::string& p, int dx, int dy, int dw, int dh,
                         float sx, float sy, float sw, float sh, int) override {
        char b[160];
        std::snprintf(b, sizeof b, "part %s %d,%d,%d,%d <- %g,%g,%g,%g", p.c_str(), dx, dy, dw, dh, sx, sy, sw, sh);
        ops.push_back(b);
        return true;
    }
    bool image_size(const std::string& p, int& w, int& h) override {
        auto it = images.find(p);
        if (it == images.end()) return false;
        w = it->second.first; h = it->second.second; return true;
    }
    bool file_exists(const std::string& p) override { return images.count(p) != 0; }
    void report(const std::string& m) override { reports.push_back(m); }
    void set_titlebar_visible(bool v) override { titlebar = v; }
    void set_tray(const std::string& s) override { tray = s; }
    void set_title(const std::string& s) override { title = s; }
};

// One canvas + env + state, and a call helper: call("drawrect", {"1","2","3","4","brushcolor-1-2-3"}).
struct Rig {
    RecordingCanvas cv;
    FakeEnv env{ cv.ops };
    pui::ScriptState st;
    std::string buf; // the script's literal output (what titleformat would append)
    pui::ScriptRuntime rt;
    explicit Rig(int hoverX = -1, int hoverY = -1) : rt(st, env, cv, 400, 300, hoverX, hoverY) {
        rt.set_text_buffer(&buf);
    }
    std::string call(const char* name, std::vector<std::string> args, bool* found = nullptr) {
        pui::ScriptArgs a; a.v = std::move(args);
        StringOut o;
        const bool ok = rt.call(name, a, o);
        if (found) *found = ok;
        return o.text;
    }
    std::vector<std::string>& ops() { return cv.ops; }
};

} // namespace t
