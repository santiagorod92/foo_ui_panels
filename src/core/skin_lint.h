// Offline skin check: what a skin folder uses that this engine can't run, without foobar2000.
// Reads the skin's foo_ui_panels.ini, main script and panel scripts and reports functions the
// engine doesn't implement (and that aren't standard titleformat), images and panel scripts the
// scripts name that aren't there, `asset.*` art that's missing, and panels that need a foreign UI
// element. The same checks the running engine prints to the console, all at once — for triaging
// another skin before (or without) loading it: `tools/skin_lint <skin folder>`.
// SDK-free (built into the unit tests and the tool).
#pragma once
#include <string>
#include <vector>

namespace pui {

struct LintFinding {
    enum class Level { Error, Warning, Info };
    Level level;
    std::string file;    // relative to the skin folder ("" = the skin as a whole)
    std::string message;
};

// One `$name(args…)` occurrence: the arguments as written (top-level commas split, parentheses
// and '…' literals respected), not evaluated.
struct ScriptCallSite {
    std::string name;
    std::vector<std::string> args;
};
std::vector<ScriptCallSite> script_call_sites(const std::string& text);

// Whether $name is one of foobar2000's standard titleformat functions (a static list; the running
// engine asks the core instead, which also knows functions other components add).
bool standard_titleformat_function(const std::string& name);

std::vector<LintFinding> lint_skin(const std::string& dir);

// --- the code editor ------------------------------------------------------------------------
// Syntax colouring: byte ranges of `text` (UTF-8) by kind. Unmarked text is literal output.
struct ScriptToken {
    enum class Kind { Function, Field, Quoted, Paren };
    Kind kind;
    size_t start, len;
};
std::vector<ScriptToken> tokenize_script(const std::string& text);
// The script as RTF with its tokens coloured — loaded into a rich-text editor in one go
// (colouring token by token re-lays-out the whole text per token: minutes for a 40 KB script).
// Colour table: 1 text, 2 function, 3 field, 4 quoted, 5 punctuation. Consolas 10pt.
std::string script_to_rtf(const std::string& text);
// What a quick look at one script finds, for the editor's status line: functions neither this
// engine nor standard titleformat has, calls with too few arguments, unbalanced parentheses.
// Empty when it looks fine.
std::vector<std::string> check_script(const std::string& text);
// 1-based line and column (in characters) of byte offset `pos`.
void line_col(const std::string& text, size_t pos, int& line, int& col);
// The same for an editor's UTF-16 text and caret index (what the platform text views report);
// "\r\n", "\r" and "\n" each end a line.
template <class Str>
void line_col_utf16(const Str& text, size_t pos, int& line, int& col) {
    line = 1; col = 1;
    for (size_t i = 0; i < pos && i < (size_t)text.size(); ++i) {
        const auto c = text[i];
        if (c == '\n' && i > 0 && text[i - 1] == '\r') continue; // second half of "\r\n"
        if (c == '\r' || c == '\n') { ++line; col = 1; } else ++col;
    }
}
// The longest script the editor colours; a bigger one stays plain text.
constexpr size_t kEditorHighlightMax = 256 * 1024;
// check_script()'s problems on one line ("⚠ a; b"), "" when there are none.
std::string problems_summary(const std::vector<std::string>& problems);
// The editor's status line: "Ln 3, Col 7    <summary or No problems found>    <apply_hint>",
// plus a note when the script is too long to colour.
std::string editor_status(int line, int col, const std::string& summary, const std::string& apply_hint, bool plain);
// "error  panels/Display.txt: …" lines plus a summary line.
std::string format_findings(const std::vector<LintFinding>& findings);

} // namespace pui
