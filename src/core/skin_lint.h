#pragma once
#include <string>
#include <vector>

namespace pui {

struct LintFinding {
    enum class Level { Error, Warning, Info };
    Level level;
    std::string file;
    std::string message;
};

struct ScriptCallSite {
    std::string name;
    std::vector<std::string> args;
};
std::vector<ScriptCallSite> script_call_sites(const std::string& text);

bool standard_titleformat_function(const std::string& name);

std::vector<LintFinding> lint_skin(const std::string& dir);

struct ScriptToken {
    enum class Kind { Function, Field, Quoted, Paren };
    Kind kind;
    size_t start, len;
};
std::vector<ScriptToken> tokenize_script(const std::string& text);
std::string script_to_rtf(const std::string& text);
std::vector<std::string> check_script(const std::string& text);
void line_col(const std::string& text, size_t pos, int& line, int& col);
template <class Str>
void line_col_utf16(const Str& text, size_t pos, int& line, int& col) {
    line = 1; col = 1;
    for (size_t i = 0; i < pos && i < (size_t)text.size(); ++i) {
        const auto c = text[i];
        if (c == '\n' && i > 0 && text[i - 1] == '\r') continue;
        if (c == '\r' || c == '\n') { ++line; col = 1; } else ++col;
    }
}
constexpr size_t kEditorHighlightMax = 256 * 1024;
std::string problems_summary(const std::vector<std::string>& problems);
std::string editor_status(int line, int col, const std::string& summary, const std::string& apply_hint, bool plain);
std::string format_findings(const std::vector<LintFinding>& findings);

}
