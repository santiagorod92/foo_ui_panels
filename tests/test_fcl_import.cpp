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
                  kLyrics = guid("547abb10-d8ac-4c39-9f8b-9c7af989443d"), kWsh = guid("75a7b642-786c-4f24-9b52-17d737dea09a");

std::string smp(const std::string& script, int rows = -1) {
    std::string out = u32(1) + std::string(1, '\0') + std::string(16, 'g') + std::string(1, '\0');
    out += u32(rows >= 0 ? 1 : 0);
    if (rows >= 0) out += str("rows") + u32(1) + u32((uint32_t)rows);
    return out + std::string(2 + 44, '\0') + str(script) + std::string(1, '\0');
}

std::string item(const std::string& g, const std::string& data, uint32_t size) {
    return g + u32(0) + std::string(4, '\0') + u32(size) + std::string(1, '\0') + str(data) + std::string(1, '\0') + str("");
}

std::string head(bool share, bool forced, double size) {
    std::string out = u32(100) + u32(share ? 1 : 0) + u32(forced ? 1 : 0) + std::string(72 - 12 - 8, '\0');
    out.append((const char*)&size, sizeof size);
    return out;
}

std::string stack(const std::string& script, const std::vector<std::tuple<std::string, std::string, std::vector<std::string>, std::string>>& kids,
                  const std::vector<std::string>& heads = {}, bool vertical = true) {
    std::string out = u32(1) + u32(vertical ? 1 : 0) + u32(1) + u32(0) + u32(0x101010) + u32(0) + u32(0) + str(script) + std::string(24 + 92, '\0');
    out += u32(0) + u32((uint32_t)kids.size());
    size_t n = 0;
    for (const auto& [g, name, rect, data] : kids) {
        out += g + str(name) + (n < heads.size() ? heads[n] : head(true, true, 100));
        ++n;
        for (const auto& r : rect) out += str(r);
        out += std::string(16, '\0') + str(data);
    }
    return out;
}

std::string fcl(const std::string& root);

std::string wsh(const std::string& engine, const std::string& script) {
    std::string out = u32(0x7b) + std::string(1, '\1') + std::string(16, 'g') + u32(0) + u32(3);
    out += str("Size") + std::string{ 3, 0 } + u32((uint32_t)-5);
    out += str("Dark") + std::string{ 11, 0 } + std::string{ '\xff', '\xff' };
    out += str("Font") + std::string{ 8, 0 } + str("Segoe \"UI\"");
    return out + std::string(2 + 44, '\0') + str(engine) + str(script) + std::string(1, '\0');
}

std::string sample() {
    const std::string inner = stack("$init_ps_global(TINT,1-2-3)$set_ps_global(SIDE,40)$showpanel_c(art,$ifequal(%SIDE%,40,0,1))$movepanel_c(LIST,%SIDE%,0,$sub(%ps_width%,%SIDE%),%ps_height%)"
                                    "$findfile(%ps_user_profile_path%/x_*)",
                                    { { kSmp, "Art", { "0", "0", "%SIDE%", "%ps_height%" }, smp("// art") },
                                      { kSmp, "List", { "", "", "", "" }, smp("// list", 3) },
                                      { kLyrics, "Words", { "", "", "", "" }, "" } });
    std::string tabs = u32(0) + u32(1) + u32(2);
    for (const auto& [script, title] : { std::pair{ "// one", "One" }, std::pair{ "// two", "Two" } })
        tabs += kSmp + str(smp(script)) + std::string(1, '\1') + str(title);
    return fcl(u32(0) + u32(3) + item(kSmp, smp("// left"), 150) + item(kTabs, tabs, 300) + item(kStack, inner, 100));
}

std::string fcl(const std::string& root) {
    std::string out = kHeader + u32(2) + u32(0) + u32(4) + kHorizontal + str("Horizontal splitter") + kTabs + str("Tab stack") + kSmp +
                      str("Spider Monkey Panel") + kLyrics + str("ESLyric");
    const std::string layout = u32(0) + u32(0) + u32(1) + kHorizontal + str("Main") + str(root);
    return out + u32(1) + kLayout + str("Layout") + u32(0) + str(layout);
}

std::string split_sample() {
    const std::vector<std::string> none = { "", "", "", "" };
    const std::string row = stack("", { { kSmp, "Side", none, smp("// side") }, { kSmp, "List", none, smp("// list") } },
                                  { head(true, false, 91), head(true, false, 116) }, false);
    const std::string body = stack("", { { kSmp, "Top", { "0", "0", "%ps_width%", "50" }, smp("// top") }, { kStack, "Row", none, row },
                                         { kSmp, "Bar", none, smp("// bar") } },
                                   { head(false, false, 39), head(true, false, 100), head(false, false, 26) });
    const std::string outer = stack("$showpanel_c(Body,1)$movepanel_c(Body,0,28,%ps_width%,$sub(%ps_height%,28))$showpanel(2,%MINI%)$showpanel(9,1)",
                                    { { kSmp, "Menu", none, smp("// menu") }, { kStack, "Body", none, body }, { kSmp, "Mini", none, smp("// mini") } },
                                    { head(false, false, 28), head(true, true, 100), head(false, true, 100) });
    return fcl(u32(0) + u32(1) + item(kStack, outer, 100));
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

TEST("fcl import: Panel Stack Splitter children without a forced layout are split along the stack") {
    std::string err;
    const auto presets = parse_fcl(split_sample(), &err);
    CHECK_EQ(err, std::string());
    const FclNode& outer = presets[0].root.children[0];
    CHECK(outer.vertical && !outer.children[0].forced && outer.children[0].fixed && outer.children[0].share == 28);
    CHECK(outer.children[1].forced && outer.children[1].visible && !outer.children[2].visible);
    const FclNode& row = outer.children[1].children[1];
    CHECK(!row.vertical && !row.children[1].fixed && row.children[1].share == 116);
    const std::string main = convert_fcl(presets, "", false, &err).main;
    CHECK_EQ(err, std::string());
    CHECK(has(main, "$puts(vis.Menu,1)") && has(main, "$puts(vis.Mini,0)"));
    CHECK(has(main, ".r,$max(0,$sub($get(") && has(main, ".h),$add(0,$ifequal($get(vis.Menu),0,0,28)))))"));
    CHECK(has(main, "$puts(sp.Body.r,$max(0,$sub($get(Body.h),$add(0,$ifequal($get(vis.Top),0,0,39),$ifequal($get(vis.Bar),0,0,26)))))"));
    CHECK(has(main, "$puts(sp.Body.s,$max(1,$add(0,$ifequal($get(vis.Row),0,0,10000))))"));
    CHECK(has(main, "$puts(sp.Body.l,39)$if($get(mv.Top),,$puts(mv.Top,1)$puts(mv.Top.x,0)$puts(mv.Top.y,$get(sp.Body.o))$puts(mv.Top.w,$get(Body.w))"
                    "$puts(mv.Top.h,$get(sp.Body.l)))$puts(sp.Body.o,$add($get(sp.Body.o),$ifequal($get(vis.Top),0,0,$get(sp.Body.l))))"));
    CHECK(has(main, "$puts(sp.Row.l,$muldiv($get(sp.Row.r),9100,$get(sp.Row.s)))$if($get(mv.Side),,$puts(mv.Side,1)$puts(mv.Side.x,$get(sp.Row.o))"
                    "$puts(mv.Side.y,0)$puts(mv.Side.w,$get(sp.Row.l))$puts(mv.Side.h,$get(Row.h)))"));
    CHECK(!has(main, "$ifequal($get(vis.Body),0,0,"));
    CHECK(has(main, "$puts(mv.Body.y,28)"));
    CHECK(has(main, "$puts(vis.Mini,$ifequal(%MINI%,0,0,1))") && !has(main, "showpanel("));
}

TEST("fcl import: WSH Panel Mod panels become JS Panels with their properties, VBScript ones stay hosted by name") {
    std::string err;
    const auto presets = parse_fcl(fcl(u32(0) + u32(2) + item(kWsh, wsh("JScript", "// @feature \"v1.4\""), 100) + item(kWsh, wsh("VBScript", "x"), 100)), &err);
    CHECK_EQ(err, std::string());
    const FclNode& js = presets[0].root.children[0];
    CHECK(js.kind == FclNode::Kind::Smp && js.script == "// @feature \"v1.4\"");
    CHECK_EQ(js.properties.size(), (size_t)3);
    CHECK(js.properties[0] == std::make_pair(std::string("Size"), std::string("-5")));
    CHECK(js.properties[1] == std::make_pair(std::string("Dark"), std::string("true")));
    CHECK(js.properties[2] == std::make_pair(std::string("Font"), std::string("\"Segoe \\\"UI\\\"\"")));
    CHECK(presets[0].root.children[1].kind == FclNode::Kind::Panel);
    const ImportedSkin skin = convert_fcl(presets, "", false, &err);
    CHECK_EQ(skin.js_panels, 1);
}

TEST("fcl import: the skin places every panel and translates the stack's script") {
    std::string err;
    const ImportedSkin skin = convert_fcl(parse_fcl(sample(), &err), "", false, &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(skin.js_panels, 4);
    CHECK(has(skin.main, "$puts(mv.Spider_Monkey_Panel.w,150)"));
    CHECK(has(skin.main, "$puts(ps.SIDE,40)"));
    CHECK(has(skin.main, "$puts(ps.TINT,$if2($get(ps.TINT),1-2-3))"));
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

TEST("fcl import: Spider Monkey Panel 1.6 settings become scripts, file includes and packages") {
    auto settings = [](const std::string& json) { return u32(2) + str(json); };
    const std::string memory = settings(R"J({"id": "settings", "panelId": "Now \u00e9", "scriptType": 1, "payload": {"script": "// in \"memory\"\n"},
        "properties": {"id": "properties", "values": {"a.b": true, "n": 3, "s": "x\\y", "o": {"k": [1, "]"]}}}})J");
    const std::string file = settings(R"J({"id": "settings", "panelId": "Disk", "scriptType": 3, "payload": {"locationType": 2, "path": "themes\\t\\p.js"}})J");
    const std::string package = settings(R"J({"id": "settings", "panelId": "Seekbar (mini)", "scriptType": 4,
        "payload": {"id": "{858943EC-DEAC-4C52-BD3E-07EAF372BBF7}", "name": "Seekbar"}, "properties": {"values": {}}})J");
    const std::string bad = settings(R"J({"id": "settings", "panelId": "Bad", "scriptType": 4, "payload": {"id": "../x"}})J");
    const std::string root = u32(0) + u32(4) + item(kSmp, memory, 100) + item(kSmp, file, 100) + item(kSmp, package, 100) + item(kSmp, bad, 100);
    const std::string head = kHeader + u32(2) + u32(0) + u32(2) + kHorizontal + str("Horizontal splitter") + kSmp + str("Spider Monkey Panel");
    const std::string layout = u32(0) + u32(0) + u32(1) + kHorizontal + str("Main") + str(root);
    std::string err;
    const ImportedSkin skin = convert_fcl(parse_fcl(head + u32(1) + kLayout + str("Layout") + u32(0) + str(layout), &err), "", false, &err);
    CHECK_EQ(err, std::string());
    auto script = [&](const std::string& box) {
        const auto it = std::find_if(skin.files.begin(), skin.files.end(), [&](const auto& f) { return f.first == "js/" + box + ".js"; });
        return it == skin.files.end() ? std::string() : it->second;
    };
    const std::string now = script("Now___");
    CHECK(has(now, "// in \"memory\"\n"));
    CHECK(has(now, "\"a.b\": true") && has(now, "\"n\": 3") && has(now, "\"s\": \"x\\\\y\"") && has(now, "\"o\": {\"k\": [1, \"]\"]}"));
    CHECK(has(script("Disk"), "include(fb.ProfilePath + \"themes\\\\t\\\\p.js\");"));
    CHECK(has(script("Seekbar__mini_"), "Object.defineProperty(window, 'Name', { get: () => \"Seekbar (mini)\" });"));
    CHECK(has(script("Seekbar__mini_"), "include(utils.GetPackagePath(\"{858943EC-DEAC-4C52-BD3E-07EAF372BBF7}\") + 'main.js');"));
    CHECK(!has(script("Bad"), "include("));
}

TEST("fcl import: a theme's Spider Monkey Panel packages are installed by their id") {
    const std::string base = (std::filesystem::temp_directory_path() / ("pui_pkg_" + std::to_string(tick_ms()))).string();
    const std::string theme = base + "/CaTRoX", profile = base + "/profile";
    std::filesystem::create_directories(theme + "/layout");
    std::filesystem::create_directories(theme + "/packages/Menu/{AAA}/scripts");
    std::filesystem::create_directories(theme + "/packages/Evil/x");
    std::filesystem::create_directories(profile);
    write_file(theme + "/layout/theme.fcl", "x");
    write_file(theme + "/packages/Menu/{AAA}/package.json", "\xEF\xBB\xBF{\n  \"author\": \"a\",\n  \"id\": \"{AAA}\"\n}");
    write_file(theme + "/packages/Menu/{AAA}/scripts/m.js", "menu");
    write_file(theme + "/packages/Evil/x/package.json", "{\"id\": \"../../escape\"}");
    std::filesystem::create_directories(theme + "/fonts");
    write_file(theme + "/fonts/Icons.TTF", "font");
    write_file(theme + "/fonts/readme.txt", "x");
    const std::string root = fcl_theme_root(theme + "/layout/theme.fcl", profile);
    CHECK(has(root, "CaTRoX"));
    std::string err;
    CHECK(copy_theme_folders(root, profile, &err));
    CHECK_EQ(read_file(profile + "/foo_spider_monkey_panel/packages/{AAA}/scripts/m.js"), std::string("menu"));
    CHECK(!std::filesystem::exists(base + "/escape"));
    CHECK(!std::filesystem::exists(profile + "/themes"));
    CHECK_EQ(read_file(profile + "/foo_js_panel/fonts/Icons.TTF"), std::string("font"));
    CHECK(!std::filesystem::exists(profile + "/foo_js_panel/fonts/readme.txt"));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}
