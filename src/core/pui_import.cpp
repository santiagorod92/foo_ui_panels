#include "pui_import.h"
#include "script_util.h"
#include <algorithm>

namespace pui {

namespace pui_text {

uint32_t u16(const std::string& d, size_t o) { return o + 2 <= d.size() ? (uint8_t)d[o] | (uint8_t)d[o + 1] << 8 : 0; }
uint32_t u32(const std::string& d, size_t o) {
    return o + 4 <= d.size() ? (uint32_t)(uint8_t)d[o] | (uint32_t)(uint8_t)d[o + 1] << 8 | (uint32_t)(uint8_t)d[o + 2] << 16 |
                                   (uint32_t)(uint8_t)d[o + 3] << 24
                             : 0;
}

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string extension(const std::string& path) {
    const size_t dot = path.rfind('.'), slash = path.rfind('/');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : lower(path.substr(dot + 1));
}

std::string latin1_to_utf8(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) out += (char)c;
        else { out += (char)(0xC0 | c >> 6); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::string pystrip(const std::string& s) {
    auto space = [](unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0xa0; };
    size_t a = 0, b = s.size();
    while (a < b && space((unsigned char)s[a])) ++a;
    while (b > a && space((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

size_t find_comment(const std::string& ln) {
    bool q = false;
    for (size_t i = 0; i + 1 < ln.size(); ++i) {
        const char c = ln[i];
        if (c == '\'') q = !q;
        else if (!q && c == '/' && ln[i + 1] == '/' && (i == 0 || ln[i - 1] != ':')) return i;
    }
    return std::string::npos;
}

std::string preprocess(const std::string& text, bool trim_tail) {
    std::string body;
    for (size_t start = 0;;) {
        const size_t nl = text.find('\n', start);
        std::string ln = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        const size_t c = find_comment(ln);
        if (c != std::string::npos) ln.resize(c);
        body += pystrip(ln);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    const size_t lp = body.rfind(')');
    if (trim_tail && lp != std::string::npos && body.size() - lp <= 8) body.resize(lp + 1);
    std::string res;
    size_t depth = 0;
    for (char c : body) {
        if (c == '(') ++depth;
        else if (c == ')') {
            if (!depth) continue;
            --depth;
        }
        res += c;
    }
    return res + std::string(depth, ')');
}

bool image_path_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' || c == '\\' ||
           c == ' ' || c == '-';
}

std::string fix_image_case(const std::string& text, const std::map<std::string, std::string>& index) {
    static const char* const kExts[] = { "png", "bmp", "jpg", "jpeg", "ico" };
    const std::string head = "/images\\";
    std::string out;
    size_t pos = 0, at = 0;
    while (at < text.size()) {
        if (text.size() - at < head.size() + 1 || lower(text.substr(at, head.size())) != head) { ++at; continue; }
        size_t e = at + head.size();
        while (e < text.size() && image_path_char(text[e])) ++e;
        size_t end = std::string::npos;
        for (size_t dot = e; end == std::string::npos && dot-- > at + head.size() + 1;) {
            if (text[dot] != '.') continue;
            for (const char* x : kExts) {
                const size_t n = std::char_traits<char>::length(x);
                if (dot + 1 + n <= e && lower(text.substr(dot + 1, n)) == x) { end = dot + 1 + n; break; }
            }
        }
        if (end == std::string::npos) { ++at; continue; }
        const std::string ref = text.substr(at, end - at);
        std::string rel = ref.substr(1);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const auto real = index.find(lower(rel));
        out += text.substr(pos, at - pos);
        if (real == index.end() || real->second == rel) out += ref;
        else {
            std::string fixed = real->second;
            std::replace(fixed.begin(), fixed.end(), '/', '\\');
            out += "/" + fixed;
        }
        pos = at = end;
    }
    return out + text.substr(pos);
}

std::set<std::string> referenced_panels(const std::string& data) {
    std::set<std::string> names;
    for (size_t p = data.find("$panel("); p != std::string::npos;) {
        size_t j = p + 7;
        while (j < data.size() && data[j] != ',' && data[j] != '(' && data[j] != ')') ++j;
        if (j > p + 7 && j < data.size() && data[j] == ',') {
            names.insert(data.substr(p + 7, j - p - 7));
            p = data.find("$panel(", j + 1);
        } else {
            p = data.find("$panel(", p + 1);
        }
    }
    return names;
}

bool safe_panel_name(const std::string& name) {
    for (unsigned char c : name)
        if (c < 0x20 || std::string("<>:\"/\\|?*").find((char)c) != std::string::npos) return false;
    return true;
}

}

namespace {

using namespace pui_text;

bool is_text(const std::string& d, size_t p, size_t n) {
    for (size_t i = p; i < p + n; ++i) {
        const unsigned char c = (unsigned char)d[i];
        if (c < 32 && c != 9 && c != 10 && c != 13) return false;
        if (c == 127) return false;
    }
    return true;
}

bool valid_utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k) if (((unsigned char)s[i + k] >> 6) != 2) return false;
        i += n;
    }
    return true;
}

std::string to_utf8(const std::string& s) { return valid_utf8(s) ? s : latin1_to_utf8(s); }

std::string largest_text(const std::string& d, size_t from, size_t to) {
    std::string best;
    to = std::min(to, d.size());
    for (size_t p = from; p + 4 <= to;) {
        const uint32_t n = u32(d, p);
        if (n >= 2 && n <= to - p - 4 && is_text(d, p + 4, n)) {
            if (n > best.size()) best = d.substr(p + 4, n);
            p += 4 + n;
        } else {
            ++p;
        }
    }
    return best;
}

struct Record { size_t off, cfg, end; std::string name; };

std::vector<Record> find_records(const std::string& d, size_t from, const std::set<std::string>& names) {
    std::vector<Record> out;
    for (size_t p = from; p + 24 < d.size();) {
        const uint32_t n = u32(d, p + 16);
        bool ok = n >= 1 && n <= 64 && p + 24 + n <= d.size() && is_text(d, p + 20, n);
        if (ok) {
            const std::string name = d.substr(p + 20, n);
            const uint32_t size = u32(d, p + 20 + n);
            const size_t cfg = p + 24 + n;
            if (names.count(name) && safe_panel_name(name) && size <= d.size() - cfg) {
                out.push_back({ p, cfg, cfg + size, name });
                p = cfg;
                continue;
            }
        }
        ++p;
    }
    return out;
}

std::map<std::string, std::set<std::string>> panel_types(const std::string& text) {
    std::map<std::string, std::set<std::string>> out;
    for (size_t p = text.find("$panel("); p != std::string::npos; p = text.find("$panel(", p + 1)) {
        const size_t a = text.find(',', p + 7);
        if (a == std::string::npos) break;
        const size_t b = text.find(',', a + 1);
        if (b == std::string::npos) break;
        out[text.substr(p + 7, a - p - 7)].insert(pystrip(text.substr(a + 1, b - a - 1)));
    }
    return out;
}

std::string base_name(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}

bool parse_pui(const std::string& d, PuiConfig& out, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    out = PuiConfig();
    if (d.size() < 64) return fail("not a Panels UI configuration (too short)");
    out.version = u32(d, 0);
    if (out.version < 3 || out.version > 4)
        return fail("not a Panels UI 0.13 configuration (format version " + std::to_string(out.version) + ")");
    size_t o = out.version >= 4 ? 52 : 48;
    const uint32_t count = u32(d, o);
    o += 4;
    if (count > 100000) return fail("the configuration's variable list is damaged");
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t kn = u32(d, o);
        if (kn > 1000 || kn > d.size() - o - 4) return fail("the configuration's variable list is damaged");
        std::string k = d.substr(o + 4, kn);
        o += 4 + kn;
        const uint32_t vn = u32(d, o);
        if (o + 4 > d.size() || vn > d.size() - o - 4) return fail("the configuration's variable list is damaged");
        out.pvars.push_back({ to_utf8(k), to_utf8(d.substr(o + 4, vn)) });
        o += 4 + vn;
    }
    const uint32_t gn = u32(d, o);
    if (o + 4 > d.size() || gn > d.size() - o - 4) return fail("the configuration's script is damaged");
    out.globals = to_utf8(d.substr(o + 4, gn));
    o += 4 + gn + 4;
    if (o > d.size()) return fail("the configuration's script is damaged");
    const std::set<std::string> names = referenced_panels(d);
    const std::vector<Record> recs = find_records(d, o, names);
    out.script = to_utf8(largest_text(d, o, recs.empty() ? d.size() : recs.front().off));
    for (size_t i = 0; i < recs.size(); ++i) {
        const size_t stop = i + 1 < recs.size() ? std::min(recs[i].end, recs[i + 1].off) : recs[i].end;
        const std::string text = to_utf8(largest_text(d, recs[i].cfg, stop));
        auto it = std::find_if(out.panels.begin(), out.panels.end(), [&](const auto& p) { return p.first == recs[i].name; });
        if (it == out.panels.end()) out.panels.push_back({ recs[i].name, text });
        else if (it->second.empty()) it->second = text;
    }
    if (out.script.empty() && out.globals.empty()) return fail("the configuration has no script");
    return true;
}

PuiSkin convert_pui(const SkinFiles& files, const std::string& main_rel, std::string* error) {
    PuiSkin skin;
    const std::string root = main_rel.rfind('/') == std::string::npos ? std::string() : main_rel.substr(0, main_rel.rfind('/') + 1);
    const std::string* main = nullptr;
    std::vector<const SkinFiles::value_type*> popups;
    std::map<std::string, std::string> index;
    for (const auto& f : files) {
        if (f.first.compare(0, root.size(), root) != 0) continue;
        const std::string rel = f.first.substr(root.size()), ext = extension(rel);
        if (f.first == main_rel) { main = &f.second; continue; }
        if (ext == "pui" || ext == "pp" || ext == "ava") { popups.push_back(&f); continue; }
        if (ext == "dll" || ext == "exe" || ext == "txt" || ext == "ini") continue;
        index[lower(rel)] = rel;
        skin.files.push_back({ rel, f.second });
        ++skin.assets;
    }
    if (!main) {
        if (error) *error = "the configuration file " + main_rel + " wasn't found";
        return skin;
    }
    PuiConfig cfg;
    if (!parse_pui(*main, cfg, error)) return skin;

    std::set<std::string> displays, written;
    auto add_panels = [&](const PuiConfig& c) {
        for (const auto& [name, text] : c.panels) {
            if (written.count(name)) continue;
            if (!displays.count(name)) continue;
            written.insert(name);
            skin.files.push_back({ "panels/" + name + ".txt", fix_image_case(preprocess(text, false), index) });
            ++skin.panels;
        }
    };
    auto collect_types = [&](const PuiConfig& c) {
        std::string all = c.globals + c.script;
        for (const auto& p : c.panels) all += p.second;
        for (const auto& [name, kinds] : panel_types(all))
            for (const auto& type : kinds) if (panel_kind(type) == PanelKind::TrackDisplay) displays.insert(name);
    };

    std::string prefix;
    for (const auto& [k, v] : cfg.pvars) prefix += "$if($strcmp($getpvar(" + k + "),),$setpvar(" + k + "," + v + "),)";
    skin.main = fix_image_case(prefix + preprocess(cfg.globals, false) + preprocess(cfg.script, false), index);
    collect_types(cfg);

    std::vector<PuiConfig> popupCfgs;
    for (const auto* p : popups) {
        PuiConfig pc;
        if (!parse_pui(p->second, pc, nullptr)) continue;
        collect_types(pc);
        skin.files.push_back({ "panels/" + base_name(p->first) + ".txt",
                               fix_image_case(preprocess(pc.globals, false) + preprocess(pc.script, false), index) });
        ++skin.popups;
        popupCfgs.push_back(std::move(pc));
    }
    add_panels(cfg);
    for (const auto& pc : popupCfgs) add_panels(pc);
    for (const auto& name : displays) {
        if (!written.insert(name).second) continue;
        skin.files.push_back({ "panels/" + name + ".txt", std::string() });
    }
    skin.ini = "# foo_ui_panels skin configuration (written by Panels UI's .pui import from " + base_name(main_rel) + ").\n"
               "drawrect.zero_fills = 1\n";
    return skin;
}

}
