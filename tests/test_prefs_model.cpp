#include "test.h"
#include "core/fs_util.h"
#include "core/prefs_model.h"
#include <filesystem>

using namespace pui;

namespace {
// Skins in a temp folder; settings in memory.
struct FakeBackend : PrefsBackend {
    PrefsSettings stored;
    int stores = 0;
    std::string root;
    PrefsSettings load() override { return stored; }
    void store(const PrefsSettings& now, const PrefsSettings&) override { stored = now; ++stores; }
    std::vector<std::string> list_skins(const std::string& r) override {
        std::vector<std::string> out;
        std::error_code ec;
        if (r.empty()) return out;
        for (auto& e : std::filesystem::directory_iterator(r, ec)) if (e.is_directory()) out.push_back(e.path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }
    std::string skin_dir(const std::string& r, const std::string& a) override {
        return !r.empty() && !a.empty() ? r + "/" + a : root + "/own";
    }
};
struct Skins {
    std::string dir;
    Skins() {
        dir = (std::filesystem::temp_directory_path() / ("pui_prefs_" + std::to_string(tick_ms()))).string();
        for (const char* d : { "/lib/alpha", "/lib/beta", "/own" }) std::filesystem::create_directories(dir + d);
        write_file(dir + "/lib/alpha/main.txt", "$getpvar(Colour)$button(0,0,0,0,0,0,a,b,'PVAR:SET:showPanel:2')");
        write_file(dir + "/lib/beta/one.txt", "x");
        write_file(dir + "/lib/beta/two.txt", "y");
        write_file(dir + "/lib/beta/preview.png", "png");
    }
    ~Skins() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
} // namespace

TEST("prefs: load, edit, changed, apply") {
    Skins s;
    FakeBackend b; b.root = s.dir;
    b.stored.root = s.dir + "/lib"; b.stored.active = "alpha";
    b.stored.pvars["Colour"] = "2";
    PrefsModel m(b);
    m.load();
    CHECK(!m.changed());
    CHECK_EQ(m.skins().size(), (size_t)2);
    CHECK_EQ(m.script(), std::string("$getpvar(Colour)$button(0,0,0,0,0,0,a,b,'PVAR:SET:showPanel:2')"));
    m.set_zoom_index(3); // 75, 90, [100]
    CHECK_EQ(m.pending().zoom, 100);
    CHECK_EQ(m.zoom_index(), 3);
    CHECK(m.changed());
    m.apply();
    CHECK(!m.changed());
    CHECK_EQ(b.stores, 1);
    CHECK_EQ(b.stored.zoom, 100);
}

TEST("prefs: skin choice — root keeps or replaces the active skin, main script resets") {
    Skins s;
    FakeBackend b; b.root = s.dir;
    PrefsModel m(b);
    m.load();
    CHECK(m.skins().empty());
    m.set_root(s.dir + "/lib");
    CHECK_EQ(m.pending().active, std::string("alpha")); // first one
    m.set_active("beta");
    CHECK_EQ(m.main_choices().size(), (size_t)2);
    CHECK(!m.skin_warning().empty());          // two scripts, no config: ambiguous
    m.set_main("two.txt");
    CHECK_EQ(m.script(), std::string("y"));
    CHECK(m.skin_warning().empty());
    CHECK(m.preview_image().find("preview.png") != std::string::npos);
    m.set_root(s.dir + "/lib");               // same root: beta stays
    CHECK_EQ(m.pending().active, std::string("beta"));
    CHECK_EQ(m.pending().main, std::string()); // but the main script is automatic again
}

TEST("prefs: chosen skin folder — a skin, a folder of skins, ambiguous, nothing to run") {
    Skins s;
    std::filesystem::create_directories(s.dir + "/empty/sub");
    FakeBackend b; b.root = s.dir;
    PrefsModel m(b);
    m.load();
    m.choose_skin_folder(s.dir + "/lib/alpha/");     // the skin itself
    CHECK_EQ(m.pending().root, s.dir + "/lib");
    CHECK_EQ(m.pending().active, std::string("alpha"));
    CHECK(m.chosen_folder_problem("x").empty());
    m.choose_skin_folder(s.dir + "/lib");            // a folder of skins: first one
    CHECK(m.chosen_folder_problem("x").empty());
    m.choose_skin_folder(s.dir + "/lib/beta");       // two .txt: ambiguous, named as such
    CHECK(m.chosen_folder_problem(s.dir + "/lib/beta").find("2 .txt files") != std::string::npos);
    m.choose_skin_folder(s.dir + "/empty");          // no script anywhere: the skin itself, kept
    CHECK_EQ(m.pending().root, s.dir);
    CHECK_EQ(m.pending().active, std::string("empty"));
    const std::string p = m.chosen_folder_problem(s.dir + "/empty");
    CHECK(p.find("no main script to run") != std::string::npos);
    CHECK(p.find(s.dir + "/empty\n") != std::string::npos); // the folder picked, not its subfolder
}

TEST("prefs: script edits are written on apply") {
    Skins s;
    FakeBackend b; b.root = s.dir;
    b.stored.root = s.dir + "/lib"; b.stored.active = "beta"; b.stored.main = "one.txt";
    PrefsModel m(b);
    m.load();
    m.set_script("edited");
    CHECK(m.changed());
    m.apply();
    CHECK_EQ(read_file(s.dir + "/lib/beta/one.txt"), std::string("edited"));
}

TEST("prefs: overrides and variables") {
    Skins s;
    FakeBackend b; b.root = s.dir;
    b.stored.root = s.dir + "/lib"; b.stored.active = "alpha";
    b.stored.pvars["_once.x"] = "1";
    PrefsModel m(b);
    m.load();
    m.set_font("Verdana", "10");
    m.set_accent(gfx::Color(1, 2, 3));
    gfx::Color c;
    CHECK(m.accent(c) && c == gfx::Color(1, 2, 3));
    CHECK_EQ(m.pending().pvars.at(PrefsModel::kAccentKey), std::string("1-2-3"));
    CHECK(m.variables().empty()); // reserved + bookkeeping pvars are hidden
    m.rescan_variables();
    auto vars = m.variables();
    CHECK_EQ(vars.size(), (size_t)2);
    CHECK_EQ(vars[0].first, std::string("Colour"));
    CHECK_EQ(vars[1].first, std::string("showPanel"));
    m.set_variable("_prefs_font_face", "nope"); // reserved: ignored
    CHECK_EQ(m.font_face(), std::string("Verdana"));
    m.set_font("", "");
    CHECK(m.font_face().empty());
    m.reset();
    CHECK(!m.accent(c));
    CHECK(m.pending().root.empty());
    CHECK_EQ(m.variables().size(), (size_t)2); // the skin's variables stay
}

TEST("prefs: scan_pvar_names finds functions and button actions") {
    auto n = PrefsModel::scan_pvar_names("$getpvar( a )$setpvar(b,1)'PVAR:SET:c:1''PVAR:TOGGLE:d'$getpvar($get(x))");
    CHECK_EQ(n.size(), (size_t)4);
    CHECK_EQ(n[0], std::string("a"));
    CHECK_EQ(n[3], std::string("d"));
}

TEST("prefs: picker entries and selection by index") {
    Skins s;
    FakeBackend b; b.root = s.dir;
    PrefsModel m(b);
    m.load();
    const auto own = m.skin_labels();
    CHECK(own == std::vector<std::string>({ prefs_text::kOwnFolder })); // just the component's own folder
    CHECK_EQ(m.skin_index(), 0);
    m.set_root(s.dir + "/lib");
    CHECK(m.skin_labels() == std::vector<std::string>({ prefs_text::kOwnFolder, "alpha", "beta" }));
    CHECK_EQ(m.skin_index(), 1);
    m.set_skin_index(2);
    CHECK_EQ(m.pending().active, std::string("beta"));
    CHECK(m.main_labels() == std::vector<std::string>({ prefs_text::kAutomatic, "one.txt", "two.txt" }));
    CHECK_EQ(m.main_index(), 0);
    m.set_main_index(2);
    CHECK_EQ(m.pending().main, std::string("two.txt"));
    CHECK_EQ(m.main_index(), 2);
    m.set_main_index(99);                                  // out of range: automatic
    CHECK_EQ(m.pending().main, std::string());
    m.set_skin_index(0);                                   // the component's own folder
    CHECK_EQ(m.pending().active, std::string());
    CHECK_EQ(m.skin_index(), 0);
    CHECK(m.root_note().find(s.dir + "/own") != std::string::npos);

    auto z = PrefsModel::zoom_labels(prefs_text::kZoomAuto100);
    CHECK_EQ(z.size(), PrefsModel::zoom_choices().size() + 1);
    CHECK_EQ(z[0], std::string(prefs_text::kZoomAuto100));
    CHECK_EQ(z[3], std::string("100%"));
}
