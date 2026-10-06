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

TEST("format_lrc_time") {
    CHECK_EQ(format_lrc_time(62.5), std::string("01:02.50"));
    CHECK_EQ(format_lrc_time(0.004), std::string("00:00.00"));
    CHECK_EQ(format_lrc_time(-3), std::string("00:00.00"));
    CHECK_EQ(format_lrc_time(59.999), std::string("01:00.00"));
}

TEST("parse_lyrics: enhanced LRC word stamps by position, trailing end stamp") {
    std::vector<LyricLine> l; bool synced = false;
    CHECK(parse_lyrics("[00:01.00] <00:01.00>Hel<00:01.50>lo <00:02.00>world<00:03.00>", l, synced));
    CHECK(synced && l.size() == 1);
    if (l.size() == 1) {
        CHECK_EQ(l[0].text, std::string("Hello world"));
        CHECK_EQ(l[0].words.size(), (size_t)3);
        if (l[0].words.size() == 3) {
            CHECK_EQ(l[0].words[0].pos, (size_t)0);
            CHECK_EQ(l[0].words[1].pos, (size_t)3);
            CHECK_EQ(l[0].words[2].pos, (size_t)6);
            CHECK(near(l[0].words[2].t, 2));
        }
        CHECK(near(l[0].end, 3));
    }
}

TEST("parse_lyrics: word stamps follow repeated tags and [offset:]") {
    std::vector<LyricLine> l; bool synced = false;
    CHECK(parse_lyrics("[offset:1000]\n[00:10.00][00:20.00]<00:10.50>La <00:11.00>la", l, synced));
    CHECK(l.size() == 2);
    if (l.size() == 2) {
        CHECK(near(l[0].t, 9) && near(l[0].words[0].t, 9.5) && near(l[0].words[1].t, 10));
        CHECK(near(l[1].t, 19) && near(l[1].words[0].t, 19.5) && near(l[1].words[1].t, 20));
    }
}

TEST("karaoke_timing: word stamps, estimate, off") {
    LyricLine line; line.t = 10; line.text = "ab cd";
    line.words = { { 0, 10 }, { 3, 11 } };
    auto k = karaoke_timing(line, 14, false);
    CHECK(k.words.size() == 2 && near(k.end, 14));
    CHECK(near(karaoke_progress(k, 0, 10.5), 0.5));
    CHECK(near(karaoke_progress(k, 1, 12.5), 0.5));
    CHECK(near(karaoke_progress(k, 1, 20), 1) && near(karaoke_progress(k, 1, 5), 0));
    line.end = 12; // the trailing stamp beats the next line
    CHECK(near(karaoke_timing(line, 14, false).end, 12));

    LyricLine plain; plain.t = 0; plain.text = "aaaa bbbb cccc dddd eeee ffff"; // 29 chars -> 2.03 s
    CHECK(karaoke_timing(plain, 30, false).words.empty());
    auto e = karaoke_timing(plain, 30, true);
    CHECK(e.words.size() == 6 && near(e.end, 29 * 0.07) && near(e.words[0].t, 0));
    CHECK(near(karaoke_timing(plain, 1.5, true).end, 1.5)); // capped by the next line
    LyricLine untimed; untimed.text = "x";
    CHECK(karaoke_timing(untimed, 5, true).words.empty());
}

TEST("layout_karaoke: wraps at spaces, splits at segments, centres rows") {
    auto measure = [](std::string_view s) { return (int)s.size() * 10; }; // 10 px a byte
    int rows = 0;
    // "Hello" stamped as Hel|lo, then "big world": 3 + 2 pieces on row 0 fit 100 px? "Hello big" = 90.
    auto p = layout_karaoke("Hello big world", { { 0, 0 }, { 3, 1 }, { 6, 2 }, { 10, 3 } }, 100, measure, &rows);
    CHECK_EQ(rows, 2);
    CHECK_EQ(p.size(), (size_t)4);
    if (p.size() == 4) {
        CHECK(p[0].start == 0 && p[0].len == 3 && p[0].seg == 0 && p[0].row == 0 && p[0].x == 5);
        CHECK(p[1].start == 3 && p[1].len == 2 && p[1].seg == 1 && p[1].x == 35); // no gap inside a word
        CHECK(p[2].start == 6 && p[2].seg == 2 && p[2].x == 65);                  // after a space
        CHECK(p[3].start == 10 && p[3].row == 1 && p[3].x == 25 && p[3].seg == 3);
    }
    // Text before the first stamp is segment -1; an over-long word gets its own row.
    p = layout_karaoke("ab cdefghijklmn", { { 3, 0 } }, 50, measure, &rows);
    CHECK_EQ(rows, 2);
    if (p.size() == 2) CHECK(p[0].seg == -1 && p[1].row == 1 && p[1].x == 0);
}

TEST("LrcSync: stamps skip blanks, never go back, undo, to_lrc") {
    LrcSync s;
    s.begin({ "", "One", "", "Two", "Three", "" });
    CHECK(s.active() && s.next() == 1);
    CHECK(s.stamp(5));
    CHECK_EQ(s.next(), (size_t)3);
    CHECK(s.stamp(4)); // earlier than the previous stamp: clamped
    CHECK(near(s.time(3), 5));
    CHECK_EQ(s.undo(), 3);
    CHECK(s.next() == 3 && s.time(3) < 0);
    CHECK(s.stamp(7.25) && s.stamp(9));
    CHECK(s.done() && !s.stamp(10));
    CHECK_EQ(s.stamped(), (size_t)3);
    CHECK_EQ(s.to_lrc("A", "T", ""), std::string("[ar:A]\n[ti:T]\n[00:05.00]One\n[00:07.25]Two\n[00:09.00]Three\n"));
    LrcSync empty; empty.begin({ "", " " });
    CHECK(!empty.active());
    CHECK_EQ(empty.undo(), -1);
}
