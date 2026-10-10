#include "test.h"
#include "mini_titleformat.h"
#include "core/builtin_skin.h"
#include "core/fs_util.h"
#include "core/skin_lint.h"
#include "core/skin_templates.h"
#include <cstdlib>

using namespace pui;

namespace {

std::string join(const std::vector<std::string>& lines) {
    std::string s;
    for (auto& l : lines) { s += l; s += '\n'; }
    return s;
}

void check_golden(const std::string& name, const std::string& script, int w, int h,
                  const std::map<std::string, std::string>& fields = {}) {
    t::RecordingCanvas cv;
    t::MiniTf env(cv.ops);
    env.fields = fields;
    ScriptState st;
    {
        ScriptRuntime rt(st, env, cv, w, h);
        env.render(rt, script);
    }
    for (auto& b : st.buttons)
        cv.ops.push_back("button " + t::rect(gfx::Rect{ b.x, b.y, b.w, b.h }) + " " + b.action +
                         (b.tooltip.empty() ? "" : " tooltip=" + b.tooltip));
    for (auto& p : st.placements)
        cv.ops.push_back("panel " + p.name + " " + p.type + " " + std::to_string(p.x) + "," + std::to_string(p.y) + "," +
                         std::to_string(p.w) + "," + std::to_string(p.h));
    const std::string got = join(cv.ops), path = "tests/golden/" + name + ".txt";
    if (std::getenv("UPDATE_GOLDEN")) { write_file(path, got); return; }
    const std::string want = read_file(path);
    if (got == want) return;
    write_file("build/tests/" + name + ".actual.txt", got);
    t::fail(__FILE__, __LINE__, "render of '" + name + "' differs from " + path + " (got: build/tests/" + name +
                                    ".actual.txt; diff them, or UPDATE_GOLDEN=1 make test if intended)");
}

}

TEST("golden: the built-in welcome screen") {
    check_golden("welcome", builtin_test_skin(), 640, 480, { { "pui_skin_folder", "C:\\skins\\none" } });
}

TEST("golden: the welcome screen at another size") {
    check_golden("welcome_small", builtin_test_skin(), 420, 300, { { "pui_skin_folder", "/Users/x/skins" } });
}

TEST("golden: the layout wizard") {
    check_golden("wizard", layout_wizard_script(), 900, 600);
}

TEST("golden: every layout template, stopped") {
    for (auto& t : skin_templates()) check_golden(std::string("template_") + t.id, t.script, 1000, 640);
}

TEST("golden: the default layout while playing") {
    check_golden("template_playing", skin_templates().front().script, 1000, 640,
                 { { "isplaying", "1" }, { "title", "Song" }, { "artist", "Artist" }, { "album", "Album" }, { "date", "2001" },
                   { "codec", "FLAC" }, { "bitrate", "900" }, { "playback_time", "1:02" }, { "length", "4:05" } });
}

static const char* kSynthetic = "tests/skins/synthetic";

TEST("golden: the synthetic skin is clean for skin_lint") {
    const auto findings = lint_skin(kSynthetic);
    for (auto& f : findings)
        if (f.level != LintFinding::Level::Info) t::fail(__FILE__, __LINE__, "lint: " + f.file + ": " + f.message);
    std::string all;
    for (const char* f : { "/synthetic.txt", "/panels/display.txt", "/panels/Popup.txt" }) all += read_file(std::string(kSynthetic) + f);
    for (auto& fn : script_functions())
        if (all.find("$" + std::string(fn.name) + "(") == std::string::npos)
            t::fail(__FILE__, __LINE__, std::string("synthetic skin never calls $") + fn.name);
}

TEST("golden: the synthetic skin's scripts") {
    const std::map<std::string, std::string> track = { { "title", "Song" }, { "artist", "Band" } };
    const std::string dir = kSynthetic;
    check_golden("synthetic_main", read_file(dir + "/synthetic.txt"), 640, 400, track);
    check_golden("synthetic_display", read_file(dir + "/panels/display.txt"), 200, 60, track);
    check_golden("synthetic_popup", read_file(dir + "/panels/Popup.txt"), 300, 200);
}
