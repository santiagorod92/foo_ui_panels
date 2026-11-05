#include "core/fcl_import.h"
#include "core/script_runtime.h"
#include "core/skin_lint.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--functions-md") == 0) {
        std::fputs(pui::script_functions_markdown().c_str(), stdout);
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "--import-fcl") == 0) {
        std::ifstream in(argv[2], std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string err;
        const auto presets = pui::parse_fcl(bytes, &err);
        const bool outline = argc > 5 && std::strcmp(argv[5], "--outline") == 0;
        const auto skin = presets.empty() ? pui::ImportedSkin{} : pui::convert_fcl(presets, argc > 4 ? argv[4] : "", outline, &err);
        if (!err.empty()) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const std::filesystem::path dir = argv[3];
        std::filesystem::create_directories(dir / "js");
        std::ofstream(dir / "main.txt", std::ios::binary) << skin.main;
        std::ofstream(dir / "foo_ui_panels.ini", std::ios::binary) << skin.ini;
        for (const auto& [rel, text] : skin.files) std::ofstream(dir / rel, std::ios::binary) << text;
        std::printf("preset \"%s\": %d JS panels\n", skin.preset.c_str(), skin.js_panels);
        for (const auto& h : skin.hosted_by_name) std::printf("  hosted by name (no Panels UI equivalent known): %s\n", h.c_str());
        return 0;
    }
    if (argc != 2 || argv[1][0] == '-') {
        std::fprintf(stderr, "usage: skin_lint <skin folder> | --functions-md | --import-fcl LAYOUT.fcl OUT_DIR [PRESET [--outline]]\n");
        return 2;
    }
    const auto findings = pui::lint_skin(argv[1]);
    std::fputs(pui::format_findings(findings).c_str(), stdout);
    for (auto& f : findings) if (f.level == pui::LintFinding::Level::Error) return 1;
    return 0;
}
