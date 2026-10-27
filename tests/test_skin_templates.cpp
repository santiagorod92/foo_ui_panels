#include "test.h"
#include "core/builtin_skin.h"
#include "core/fs_util.h"
#include "core/i18n.h"
#include "core/skin_config.h"
#include "core/skin_lint.h"
#include "core/skin_templates.h"
#include <cctype>
#include <filesystem>
#include <set>

using namespace pui;

namespace {
void check_parens(const std::string& what, const std::string& script) {
    bool quoted = false, comment = false;
    for (size_t i = 0; i < script.size(); ++i) {
        const char c = script[i];
        if (c == '\n') { comment = false; continue; }
        if (comment) continue;
        if (!quoted && c == '/' && i + 1 < script.size() && script[i + 1] == '/' && (i == 0 || script[i - 1] == '\n')) { comment = true; continue; }
        if (c == '\'') { quoted = !quoted; continue; }
        if (c != '(' || quoted) continue;
        size_t j = i;
        while (j > 0 && (isalnum((unsigned char)script[j - 1]) || script[j - 1] == '_')) --j;
        if (j == i || j == 0 || script[j - 1] != '$')
            t::fail(__FILE__, __LINE__, what + ": bare '(' at " + std::to_string(i) + ": " + script.substr(i > 30 ? i - 30 : 0, 60));
    }
}

struct TempDir {
    std::string dir;
    TempDir() { dir = (std::filesystem::temp_directory_path() / ("pui_tpl_" + std::to_string(tick_ms()))).string(); }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
}

TEST("templates: unique ids and names, each with a thumbnail, script and config") {
    std::set<std::string> ids, names;
    for (auto& t : skin_templates()) {
        CHECK(ids.insert(t.id).second);
        CHECK(names.insert(t.name).second);
        CHECK(!t.thumbnail.empty());
        CHECK(t.script.find("$panel(") != std::string::npos);
        CHECK(t.ini.find("script = main.txt") != std::string::npos);
        CHECK(t.ini.find("playlist.header = 1") != std::string::npos);
        CHECK_EQ(find_skin_template(t.id), &t);
        const bool shown = layout_wizard_script().find(std::string("SKIN:TEMPLATE:") + t.id + ",") != std::string::npos;
        CHECK_EQ(shown, !t.needs_js_panel);
        CHECK(layout_wizard_script(true).find(std::string("SKIN:TEMPLATE:") + t.id + ",") != std::string::npos);
        CHECK(std::filesystem::is_regular_file(std::string("res/wizard/") + t.id + ".png"));
        CHECK(layout_wizard_script(true).find(std::string("builtin:wizard/") + t.id + ".png") != std::string::npos);
    }
    CHECK(find_skin_template("nope") == nullptr);
}

TEST("templates: the JS Panel layout is only offered with JS Panel installed, and ships its scripts") {
    const SkinTemplate* js = find_skin_template("albums-js");
    CHECK(js != nullptr);
    if (!js) return;
    CHECK(js->needs_js_panel);
    CHECK_EQ(wizard_templates(false).size() + 3, wizard_templates(true).size());
    for (const SkinTemplate* t : wizard_templates(false)) CHECK(!t->needs_js_panel);
    CHECK(js->script.find("$panel(NowPlaying,JS Panel,") != std::string::npos);
    CHECK(js->ini.find("panel.NowPlaying.script = js/now_playing.js") != std::string::npos);
    CHECK_EQ(js->files.size(), (size_t)2);
    TempDir tmp;
    std::string err;
    const std::string dir = install_skin_template(*js, tmp.dir, &err);
    CHECK(err.empty());
    const std::string now = read_file(dir + "/js/now_playing.js");
    CHECK(now.find("include('lib/palette.js')") != std::string::npos);
    CHECK(read_file(dir + "/js/lib/palette.js").find("const palette") != std::string::npos);
    SkinConfig cfg;
    cfg.load(dir);
    CHECK_EQ(cfg.str("panel.NowPlaying.script"), std::string("js/now_playing.js"));
}

TEST("templates: names and summaries fit the wizard cards in every language") {
    using namespace pui::i18n;
    auto ems = [](const char* s, double latin) {
        double w = 0;
        for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
            if ((*p & 0xC0) == 0x80) continue;
            w += *p >= 0xE3 ? 1.0 : latin;
        }
        return w;
    };
    const double cardText = (1000.0 - 104) / 4 - 24;
    const double nameMax = cardText / 14.7, summaryMax = 3 * cardText / 12.0 * 0.85;
    const Language before = language();
    for (const auto& info : kLanguages) {
        set_language(info.lang);
        for (auto& t : skin_templates()) {
            if (ems(tr(t.name), 0.55) > nameMax) t::fail(__FILE__, __LINE__, std::string(info.code) + " name too wide: " + tr(t.name));
            if (ems(tr(t.summary), 0.5) > summaryMax)
                t::fail(__FILE__, __LINE__, std::string(info.code) + " summary too long for 3 lines: " + tr(t.summary));
        }
    }
    set_language(before);
    const std::string wiz = layout_wizard_script(true);
    CHECK(wiz.find(",50,150-155-165,wrap)") != std::string::npos);
    CHECK(wiz.find("$drawstring(JS,") != std::string::npos);
    CHECK(layout_wizard_script(false).find("$drawstring(JS,") == std::string::npos);
}

TEST("templates: scripts only use parentheses for function calls (foobar2000's titleformat)") {
    for (auto& t : skin_templates()) check_parens(t.id, t.script);
    check_parens("wizard", layout_wizard_script());
    check_parens("wizard with JS Panel", layout_wizard_script(true));
    check_parens("welcome", builtin_test_skin());
}

TEST("templates: the playlist and library JS layouts ship their scripts and point each panel at one") {
    const SkinTemplate* pl = find_skin_template("playlist-js");
    const SkinTemplate* lib = find_skin_template("library-js");
    CHECK(pl != nullptr && lib != nullptr);
    if (!pl || !lib) return;
    CHECK(pl->needs_js_panel && lib->needs_js_panel);
    CHECK(pl->script.find("$panel(Playlist,Single Column Playlist,") != std::string::npos);
    CHECK(lib->script.find("$panel(Library,JS Panel,") != std::string::npos);
    CHECK(lib->script.find("$panel(Artists,JS Panel,") != std::string::npos);
    CHECK(lib->ini.find("panel.Library.script = js/library_tree.js") != std::string::npos);
    CHECK(lib->ini.find("panel.Artists.script = js/navidrome_artists.js") != std::string::npos);
    CHECK_EQ(lib->files.size(), (size_t)4);
    TempDir tmp;
    std::string err;
    const std::string dir = install_skin_template(*lib, tmp.dir, &err);
    CHECK(err.empty());
    for (const char* f : { "js/now_playing.js", "js/lib/palette.js", "js/library_tree.js", "js/navidrome_artists.js" })
        CHECK(std::filesystem::is_regular_file(fs_path(dir + "/" + f)));
    const std::string wiz = layout_wizard_script(true);
    CHECK(wiz.find("$eval({%_width% - 104} / 4)") != std::string::npos);
    CHECK(wiz.find("$eval(2 * {{%_height% - 182} / 2 - 88})") != std::string::npos);
    CHECK(wiz.find("$eval(2 * {{%_height% - 182} / 2 - 40})") != std::string::npos);
    CHECK(layout_wizard_script(false).find("$eval({%_width% - 88} / 3)") != std::string::npos);
}

TEST("templates: installed as a skin folder, never over an existing one, and lint clean") {
    TempDir tmp;
    for (auto& t : skin_templates()) {
        std::string err;
        const std::string dir = install_skin_template(t, tmp.dir + "/skins", &err);
        CHECK(err.empty());
        CHECK_EQ(dir, tmp.dir + "/skins" + kPathSep + t.name);
        CHECK_EQ(read_file(dir + "/main.txt"), t.script);
        CHECK_EQ(read_file(dir + "/" + SkinConfig::kFileName), t.ini);
        for (auto& f : lint_skin(dir))
            if (f.level != LintFinding::Level::Info) t::fail(__FILE__, __LINE__, std::string(t.id) + ": " + f.file + ": " + f.message);
    }
    const SkinTemplate& first = skin_templates().front();
    write_file(tmp.dir + "/skins/" + first.name + "/main.txt", "edited");
    CHECK_EQ(install_skin_template(first, tmp.dir + "/skins"), tmp.dir + "/skins" + kPathSep + first.name + " 2");
    CHECK_EQ(read_file(tmp.dir + "/skins/" + first.name + "/main.txt"), std::string("edited"));
}
