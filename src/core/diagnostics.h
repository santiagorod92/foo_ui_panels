#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace pui {

struct DiagnosticsInfo {
    std::string componentVersion;
    std::string foobarVersion;
    std::string platform;
    std::string osVersion;
    std::string arch;
    std::string wineVersion;
    std::string home;

    int playerWindows = 0;
    std::string skinDir;
    std::string mainScript;
    bool skinConfig = false;
    bool wizard = false;
    bool miniMode = false;
    std::vector<std::pair<std::string, std::string>> panels;
    std::vector<std::pair<std::string, std::vector<std::string>>> problems;

    int zoomSetting = 0;
    int effectiveZoom = 100;
    bool onTop = false;
    bool showProblems = true;
    bool verboseLogging = false;
    std::size_t pvarCount = 0;

    bool navidrome = false;
    std::vector<std::string> components;

    std::string logPath;
    std::vector<std::string> logLines;
};

const char* build_arch();
std::string redact_home(std::string text, const std::string& home);
std::string build_diagnostics(const DiagnosticsInfo& d);
std::string new_issue_url();

std::string collect_diagnostics();
void start_logging();

}
