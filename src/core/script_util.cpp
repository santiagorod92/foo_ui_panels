#include "script_util.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pui {

// --- $eval integer expression evaluator ------------------------------------
namespace {
struct Expr {
    const char* s;
    void skip() { while (*s == ' ') ++s; }
    long number() {
        skip();
        // {} and () are both grouping in PanelsUI $eval.
        if (*s == '(' || *s == '{') {
            char close = (*s == '(') ? ')' : '}';
            ++s; long v = expr(); skip(); if (*s == close) ++s; return v;
        }
        long sign = 1; if (*s == '-') { sign = -1; ++s; }
        long v = 0; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
        return sign * v;
    }
    long term() { long v = number();
        for (;;) { skip();
            if (*s == '*') { ++s; v *= number(); }
            else if (*s == '/') { ++s; long d = number(); v = d ? v / d : 0; }
            else if (*s == '%') { ++s; long d = number(); v = d ? v % d : 0; }
            else break; } return v; }
    long expr() { long v = term();
        for (;;) { skip();
            if (*s == '+') { ++s; v += term(); }
            else if (*s == '-') { ++s; v -= term(); }
            else break; } return v; }
};
} // namespace

long eval_expr(const std::string& in) { Expr e{ in.c_str() }; return e.expr(); }

// --- colours ---------------------------------------------------------------
gfx::Color parse_rgb(const char* s) {
    int v[4] = { 0,0,0,255 }, n = 0;
    while (*s && n < 4) {
        while (*s && (*s < '0' || *s > '9')) ++s; // skip non-digit (prefix/dashes)
        if (!*s) break;
        int x = 0; while (*s >= '0' && *s <= '9') { x = x * 10 + (*s - '0'); ++s; }
        v[n++] = x;
        if (*s == '-') ++s; else break;
    }
    return gfx::Color(v[0], v[1], v[2]);
}

bool parse_config_color(const std::string& v, gfx::Color& out) {
    size_t i = v.find_first_not_of(" \t");
    if (i == std::string::npos) return false;
    if (v[i] == '#') {
        unsigned x = 0;
        if (v.size() - i - 1 < 6 || sscanf(v.c_str() + i + 1, "%6x", &x) != 1) return false;
        out = gfx::Color((x >> 16) & 255, (x >> 8) & 255, x & 255);
        return true;
    }
    int c[3], n = 0; const char* p = v.c_str() + i;
    while (*p && n < 3) {
        while (*p && (*p < '0' || *p > '9')) ++p;
        if (!*p) break;
        c[n++] = (int)strtol(p, const_cast<char**>(&p), 10) & 255;
    }
    if (n != 3) return false;
    out = gfx::Color(c[0], c[1], c[2]);
    return true;
}

bool find_color(const std::string& spec, const char* key, gfx::Color& out) {
    auto p = spec.find(key);
    if (p == std::string::npos) return false;
    const char* v = spec.c_str() + p + strlen(key);
    while (*v == '-' || *v == ' ') ++v;
    if (strncmp(v, "null", 4) == 0) return false; // transparent
    out = parse_rgb(v);
    return true;
}

bool parse_argb(const std::string& s, gfx::Color& out, int& alpha) {
    int v[4] = { 0, 0, 0, 0 }, n = 0;
    for (const char* q = s.c_str(); *q && n < 4;) {
        if (*q < '0' || *q > '9') { ++q; continue; }
        v[n++] = atoi(q);
        while (*q >= '0' && *q <= '9') ++q;
    }
    if (n >= 4) { alpha = v[0]; out = gfx::Color(v[1], v[2], v[3]); return true; }
    if (n == 3) { alpha = 255; out = gfx::Color(v[0], v[1], v[2]); return true; }
    return false;
}

unsigned long parse_hex_colorref(const std::string& s) {
    return strtoul(s.c_str(), nullptr, 16);
}

std::string hex_colorref(unsigned long c) {
    char buf[16]; snprintf(buf, sizeof buf, "%lx", c & 0xFFFFFFul); return buf;
}

// --- script arguments ------------------------------------------------------
// fooAvA's text buttons are $button2(...,$font(face,size,style,colour)Label,...): skip the
// balanced $func(...) prefix, the rest is the label.
std::string trailing_literal(const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r')) ++i;
        if (i >= body.size()) break;
        if (body[i] != '$') return body.substr(i);
        size_t op = body.find('(', i);
        if (op == std::string::npos) return std::string();
        int depth = 1;
        size_t j = op + 1;
        for (; j < body.size() && depth > 0; ++j) {
            if (body[j] == '(') ++depth;
            else if (body[j] == ')') --depth;
        }
        if (depth > 0) return std::string();
        i = j;
    }
    return std::string();
}

std::string clean_action(std::string a) {
    size_t b = a.find_first_not_of(" \t"), e = a.find_last_not_of(" \t");
    if (b == std::string::npos) return {};
    a = a.substr(b, e - b + 1);
    if (a.size() >= 2 && a.front() == '\'' && a.back() == '\'') a = a.substr(1, a.size() - 2);
    return a;
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
    return s;
}

void parse_img_opts(const std::string& o, int& alpha, int& flip) {
    auto a = o.find("alpha-");        if (a != std::string::npos) alpha = atoi(o.c_str() + a + 6);
    auto r = o.find("ROTATEFLIP-");   if (r != std::string::npos) flip  = atoi(o.c_str() + r + 11);
}

unsigned text_opts(const std::string& o) {
    unsigned f = gfx::kEndEllipsis;
    if (o.find("center")  != std::string::npos) f |= gfx::kAlignCenter;
    if (o.find("right")   != std::string::npos) f |= gfx::kAlignRight;
    if (o.find("vcenter") != std::string::npos) f |= gfx::kVCenter | gfx::kSingleLine;
    return f;
}

// The skin sometimes glues these straight onto the colour ("glow-114-114-114glowexpand-0"), so
// they don't always arrive as their own token.
void parse_glow_tail(const std::string& t, int& expand, int& alpha) {
    size_t e = t.find("glowexpand-");
    if (e != std::string::npos) expand = atoi(t.c_str() + e + 11);
    size_t a = t.find("glowalpha-");
    if (a != std::string::npos) alpha = atoi(t.c_str() + a + 10);
}

const char* map_type(const std::string& t) {
    auto has = [&](const char* k) { return t.find(k) != std::string::npos; };
    if (has("Channel spectrum") || t == "Spectrum")      return "Spectrum";
    if (has("Single Column Playlist") || has("ELPlaylist")) return "Playlist View";
    if (has("Track Display") || has("Peakmeter") || has("Peak Meter") || has("Album Art") || has("Lyric") || has("Playlist switcher") || has("Seek") || has("Volume") || has("Chronflow") ||
        has("Quick Search") || has("Album list") || has("Graphical Browser"))
        return nullptr; // native (Album list/Graphical Browser: no stock DUI element to host)
    return t.c_str();
}

// --- main-menu actions -----------------------------------------------------
std::string menu_label(const char* s) {
    std::string out;
    for (; *s; ++s) {
        if (*s == '&') { if (s[1] == '&') out += *++s; continue; }
        out += (char)tolower((unsigned char)*s);
    }
    return out;
}

int best_menu_match(const std::vector<std::string>& paths, const std::string& action) {
    const std::string want = menu_label(action.c_str());
    const std::string tail = "/" + want;
    int best = -1; size_t bestLen = 0;
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string& path = paths[i];
        if (path == want) return (int)i;
        if (path.size() > tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0
            && (best < 0 || path.size() < bestLen)) { best = (int)i; bestLen = path.size(); }
    }
    return best;
}

} // namespace pui
