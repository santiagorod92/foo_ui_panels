#include "fcl_import.h"
#include "fs_util.h"
#include <algorithm>
#include <cctype>
#include <cmath>
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
const Guid kWsh = guid("75a7b642-786c-4f24-9b52-17d737dea09a");
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

size_t json_space(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return i;
}

size_t json_skip(const std::string& s, size_t i) {
    i = json_space(s, i);
    if (i >= s.size()) return std::string::npos;
    if (s[i] == '"') {
        for (++i; i < s.size(); ++i) {
            if (s[i] == '\\') ++i;
            else if (s[i] == '"') return i + 1;
        }
        return std::string::npos;
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        for (; i < s.size(); ++i) {
            if (s[i] == '"') {
                i = json_skip(s, i);
                if (i == std::string::npos) return i;
                --i;
            } else if (s[i] == '{' || s[i] == '[') ++depth;
            else if ((s[i] == '}' || s[i] == ']') && --depth == 0) return i + 1;
        }
        return std::string::npos;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !std::isspace((unsigned char)s[i])) ++i;
    return i;
}

void utf8_append(std::string& out, uint32_t c) {
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xc0 | c >> 6); out += (char)(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) { out += (char)(0xe0 | c >> 12); out += (char)(0x80 | (c >> 6 & 0x3f)); out += (char)(0x80 | (c & 0x3f)); }
    else {
        out += (char)(0xf0 | c >> 18);
        out += (char)(0x80 | (c >> 12 & 0x3f));
        out += (char)(0x80 | (c >> 6 & 0x3f));
        out += (char)(0x80 | (c & 0x3f));
    }
}

std::string json_text(const std::string& raw) {
    std::string out;
    if (raw.size() < 2 || raw.front() != '"') return raw;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] != '\\') { out += raw[i]; continue; }
        const char e = raw[++i];
        if (e == 'n') out += '\n';
        else if (e == 'r') out += '\r';
        else if (e == 't') out += '\t';
        else if (e == 'b') out += '\b';
        else if (e == 'f') out += '\f';
        else if (e == 'u' && i + 4 < raw.size()) {
            uint32_t c = (uint32_t)std::strtoul(raw.substr(i + 1, 4).c_str(), nullptr, 16);
            i += 4;
            if (c >= 0xd800 && c < 0xdc00 && i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
                const uint32_t lo = (uint32_t)std::strtoul(raw.substr(i + 3, 4).c_str(), nullptr, 16);
                if (lo >= 0xdc00 && lo < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00); i += 6; }
            }
            utf8_append(out, c);
        } else out += e;
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> json_members(const std::string& obj) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = json_space(obj, 0);
    if (i >= obj.size() || obj[i] != '{') return out;
    i = json_space(obj, i + 1);
    while (i < obj.size() && obj[i] == '"') {
        const size_t k = json_skip(obj, i);
        if (k == std::string::npos) return out;
        const std::string key = json_text(obj.substr(i, k - i));
        i = json_space(obj, k);
        if (i >= obj.size() || obj[i] != ':') return out;
        const size_t v = json_space(obj, i + 1), end = json_skip(obj, v);
        if (end == std::string::npos) return out;
        out.emplace_back(key, obj.substr(v, end - v));
        i = json_space(obj, end);
        if (i < obj.size() && obj[i] == ',') i = json_space(obj, i + 1);
    }
    return out;
}

std::string json_get(const std::string& obj, const std::string& key) {
    for (auto& [k, v] : json_members(obj))
        if (k == key) return v;
    return {};
}

bool safe_package_id(const std::string& id) {
    return !id.empty() && id.find_first_of("/\\:") == std::string::npos && id.find("..") == std::string::npos;
}

void parse_smp_settings(const std::string& json, FclNode& node) {
    if (json_text(json_get(json, "id")) != "settings") return;
    const std::string payload = json_get(json, "payload");
    const int type = std::atoi(json_get(json, "scriptType").c_str());
    if (type == 1) node.script = json_text(json_get(payload, "script"));
    else if (type == 3) {
        const int location = std::atoi(json_get(payload, "locationType").c_str());
        const char* base = location == 1 ? "fb.ComponentPath + " : location == 2 ? "fb.ProfilePath + " : location == 3 ? "fb.FoobarPath + " : "";
        node.script = std::string("include(") + base + json_string(json_text(json_get(payload, "path"))) + ");";
    } else if (type == 4 || type == 5) {
        const std::string id = json_text(json_get(payload, "id"));
        if (!safe_package_id(id)) return;
        node.script = "include(utils.GetPackagePath(" + json_string(id) + ") + 'main.js');";
    } else return;
    for (auto& [k, v] : json_members(json_get(json_get(json, "properties"), "values"))) node.properties.emplace_back(k, v);
    node.panel_id = json_text(json_get(json, "panelId"));
    if (node.name.empty()) node.name = node.panel_id;
}

void parse_smp(const std::string& data, FclNode& node) {
    try {
        Reader r(data);
        const uint32_t version = r.u32();
        if (version == 2) {
            parse_smp_settings(r.string(), node);
            return;
        }
        if (version != 1) return;
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

std::string lower(std::string s);

std::string wsh_value(Reader& r, uint16_t vt) {
    auto number = [](double d) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", d);
        return std::string(buf);
    };
    auto bytes = [&](size_t n) {
        const std::string b = r.take(n);
        int64_t v = 0;
        std::memcpy(&v, b.data(), n);
        return v;
    };
    switch (vt) {
    case 16: return std::to_string((int8_t)bytes(1));
    case 17: return std::to_string((uint8_t)bytes(1));
    case 2: return std::to_string((int16_t)bytes(2));
    case 18: return std::to_string((uint16_t)bytes(2));
    case 11: return bytes(2) ? "true" : "false";
    case 3:
    case 22: return std::to_string((int32_t)bytes(4));
    case 19:
    case 23: return std::to_string((uint32_t)bytes(4));
    case 4: {
        const uint32_t v = (uint32_t)bytes(4);
        float f = 0;
        std::memcpy(&f, &v, 4);
        return number(f);
    }
    case 20: return std::to_string(bytes(8));
    case 21: return std::to_string((uint64_t)bytes(8));
    case 6: return number((double)bytes(8) / 10000);
    case 5:
    case 7: {
        const int64_t v = bytes(8);
        double d = 0;
        std::memcpy(&d, &v, 8);
        return number(d);
    }
    case 8: return json_string(r.string());
    default: return {};
    }
}

bool parse_wsh(const std::string& data, FclNode& node) {
    try {
        Reader r(data);
        const uint32_t version = r.u32();
        if (version < 0x73 || version > 0x7b) return false;
        if (version >= 0x7a) r.take(1);
        if (version >= 0x79) r.take(16);
        if (version >= 0x78) r.u32();
        if (version >= 0x77) {
            const uint32_t count = r.u32();
            if (count > 100000) return false;
            for (uint32_t i = 0; i < count; ++i) {
                const std::string name = r.string();
                const std::string vt = r.take(2);
                const std::string value = wsh_value(r, (uint16_t)((unsigned char)vt[0] | ((unsigned char)vt[1] << 8)));
                if (!value.empty()) node.properties.emplace_back(name, value);
            }
        }
        if (version >= 0x76) r.take(1);
        if (version >= 0x75) r.take(1);
        if (version >= 0x74) r.take(kWindowPlacement);
        const std::string engine = lower(r.string());
        if (!engine.empty() && engine.find("jscript") == std::string::npos && engine.find("javascript") == std::string::npos) return false;
        node.script = r.string();
        return true;
    } catch (const std::exception&) {
        return false;
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
    r.u32();
    node.vertical = r.u32() != 0;
    for (int i = 0; i < 5; ++i) r.u32();
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
        const bool share = head[4] || head[5] || head[6] || head[7];
        child.forced = head[8] || head[9] || head[10] || head[11];
        child.fixed = !share;
        child.visible = share || !child.forced;
        std::memcpy(&child.share, head.data() + 64, sizeof child.share);
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
    } else if (g == kWsh) {
        if (parse_wsh(data, node)) node.kind = FclNode::Kind::Smp;
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

struct Kids {
    std::map<std::string, std::string> names;
    std::vector<std::string> ids;
};

class Converter {
public:
    explicit Converter(bool outline) : m_outline(outline) {}

    void collect(const FclNode& node) {
        auto scan = [&](const std::string& text) {
            rewrite(text,
                    [&](const std::string& name, const std::vector<std::string>& args, std::string&) {
                        if ((lower(name) == "set_ps_global" || lower(name) == "init_ps_global") && !args.empty()) m_globals.insert(trim(args[0]));
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
        std::string box, script, panel_id;
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

    std::string tf(const std::string& text, const std::string& box, const Kids* kids) {
        auto kid = [&](const std::string& n) -> std::string {
            if (!kids) return {};
            auto it = kids->names.find(lower(trim(n)));
            return it == kids->names.end() ? std::string() : it->second;
        };
        auto nth = [&](const std::string& n) -> std::string {
            const std::string t = trim(n);
            if (!kids || t.empty() || t.size() > 4 || !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); }))
                return {};
            const size_t i = std::stoul(t);
            return i < kids->ids.size() ? kids->ids[i] : std::string();
        };
        return rewrite(
            text,
            [&](const std::string& name, const std::vector<std::string>& a, std::string& out) {
                const std::string low = lower(name);
                if (low == "set_ps_global" && a.size() >= 2) { out = "$puts(ps." + trim(a[0]) + "," + a[1] + ")"; return true; }
                if (low == "init_ps_global" && a.size() >= 2) {
                    out = "$puts(ps." + trim(a[0]) + ",$if2($get(ps." + trim(a[0]) + ")," + a[1] + "))";
                    return true;
                }
                if (low == "get_ps_global" && !a.empty()) { out = "$get(ps." + trim(a[0]) + ")"; return true; }
                if ((low == "showpanel_c" || low == "showpanel") && a.size() >= 2) {
                    const std::string k = low == "showpanel" ? nth(a[0]) : kid(a[0]);
                    out = k.empty() ? std::string() : "$puts(vis." + k + ",$ifequal(" + a[1] + ",0,0,1))";
                    return true;
                }
                if ((low == "movepanel_c" || low == "movepanel") && a.size() >= 5) {
                    const std::string k = low == "movepanel" ? nth(a[0]) : kid(a[0]);
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

    void place(const FclNode& node, const std::string& box, const std::string& parent, const Kids* kids) {
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

    void split(const FclNode& node, const std::string& box, const std::vector<std::string>& ids) {
        const std::string sp = "sp." + box, along = node.vertical ? "h" : "w", across = node.vertical ? "w" : "h";
        std::string fixed = "$add(0", weight = "$add(0";
        bool any = false;
        for (size_t i = 0; i < ids.size(); ++i) {
            const FclNode& c = node.children[i];
            if (c.forced) continue;
            any = true;
            const std::string amount = std::to_string(std::lround(c.fixed ? c.share : c.share * 100));
            (c.fixed ? fixed : weight) += ",$ifequal($get(vis." + ids[i] + "),0,0," + amount + ")";
        }
        if (!any) return;
        lines.push_back("$puts(" + sp + ".r,$max(0,$sub($get(" + box + "." + along + ")," + fixed + "))))$puts(" + sp + ".s,$max(1," + weight +
                        ")))$puts(" + sp + ".o,0)");
        for (size_t i = 0; i < ids.size(); ++i) {
            const FclNode& c = node.children[i];
            if (c.forced) continue;
            const std::string& id = ids[i];
            const std::string length = c.fixed ? std::to_string(std::lround(c.share))
                                               : "$muldiv($get(" + sp + ".r)," + std::to_string(std::lround(c.share * 100)) + ",$get(" + sp + ".s))";
            const std::string offset = "$get(" + sp + ".o)", full = "$get(" + box + "." + across + ")", size = "$get(" + sp + ".l)";
            lines.push_back("$puts(" + sp + ".l," + length + ")$if($get(mv." + id + "),,$puts(mv." + id + ",1)$puts(mv." + id + ".x," +
                            (node.vertical ? "0" : offset) + ")$puts(mv." + id + ".y," + (node.vertical ? offset : "0") + ")$puts(mv." + id +
                            ".w," + (node.vertical ? full : size) + ")$puts(mv." + id + ".h," + (node.vertical ? size : full) + "))$puts(" + sp +
                            ".o,$add(" + offset + ",$ifequal($get(vis." + id + "),0,0," + size + ")))");
        }
    }

    void emit(const FclNode& node, const std::string& box, const std::string* parent, const Kids* kids) {
        if (parent) place(node, box, *parent, kids);
        switch (node.kind) {
        case FclNode::Kind::Stack: {
            Kids mine;
            std::vector<std::string>& ids = mine.ids;
            for (const auto& c : node.children) {
                ids.push_back(ident(c.name.empty() ? c.type : c.name));
                mine.names[lower(c.name)] = ids.back();
            }
            for (size_t i = 0; i < ids.size(); ++i)
                lines.push_back("$puts(vis." + ids[i] + "," + (node.children[i].visible ? "1" : "0") + ")$puts(mv." + ids[i] + ",)");
            if (!node.script.empty()) {
                std::string s = tf(node.script, box, &mine);
                s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
                lines.push_back(s);
            }
            split(node, box, ids);
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
                const std::string id = ident(!active->title.empty() ? active->title : !active->name.empty() ? active->name : active->type);
                lines.push_back("$puts(mv." + id + ",)");
                FclNode copy = *active;
                copy.rect.clear();
                emit(copy, id, &box, nullptr);
            }
            break;
        }
        case FclNode::Kind::Smp:
            scripts.push_back({ box, node.script, node.panel_id, node.properties });
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
        if (!s.panel_id.empty())
            seed += "Object.defineProperty(window, 'Name', { get: () => " + json_string(s.panel_id) + " });\n";
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
        if (std::filesystem::is_directory(dir / "themes", ec) || std::filesystem::is_directory(dir / "packages", ec))
            return dir == prof ? std::string() : fs_utf8(dir);
        if (dir == dir.parent_path()) break;
    }
    return {};
}

bool copy_theme_folders(const std::string& root, const std::string& profile, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path from = fs_path(root), to = fs_path(profile);
    auto failed = [&] {
        if (error) *error = ec.message();
        return false;
    };
    if (fs::is_directory(from / "themes", ec)) {
        fs::copy(from / "themes", to / "themes", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) return failed();
    }
    if (fs::is_directory(from / "wsh-data", ec)) {
        fs::copy(from / "wsh-data", to / "wsh-data", fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (ec) return failed();
    }
    if (fs::is_directory(from / "packages", ec)) {
        const fs::path packages = to / "foo_spider_monkey_panel" / "packages";
        for (auto it = fs::recursive_directory_iterator(from / "packages", ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 2 || it->path().filename() != "package.json") continue;
            std::string meta = read_file(fs_utf8(it->path()));
            if (meta.rfind("\xEF\xBB\xBF", 0) == 0) meta.erase(0, 3);
            const std::string id = json_text(json_get(meta, "id"));
            if (!safe_package_id(id)) continue;
            fs::create_directories(packages / fs_path(id), ec);
            if (!ec) fs::copy(it->path().parent_path(), packages / fs_path(id), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) return failed();
        }
        if (ec) return failed();
    }
    if (fs::is_directory(from / "fonts", ec)) {
        const fs::path fonts = to / "foo_js_panel" / "fonts";
        for (auto it = fs::directory_iterator(from / "fonts", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const std::string ext = lower(fs_utf8(it->path().extension()));
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            fs::create_directories(fonts, ec);
            if (!ec) fs::copy_file(it->path(), fonts / it->path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) return failed();
        }
        if (ec) return failed();
    }
    return true;
}

}
