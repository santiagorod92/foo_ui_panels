#pragma once
#include <string>
#include <utility>
#include <vector>

namespace pui {

struct FclNode {
    enum class Kind { Horizontal, Vertical, Tabs, Stack, Smp, Panel } kind = Kind::Panel;
    std::string type, name, title, script;
    std::vector<std::string> rect;
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<FclNode> children;
    unsigned size = 0;
    bool hidden = false, visible = true, active = false;
};

struct FclPreset {
    std::string name;
    bool active = false;
    bool empty = true;
    FclNode root;
};

std::vector<FclPreset> parse_fcl(const std::string& bytes, std::string* error);

struct ImportedSkin {
    std::string preset, main, ini;
    std::vector<std::pair<std::string, std::string>> files;
    std::vector<std::string> hosted_by_name;
    int js_panels = 0;
};

ImportedSkin convert_fcl(const std::vector<FclPreset>& presets, const std::string& preset, bool outline, std::string* error);

std::vector<std::string> tf_split_args(const std::string& text);

std::string fcl_skin_name(const std::string& fclPath);
std::string fcl_theme_root(const std::string& fclPath, const std::string& profile);
bool copy_theme_folders(const std::string& root, const std::string& profile, std::string* error);

}
