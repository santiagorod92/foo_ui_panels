#pragma once
#include <string>
#include <vector>

namespace pui {

struct SkinTemplate {
    enum class Part { Toolbar, Status, Playlist, Cover, Tree, Albums, Lyrics, Spectrum };
    struct Box { int x, y, w, h; Part part; };

    const char* id;
    const char* name;
    const char* summary;
    std::vector<Box> thumbnail;
    std::string script;
    std::string ini;
};

const std::vector<SkinTemplate>& skin_templates();
const SkinTemplate* find_skin_template(const std::string& id);

std::string layout_wizard_script();

std::string install_skin_template(const SkinTemplate& t, const std::string& parent, std::string* err = nullptr);

}
