#include "test.h"
#include "core/skin_config.h"
#include "core/fs_util.h"
#include <atomic>
#include <chrono>
#include <filesystem>

using namespace pui;
namespace fs = std::filesystem;

// A fresh empty folder under the system temp dir, removed again at scope exit.
struct TempDir {
    fs::path path;
    TempDir() {
        static std::atomic<int> n{ 0 };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("pui_test_" + std::to_string(stamp) + "_" + std::to_string(n++));
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return fs_utf8(path); }
    void write(const std::string& name, const std::string& text) const { write_file(str() + "/" + name, text); }
};

TEST("SkinConfig: parsing, comments, BOM, typed getters") {
    TempDir d;
    d.write(SkinConfig::kFileName,
            "\xEF\xBB\xBF# comment\n"
            "; another comment\n"
            "  key = value with spaces  \r\n"
            "num = 42\n"
            "bad = x\n"
            "nums = 57 8, 494 474\n"
            "mal = 1 x\n"
            "no equals sign here\n"
            " = orphan value\n"
            "font.alias.Swis = Arial\n"
            "font.alias.Other = B\n"
            "fontz = not a prefix match\n");
    SkinConfig c;
    c.load(d.str());
    CHECK(!c.empty());
    CHECK_EQ(c.str("key"), std::string("value with spaces"));
    CHECK_EQ(c.str("missing", "def"), std::string("def"));
    CHECK_EQ(c.num("num", 0), 42);
    CHECK_EQ(c.num("bad", 7), 7);
    CHECK_EQ(c.num("missing", 9), 9);
    CHECK(c.nums("nums") == (std::vector<int>{ 57, 8, 494, 474 }));
    CHECK(c.nums("mal").empty());
    CHECK(c.nums("missing").empty());
    auto aliases = c.with_prefix("font.alias.");
    CHECK_EQ(aliases.size(), (size_t)2);
    CHECK_EQ(aliases["Swis"], std::string("Arial"));
}

TEST("SkinConfig: missing file leaves everything unset") {
    TempDir d;
    SkinConfig c;
    c.load(d.str());
    CHECK(c.empty());
    CHECK_EQ(c.num("x", 3), 3);
}

TEST("resolve_main_script_with: override > ini > the only .txt") {
    TempDir d;
    d.write("a.txt", "A");
    SkinConfig none;
    std::string why;
    CHECK_EQ(resolve_main_script_with(d.str(), none, "", &why), d.str() + "/a.txt");
    CHECK(why.empty());

    d.write("b.txt", "B");
    why.clear();
    CHECK_EQ(resolve_main_script_with(d.str(), none, "", &why), std::string());
    CHECK(why.find("2 *.txt") != std::string::npos);

    d.write(SkinConfig::kFileName, "script = b.txt\n");
    SkinConfig cfg; cfg.load(d.str());
    CHECK_EQ(resolve_main_script_with(d.str(), cfg, "", nullptr), d.str() + "/b.txt");
    CHECK_EQ(resolve_main_script_with(d.str(), cfg, "a.txt", nullptr), d.str() + "/a.txt");

    why.clear(); // a stale override is reported, then the ini still decides
    CHECK_EQ(resolve_main_script_with(d.str(), cfg, "gone.txt", &why), d.str() + "/b.txt");
    CHECK(why.find("gone.txt") != std::string::npos);
}

TEST("main_script_candidates: top-level .txt only, sorted") {
    TempDir d;
    d.write("z.txt", "");
    d.write("a.txt", "");
    d.write("notes.md", "");
    fs::create_directories(d.path / "panels");
    d.write("panels/inner.txt", "");
    CHECK(main_script_candidates(d.str()) == (std::vector<std::string>{ "a.txt", "z.txt" }));
}
