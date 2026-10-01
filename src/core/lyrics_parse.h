// Lyrics text handling for the Lyric Show panel: LRC parsing and the bits of lrclib.net's JSON it
// reads. No foobar2000 SDK — compiled straight into the host-side unit tests (`make test`).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace pui {

// "mm:ss", "mm:ss.xx" or "mm:ss:xx" -> seconds. False for anything else (e.g. "ar:Artist").
bool parse_lrc_time(const std::string& tag, double& out);
// LRC (or plain) text -> (seconds, line) pairs. Timestamped lines win: sorted by time, one entry
// per [tag] on a line, [offset:ms] applied, inline <mm:ss.xx> word stamps removed, metadata tags
// ([ar:], [ti:] …) dropped. Without any timestamp: the plain lines (time -1), outer blank lines
// trimmed. False when nothing is left.
bool parse_lyrics(std::string text, std::vector<std::pair<double, std::string>>& lines, bool& synced);

// First non-null, non-blank string value of `"key":` in the JSON text (works on lrclib's search
// array too); \n \t \" \\ \uXXXX (incl. surrogate pairs; malformed -> U+FFFD) decoded.
bool json_string(const std::string& j, const char* key, std::string& out);
// `"key": true` anywhere in the JSON text.
bool json_true(const std::string& j, const char* key);
// RFC 3986 percent-encoding (unreserved characters kept as is).
std::string url_encode(const std::string& s);

} // namespace pui
