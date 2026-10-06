#include "test.h"
#include "core/fs_util.h"
#include "core/skin_lint.h"
#include <filesystem>

using namespace pui;

namespace {
// A throwaway skin folder under the system temp dir.
struct TempSkin {
    std::string dir;
    TempSkin() {
        dir = (std::filesystem::temp_directory_path() / ("pui_lint_" + std::to_string(tick_ms()))).string();
        std::filesystem::create_directories(dir + "/panels");
        std::filesystem::create_directories(dir + "/img");
    }
    ~TempSkin() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    void put(const std::string& rel, const std::string& text) { write_file(dir + "/" + rel, text); }
};
bool has(const std::vector<LintFinding>& f, LintFinding::Level lv, const std::string& part) {
    for (auto& x : f) if (x.level == lv && (x.file + ": " + x.message).find(part) != std::string::npos) return true;
    return false;
}
} // namespace

TEST("lint: call sites split top-level arguments only") {
    auto c = script_call_sites("$button(1,$add(2,3),'a,b',x.png)");
    CHECK_EQ(c.size(), (size_t)2);
    CHECK_EQ(c[0].name, std::string("button"));
    CHECK_EQ(c[0].args.size(), (size_t)4);
    CHECK_EQ(c[0].args[1], std::string("$add(2,3)"));
    CHECK_EQ(c[0].args[2], std::string("'a,b'"));
    CHECK_EQ(c[1].name, std::string("add"));
    CHECK(script_call_sites("$f()")[0].args.empty());
    CHECK(script_call_sites("$f(").empty()); // unbalanced
}

TEST("lint: standard titleformat vs. unknown functions") {
    CHECK(standard_titleformat_function("ifgreater"));
    CHECK(!standard_titleformat_function("drawrect"));
    CHECK(standard_titleformat_function("UPPER"));
}

TEST("lint: a skin with problems") {
    TempSkin s;
    s.put("img/ok.png", "x");
    s.put("main.txt",
          "$imageabs2(0,0,0,0,0,0,1,1,img/ok.png)$imageabs2(0,0,0,0,0,0,1,1,img/gone.png)"
          "$imageabs2(0,0,0,0,0,0,1,1,img/$getpvar(x).png)"   // computed: not checked
          "$panel(disp,Track Display,0,0,10,10)$panel(eq,Equalizer,0,0,1,1)"
          "$button(0,0,0,0,0,0,img/ok.png,img/ok.png,'POPUP:settings.ava')"
          "$frobnicate(1)$drawrect(1,2)$scplsetlayout(x)");
    s.put("panels/disp.txt", "$panel(inner,Track Display,0,0,1,1)$upper(%title%)");
    s.put("foo_ui_panels.ini", "asset.nocover = img/none_{theme}.png\n");
    auto f = lint_skin(s.dir);
    CHECK(has(f, LintFinding::Level::Warning, "main.txt: image not found: img/gone.png"));
    CHECK(!has(f, LintFinding::Level::Warning, "getpvar"));
    CHECK(has(f, LintFinding::Level::Warning, "unknown function $frobnicate"));
    CHECK(has(f, LintFinding::Level::Error, "$drawrect needs 5 arguments, has 2"));
    CHECK(has(f, LintFinding::Level::Info, "$scplsetlayout is accepted but not implemented"));
    CHECK(has(f, LintFinding::Level::Info, "panel 'eq' (Equalizer)"));
    CHECK(has(f, LintFinding::Level::Error, "panel script not found: panels/settings.ava.txt"));
    CHECK(has(f, LintFinding::Level::Error, "panel script not found: panels/inner.txt")); // found transitively
    CHECK(!has(f, LintFinding::Level::Error, "panels/disp.txt"));
    CHECK(has(f, LintFinding::Level::Warning, "asset.nocover not found: img/none_1.png"));
    CHECK(f.front().level == LintFinding::Level::Error); // errors first
    CHECK(format_findings(f).find(" error(s), ") != std::string::npos);
}

TEST("lint: no main script, not a folder") {
    TempSkin s;
    s.put("a.txt", "x"); s.put("b.txt", "y"); // two candidates, no config: ambiguous
    CHECK(has(lint_skin(s.dir), LintFinding::Level::Error, "no main script"));
    CHECK(has(lint_skin(s.dir + "/nope"), LintFinding::Level::Error, "not a folder"));
}

TEST("editor: tokens") {
    const std::string t = "a$font(x,'b,c')%title% 100% [x]";
    auto k = tokenize_script(t);
    using K = ScriptToken::Kind;
    auto at = [&](size_t i) { return t.substr(k[i].start, k[i].len); };
    CHECK(k[0].kind == K::Function && at(0) == "$font");
    CHECK(k[1].kind == K::Paren && at(1) == "(");
    CHECK(k[2].kind == K::Paren && at(2) == ",");
    CHECK(k[3].kind == K::Quoted && at(3) == "'b,c'");
    CHECK(k[5].kind == K::Field && at(5) == "%title%");
    CHECK(k.back().kind == K::Paren && at(k.size() - 1) == "]");
    for (auto& x : k) CHECK(t.substr(x.start, x.len) != "% [x"); // a lone % isn't a field
}

TEST("editor: check_script and line_col") {
    CHECK(check_script("$if(%title%,$drawrect(1,2,3,4,x))").empty());
    auto p = check_script("$drawrect(1,2)$nope(1)$if(a,b");
    CHECK_EQ(p.size(), (size_t)3);
    CHECK(p[0].find("$nope") != std::string::npos);
    CHECK(p[1].find("$drawrect needs 5") != std::string::npos);
    CHECK(p[2].find("1 unclosed") != std::string::npos);
    CHECK(check_script("'open").back().find("quote") != std::string::npos);
    int l, c;
    line_col("ab\n\xc3\xa9x", 5, l, c); // after "é"
    CHECK_EQ(l, 2); CHECK_EQ(c, 2);
}

TEST("editor: script_to_rtf colours tokens and escapes") {
    const std::string r = script_to_rtf("$f(a{b}\\c)\n\xc3\xa9");
    CHECK(r.compare(0, 6, "{\\rtf1") == 0);
    CHECK(r.find("\\cf2 $f\\cf1 ") != std::string::npos);
    CHECK(r.find("a\\{b\\}\\\\c") != std::string::npos);
    CHECK(r.find("\\par\n") != std::string::npos);
    CHECK(r.find("\\u233?") != std::string::npos); // é
    CHECK(r.back() == '}');
}

TEST("editor status line: caret position, problems, long scripts") {
    int line, col;
    line_col_utf16(std::u16string(u"ab\r\ncd\ne"), 0, line, col);
    CHECK(line == 1 && col == 1);
    line_col_utf16(std::u16string(u"ab\r\ncd\ne"), 4, line, col); // "\r\n" is one line break
    CHECK(line == 2 && col == 1);
    line_col_utf16(std::u16string(u"ab\r\ncd\ne"), 6, line, col);
    CHECK(line == 2 && col == 3);
    line_col_utf16(std::u16string(u"a\rb\nc"), 99, line, col);   // lone \r (RichEdit) and \n
    CHECK(line == 3 && col == 2);

    CHECK_EQ(problems_summary({}), std::string());
    CHECK_EQ(problems_summary({ "a", "b" }), std::string("\xe2\x9a\xa0 a; b"));
    CHECK_EQ(editor_status(3, 7, "", "Ctrl+S: apply", false),
             std::string("Ln 3, Col 7    No problems found    Ctrl+S: apply"));
    CHECK_EQ(editor_status(1, 1, "\xe2\x9a\xa0 x", "Ctrl+S: apply", true),
             std::string("Ln 1, Col 1    \xe2\x9a\xa0 x    Ctrl+S: apply    (long script: no colouring)"));
}
