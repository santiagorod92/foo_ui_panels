// Lyrics text handling for the Lyric Show panel: LRC parsing and the bits of lrclib.net's JSON it
// reads. No foobar2000 SDK — compiled straight into the host-side unit tests (`make test`).
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pui {

// One timed segment of a line (a word or syllable): starts at byte `pos` of the line's text.
struct LyricWord { size_t pos = 0; double t = 0; };
struct LyricLine {
    double t = -1;                // < 0: no timestamp (plain lyrics)
    std::string text;
    std::vector<LyricWord> words; // enhanced LRC <mm:ss.xx> stamps, by position; may be empty
    double end = -1;              // a trailing stamp with no text after it: when the line ends
};

// "mm:ss", "mm:ss.xx" or "mm:ss:xx" -> seconds. False for anything else (e.g. "ar:Artist").
bool parse_lrc_time(const std::string& tag, double& out);
// Seconds -> "mm:ss.xx" (LRC tag body; negative clamps to 0).
std::string format_lrc_time(double t);
// LRC (or plain) text -> lines. Timestamped lines win: sorted by time, one entry per [tag] on a
// line, [offset:ms] applied, metadata tags ([ar:], [ti:] …) dropped, inline <mm:ss.xx> word
// stamps (enhanced LRC) taken out of the text into `words`. Without any timestamp: the plain
// lines (time -1), outer blank lines trimmed. False when nothing is left.
bool parse_lyrics(std::string text, std::vector<LyricLine>& lines, bool& synced);
// Same, without the word stamps.
bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced);

// Karaoke timing of a timestamped line: its segments' start times, ending at `end` (the line's
// trailing stamp, else the next line's time). With word stamps, those; without them and
// `estimate`, the line's words spread evenly by length over a plausible singing time (capped by
// the next line); else empty. `next_t` < 0 = no next line.
struct KaraokeTiming { std::vector<LyricWord> words; double end = 0; };
KaraokeTiming karaoke_timing(const LyricLine& line, double next_t, bool estimate);
// How much of segment `i` is sung at playback time `pos`: 0..1.
double karaoke_progress(const KaraokeTiming& k, size_t i, double pos);

// A piece of a karaoke line laid out for drawing: text[start, start+len) at `x` on `row`,
// belonging to segment `seg` (-1: before the first stamp).
struct KaraokePiece { size_t start = 0, len = 0; int x = 0, row = 0; int seg = -1; };
// Word-wraps `text` into rows no wider than `max_w` (a word longer than that gets a row of its
// own), each row centred in `max_w`; pieces split at spaces and at segment starts. `measure`
// returns the width of a string in the font it will be drawn with.
std::vector<KaraokePiece> layout_karaoke(const std::string& text, const std::vector<LyricWord>& words,
                                         int max_w, const std::function<int(std::string_view)>& measure,
                                         int* rows = nullptr);

// Tap-to-sync: stamps lyric lines one by one with the playback time. Blank lines are skipped
// (and not written out). Stamps never go backwards.
class LrcSync {
public:
    void begin(const std::vector<std::string>& lines);
    bool active() const { return m_active; }
    void cancel() { m_active = false; }
    const std::vector<std::string>& lines() const { return m_lines; }
    double time(size_t i) const { return m_times[i]; } // < 0: not stamped yet
    // The line the next stamp goes to (lines().size() when all are stamped).
    size_t next() const { return m_next; }
    bool done() const { return m_next >= m_lines.size(); }
    size_t stamped() const;
    bool stamp(double t);
    // Clears the last stamp; returns the line it belonged to (or -1 if nothing was stamped).
    int undo();
    // The stamped lines as LRC, with [ar:]/[ti:]/[al:] headers when given.
    std::string to_lrc(const std::string& artist, const std::string& title, const std::string& album) const;

private:
    void skip_blank();
    std::vector<std::string> m_lines;
    std::vector<double> m_times;
    size_t m_next = 0;
    bool m_active = false;
};

// First non-null, non-blank string value of `"key":` in the JSON text (works on lrclib's search
// array too); \n \t \" \\ \uXXXX (incl. surrogate pairs; malformed -> U+FFFD) decoded.
bool json_string(const std::string& j, const char* key, std::string& out);
// `"key": true` anywhere in the JSON text.
bool json_true(const std::string& j, const char* key);
// RFC 3986 percent-encoding (unreserved characters kept as is).
std::string url_encode(const std::string& s);

} // namespace pui
