// The layout wizard's starting points: small skins of our own, built into the component, in the
// spirit of the Default UI's Quick Appearance Setup. Picking one writes it out as an ordinary skin
// folder (main script + foo_ui_panels.ini) that the person then owns and can edit like any other
// skin; the wizard itself is a built-in script generated from this list. SDK-free (unit-tested).
#pragma once
#include <string>
#include <vector>

namespace pui {

struct SkinTemplate {
    // What a box in the wizard's thumbnail stands for (its colour).
    enum class Part { Toolbar, Status, Playlist, Cover, Tree, Albums, Lyrics, Spectrum };
    struct Box { int x, y, w, h; Part part; }; // in percent of the window

    const char* id;      // action argument (SKIN:TEMPLATE:<id>), stable
    const char* name;    // shown in the wizard; also the new skin folder's name
    const char* summary; // one line under the name
    std::vector<Box> thumbnail;
    std::string script;  // main.txt
    std::string ini;     // foo_ui_panels.ini
};

// Every template, the default (first-run) one first.
const std::vector<SkinTemplate>& skin_templates();
const SkinTemplate* find_skin_template(const std::string& id);

// The wizard: a card per template (thumbnail, name, summary; SKIN:TEMPLATE:<id>), plus
// "Use my own skin folder…" (SKIN:CHOOSE_FOLDER) and "Close" (WIZARD:CLOSE).
std::string layout_wizard_script();

// Writes `t` as a new skin folder under `parent` — named after it, " 2", " 3"… when taken, so an
// edited copy is never overwritten. Returns the folder (UTF-8), or "" with `err` set.
std::string install_skin_template(const SkinTemplate& t, const std::string& parent, std::string* err = nullptr);

} // namespace pui
