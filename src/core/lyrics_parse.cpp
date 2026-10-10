#include "lyrics_parse.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
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

std::string format_lrc_time(double t) {
    long cs = t > 0 ? (long)(t * 100 + 0.5) : 0;
    char buf[32];
    snprintf(buf, sizeof buf, "%02ld:%02ld.%02ld", cs / 6000, cs / 100 % 60, cs % 100);
    return buf;
}

bool parse_lyrics(std::string text, std::vector<LyricLine>& lines, bool& synced) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    lines.clear(); synced = false;
    std::vector<LyricLine> timed, plain;
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
        std::string clean;
        std::vector<LyricWord> stamps;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '<') {
                size_t e = body.find('>', i); double t;
                if (e != std::string::npos && parse_lrc_time(body.substr(i + 1, e - i - 1), t)) {
                    stamps.push_back({ clean.size(), t }); i = e; continue;
                }
            }
            clean += body[i];
        }
        LyricLine out;
        out.text = trim(clean);
        size_t lead = 0;
        while (lead < clean.size() && (unsigned char)clean[lead] <= ' ') ++lead;
        for (auto w : stamps) {
            size_t pos = w.pos > lead ? w.pos - lead : 0;
            while (pos < out.text.size() && out.text[pos] == ' ') ++pos;
            if (pos >= out.text.size()) { out.end = w.t; continue; }
            if (!out.words.empty() && out.words.back().pos == pos) out.words.back().t = w.t;
            else out.words.push_back({ pos, w.t });
        }
        if (!ts.empty()) {
            for (double t : ts) {
                LyricLine c = out;
                const double shift = t - ts[0] - offset;
                c.t = t - offset;
                for (auto& w : c.words) w.t += shift;
                if (c.end >= 0) c.end += shift;
                timed.push_back(std::move(c));
            }
        } else {
            out.words.clear(); out.end = -1;
            plain.push_back(std::move(out));
        }
    }
    if (!timed.empty()) {
        std::stable_sort(timed.begin(), timed.end(), [](auto& a, auto& b) { return a.t < b.t; });
        lines = std::move(timed); synced = true;
    } else {
        while (!plain.empty() && plain.back().text.empty()) plain.pop_back();
        while (!plain.empty() && plain.front().text.empty()) plain.erase(plain.begin());
        lines = std::move(plain);
    }
    return !lines.empty();
}

bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced) {
    std::vector<LyricLine> l;
    lines.clear();
    if (!parse_lyrics(std::move(text), l, synced)) return false;
    for (auto& x : l) lines.push_back({ x.t, std::move(x.text) });
    return true;
}

KaraokeTiming karaoke_timing(const LyricLine& line, double next_t, bool estimate) {
    KaraokeTiming k;
    if (line.t < 0 || line.text.empty()) return k;
    if (!line.words.empty()) {
        k.words = line.words;
        if (line.end > k.words.back().t) k.end = line.end;
        else if (next_t > k.words.back().t) k.end = next_t;
        else k.end = k.words.back().t + 1.0;
        return k;
    }
    if (!estimate) return k;
    std::vector<size_t> starts;
    for (size_t i = 0; i < line.text.size(); ++i)
        if (line.text[i] != ' ' && (i == 0 || line.text[i - 1] == ' ')) starts.push_back(i);
    if (starts.empty()) return k;
    double dur = std::max(1.0, line.text.size() * 0.07);
    if (next_t > line.t) dur = std::min(dur, next_t - line.t);
    k.end = line.t + dur;
    for (size_t s : starts) k.words.push_back({ s, line.t + dur * s / line.text.size() });
    return k;
}

double karaoke_progress(const KaraokeTiming& k, size_t i, double pos) {
    if (i >= k.words.size()) return 0;
    const double a = k.words[i].t, b = i + 1 < k.words.size() ? k.words[i + 1].t : k.end;
    if (pos >= b) return 1;
    if (pos <= a || b <= a) return pos >= a ? 1 : 0;
    return (pos - a) / (b - a);
}

std::vector<KaraokePiece> layout_karaoke(const std::string& text, const std::vector<LyricWord>& words,
                                         int max_w, const std::function<int(std::string_view)>& measure,
                                         int* rows_out) {
    struct Raw { size_t start, len; int seg, w; bool space_before; };
    std::vector<Raw> raw;
    auto seg_at = [&](size_t pos) {
        int s = -1;
        for (size_t i = 0; i < words.size() && words[i].pos <= pos; ++i) s = (int)i;
        return s;
    };
    bool space = false;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == ' ') { space = true; ++i; continue; }
        size_t j = i + 1;
        while (j < text.size() && text[j] != ' ') {
            bool segStart = false;
            for (auto& w : words) if (w.pos == j) segStart = true;
            if (segStart) break;
            ++j;
        }
        raw.push_back({ i, j - i, seg_at(i), measure(std::string_view(text).substr(i, j - i)), space && !raw.empty() });
        space = false;
        i = j;
    }
    const int spaceW = measure(" ");
    std::vector<KaraokePiece> out;
    std::vector<std::pair<size_t, size_t>> rowRanges;
    std::vector<int> rowW;
    int row = 0, x = 0;
    size_t rowFirst = 0;
    for (size_t i = 0; i < raw.size();) {
        size_t j = i + 1;
        int ww = raw[i].w;
        while (j < raw.size() && !raw[j].space_before) ww += raw[j++].w;
        const int lead = (x > 0 && raw[i].space_before) ? spaceW : 0;
        if (x > 0 && x + lead + ww > max_w) {
            rowRanges.push_back({ rowFirst, out.size() }); rowW.push_back(x);
            ++row; x = 0; rowFirst = out.size();
        } else x += lead;
        for (size_t k = i; k < j; ++k) {
            out.push_back({ raw[k].start, raw[k].len, x, row, raw[k].seg });
            x += raw[k].w;
        }
        i = j;
    }
    if (!out.empty()) { rowRanges.push_back({ rowFirst, out.size() }); rowW.push_back(x); }
    for (size_t r = 0; r < rowRanges.size(); ++r) {
        const int dx = std::max(0, (max_w - rowW[r]) / 2);
        for (size_t k = rowRanges[r].first; k < rowRanges[r].second; ++k) out[k].x += dx;
    }
    if (rows_out) *rows_out = (int)rowRanges.size();
    return out;
}

void LrcSync::begin(const std::vector<std::string>& lines) {
    m_lines.clear();
    for (auto& l : lines) m_lines.push_back(trim(l));
    while (!m_lines.empty() && m_lines.back().empty()) m_lines.pop_back();
    m_times.assign(m_lines.size(), -1);
    m_next = 0;
    skip_blank();
    m_active = !m_lines.empty();
}

void LrcSync::skip_blank() {
    while (m_next < m_lines.size() && m_lines[m_next].empty()) ++m_next;
}

size_t LrcSync::stamped() const {
    size_t n = 0;
    for (double t : m_times) if (t >= 0) ++n;
    return n;
}

bool LrcSync::stamp(double t) {
    if (!m_active || done()) return false;
    for (size_t i = m_next; i-- > 0;)
        if (m_times[i] >= 0) { t = std::max(t, m_times[i]); break; }
    m_times[m_next++] = std::max(0.0, t);
    skip_blank();
    return true;
}

int LrcSync::undo() {
    for (size_t i = m_next; i-- > 0;) {
        if (m_times[i] >= 0) { m_times[i] = -1; m_next = i; return (int)i; }
    }
    return -1;
}

std::string LrcSync::to_lrc(const std::string& artist, const std::string& title, const std::string& album) const {
    std::string o;
    if (!artist.empty()) o += "[ar:" + artist + "]\n";
    if (!title.empty()) o += "[ti:" + title + "]\n";
    if (!album.empty()) o += "[al:" + album + "]\n";
    for (size_t i = 0; i < m_lines.size(); ++i)
        if (m_times[i] >= 0) o += "[" + format_lrc_time(m_times[i]) + "]" + m_lines[i] + "\n";
    return o;
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

bool json_string(const std::string& j, const char* key, std::string& out) {
    std::string k = std::string("\"") + key + "\"";
    size_t from = 0;
    for (;;) {
        size_t p = j.find(k, from);
        if (p == std::string::npos) return false;
        p += k.size(); from = p;
        while (p < j.size() && (j[p] == ' ' || j[p] == ':' || j[p] == '\n' || j[p] == '\t')) ++p;
        if (p >= j.size() || j[p] != '"') continue;
        ++p; out.clear();
        while (p < j.size() && j[p] != '"') {
            if (j[p] == '\\' && p + 1 < j.size()) {
                ++p;
                switch (j[p]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': break;
                case 'u': {
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

bool json_true(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    for (size_t p = j.find(k); p != std::string::npos; p = j.find(k, p + 1)) {
        size_t q = p + k.size();
        while (q < j.size() && (j[q] == ' ' || j[q] == ':' || j[q] == '\n' || j[q] == '\t')) ++q;
        if (j.compare(q, 4, "true") == 0) return true;
    }
    return false;
}

}
