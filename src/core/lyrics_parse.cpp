#include "lyrics_parse.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pui {

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

bool parse_lrc_time(const std::string& tag, double& out) {
    int m = 0, sec = 0; size_t i = 0;
    if (tag.empty() || !isdigit((unsigned char)tag[0])) return false;
    while (i < tag.size() && isdigit((unsigned char)tag[i])) m = m * 10 + (tag[i++] - '0');
    if (i >= tag.size() || tag[i] != ':') return false;
    ++i;
    if (i >= tag.size() || !isdigit((unsigned char)tag[i])) return false;
    while (i < tag.size() && isdigit((unsigned char)tag[i])) sec = sec * 10 + (tag[i++] - '0');
    double frac = 0;
    if (i < tag.size() && (tag[i] == '.' || tag[i] == ':')) {
        ++i; double scale = 0.1;
        while (i < tag.size() && isdigit((unsigned char)tag[i])) { frac += (tag[i++] - '0') * scale; scale /= 10; }
    }
    if (i != tag.size()) return false;
    out = m * 60.0 + sec + frac;
    return true;
}

// LRC (or plain) text -> lines. Lines with no [mm:ss.xx] tag are kept only for plain lyrics.
bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    lines.clear(); synced = false;
    std::vector<std::pair<double, std::string>> timed, plain;
    double offset = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r')) line.pop_back();

        std::vector<double> ts; size_t p = 0; bool meta = false;
        while (p < line.size() && line[p] == '[') {
            size_t e = line.find(']', p);
            if (e == std::string::npos) break;
            std::string tag = line.substr(p + 1, e - p - 1);
            double t;
            if (parse_lrc_time(tag, t)) { ts.push_back(t); p = e + 1; }
            else {
                if (ts.empty()) {
                    meta = true;
                    if (lower(tag).compare(0, 7, "offset:") == 0) offset = atof(tag.c_str() + 7) / 1000.0;
                }
                break;
            }
        }
        if (meta && ts.empty()) continue;
        std::string body = line.substr(p);
        // strip inline word timestamps <mm:ss.xx>
        std::string clean;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '<') { size_t e = body.find('>', i); double t; if (e != std::string::npos && parse_lrc_time(body.substr(i + 1, e - i - 1), t)) { i = e; continue; } }
            clean += body[i];
        }
        body = trim(clean);
        if (!ts.empty()) for (double t : ts) timed.push_back({ t - offset, body });
        else plain.push_back({ -1, body });
    }
    if (!timed.empty()) {
        std::stable_sort(timed.begin(), timed.end(), [](auto& a, auto& b) { return a.first < b.first; });
        lines = timed; synced = true;
    } else {
        while (!plain.empty() && plain.back().second.empty()) plain.pop_back();
        while (!plain.empty() && plain.front().second.empty()) plain.erase(plain.begin());
        lines = plain;
    }
    return !lines.empty();
}


std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

static void utf8_append(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
}

// First non-null string value of `"key":` in the JSON text (also works on the search array).
bool json_string(const std::string& j, const char* key, std::string& out) {
    std::string k = std::string("\"") + key + "\"";
    size_t from = 0;
    for (;;) {
        size_t p = j.find(k, from);
        if (p == std::string::npos) return false;
        p += k.size(); from = p;
        while (p < j.size() && (j[p] == ' ' || j[p] == ':' || j[p] == '\n' || j[p] == '\t')) ++p;
        if (p >= j.size() || j[p] != '"') continue; // null
        ++p; out.clear();
        while (p < j.size() && j[p] != '"') {
            if (j[p] == '\\' && p + 1 < j.size()) {
                ++p;
                switch (j[p]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                case 'u': {
                    // \uXXXX, a surrogate pair as two of them. Malformed input (truncated, a lone
                    // or mismatched surrogate) becomes U+FFFD instead of reading past the body.
                    auto hex4 = [&](size_t at, unsigned& v) {
                        if (at + 4 > j.size()) return false;
                        v = 0;
                        for (size_t k = at; k < at + 4; ++k) {
                            char c = j[k]; v <<= 4;
                            if (c >= '0' && c <= '9') v |= c - '0';
                            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                            else return false;
                        }
                        return true;
                    };
                    unsigned cp = 0, lo = 0;
                    if (!hex4(p + 1, cp)) { out += "\xEF\xBF\xBD"; break; }
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        if (j.compare(p + 1, 2, "\\u") == 0 && hex4(p + 3, lo) && lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6;
                        } else cp = 0xFFFD;
                    } else if (cp >= 0xDC00 && cp < 0xE000) cp = 0xFFFD;
                    utf8_append(out, cp); break; }
                default: out += j[p];
                }
            } else out += j[p];
            ++p;
        }
        if (!trim(out).empty()) return true;
    }
}


// `"key": true` anywhere in the JSON text.
bool json_true(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    for (size_t p = j.find(k); p != std::string::npos; p = j.find(k, p + 1)) {
        size_t q = p + k.size();
        while (q < j.size() && (j[q] == ' ' || j[q] == ':' || j[q] == '\n' || j[q] == '\t')) ++q;
        if (j.compare(q, 4, "true") == 0) return true;
    }
    return false;
}

} // namespace pui
