#include "test.h"
#include "core/builtin_skin.h"
#include "core/fs_util.h"
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
        CHECK_EQ(find_skin_template(t.id), &t);
        CHECK(layout_wizard_script().find(std::string("SKIN:TEMPLATE:") + t.id) != std::string::npos);
        CHECK(std::filesystem::is_regular_file(std::string("res/wizard/") + t.id + ".png"));
        CHECK(layout_wizard_script().find(std::string("builtin:wizard/") + t.id + ".png") != std::string::npos);
    }
    CHECK(find_skin_template("nope") == nullptr);
}

TEST("templates: scripts only use parentheses for function calls (foobar2000's titleformat)") {
    for (auto& t : skin_templates()) check_parens(t.id, t.script);
    check_parens("wizard", layout_wizard_script());
    check_parens("welcome", builtin_test_skin());
}

TEST("templates: installed as a skin folder, never over an existing one, and lint clean") {
    TempDir tmp;
    for (auto& t : skin_templates()) {
        std::string err;
        const std::string dir = install_skin_template(t, tmp.dir + "/skins", &err);
        CHECK(err.empty());
        CHECK_EQ(dir, tmp.dir + "/skins/" + t.name);
        CHECK_EQ(read_file(dir + "/main.txt"), t.script);
        CHECK_EQ(read_file(dir + "/" + SkinConfig::kFileName), t.ini);
        for (auto& f : lint_skin(dir))
            if (f.level != LintFinding::Level::Info) t::fail(__FILE__, __LINE__, std::string(t.id) + ": " + f.file + ": " + f.message);
    }
    const SkinTemplate& first = skin_templates().front();
    write_file(tmp.dir + "/skins/" + first.name + "/main.txt", "edited");
    CHECK_EQ(install_skin_template(first, tmp.dir + "/skins"), tmp.dir + "/skins/" + first.name + " 2");
    CHECK_EQ(read_file(tmp.dir + "/skins/" + first.name + "/main.txt"), std::string("edited"));
}
