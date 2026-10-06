#include "skin_lint.h"
#include "fs_util.h"
#include "script_runtime.h"
#include "script_util.h"
#include "skin_config.h"
#include <algorithm>
#include <set>

namespace pui {

std::vector<ScriptCallSite> script_call_sites(const std::string& text) {
    std::vector<ScriptCallSite> out;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') { quoted = !quoted; continue; }
        if (quoted || c != '$') continue;
        size_t j = i + 1;
        while (j < text.size() && (isalnum((unsigned char)text[j]) || text[j] == '_')) ++j;
        if (j == i + 1 || j >= text.size() || text[j] != '(') continue;
        ScriptCallSite call;
        call.name = text.substr(i + 1, j - i - 1);
        // Arguments up to the matching ')': nested calls and '…' literals don't split.
        int depth = 0;
        bool q = false;
        std::string cur;
        size_t k = j + 1;
        for (; k < text.size(); ++k) {
            const char d = text[k];
            if (d == '\'') q = !q;
            if (!q && d == '(') ++depth;
            if (!q && d == ')') { if (depth == 0) break; --depth; }
            if (!q && d == ',' && depth == 0) { call.args.push_back(cur); cur.clear(); continue; }
            cur += d;
        }
        if (k >= text.size()) continue; // unbalanced: the core reports it when compiling
        if (!call.args.empty() || !cur.empty()) call.args.push_back(cur);
        out.push_back(std::move(call));
    }
    return out;
}

bool standard_titleformat_function(const std::string& name) {
    static const std::set<std::string> known = {
        // control flow
        "if", "if2", "if3", "ifequal", "ifgreater", "iflonger", "select",
        // arithmetic / boolean
        "add", "sub", "mul", "div", "mod", "muldiv", "min", "max", "rand", "greater",
        "and", "or", "not", "xor",
        // strings
        "abbr", "ansi", "ascii", "caps", "caps2", "char", "crc32", "crlf", "cut", "directory",
        "directory_path", "ext", "filename", "fix_eol", "hex", "insert", "left", "len", "len2",
        "longer", "longest", "lower", "num", "pad", "pad_right", "padcut", "padcut_right",
        "progress", "progress2", "repeat", "replace", "right", "roman", "rot13", "shortest",
        "strchr", "strcmp", "stricmp", "strrchr", "strstr", "substr", "stripprefix", "swapprefix",
        "tab", "trim", "upper", "nodiacritics",
        // track info / variables / dates
        "meta", "meta_sep", "meta_test", "meta_num", "info", "get", "put", "puts",
        "year", "month", "day_of_month", "date", "time",
        // colours
        "rgb", "blend", "transition", "hsl",
    };
    std::string n = name; // titleformat function names ignore case
    for (auto& c : n) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return known.count(n) != 0;
}

namespace {

struct Linter {
    std::string dir;
    SkinConfig cfg;
    std::vector<LintFinding> out;
    std::set<std::string> said;

    void add(LintFinding::Level lv, const std::string& file, const std::string& msg) {
        if (said.insert(file + "\n" + msg).second) out.push_back({ lv, file, msg });
    }

    std::string rel(const std::string& path) const {
        return path.compare(0, dir.size() + 1, dir + "/") == 0 ? path.substr(dir.size() + 1) : path;
    }

    // A script argument that is a plain path (nothing computed in it), resolved like the runtime
    // does; "" when it is computed, absolute, a wildcard or not an image.
    std::string literal_image(std::string a) const {
        a = clean_action(a);
        if (a.empty() || a.find_first_of("$%[*?") != std::string::npos) return {};
        if (a.find(".png") == std::string::npos && a.find(".jpg") == std::string::npos &&
            a.find(".bmp") == std::string::npos && a.find(".gif") == std::string::npos) return {};
        for (auto& c : a) if (c == '\\') c = '/';
        if ((a.size() >= 2 && a[1] == ':') || a.compare(0, 2, "//") == 0) return {};
        while (!a.empty() && (a[0] == '.' || a[0] == '/')) a.erase(0, 1);
        return dir + "/" + a;
    }

    std::string panels_dir() const { return dir + "/" + cfg.str("panels", "panels"); }

    void lint_script(const std::string& path, std::set<std::string>& panelScripts) {
        const std::string file = rel(path);
        const std::string text = read_file(path);
        if (text.empty()) { add(LintFinding::Level::Error, file, "empty or unreadable script"); return; }
        std::set<std::string> unknown, stubs;
        for (const ScriptCallSite& c : script_call_sites(text)) {
            const ScriptFunction* f = find_script_function(c.name);
            if (f && f->status == ScriptFunction::Status::Stub) stubs.insert("$" + c.name);
            else if (!f && !standard_titleformat_function(c.name)) unknown.insert("$" + c.name);
            if (f && c.args.size() < f->minArgs)
                add(LintFinding::Level::Error, file, "$" + c.name + " needs " + std::to_string(f->minArgs) +
                    " arguments, has " + std::to_string(c.args.size()) + " (renders as an error)");
            // Images the call names literally.
            std::vector<size_t> imgArgs;
            if (c.name == "imageabs" || c.name == "draw_image") imgArgs = { 4 };
            else if (c.name == "imageabs2") imgArgs = { 8 };
            else if (c.name == "button" || c.name == "button2") imgArgs = { 6, 7 };
            else if (c.name == "imagebutton") imgArgs = { 2, 3 };
            for (size_t i : imgArgs) {
                if (i >= c.args.size()) continue;
                check_file(file, literal_image(c.args[i]), "image");
            }
            // Panels: native, or a foreign UI element; a Track Display runs panels/<name>.txt.
            if (c.name == "panel" && c.args.size() >= 2) {
                const std::string name = clean_action(c.args[0]), type = clean_action(c.args[1]);
                if (name.find_first_of("$%") != std::string::npos || type.find_first_of("$%") != std::string::npos) continue;
                const PanelKind kind = panel_kind(type);
                if (kind == PanelKind::Embedded)
                    add(LintFinding::Level::Info, file, "panel '" + name + "' (" + type + ") is hosted from the UI element \"" +
                        map_type(type) + "\" — empty unless a component provides it");
                else if (kind == PanelKind::TrackDisplay)
                    panelScripts.insert(name);
            }
            // POPUP:<file> buttons open panels/<file>.txt.
            for (const std::string& a : c.args) {
                const std::string act = clean_action(a);
                if (act.compare(0, 6, "POPUP:") == 0) panelScripts.insert(unquote(act.substr(6)));
            }
        }
        for (auto& n : unknown)
            add(LintFinding::Level::Warning, file, "unknown function " + n +
                " (not this engine's, not standard titleformat — from another component, or rendered as an error)");
        for (auto& n : stubs)
            add(LintFinding::Level::Info, file, n + " is accepted but not implemented (ignored)");
    }

    // A file the skin names: missing is a warning; present only under another letter case is a
    // note (fine on Windows and a default macOS volume, missing on a case-sensitive one).
    void check_file(const std::string& file, const std::string& path, const std::string& what) {
        if (path.empty() || file_exists_utf8(path)) return;
        const std::filesystem::path p = fs_path(path);
        std::error_code ec;
        std::filesystem::directory_iterator it(p.parent_path(), ec), end;
        const std::string want = lower(fs_utf8(p.filename()));
        for (; !ec && it != end; it.increment(ec)) {
            if (lower(fs_utf8(it->path().filename())) != want) continue;
            add(LintFinding::Level::Info, file, what + " " + rel(path) + " is spelled " +
                fs_utf8(it->path().filename()) + " on disk (breaks on case-sensitive file systems)");
            return;
        }
        add(LintFinding::Level::Warning, file, what + " not found: " + rel(path));
    }

    static std::string lower(std::string s) {
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return s;
    }

    static bool file_exists_utf8(const std::string& p) {
        std::error_code ec;
        return std::filesystem::is_regular_file(fs_path(p), ec);
    }

    void run() {
        std::error_code ec;
        if (!std::filesystem::is_directory(fs_path(dir), ec)) {
            add(LintFinding::Level::Error, "", "not a folder: " + dir);
            return;
        }
        cfg.load(dir);
        if (cfg.empty())
            add(LintFinding::Level::Info, "", std::string("no ") + SkinConfig::kFileName + " — neutral defaults");
        std::string why;
        const std::string main = resolve_main_script_with(dir, cfg, "", &why);
        if (main.empty()) {
            add(LintFinding::Level::Error, "", "no main script: " + (why.empty() ? std::string("none found") : why));
            return;
        }
        if (!why.empty()) add(LintFinding::Level::Info, "", why);
        std::set<std::string> wanted, done;
        lint_script(main, wanted);
        // Panel scripts the skin reaches, transitively (a Display.txt can host more panels).
        while (true) {
            std::string next;
            for (auto& n : wanted) if (!done.count(n)) { next = n; break; }
            if (next.empty()) break;
            done.insert(next);
            const std::string path = panels_dir() + "/" + next + ".txt";
            if (!file_exists_utf8(path)) add(LintFinding::Level::Error, rel(main), "panel script not found: " + rel(path));
            else lint_script(path, wanted);
        }
        // asset.* art for the native panels ({theme} as the default theme, {n} = 1).
        std::string images = cfg.str("images");
        for (auto& c : images) if (c == '\\') c = '/';
        while (!images.empty() && images.back() == '/') images.pop_back();
        const std::string imgDir = images.empty() ? dir : dir + "/" + images;
        for (auto& [key, value] : cfg.with_prefix("asset.")) {
            std::string a = value;
            auto put = [&](const std::string& tag, int v) {
                for (size_t p; (p = a.find(tag)) != std::string::npos;) a.replace(p, tag.size(), std::to_string(v));
            };
            put("{theme}", cfg.num("theme.index_default", 1));
            put("{n}", 1);
            for (auto& c : a) if (c == '\\') c = '/';
            if (a.find('.') == std::string::npos) continue; // not a file (e.g. a window rect "57 8 494 474")
            check_file(SkinConfig::kFileName, imgDir + "/" + a, "asset." + key);
        }
    }
};

} // namespace

std::vector<LintFinding> lint_skin(const std::string& dirIn) {
    Linter l;
    l.dir = dirIn;
    for (auto& c : l.dir) if (c == '\\') c = '/';
    while (l.dir.size() > 1 && l.dir.back() == '/') l.dir.pop_back();
    l.run();
    std::stable_sort(l.out.begin(), l.out.end(), [](const LintFinding& a, const LintFinding& b) { return a.level < b.level; });
    return l.out;
}

std::vector<ScriptToken> tokenize_script(const std::string& t) {
    std::vector<ScriptToken> out;
    using K = ScriptToken::Kind;
    for (size_t i = 0; i < t.size();) {
        const char c = t[i];
        if (c == '\'') { // '…' literal, through the closing quote (or the end)
            size_t j = t.find('\'', i + 1);
            j = j == std::string::npos ? t.size() : j + 1;
            out.push_back({ K::Quoted, i, j - i });
            i = j;
        } else if (c == '%') { // %field% (a lone % is literal)
            size_t j = t.find('%', i + 1);
            const bool field = j != std::string::npos &&
                std::all_of(t.begin() + (long)i + 1, t.begin() + (long)j,
                            [](char ch) { return isalnum((unsigned char)ch) || ch == '_' || ch == ' '; });
            if (field && j > i + 1) { out.push_back({ K::Field, i, j + 1 - i }); i = j + 1; }
            else ++i;
        } else if (c == '$') {
            size_t j = i + 1;
            while (j < t.size() && (isalnum((unsigned char)t[j]) || t[j] == '_')) ++j;
            if (j > i + 1) out.push_back({ K::Function, i, j - i });
            i = j > i + 1 ? j : i + 1;
        } else if (c == '(' || c == ')' || c == ',' || c == '[' || c == ']') {
            out.push_back({ K::Paren, i, 1 });
            ++i;
        } else ++i;
    }
    return out;
}

std::string script_to_rtf(const std::string& t) {
    std::string out = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern Consolas;}}"
                      "{\\colortbl;\\red30\\green30\\blue30;\\red0\\green70\\blue190;\\red140\\green30\\blue140;"
                      "\\red30\\green130\\blue60;\\red170\\green100\\blue0;}\\f0\\fs20\\cf1 ";
    out.reserve(out.size() + t.size() * 2);
    auto emit = [&](size_t from, size_t to) { // UTF-8 bytes -> RTF text
        for (size_t i = from; i < to;) {
            const unsigned char c = (unsigned char)t[i];
            if (c < 0x80) {
                if (c == '\\' || c == '{' || c == '}') { out += '\\'; out += (char)c; }
                else if (c == '\n') out += "\\par\n";
                else if (c == '\t') out += "\\tab ";
                else if (c != '\r') out += (char)c;
                ++i;
                continue;
            }
            const size_t n = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            unsigned cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
            for (size_t k = 1; k < n && i + k < to; ++k) cp = (cp << 6) | ((unsigned char)t[i + k] & 0x3F);
            i += n;
            auto u16 = [&](unsigned v) { out += "\\u" + std::to_string((int)(short)v) + "?"; };
            if (cp >= 0x10000) { cp -= 0x10000; u16(0xD800 + (cp >> 10)); u16(0xDC00 + (cp & 0x3FF)); }
            else u16(cp);
        }
    };
    size_t pos = 0;
    for (const ScriptToken& k : tokenize_script(t)) {
        emit(pos, k.start);
        const int cf = k.kind == ScriptToken::Kind::Function ? 2 : k.kind == ScriptToken::Kind::Field ? 3
                     : k.kind == ScriptToken::Kind::Quoted ? 4 : 5;
        out += "\\cf" + std::to_string(cf) + " ";
        emit(k.start, k.start + k.len);
        out += "\\cf1 ";
        pos = k.start + k.len;
    }
    emit(pos, t.size());
    return out + "}";
}

std::vector<std::string> check_script(const std::string& text) {
    std::vector<std::string> out;
    std::set<std::string> unknown;
    for (const ScriptCallSite& c : script_call_sites(text)) {
        const ScriptFunction* f = find_script_function(c.name);
        if (!f && !standard_titleformat_function(c.name)) unknown.insert("$" + c.name);
        else if (f && c.args.size() < f->minArgs)
            out.push_back("$" + c.name + " needs " + std::to_string(f->minArgs) + " arguments, has " +
                          std::to_string(c.args.size()));
    }
    if (!unknown.empty()) {
        std::string list;
        for (auto& n : unknown) list += (list.empty() ? "" : ", ") + n;
        out.insert(out.begin(), "not this engine's nor standard titleformat (another component's?): " + list);
    }
    int depth = 0;
    bool quoted = false;
    for (char c : text) {
        if (c == '\'') quoted = !quoted;
        else if (!quoted && c == '(') ++depth;
        else if (!quoted && c == ')') --depth;
    }
    if (quoted) out.push_back("a ' quote is never closed");
    if (depth > 0) out.push_back(std::to_string(depth) + " unclosed '('");
    else if (depth < 0) out.push_back(std::to_string(-depth) + " ')' too many");
    return out;
}

void line_col(const std::string& text, size_t pos, int& line, int& col) {
    line = 1; col = 1;
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '\n') { ++line; col = 1; }
        else if ((c & 0xC0) != 0x80) ++col; // count characters, not continuation bytes
    }
}

std::string problems_summary(const std::vector<std::string>& problems) {
    std::string s;
    for (auto& p : problems) s += (s.empty() ? "\xe2\x9a\xa0 " : "; ") + p;
    return s;
}

std::string editor_status(int line, int col, const std::string& summary, const std::string& apply_hint, bool plain) {
    return "Ln " + std::to_string(line) + ", Col " + std::to_string(col) + "    " +
           (summary.empty() ? "No problems found" : summary) + "    " + apply_hint +
           (plain ? "    (long script: no colouring)" : "");
}

std::string format_findings(const std::vector<LintFinding>& findings) {
    std::string s;
    int n[3] = { 0, 0, 0 };
    for (auto& f : findings) {
        static const char* const tag[] = { "error  ", "warning", "info   " };
        ++n[(int)f.level];
        s += std::string(tag[(int)f.level]) + " " + (f.file.empty() ? "" : f.file + ": ") + f.message + "\n";
    }
    s += std::to_string(n[0]) + " error(s), " + std::to_string(n[1]) + " warning(s), " + std::to_string(n[2]) + " note(s)\n";
    return s;
}

} // namespace pui
