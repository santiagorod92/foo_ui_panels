#include "core/fcl_import.h"
#include "core/fooava_import.h"
#include "core/pui_import.h"
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
    if (argc == 4 && std::strcmp(argv[1], "--import-fooava") == 0) {
        std::string err;
        pui::FooavaFiles files;
        if (std::filesystem::is_directory(argv[2])) files = pui::read_fooava_folder(argv[2]);
        else {
            std::ifstream in(argv[2], std::ios::binary);
            pui::unpack_fooava_installer(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), files, &err);
        }
        const auto skin = err.empty() ? pui::convert_fooava(files, &err) : pui::FooavaSkin{};
        if (!err.empty()) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const std::filesystem::path dir = argv[3];
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "main.txt", std::ios::binary) << skin.main;
        std::ofstream(dir / "foo_ui_panels.ini", std::ios::binary) << skin.ini;
        for (const auto& [rel, data] : skin.files) {
            std::filesystem::create_directories((dir / rel).parent_path());
            std::ofstream(dir / rel, std::ios::binary) << data;
        }
        std::printf("%d images, %d panel scripts, %d popups\n", skin.images, skin.panels, skin.popups);
        return 0;
    }
    if (argc == 4 && std::strcmp(argv[1], "--import-pui") == 0) {
        const std::filesystem::path src = std::filesystem::absolute(argv[2]);
        const std::filesystem::path folder = src.parent_path();
        const std::string main = src.filename().string();
        pui::SkinFiles files;
        for (auto it = std::filesystem::recursive_directory_iterator(folder); it != std::filesystem::recursive_directory_iterator(); ++it) {
            if (!it->is_regular_file()) continue;
            std::ifstream in(it->path(), std::ios::binary);
            files.push_back({ it->path().lexically_relative(folder).generic_string(),
                              std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) });
        }
        std::string err;
        const auto skin = pui::convert_pui(files, main, &err);
        if (!err.empty()) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const std::filesystem::path dir = argv[3];
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "main.txt", std::ios::binary) << skin.main;
        std::ofstream(dir / "foo_ui_panels.ini", std::ios::binary) << skin.ini;
        for (const auto& [rel, data] : skin.files) {
            std::filesystem::create_directories((dir / rel).parent_path());
            std::ofstream(dir / rel, std::ios::binary) << data;
        }
        std::printf("%d assets, %d panel scripts, %d popups\n", skin.assets, skin.panels, skin.popups);
        return 0;
    }
    if (argc != 2 || argv[1][0] == '-') {
        std::fprintf(stderr, "usage: skin_lint <skin folder> | --functions-md | --import-fcl LAYOUT.fcl OUT_DIR [PRESET [--outline]] | "
                             "--import-fooava INSTALLER.exe|FOLDER OUT_DIR | --import-pui CONFIG.pui OUT_DIR\n");
        return 2;
    }
    const auto findings = pui::lint_skin(argv[1]);
    std::fputs(pui::format_findings(findings).c_str(), stdout);
    for (auto& f : findings) if (f.level == pui::LintFinding::Level::Error) return 1;
    return 0;
}
