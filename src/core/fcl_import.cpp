#include "fcl_import.h"
#include "fs_util.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace pui {

namespace {

using Guid = std::string;

Guid guid(const char* text) {
    unsigned v[16] = {};
    std::sscanf(text, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x", &v[3], &v[2], &v[1], &v[0], &v[5], &v[4], &v[7], &v[6],
                &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15]);
    Guid g(16, '\0');
    for (int i = 0; i < 16; ++i) g[i] = (char)v[i];
    return g;
}

const Guid kHeader = guid("9faadff3-e51a-4a8b-b4a3-d209a36ab301");
const Guid kLayoutDataset = guid("2cf00365-f2d7-4e78-9fca-c1d56e2707d8");
const Guid kHorizontal = guid("8fa0bc24-882a-4fff-8a3b-215ea7fbd07f");
const Guid kVertical = guid("77653a44-66d1-49e0-9a7a-1c71898c0441");
const Guid kTabs = guid("5cb67c98-b77f-4926-a79f-49d9b21b9705");
const Guid kSmp = guid("32271525-436d-4afa-8dfd-437ab0c83118");
const Guid kStack = guid("4532ec89-c567-4267-b33a-0c154b6f0105");
constexpr size_t kStackChildHead = 72, kStackChildTail = 16, kLogFont = 92, kWindowPlacement = 44;

class Reader {
public:
    explicit Reader(const std::string& d) : m_d(d) {}
    std::string take(size_t n) {
        if (m_p + n > m_d.size()) throw std::runtime_error("the layout file is cut short");
        std::string b = m_d.substr(m_p, n);
        m_p += n;
        return b;
    }
    uint32_t u32() {
        const std::string b = take(4);
        return (uint32_t)(uint8_t)b[0] | (uint32_t)(uint8_t)b[1] << 8 | (uint32_t)(uint8_t)b[2] << 16 | (uint32_t)(uint8_t)b[3] << 24;
    }
    bool flag() { return take(1)[0] != 0; }
    Guid id() { return take(16); }
    std::string string() { return take(u32()); }
    size_t left() const { return m_d.size() - m_p; }

private:
    const std::string& m_d;
    size_t m_p = 0;
};

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out + "\"";
}

void parse_smp(const std::string& data, FclNode& node) {
    try {
        Reader r(data);
        if (r.u32() != 1) return;
        r.take(1 + 16 + 1);
        const uint32_t count = r.u32();
        if (count > 100000) return;
        for (uint32_t i = 0; i < count; ++i) {
            const std::string name = r.string();
            const uint32_t kind = r.u32();
            std::string value;
            if (kind == 0) value = r.flag() ? "true" : "false";
            else if (kind == 1) { const uint32_t v = r.u32(); value = std::to_string((int32_t)v); }
            else if (kind == 2) {
                const std::string b = r.take(8);
                double d = 0;
                std::memcpy(&d, b.data(), 8);
                char buf[40];
                std::snprintf(buf, sizeof buf, "%.17g", d);
                value = buf;
            } else if (kind == 3) value = json_string(r.string());
            else return;
            node.properties.emplace_back(name, value);
        }
        r.take(2 + kWindowPlacement);
        node.script = r.string();
    } catch (const std::exception&) {
    }
}

FclNode parse_node(const Guid& g, const std::string& data, const std::map<Guid, std::string>& names, int depth);

std::vector<FclNode> parse_splitter(const std::string& data, const std::map<Guid, std::string>& names, int depth) {
    std::vector<FclNode> children;
    if (data.empty()) return children;
    Reader r(data);
    r.u32();
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        const Guid g = r.id();
        r.u32();
        r.flag();
        const bool hidden = r.flag();
        r.flag();
        const bool autohide = r.flag();
        const uint32_t size = r.u32();
        r.flag();
        FclNode child = parse_node(g, r.string(), names, depth + 1);
        const bool custom = r.flag();
        const std::string title = r.string();
        child.size = size;
        child.hidden = hidden || autohide;
        if (custom) child.title = title;
        children.push_back(std::move(child));
    }
    for (auto& child : children) {
        if (r.left() < 4) break;
        const std::string extra = r.string();
        if (extra.size() >= 8) child.size = Reader(extra).u32();
    }
    return children;
}

std::vector<FclNode> parse_tabs(const std::string& data, const std::map<Guid, std::string>& names, int depth) {
    std::vector<FclNode> children;
    if (data.empty()) return children;
    Reader r(data);
    r.u32();
    const uint32_t active = r.u32();
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        const Guid g = r.id();
        FclNode child = parse_node(g, r.string(), names, depth + 1);
        const bool custom = r.flag();
        const std::string title = r.string();
        if (custom) child.title = title;
        child.active = i == active;
        children.push_back(std::move(child));
    }
    return children;
}

void parse_stack(const std::string& data, const std::map<Guid, std::string>& names, int depth, FclNode& node) {
    Reader r(data);
    for (int i = 0; i < 7; ++i) r.u32();
    node.script = r.string();
    r.take(24 + kLogFont);
    r.u32();
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count; ++i) {
        const Guid g = r.id();
        const std::string name = r.string();
        const std::string head = r.take(kStackChildHead);
        std::vector<std::string> rect;
        for (int k = 0; k < 4; ++k) rect.push_back(r.string());
        r.take(kStackChildTail);
        FclNode child = parse_node(g, r.string(), names, depth + 1);
        child.name = name;
        child.rect = rect;
        child.visible = head[4] || head[5] || head[6] || head[7];
        node.children.push_back(std::move(child));
    }
}

FclNode parse_node(const Guid& g, const std::string& data, const std::map<Guid, std::string>& names, int depth) {
    if (depth > 64) throw std::runtime_error("the layout nests too deep");
    FclNode node;
    auto it = names.find(g);
    node.type = it != names.end() ? it->second : std::string("panel");
    if (g == kHorizontal || g == kVertical) {
        node.kind = g == kHorizontal ? FclNode::Kind::Horizontal : FclNode::Kind::Vertical;
        node.children = parse_splitter(data, names, depth);
    } else if (g == kTabs) {
        node.kind = FclNode::Kind::Tabs;
        node.children = parse_tabs(data, names, depth);
    } else if (g == kStack) {
        node.kind = FclNode::Kind::Stack;
        parse_stack(data, names, depth, node);
    } else if (g == kSmp) {
        node.kind = FclNode::Kind::Smp;
        parse_smp(data, node);
    }
    return node;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

using CallFn = std::function<bool(const std::string&, const std::vector<std::string>&, std::string&)>;
using FieldFn = std::function<bool(const std::string&, std::string&)>;

std::string rewrite(const std::string& text, const CallFn& call, const FieldFn& field) {
    std::string out;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (c == '\'') {
            size_t j = text.find('\'', i + 1);
            if (j == std::string::npos) j = n - 1;
            out.append(text, i, j - i + 1);
            i = j + 1;
        } else if (c == '$') {
            size_t j = i + 1;
            while (j < n && (std::isalnum((unsigned char)text[j]) || text[j] == '_')) ++j;
            if (j == i + 1 || j >= n || text[j] != '(') {
                out += c;
                ++i;
                continue;
            }
            const std::string name = text.substr(i + 1, j - i - 1);
            const size_t start = j + 1;
            int depth = 1;
            bool quoted = false;
            size_t k = start;
            while (k < n && depth) {
                if (text[k] == '\'') quoted = !quoted;
                else if (!quoted && text[k] == '(') ++depth;
                else if (!quoted && text[k] == ')') --depth;
                ++k;
            }
            const std::string inner = depth == 0 ? text.substr(start, k - 1 - start) : text.substr(start);
            std::vector<std::string> args;
            if (!inner.empty())
                for (const std::string& a : tf_split_args(inner)) args.push_back(rewrite(a, call, field));
            std::string replaced;
            if (call(name, args, replaced)) out += replaced;
            else {
                out += "$" + name + "(";
                for (size_t a = 0; a < args.size(); ++a) out += (a ? "," : "") + args[a];
                out += ")";
            }
            i = k;
        } else if (c == '%') {
            const size_t j = text.find('%', i + 1);
            if (j == std::string::npos) {
                out.append(text, i, std::string::npos);
                break;
            }
            std::string replaced;
            if (field(text.substr(i + 1, j - i - 1), replaced)) out += replaced;
            else out.append(text, i, j - i + 1);
            i = j + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

const char* native_panel(const std::string& type) {
    static const std::map<std::string, const char*> map = {
        { "eslyric", "Lyric Show" }, { "lyric show", "Lyric Show" }, { "openlyrics", "Lyric Show" },
        { "shpeck", "Spectrum" }, { "spectrum analyser", "Spectrum" }, { "spectrum", "Spectrum" },
        { "peakmeter spectrum visualisation", "Spectrum" }, { "vu meter", "Peak meter" },
        { "playlist view", "Playlist" }, { "ng playlist", "Playlist" }, { "playlist", "Playlist" },
        { "single column playlist", "Playlist" }, { "playlist switcher", "Playlists" }, { "playlist tabs", "Playlists" },
        { "album list", "Library tree" }, { "library tree", "Library tree" }, { "facets", "Library tree" },
        { "artwork view", "Album art" }, { "album art", "Album art" }, { "seekbar", "Seekbar" }, { "volume", "Volume" },
        { "item details", "Text" }, { "item properties", "Text" }, { "status pane", "Text" },
    };
    auto it = map.find(lower(type));
    return it == map.end() ? nullptr : it->second;
}

const char* const kRefresh = R"JS(
(function () {
    if (typeof panelsui === 'undefined' || !panelsui.IsAvailable) return;
    const refresh = () => panelsui.RunAction('PVAR:TOGGLE:pss.refresh');
    globalThis.RefreshPSS = refresh;
    const run = fb.RunMainMenuCommand;
    fb.RunMainMenuCommand = function (cmd) {
        if (/panel stack splitter/i.test(String(cmd))) { refresh(); return true; }
        return run.call(fb, cmd);
    };
})();
)JS";

class Converter {
public:
    explicit Converter(bool outline) : m_outline(outline) {}

    void collect(const FclNode& node) {
        auto scan = [&](const std::string& text) {
            rewrite(text,
                    [&](const std::string& name, const std::vector<std::string>& args, std::string&) {
                        if (lower(name) == "set_ps_global" && !args.empty()) m_globals.insert(trim(args[0]));
                        return false;
                    },
                    [](const std::string&, std::string&) { return false; });
        };
        if (node.kind != FclNode::Kind::Smp) scan(node.script);
        for (const auto& r : node.rect) scan(r);
        for (const auto& c : node.children) collect(c);
    }

    void root(const FclNode& node) {
        lines.push_back("$puts(root.x,0)$puts(root.y,0)$puts(root.w,%_width%)$puts(root.h,%_height%)$puts(pss.refresh,$getpvar(pss.refresh))");
        emit(node, "root", nullptr, nullptr);
        if (!m_outline) return;
        std::vector<std::string> names(m_globals.begin(), m_globals.end());
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        const size_t rows = names.size() / 6 + 1;
        for (size_t n = 0; n < names.size(); n += 6) {
            std::string row;
            for (size_t k = n; k < std::min(names.size(), n + 6); ++k) row += (k > n ? "  " : "") + names[k] + "=$get(ps." + names[k] + ")";
            lines.push_back("$font(Segoe UI,11,)$drawstring(" + row + ",4,$sub(%_height%," + std::to_string(14 * (rows - n / 6)) +
                            "),%_width%,14,120-255-120)");
        }
    }

    std::vector<std::string> lines;
    struct Script {
        std::string box, script;
        std::vector<std::pair<std::string, std::string>> properties;
    };
    std::vector<Script> scripts;
    std::vector<std::string> hosted;

private:
    std::string ident(const std::string& raw) {
        std::string base;
        for (char c : raw.empty() ? std::string("panel") : raw) base += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        std::string name = base;
        for (int n = 2; m_names.count(name); ++n) name = base + "_" + std::to_string(n);
        m_names.insert(name);
        return name;
    }

    std::string tf(const std::string& text, const std::string& box, const std::map<std::string, std::string>* kids) {
        auto kid = [&](const std::string& n) -> std::string {
            if (!kids) return {};
            auto it = kids->find(lower(trim(n)));
            return it == kids->end() ? std::string() : it->second;
        };
        return rewrite(
            text,
            [&](const std::string& name, const std::vector<std::string>& a, std::string& out) {
                const std::string low = lower(name);
                if (low == "set_ps_global" && a.size() >= 2) { out = "$puts(ps." + trim(a[0]) + "," + a[1] + ")"; return true; }
                if (low == "get_ps_global" && !a.empty()) { out = "$get(ps." + trim(a[0]) + ")"; return true; }
                if (low == "showpanel_c" && a.size() >= 2) {
                    const std::string k = kid(a[0]);
                    out = k.empty() ? std::string() : "$puts(vis." + k + ",$ifequal(" + a[1] + ",0,0,1))";
                    return true;
                }
                if (low == "movepanel_c" && a.size() >= 5) {
                    const std::string k = kid(a[0]);
                    out.clear();
                    if (k.empty()) return true;
                    out = "$puts(mv." + k + ",1)";
                    const char* keys = "xywh";
                    for (int i = 0; i < 4; ++i) out += "$puts(mv." + k + "." + keys[i] + "," + a[1 + i] + ")";
                    return true;
                }
                if ((low == "drawrect" || low == "gradientrect" || low == "imageabs" || low == "drawroundrect") && a.size() >= 4) {
                    std::vector<std::string> moved = { "$add($get(" + box + ".x)," + a[0] + ")", "$add($get(" + box + ".y)," + a[1] + ")" };
                    moved.insert(moved.end(), a.begin() + 2, a.end());
                    if (low == "gradientrect") {
                        moved.resize(std::min<size_t>(moved.size(), 6));
                        for (size_t i = 4; i < moved.size(); ++i) {
                            std::string c = trim(moved[i]);
                            if (std::count(c.begin(), c.end(), '-') == 3) c = c.substr(0, c.rfind('-'));
                            moved[i] = c;
                        }
                    }
                    out = "$" + name + "(";
                    for (size_t i = 0; i < moved.size(); ++i) out += (i ? "," : "") + moved[i];
                    out += ")";
                    return true;
                }
                return false;
            },
            [&](const std::string& name, std::string& out) {
                const std::string low = lower(name);
                if (low == "ps_width" || low == "_width" || low == "ps_wdith") { out = "$get(" + box + ".w)"; return true; }
                if (low == "ps_height" || low == "_height" || low == "ps_heigth") { out = "$get(" + box + ".h)"; return true; }
                if (low == "ps_user_profile_path") { out = "%pui_profile_folder%"; return true; }
                if (m_globals.count(name)) { out = "$get(ps." + name + ")"; return true; }
                return false;
            });
    }

    void place(const FclNode& node, const std::string& box, const std::string& parent, const std::map<std::string, std::string>* kids) {
        const char* keys = "xywh";
        const std::string defaults[4] = { "0", "0", "$get(" + parent + ".w)", "$get(" + parent + ".h)" };
        for (int i = 0; i < 4; ++i) {
            const std::string expr = i < (int)node.rect.size() ? node.rect[i] : std::string();
            const std::string value = trim(expr).empty() ? defaults[i] : tf(expr, parent, kids);
            std::string moved = "$if($get(mv." + box + "),$get(mv." + box + "." + keys[i] + ")," + value + ")";
            if (i < 2) moved = "$add($get(" + parent + "." + keys[i] + ")," + moved + ")";
            lines.push_back("$puts(" + box + "." + keys[i] + "," + moved + ")");
        }
    }

    std::string panel(const std::string& box, const std::string& kind) {
        const std::string g = "$get(" + box + ".x),$get(" + box + ".y),$get(" + box + ".w),$get(" + box + ".h)";
        if (!m_outline) return "$panel(" + box + "," + kind + "," + g + ")";
        return "$drawrect(" + g + ",brushcolor-null pencolor-255-80-80)$font(Segoe UI,11,)$drawstring(" + box + " " + kind + " $get(" + box +
               ".x)/$get(" + box + ".y) $get(" + box + ".w)x$get(" + box + ".h),$add($get(" + box + ".x),3),$add($get(" + box +
               ".y),2),$get(" + box + ".w),16,255-220-120)";
    }

    void emit(const FclNode& node, const std::string& box, const std::string* parent, const std::map<std::string, std::string>* kids) {
        if (parent) place(node, box, *parent, kids);
        switch (node.kind) {
        case FclNode::Kind::Stack: {
            std::vector<std::string> ids;
            std::map<std::string, std::string> mine;
            for (const auto& c : node.children) {
                ids.push_back(ident(c.name.empty() ? c.type : c.name));
                mine[lower(c.name)] = ids.back();
            }
            for (size_t i = 0; i < ids.size(); ++i)
                lines.push_back("$puts(vis." + ids[i] + "," + (node.children[i].visible ? "1" : "0") + ")$puts(mv." + ids[i] + ",)");
            if (!node.script.empty()) {
                std::string s = tf(node.script, box, &mine);
                s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
                lines.push_back(s);
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                const size_t start = lines.size();
                emit(node.children[i], ids[i], &box, &mine);
                std::string body;
                for (size_t k = start; k < lines.size(); ++k) body += lines[k];
                lines.resize(start);
                lines.push_back("$ifequal($get(vis." + ids[i] + "),0,," + body + ")");
            }
            break;
        }
        case FclNode::Kind::Horizontal:
        case FclNode::Kind::Vertical: {
            const bool h = node.kind == FclNode::Kind::Horizontal;
            std::vector<const FclNode*> shown;
            for (const auto& c : node.children)
                if (!c.hidden) shown.push_back(&c);
            std::string offset = "0";
            for (size_t n = 0; n < shown.size(); ++n) {
                const FclNode& c = *shown[n];
                const std::string id = ident(!c.title.empty() ? c.title : !c.name.empty() ? c.name : c.type);
                const std::string length = n + 1 == shown.size() ? "$sub($get(" + box + "." + (h ? "w" : "h") + ")," + offset + ")" : std::to_string(c.size);
                lines.push_back("$puts(mv." + id + ",1)");
                lines.push_back("$puts(mv." + id + ".x," + (h ? offset : std::string("0")) + ")");
                lines.push_back("$puts(mv." + id + ".y," + (h ? std::string("0") : offset) + ")");
                lines.push_back("$puts(mv." + id + ".w," + (h ? length : "$get(" + box + ".w)") + ")");
                lines.push_back("$puts(mv." + id + ".h," + (h ? "$get(" + box + ".h)" : length) + ")");
                FclNode copy = c;
                copy.rect.clear();
                emit(copy, id, &box, nullptr);
                offset = "$add(" + offset + "," + length + ")";
            }
            break;
        }
        case FclNode::Kind::Tabs: {
            const FclNode* active = nullptr;
            for (const auto& c : node.children)
                if (c.active) active = &c;
            if (!active && !node.children.empty()) active = &node.children.front();
            if (active) {
                const std::string id = ident(!active->title.empty() ? active->title : active->type);
                lines.push_back("$puts(mv." + id + ",)");
                FclNode copy = *active;
                copy.rect.clear();
                emit(copy, id, &box, nullptr);
            }
            break;
        }
        case FclNode::Kind::Smp:
            scripts.push_back({ box, node.script, node.properties });
            lines.push_back(panel(box, "JS Panel"));
            break;
        case FclNode::Kind::Panel: {
            static const std::set<std::string> skip = { "menu", "buttons", "spacer", "panel stack splitter" };
            if (skip.count(lower(node.type))) break;
            const char* native = native_panel(node.type);
            if (!native) hosted.push_back(node.type);
            lines.push_back(panel(box, native ? native : node.type));
            break;
        }
        }
    }

    bool m_outline;
    std::set<std::string> m_globals, m_names;
};

}

std::vector<std::string> tf_split_args(const std::string& text) {
    std::vector<std::string> args;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    for (char c : text) {
        if (c == '\'') quoted = !quoted;
        else if (!quoted && c == '(') ++depth;
        else if (!quoted && c == ')') --depth;
        else if (!quoted && c == ',' && depth == 0) {
            args.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    args.push_back(cur);
    return args;
}

std::vector<FclPreset> parse_fcl(const std::string& bytes, std::string* error) {
    std::vector<FclPreset> presets;
    try {
        Reader r(bytes);
        if (r.id() != kHeader) throw std::runtime_error("not a Columns UI layout export (.fcl)");
        if (r.u32() >= 1) r.u32();
        std::map<Guid, std::string> names;
        const uint32_t panelCount = r.u32();
        for (uint32_t i = 0; i < panelCount; ++i) {
            const Guid g = r.id();
            names[g] = r.string();
        }
        const uint32_t datasets = r.u32();
        for (uint32_t i = 0; i < datasets; ++i) {
            const Guid g = r.id();
            r.string();
            const uint32_t refs = r.u32();
            for (uint32_t k = 0; k < refs; ++k) r.u32();
            const std::string blob = r.string();
            if (g != kLayoutDataset) continue;
            Reader lr(blob);
            lr.u32();
            const uint32_t active = lr.u32();
            const uint32_t count = lr.u32();
            for (uint32_t k = 0; k < count; ++k) {
                FclPreset p;
                const Guid pg = lr.id();
                p.name = lr.string();
                const std::string data = lr.string();
                p.active = k == active;
                p.empty = pg == std::string(16, '\0');
                if (!p.empty) p.root = parse_node(pg, data, names, 0);
                presets.push_back(std::move(p));
            }
        }
        if (presets.empty()) throw std::runtime_error("the file has no layout");
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        presets.clear();
    }
    return presets;
}

ImportedSkin convert_fcl(const std::vector<FclPreset>& presets, const std::string& preset, bool outline, std::string* error) {
    ImportedSkin out;
    const FclPreset* chosen = nullptr;
    for (const auto& p : presets)
        if (preset.empty() ? p.active : p.name == preset) chosen = &p;
    if (!chosen && preset.empty() && !presets.empty()) chosen = &presets.front();
    if (!chosen || chosen->empty) {
        if (error) *error = preset.empty() ? "the layout has no preset to convert" : "no layout preset named " + preset;
        return out;
    }
    Converter c(outline);
    c.collect(chosen->root);
    c.root(chosen->root);
    out.preset = chosen->name;
    for (const auto& l : c.lines)
        if (!l.empty()) out.main += l + "\n";
    out.ini = "script = main.txt\n";
    for (const auto& s : c.scripts) {
        std::string seed = "(function () {\n    const seed = {";
        for (size_t i = 0; i < s.properties.size(); ++i)
            seed += (i ? ", " : "") + json_string(s.properties[i].first) + ": " + s.properties[i].second;
        seed += "};\n    for (const k in seed) window.GetProperty(k, seed[k]);\n})();\n";
        out.files.emplace_back("js/" + s.box + ".js", seed + s.script + "\n" + kRefresh);
        out.ini += "panel." + s.box + ".script = js/" + s.box + ".js\n";
    }
    out.js_panels = (int)c.scripts.size();
    out.hosted_by_name = c.hosted;
    return out;
}

std::string fcl_skin_name(const std::string& fclPath) {
    std::string slashed = fclPath;
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    const std::filesystem::path p = fs_path(slashed);
    std::string name;
    if (p.has_parent_path() && p.parent_path().has_parent_path() && lower(fs_utf8(p.parent_path().parent_path().filename())) == "themes")
        name = fs_utf8(p.parent_path().filename());
    if (name.empty()) name = fs_utf8(p.stem());
    if (name.empty()) name = "Imported layout";
    name[0] = (char)std::toupper((unsigned char)name[0]);
    return name;
}

std::string fcl_theme_root(const std::string& fclPath, const std::string& profile) {
    std::error_code ec;
    const auto prof = std::filesystem::weakly_canonical(fs_path(profile), ec);
    std::filesystem::path dir = std::filesystem::weakly_canonical(fs_path(fclPath), ec).parent_path();
    for (int up = 0; up < 4 && !dir.empty(); ++up, dir = dir.parent_path()) {
        if (std::filesystem::is_directory(dir / "themes", ec)) return dir == prof ? std::string() : fs_utf8(dir);
        if (dir == dir.parent_path()) break;
    }
    return {};
}

bool copy_theme_folders(const std::string& root, const std::string& profile, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path from = fs_path(root), to = fs_path(profile);
    fs::copy(from / "themes", to / "themes", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    if (fs::is_directory(from / "wsh-data", ec)) {
        fs::copy(from / "wsh-data", to / "wsh-data", fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (ec) {
            if (error) *error = ec.message();
            return false;
        }
    }
    return true;
}

}
