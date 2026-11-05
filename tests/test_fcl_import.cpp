#include "test.h"
#include "core/fcl_import.h"
#include "core/fs_util.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace pui;

namespace {

std::string u32(uint32_t n) { return std::string{ (char)(n & 0xff), (char)((n >> 8) & 0xff), (char)((n >> 16) & 0xff), (char)(n >> 24) }; }
std::string str(const std::string& s) { return u32((uint32_t)s.size()) + s; }

std::string guid(const char* text) {
    unsigned v[16] = {};
    std::sscanf(text, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x", &v[3], &v[2], &v[1], &v[0], &v[5], &v[4], &v[7], &v[6],
                &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15]);
    std::string g(16, '\0');
    for (int i = 0; i < 16; ++i) g[i] = (char)v[i];
    return g;
}

const std::string kHeader = guid("9faadff3-e51a-4a8b-b4a3-d209a36ab301"), kLayout = guid("2cf00365-f2d7-4e78-9fca-c1d56e2707d8"),
                  kHorizontal = guid("8fa0bc24-882a-4fff-8a3b-215ea7fbd07f"), kTabs = guid("5cb67c98-b77f-4926-a79f-49d9b21b9705"),
                  kSmp = guid("32271525-436d-4afa-8dfd-437ab0c83118"), kStack = guid("4532ec89-c567-4267-b33a-0c154b6f0105"),
                  kLyrics = guid("547abb10-d8ac-4c39-9f8b-9c7af989443d");

std::string smp(const std::string& script, int rows = -1) {
    std::string out = u32(1) + std::string(1, '\0') + std::string(16, 'g') + std::string(1, '\0');
    out += u32(rows >= 0 ? 1 : 0);
    if (rows >= 0) out += str("rows") + u32(1) + u32((uint32_t)rows);
    return out + std::string(2 + 44, '\0') + str(script) + std::string(1, '\0');
}

std::string item(const std::string& g, const std::string& data, uint32_t size) {
    return g + u32(0) + std::string(4, '\0') + u32(size) + std::string(1, '\0') + str(data) + std::string(1, '\0') + str("");
}

std::string stack(const std::string& script, const std::vector<std::tuple<std::string, std::string, std::vector<std::string>, std::string>>& kids) {
    std::string out = u32(1) + u32(1) + u32(1) + u32(0) + u32(0x101010) + u32(0) + u32(0) + str(script) + std::string(24 + 92, '\0');
    out += u32(0) + u32((uint32_t)kids.size());
    for (const auto& [g, name, rect, data] : kids) {
        out += g + str(name) + u32(100) + u32(1) + std::string(72 - 8, '\0');
        for (const auto& r : rect) out += str(r);
        out += std::string(16, '\0') + str(data);
    }
    return out;
}

std::string sample() {
    const std::string inner = stack("$set_ps_global(SIDE,40)$showpanel_c(art,$ifequal(%SIDE%,40,0,1))$movepanel_c(LIST,%SIDE%,0,$sub(%ps_width%,%SIDE%),%ps_height%)"
                                    "$findfile(%ps_user_profile_path%/x_*)",
                                    { { kSmp, "Art", { "0", "0", "%SIDE%", "%ps_height%" }, smp("// art") },
                                      { kSmp, "List", { "", "", "", "" }, smp("// list", 3) },
                                      { kLyrics, "Words", { "", "", "", "" }, "" } });
    std::string tabs = u32(0) + u32(1) + u32(2);
    for (const auto& [script, title] : { std::pair{ "// one", "One" }, std::pair{ "// two", "Two" } })
        tabs += kSmp + str(smp(script)) + std::string(1, '\1') + str(title);
    const std::string root = u32(0) + u32(3) + item(kSmp, smp("// left"), 150) + item(kTabs, tabs, 300) + item(kStack, inner, 100);
    std::string out = kHeader + u32(2) + u32(0) + u32(4) + kHorizontal + str("Horizontal splitter") + kTabs + str("Tab stack") + kSmp +
                      str("Spider Monkey Panel") + kLyrics + str("ESLyric");
    const std::string layout = u32(0) + u32(0) + u32(1) + kHorizontal + str("Main") + str(root);
    return out + u32(1) + kLayout + str("Layout") + u32(0) + str(layout);
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}

TEST("fcl import: reads splitters, tab stacks and Panel Stack Splitter containers") {
    std::string err;
    const auto presets = parse_fcl(sample(), &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(presets.size(), (size_t)1);
    const FclNode& root = presets[0].root;
    CHECK(root.kind == FclNode::Kind::Horizontal);
    CHECK_EQ(root.children.size(), (size_t)3);
    CHECK_EQ(root.children[0].size, 150u);
    CHECK(root.children[1].kind == FclNode::Kind::Tabs && root.children[1].children[1].title == "Two");
    const FclNode& st = root.children[2];
    CHECK(st.kind == FclNode::Kind::Stack);
    CHECK_EQ(st.children[0].name, std::string("Art"));
    CHECK_EQ(st.children[0].rect[2], std::string("%SIDE%"));
    CHECK_EQ(st.children[1].properties.size(), (size_t)1);
    CHECK_EQ(st.children[1].properties[0].second, std::string("3"));
    CHECK(parse_fcl("nope", &err).empty() && !err.empty());
}

TEST("fcl import: the skin places every panel and translates the stack's script") {
    std::string err;
    const ImportedSkin skin = convert_fcl(parse_fcl(sample(), &err), "", false, &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(skin.js_panels, 4);
    CHECK(has(skin.main, "$puts(mv.Spider_Monkey_Panel.w,150)"));
    CHECK(has(skin.main, "$puts(ps.SIDE,40)"));
    CHECK(has(skin.main, "$puts(vis.Art,$ifequal($ifequal($get(ps.SIDE),40,0,1),0,0,1))"));
    CHECK(has(skin.main, "$puts(mv.List.x,$get(ps.SIDE))"));
    CHECK(has(skin.main, "$findfile(%pui_profile_folder%/x_*)"));
    CHECK(has(skin.main, "$panel(List,JS Panel,$get(List.x),$get(List.y),$get(List.w),$get(List.h))"));
    CHECK(has(skin.main, "$panel(Words,Lyric Show,"));
    CHECK(has(skin.main, "$panel(Two,JS Panel,") && !has(skin.main, "$panel(One,"));
    CHECK(has(skin.ini, "panel.List.script = js/List.js"));
    const auto list = std::find_if(skin.files.begin(), skin.files.end(), [](const auto& f) { return f.first == "js/List.js"; });
    CHECK(list != skin.files.end() && has(list->second, "\"rows\": 3") && has(list->second, "// list") && has(list->second, "RefreshPSS"));
    CHECK(skin.hosted_by_name.empty());
    CHECK(convert_fcl(parse_fcl(sample(), &err), "Other", false, &err).main.empty() && !err.empty());
}

TEST("fcl import: the skin is named after the theme folder, and an unzipped theme's files are found") {
    CHECK_EQ(fcl_skin_name("/x/themes/eole/columnsUI_eole.fcl"), std::string("Eole"));
    CHECK_EQ(fcl_skin_name("C:\\dl\\my layout.fcl"), std::string("My layout"));
    const std::string base = (std::filesystem::temp_directory_path() / ("pui_fcl_" + std::to_string(tick_ms()))).string();
    const std::string theme = base + "/Eole v1/themes/eole", profile = base + "/profile";
    std::filesystem::create_directories(theme);
    std::filesystem::create_directories(base + "/Eole v1/wsh-data/eole-settings");
    std::filesystem::create_directories(profile + "/wsh-data/eole-settings");
    write_file(theme + "/columnsUI_eole.fcl", "x");
    write_file(theme + "/main.js", "new");
    write_file(base + "/Eole v1/wsh-data/eole-settings/LAYOUT_0", "");
    write_file(base + "/Eole v1/wsh-data/eole-settings/MAINPANEL_0", "");
    write_file(profile + "/wsh-data/eole-settings/MAINPANEL_0", "mine");
    const std::string root = fcl_theme_root(theme + "/columnsUI_eole.fcl", profile);
    CHECK(has(root, "Eole v1"));
    std::string err;
    CHECK(copy_theme_folders(root, profile, &err));
    CHECK_EQ(read_file(profile + "/themes/eole/main.js"), std::string("new"));
    CHECK(std::filesystem::exists(profile + "/wsh-data/eole-settings/LAYOUT_0"));
    CHECK_EQ(read_file(profile + "/wsh-data/eole-settings/MAINPANEL_0"), std::string("mine"));
    CHECK_EQ(fcl_theme_root(profile + "/themes/eole/columnsUI_eole.fcl", profile), std::string());
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}
