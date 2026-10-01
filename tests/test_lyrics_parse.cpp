#include "test.h"
#include "core/lyrics_parse.h"
#include <cmath>

using namespace pui;
using Lines = std::vector<std::pair<double, std::string>>;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

TEST("parse_lrc_time") {
    double t = 0;
    CHECK(parse_lrc_time("01:02.50", t) && near(t, 62.5));
    CHECK(parse_lrc_time("1:02", t) && near(t, 62));
    CHECK(parse_lrc_time("00:05:20", t) && near(t, 5.2));
    CHECK(!parse_lrc_time("ar:Artist", t));
    CHECK(!parse_lrc_time("1:x", t));
    CHECK(!parse_lrc_time("01:02.5x", t));
    CHECK(!parse_lrc_time("", t));
}

TEST("parse_lyrics: synced, sorted, repeated tags, metadata dropped") {
    Lines l; bool synced = false;
    CHECK(parse_lyrics("[ar:Someone]\n[00:10.00]B\n[00:05.00]A\n[00:20.00][00:30.00]Chorus\n", l, synced));
    CHECK(synced);
    CHECK_EQ(l.size(), (size_t)4);
    if (l.size() == 4) {
        CHECK(near(l[0].first, 5) && l[0].second == "A");
        CHECK(near(l[1].first, 10) && l[1].second == "B");
        CHECK(near(l[2].first, 20) && l[2].second == "Chorus");
        CHECK(near(l[3].first, 30) && l[3].second == "Chorus");
    }
}

TEST("parse_lyrics: [offset:] shifts timestamps earlier") {
    Lines l; bool synced = false;
    CHECK(parse_lyrics("[offset:500]\n[00:10.00]X", l, synced));
    CHECK(synced && l.size() == 1 && near(l[0].first, 9.5));
}

TEST("parse_lyrics: inline word stamps, BOM, CRLF") {
    Lines l; bool synced = false;
    CHECK(parse_lyrics("\xEF\xBB\xBF[00:01.00]<00:01.00>Hel<00:01.50>lo  \r\n", l, synced));
    CHECK(synced && l.size() == 1);
    if (!l.empty()) CHECK_EQ(l[0].second, std::string("Hello"));
}

TEST("parse_lyrics: plain text keeps inner blank lines, trims outer ones") {
    Lines l; bool synced = true;
    CHECK(parse_lyrics("\n\nLine1\n\nLine2\n\n", l, synced));
    CHECK(!synced);
    CHECK_EQ(l.size(), (size_t)3);
    if (l.size() == 3) {
        CHECK_EQ(l[0].second, std::string("Line1"));
        CHECK_EQ(l[1].second, std::string());
        CHECK_EQ(l[2].second, std::string("Line2"));
        CHECK(near(l[0].first, -1));
    }
}

TEST("parse_lyrics: timed lines win over untimed ones; empty input fails") {
    Lines l; bool synced = false;
    CHECK(parse_lyrics("Intro text\n[00:02.00]Timed", l, synced));
    CHECK(synced && l.size() == 1);
    CHECK(!parse_lyrics("", l, synced));
    CHECK(!parse_lyrics("[ti:Only metadata]\n", l, synced));
}

TEST("json_string: null skipped, escapes decoded") {
    std::string out;
    const std::string j = R"({"syncedLyrics":null,"plainLyrics":"a\nb \"q\" \\ é"})";
    CHECK(!json_string(j, "syncedLyrics", out));
    CHECK(json_string(j, "plainLyrics", out));
    CHECK_EQ(out, std::string("a\nb \"q\" \\ \xC3\xA9"));
    CHECK(!json_string(j, "missing", out));
}

TEST("json_string: surrogate pairs and malformed \\u") {
    std::string out;
    CHECK(json_string(R"({"k":"🎵"})", "k", out));
    CHECK_EQ(out, std::string("\xF0\x9F\x8E\xB5"));
    CHECK(json_string(R"({"k":"\ud83c x"})", "k", out));
    CHECK_EQ(out, std::string("\xEF\xBF\xBD x"));
    CHECK(json_string(R"({"k":"x\u12")", "k", out)); // truncated escape at the end of the body
    CHECK_EQ(out.substr(0, 4), std::string("x\xEF\xBF\xBD"));
}

TEST("json_string: blank values skipped (search results array)") {
    std::string out;
    CHECK(json_string(R"([{"plainLyrics":"  "},{"plainLyrics":"real"}])", "plainLyrics", out));
    CHECK_EQ(out, std::string("real"));
}

TEST("json_true") {
    CHECK(json_true(R"({"instrumental": true})", "instrumental"));
    CHECK(!json_true(R"({"instrumental":false})", "instrumental"));
    CHECK(!json_true(R"({"other":true})", "instrumental"));
}

TEST("url_encode") {
    CHECK_EQ(url_encode("AC/DC & Me \xC3\xB1"), std::string("AC%2FDC%20%26%20Me%20%C3%B1"));
    CHECK_EQ(url_encode("a-b_c.d~"), std::string("a-b_c.d~"));
}
