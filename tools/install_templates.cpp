// Writes every layout-wizard template as a skin folder under a directory, as picking its card in
// the wizard would (used by scripts/wizard-previews.sh). Prints "<id>\t<folder>" per template.
//   install_templates <dir>
#include "../src/core/skin_templates.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: install_templates <dir>\n"); return 2; }
    for (const auto& t : pui::skin_templates()) {
        std::string err, dir = pui::install_skin_template(t, argv[1], &err);
        if (dir.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::printf("%s\t%s\n", t.id, dir.c_str());
    }
    return 0;
}
