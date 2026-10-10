#include "test.h"
#include "core/log.h"
#include "core/fs_util.h"
#include <filesystem>

using namespace pui;

namespace {
std::string temp_log(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(p.string() + ".1", ec);
    return p.string();
}
}

TEST("log: format_line pads level and tag") {
    const log::LocalTime t{ 2026, 10, 10, 9, 5, 7, 42 };
    CHECK_EQ(log::format_line(t, log::Level::Warn, "skin", 12, "hello"),
             std::string("09:05:07.042  WARN   skin      [t0012] hello"));
    CHECK_EQ(log::format_date(t), std::string("2026-10-10"));
}

TEST("log: LineRing keeps the newest lines") {
    log::LineRing r(2);
    r.push("a"); r.push("b"); r.push("c");
    const auto l = r.lines();
    CHECK_EQ(l.size(), (size_t)2);
    CHECK_EQ(l[0], std::string("b"));
    CHECK_EQ(l[1], std::string("c"));
}

TEST("log: info reaches the file only when verbose, always the ring") {
    log::Logger& l = log::Logger::get();
    l.reset_for_tests();
    const std::string path = temp_log("pui_test_verbose.log");
    l.configure(path);
    log::info("t", "quiet info");
    log::warn("t", "loud warn");
    l.set_verbose(true);
    log::info("t", "verbose info");
    const std::string file = read_file(path);
    CHECK(file.find("foo_ui_panels session") != std::string::npos);
    CHECK(file.find("quiet info") == std::string::npos);
    CHECK(file.find("loud warn") != std::string::npos);
    CHECK(file.find("verbose info") != std::string::npos);
    bool ringHasQuiet = false;
    for (const auto& line : l.recent()) ringHasQuiet |= line.find("quiet info") != std::string::npos;
    CHECK(ringHasQuiet);
    l.reset_for_tests();
}

TEST("log: lines before configure() are written once it is") {
    log::Logger& l = log::Logger::get();
    l.reset_for_tests();
    log::error("t", "early error");
    const std::string path = temp_log("pui_test_pending.log");
    l.configure(path);
    CHECK(read_file(path).find("early error") != std::string::npos);
    l.reset_for_tests();
}

TEST("log: rotates past the size limit") {
    log::Logger& l = log::Logger::get();
    l.reset_for_tests();
    const std::string path = temp_log("pui_test_rotate.log");
    l.configure(path, 200);
    for (int i = 0; i < 10; ++i) log::warn("t", "line " + std::to_string(i) + " with some padding text");
    std::error_code ec;
    CHECK(std::filesystem::exists(path + ".1", ec));
    CHECK(std::filesystem::file_size(path, ec) < 400);
    l.reset_for_tests();
}

TEST("log: console sink gets the message, guarded() logs exceptions") {
    log::Logger& l = log::Logger::get();
    l.reset_for_tests();
    std::string seen;
    l.set_console([&](const std::string& m) { seen = m; });
    log::console(log::Level::Info, "t", "to the console");
    CHECK_EQ(seen, std::string("to the console"));
    log::info("t", "not to the console");
    CHECK_EQ(seen, std::string("to the console"));
    CHECK(!log::guarded("t", "boom", [] { throw std::runtime_error("bad"); }));
    CHECK(log::guarded("t", "fine", [] {}));
    bool found = false;
    for (const auto& line : l.recent()) found |= line.find("boom failed: bad") != std::string::npos;
    CHECK(found);
    l.reset_for_tests();
}
