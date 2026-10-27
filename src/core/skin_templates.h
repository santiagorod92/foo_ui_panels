#pragma once
#include <string>
#include <utility>
#include <vector>

namespace pui {

struct SkinTemplate {
    enum class Part { Toolbar, Status, Playlist, Cover, Tree, Albums, Lyrics, Spectrum, Script };
    struct Box { int x, y, w, h; Part part; };

    const char* id;
    const char* name;
    const char* summary;
    std::vector<Box> thumbnail;
    std::string script;
    std::string ini;
    bool needs_js_panel = false;
    std::vector<std::pair<std::string, std::string>> files;
};

const std::vector<SkinTemplate>& skin_templates();
const SkinTemplate* find_skin_template(const std::string& id);

std::string layout_wizard_script(bool jsPanel = false);
std::vector<const SkinTemplate*> wizard_templates(bool jsPanel);

std::string install_skin_template(const SkinTemplate& t, const std::string& parent, std::string* err = nullptr);

}
