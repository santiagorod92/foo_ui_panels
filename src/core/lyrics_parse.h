#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pui {

struct LyricWord { size_t pos = 0; double t = 0; };
struct LyricLine {
    double t = -1;
    std::string text;
    std::vector<LyricWord> words;
    double end = -1;
};

bool parse_lrc_time(const std::string& tag, double& out);
std::string format_lrc_time(double t);
bool parse_lyrics(std::string text, std::vector<LyricLine>& lines, bool& synced);
bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced);

struct KaraokeTiming { std::vector<LyricWord> words; double end = 0; };
KaraokeTiming karaoke_timing(const LyricLine& line, double next_t, bool estimate);
double karaoke_progress(const KaraokeTiming& k, size_t i, double pos);

struct KaraokePiece { size_t start = 0, len = 0; int x = 0, row = 0; int seg = -1; };
std::vector<KaraokePiece> layout_karaoke(const std::string& text, const std::vector<LyricWord>& words,
                                         int max_w, const std::function<int(std::string_view)>& measure,
                                         int* rows = nullptr);

class LrcSync {
public:
    void begin(const std::vector<std::string>& lines);
    bool active() const { return m_active; }
    void cancel() { m_active = false; }
    const std::vector<std::string>& lines() const { return m_lines; }
    double time(size_t i) const { return m_times[i]; }
    size_t next() const { return m_next; }
    bool done() const { return m_next >= m_lines.size(); }
    size_t stamped() const;
    bool stamp(double t);
    int undo();
    std::string to_lrc(const std::string& artist, const std::string& title, const std::string& album) const;

private:
    void skip_blank();
    std::vector<std::string> m_lines;
    std::vector<double> m_times;
    size_t m_next = 0;
    bool m_active = false;
};

bool json_string(const std::string& j, const char* key, std::string& out);
bool json_true(const std::string& j, const char* key);
std::string url_encode(const std::string& s);

}
