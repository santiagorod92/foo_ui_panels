#include "core/script_runtime.h"
#include "core/skin_lint.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--functions-md") == 0) {
        std::fputs(pui::script_functions_markdown().c_str(), stdout);
        return 0;
    }
    if (argc != 2 || argv[1][0] == '-') {
        std::fprintf(stderr, "usage: skin_lint <skin folder> | --functions-md\n");
        return 2;
    }
    const auto findings = pui::lint_skin(argv[1]);
    std::fputs(pui::format_findings(findings).c_str(), stdout);
    for (auto& f : findings) if (f.level == pui::LintFinding::Level::Error) return 1;
    return 0;
}
