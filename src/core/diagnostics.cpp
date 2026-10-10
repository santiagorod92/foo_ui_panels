#include "diagnostics.h"
#include <cctype>

namespace pui {

const char* build_arch() {
#if defined(_M_ARM64EC)
    return "ARM64EC";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void replace_ci(std::string& text, const std::string& needle, const std::string& with) {
    if (needle.empty()) return;
    const std::string n = lower(needle);
    std::string hay = lower(text);
    size_t pos = 0;
    while ((pos = hay.find(n, pos)) != std::string::npos) {
        text.replace(pos, n.size(), with);
        hay.replace(pos, n.size(), with);
        pos += with.size();
    }
}

bool is_sep(char c) { return c == '/' || c == '\\'; }

std::string flip_seps(std::string s) {
    for (char& c : s) c = c == '/' ? '\\' : c == '\\' ? '/' : c;
    return s;
}

std::string on_off(bool b) { return b ? "on" : "off"; }

}

std::string redact_home(std::string text, const std::string& home) {
    std::string h = home;
    while (h.size() > 1 && is_sep(h.back())) h.pop_back();
    if (h.size() < 2) return text;
    replace_ci(text, h, "~");
    replace_ci(text, flip_seps(h), "~");
    size_t cut = h.size();
    while (cut > 0 && !is_sep(h[cut - 1])) --cut;
    const std::string user = h.substr(cut);
    if (user.size() < 2) return text;
    for (const char* a : { "/", "\\" })
        for (const char* b : { "/", "\\" }) replace_ci(text, a + user + b, std::string(a) + "<user>" + b);
    return text;
}

std::string build_diagnostics(const DiagnosticsInfo& d) {
    auto clean = [&](const std::string& s) { return redact_home(s, d.home); };
    std::string out;
    out += "### foo_ui_panels diagnostics\n\n";
    out += "- Component: " + d.componentVersion + " (" + d.arch + ")\n";
    out += "- foobar2000: " + d.foobarVersion + "\n";
    out += "- OS: " + d.platform + " " + d.osVersion;
    if (!d.wineVersion.empty()) out += ", Wine " + d.wineVersion;
    out += "\n";

    if (d.playerWindows == 0) {
        out += "- Player window: not open (Panels UI isn't the active user interface / layout element)\n";
    } else {
        out += "- Player window: open";
        if (d.playerWindows > 1) out += " (" + std::to_string(d.playerWindows) + ")";
        if (d.wizard) out += ", layout wizard showing";
        if (d.miniMode) out += ", mini mode";
        out += "\n";
    }
    out += "- Skin folder: " + (d.skinDir.empty() ? std::string("(none)") : clean(d.skinDir)) + "\n";
    out += "- Main script: " + (d.mainScript.empty() ? std::string("(built-in)") : clean(d.mainScript)) +
           ", foo_ui_panels.ini: " + (d.skinConfig ? "yes" : "no") + "\n";
    out += "- Zoom: " + (d.zoomSetting > 0 ? std::to_string(d.zoomSetting) + "%" : std::string("automatic")) +
           " (in effect " + std::to_string(d.effectiveZoom) + "%), always on top: " + on_off(d.onTop) +
           ", script problem markers: " + on_off(d.showProblems) + "\n";
    out += "- Persistent variables: " + std::to_string(d.pvarCount) + "\n";
    out += "- foo_navidrome: " + std::string(d.navidrome ? "installed" : "not installed") + "\n";
    out += "- Verbose logging: " + on_off(d.verboseLogging) + "\n";
    if (!d.logPath.empty()) out += "- Log file: " + clean(d.logPath) + "\n";

    if (!d.panels.empty()) {
        out += "\n<details><summary>Panels (" + std::to_string(d.panels.size()) + ")</summary>\n\n";
        for (const auto& [name, kind] : d.panels) out += "- " + name + ": " + kind + "\n";
        out += "\n</details>\n";
    }

    std::size_t problemCount = 0;
    for (const auto& p : d.problems) problemCount += p.second.size();
    if (problemCount) {
        out += "\n<details><summary>Script problems (" + std::to_string(problemCount) + ")</summary>\n\n";
        for (const auto& [label, msgs] : d.problems)
            for (const auto& m : msgs) out += "- " + clean(label) + ": " + clean(m) + "\n";
        out += "\n</details>\n";
    }

    if (!d.components.empty()) {
        out += "\n<details><summary>Installed components (" + std::to_string(d.components.size()) +
               ")</summary>\n\n";
        for (const auto& c : d.components) out += "- " + c + "\n";
        out += "\n</details>\n";
    }

    out += "\n<details><summary>Recent log (" + std::to_string(d.logLines.size()) + " lines)</summary>\n\n```\n";
    for (const auto& l : d.logLines) out += clean(l) + "\n";
    out += "```\n</details>\n";
    return out;
}

std::string new_issue_url() {
    return "https://github.com/santiagorod92/foo_ui_panels/issues/new?template=bug_report.yml";
}

}
