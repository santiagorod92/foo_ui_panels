#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pui {

namespace pui_text {
uint32_t u16(const std::string& d, size_t o);
uint32_t u32(const std::string& d, size_t o);
std::string lower(std::string s);
bool ends_with(const std::string& s, const std::string& tail);
std::string extension(const std::string& path);
std::string latin1_to_utf8(const std::string& s);
std::string pystrip(const std::string& s);
size_t find_comment(const std::string& ln);
std::string preprocess(const std::string& text, bool trim_tail = true);
bool image_path_char(char c);
std::string fix_image_case(const std::string& text, const std::map<std::string, std::string>& index);
std::set<std::string> referenced_panels(const std::string& data);
bool safe_panel_name(const std::string& name);
}

using SkinFiles = std::vector<std::pair<std::string, std::string>>;

struct PuiConfig {
    uint32_t version = 0;
    std::vector<std::pair<std::string, std::string>> pvars;
    std::string globals, script;
    std::vector<std::pair<std::string, std::string>> panels;
};

bool parse_pui(const std::string& data, PuiConfig& out, std::string* error);

struct PuiSkin {
    std::string main, ini;
    SkinFiles files;
    int panels = 0, popups = 0, assets = 0;
};

PuiSkin convert_pui(const SkinFiles& files, const std::string& main_rel, std::string* error);

}
