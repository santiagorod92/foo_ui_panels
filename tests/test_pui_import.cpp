#include "test.h"
#include "core/pui_import.h"
#include "core/fs_util.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

using namespace pui;

namespace {

std::string u32(uint32_t n) { return std::string{ (char)(n & 0xff), (char)((n >> 8) & 0xff), (char)((n >> 16) & 0xff), (char)(n >> 24) }; }
std::string str(const std::string& s) { return u32((uint32_t)s.size()) + s; }

std::string record(const std::string& name, const std::string& cfg) { return std::string(16, '\x11') + str(name) + str(cfg); }

std::string pui_file(uint32_t version, const std::vector<std::pair<std::string, std::string>>& pvars, const std::string& globals,
                     const std::string& script, const std::string& children) {
    std::string d = u32(version);
    if (version >= 4) d += u32(0x96000000u);
    d += u32(44) + u32(0) + u32(1);
    while (d.size() < (version >= 4 ? 52u : 48u)) d += u32(0xffffffffu);
    d += u32((uint32_t)pvars.size());
    for (const auto& [k, v] : pvars) d += str(k) + str(v);
    d += str(globals);
    const std::string font = std::string("M\0S\0 \0S\0h\0e\0l\0l\0", 16) + std::string(48, '\0');
    const std::string root = std::string(8, '\xcd') + font + u32(7) + str(script) + std::string(12, '\0') + children;
    return d + str(root);
}

const std::string* find(const PuiSkin& s, const std::string& rel) {
    for (const auto& f : s.files) if (f.first == rel) return &f.second;
    return nullptr;
}

}

TEST("pui: parse a Panels UI 0.13 configuration (version 3 and 4)") {
    const std::string child = record("Info", std::string(40, '\0') + str("// Title\r\n$font(Tahoma,9)\r\n  %title%"));
    for (uint32_t v : { 3u, 4u }) {
        const std::string d = pui_file(v, { { "rHeight", "450" }, { "mode", "1" } }, "$puts(f,Tahoma)",
                                       "$panel(Info,Track Display,0,0,10,10,)$panel(List,Single Column Playlist,0,10,10,10,)",
                                       child + record("List", std::string(30, '\0') + str("layout script, not ours to run")));
        PuiConfig c;
        std::string err;
        CHECK(parse_pui(d, c, &err));
        CHECK_EQ(err, std::string());
        CHECK_EQ(c.version, v);
        CHECK_EQ(c.pvars.size(), (size_t)2);
        CHECK_EQ(c.pvars[0].first, std::string("rHeight"));
        CHECK_EQ(c.pvars[0].second, std::string("450"));
        CHECK_EQ(c.globals, std::string("$puts(f,Tahoma)"));
        CHECK(c.script.rfind("$panel(Info,", 0) == 0);
        CHECK_EQ(c.panels.size(), (size_t)2);
        CHECK_EQ(c.panels[0].first, std::string("Info"));
        CHECK(c.panels[0].second.find("%title%") != std::string::npos);
    }
}

TEST("pui: damaged or foreign files are refused with a reason") {
    PuiConfig c;
    std::string err;
    CHECK(!parse_pui("short", c, &err));
    CHECK(!err.empty());
    CHECK(!parse_pui(u32(9) + std::string(100, '\0'), c, &err));
    CHECK(err.find("version 9") != std::string::npos);
    CHECK(!parse_pui(u32(3) + std::string(44, '\0') + u32(5) + str("k") + u32(0x7fffffff) + std::string(40, '\0'), c, &err));
    CHECK(err.find("damaged") != std::string::npos);
}

TEST("pui: convert a skin folder: main script, Track Display panels, popups, images") {
    const std::string child = record("Info", std::string(40, '\0') + str("$font(Tahoma,9)%title%  // comment"));
    const std::string main = pui_file(3, { { "rHeight", "450" } }, "$puts(f,Tahoma)",
                                      "// Background\r\n$imageabs(0,0,0,0,/images\\Go.PNG)\r\n"
                                      "$panel(Info,Track Display,0,0,10,10,)$panel(Bar,Track Display,0,0,1,1,)"
                                      "$button(0,0,0,0,0,0,/images\\go.png,/images\\go.png,POPUP:library.pp,)",
                                      child);
    const std::string popup = pui_file(3, {}, "", "$panel(LF,Library Filter,0,0,%_width%,%_height%,)", "");
    const SkinFiles files = {
        { "basic/basic.pui", main },
        { "basic/library.pp", popup },
        { "basic/images/go.png", "png" },
        { "basic/Readme.txt", "readme" },
        { "other/skip.png", "x" },
    };
    std::string err;
    const PuiSkin s = convert_pui(files, "basic/basic.pui", &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(s.assets, 1);
    CHECK_EQ(s.panels, 1);
    CHECK_EQ(s.popups, 1);
    CHECK(s.main.rfind("$if($strcmp($getpvar(rHeight),),$setpvar(rHeight,450),)$puts(f,Tahoma)", 0) == 0);
    CHECK(s.main.find("/images\\go.png)") != std::string::npos);
    CHECK(s.main.find("Background") == std::string::npos);
    CHECK(find(s, "images/go.png"));
    CHECK(!find(s, "Readme.txt"));
    CHECK(!find(s, "skip.png"));
    const std::string* info = find(s, "panels/Info.txt");
    CHECK(info && *info == "$font(Tahoma,9)%title%");
    const std::string* bar = find(s, "panels/Bar.txt");
    CHECK(bar && bar->empty());
    const std::string* lib = find(s, "panels/library.pp.txt");
    CHECK(lib && lib->find("$panel(LF,Library Filter,") != std::string::npos);
    CHECK(s.ini.find("drawrect.zero_fills = 1") != std::string::npos);

    const PuiSkin missing = convert_pui(files, "basic/none.pui", &err);
    CHECK(err.find("none.pui") != std::string::npos);
    CHECK(missing.main.empty());
}

TEST("pui: a real Panels UI 0.13 skin folder (PUI_SKIN=<file.pui>)") {
    const char* path = std::getenv("PUI_SKIN");
    if (!path || !*path) return;
    namespace fs = std::filesystem;
    const fs::path src = fs_path(path), folder = src.parent_path();
    SkinFiles files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back({ fs_utf8(it->path().lexically_relative(folder)), read_file(fs_utf8(it->path())) });
    std::string err;
    const PuiSkin s = convert_pui(files, fs_utf8(src.filename()), &err);
    CHECK_EQ(err, std::string());
    CHECK(s.main.find("$panel(") != std::string::npos);
    CHECK(s.assets > 0);
}
