#include "test.h"
#include "core/diagnostics.h"

using namespace pui;

TEST("diagnostics: redact_home hides the home folder and user name") {
    CHECK_EQ(redact_home("C:\\Users\\Ana\\AppData\\foo.log", "C:\\Users\\Ana"), std::string("~\\AppData\\foo.log"));
    CHECK_EQ(redact_home("c:/users/ana/x", "C:\\Users\\Ana\\"), std::string("~/x"));
    CHECK_EQ(redact_home("Z:\\home\\ana\\skins", "C:\\users\\ana"), std::string("Z:\\home\\<user>\\skins"));
    CHECK_EQ(redact_home("/Users/ana/Library", "/Users/ana"), std::string("~/Library"));
    CHECK_EQ(redact_home("nothing here", ""), std::string("nothing here"));
}

TEST("diagnostics: report has versions, skin, panels, problems and the log") {
    DiagnosticsInfo d;
    d.componentVersion = "1.2.3";
    d.foobarVersion = "foobar2000 v2.26";
    d.platform = "Windows";
    d.osVersion = "10.0.26100";
    d.arch = "ARM64EC";
    d.home = "C:\\Users\\ana";
    d.playerWindows = 1;
    d.skinDir = "C:\\Users\\ana\\skins\\fooAvA";
    d.mainScript = "fooava.txt";
    d.skinConfig = true;
    d.panels = { { "Display", "Track Display" } };
    d.problems = { { "panels/Display.txt", { "unknown $foo" } } };
    d.logLines = { "12:00:00.000  WARN   skin  C:\\Users\\ana\\x" };
    const std::string r = build_diagnostics(d);
    CHECK(r.find("- Component: 1.2.3 (ARM64EC)") != std::string::npos);
    CHECK(r.find("- OS: Windows 10.0.26100\n") != std::string::npos);
    CHECK(r.find("- Skin folder: ~\\skins\\fooAvA") != std::string::npos);
    CHECK(r.find("foo_ui_panels.ini: yes") != std::string::npos);
    CHECK(r.find("- Display: Track Display") != std::string::npos);
    CHECK(r.find("panels/Display.txt: unknown $foo") != std::string::npos);
    CHECK(r.find("WARN   skin  ~\\x") != std::string::npos);
    CHECK(r.find("ana") == std::string::npos);
}

TEST("diagnostics: no player window, Wine") {
    DiagnosticsInfo d;
    d.platform = "Windows";
    d.wineVersion = "11.0";
    const std::string r = build_diagnostics(d);
    CHECK(r.find("Player window: not open") != std::string::npos);
    CHECK(r.find(", Wine 11.0") != std::string::npos);
    CHECK(r.find("Recent log (0 lines)") != std::string::npos);
}
