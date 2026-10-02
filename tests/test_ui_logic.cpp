#include "test.h"
#include "core/ui_logic.h"
#include <cstring>

using namespace pui;

TEST("zoom_step: presets up and down, clamped at the ends") {
    CHECK_EQ(zoom_step(100, +1), 110);
    CHECK_EQ(zoom_step(100, -1), 90);
    CHECK_EQ(zoom_step(130, +1), 150); // off-preset values snap to the next preset
    CHECK_EQ(zoom_step(130, -1), 125);
    CHECK_EQ(zoom_step(300, +1), 300);
    CHECK_EQ(zoom_step(75, -1), 75);
    CHECK_EQ(zoom_step(60, -1), 60);   // already below the lowest preset: stays
    CHECK_EQ(zoom_step(100, 0), 100);
}

TEST("zoom_factor_for: automatic follows the system dpi") {
    CHECK_EQ(zoom_factor_for(0, 96), 1.0);
    CHECK_EQ(zoom_factor_for(0, 144), 1.5);
    CHECK_EQ(zoom_factor_for(0, 0), 1.0);
    CHECK_EQ(zoom_factor_for(125, 144), 1.25);
    CHECK_EQ(zoom_factor_for(1000, 96), 4.0); // clamped to kZoomMax
}

TEST("to_device / to_logical round-trip without overshooting the edge") {
    CHECK_EQ(to_device(100, 1.5), 150);
    CHECK_EQ(to_logical(150, 1.5), 100);
    CHECK_EQ(to_logical(149, 1.5), 99);
    CHECK_EQ(to_device(7, 1.25), 9);
    CHECK_EQ(to_logical(5, 1.0), 5);
}

TEST("split_actions: semicolons, whitespace, empties") {
    auto a = split_actions(" PVAR:SET:a:1 ; WINDOWSIZE:430:172:RIGHT:TOP;;  ");
    CHECK_EQ(a.size(), (size_t)2);
    CHECK_EQ(a[0], std::string("PVAR:SET:a:1"));
    CHECK_EQ(a[1], std::string("WINDOWSIZE:430:172:RIGHT:TOP"));
    CHECK(split_actions("").empty());
    auto one = split_actions("Next");
    CHECK(one.size() == 1 && one[0] == "Next");
}

TEST("parse_anchor: separators and case") {
    std::string h, v;
    parse_anchor("right top", h, v);
    CHECK_EQ(h, std::string("RIGHT")); CHECK_EQ(v, std::string("TOP"));
    parse_anchor("CENTER:BOTTOM", h, v);
    CHECK_EQ(h, std::string("CENTER")); CHECK_EQ(v, std::string("BOTTOM"));
    parse_anchor("", h, v);
    CHECK(h.empty() && v.empty());
}

TEST("name_hash128: deterministic, distinct names differ") {
    uint8_t a[16], b[16], c[16];
    name_hash128("Mini mode", a);
    name_hash128("Mini mode", b);
    name_hash128("Mini mode ", c);
    CHECK(memcmp(a, b, 16) == 0);
    CHECK(memcmp(a, c, 16) != 0);
}

TEST("mini_mode_plan: enter, leave, nothing to do") {
    MiniPlan p = mini_mode_plan(950, 750, 430, 172, 0, 0);
    CHECK(p.act && p.enter && p.w == 430 && p.h == 172);
    p = mini_mode_plan(430, 172, 430, 172, 950, 750);
    CHECK(p.act && !p.enter && p.w == 950 && p.h == 750);
    CHECK(!mini_mode_plan(430, 172, 430, 172, 0, 0).act);       // nothing saved to go back to
    CHECK(!mini_mode_plan(430, 172, 430, 172, 430, 172).act);   // saved size is the mini size
    CHECK(!mini_mode_plan(950, 750, 0, 0, 0, 0).act);           // skin declares no mini size
}

TEST("filter_matches: every word, case-insensitive") {
    CHECK(filter_matches("", "anything"));
    CHECK(filter_matches("pink", "Pink Floyd|The Wall"));
    CHECK(filter_matches("wall PINK", "Pink Floyd|The Wall"));
    CHECK(!filter_matches("pink zeppelin", "Pink Floyd|The Wall"));
    CHECK(filter_matches("  floyd  ", "Pink Floyd"));
}

TEST("album_sort_from / album_sort_name round-trip") {
    for (AlbumSort s : { AlbumSort::Artist, AlbumSort::Album, AlbumSort::Year, AlbumSort::Added })
        CHECK(album_sort_from(album_sort_name(s)) == s);
    CHECK(album_sort_from("") == AlbumSort::Artist);
    CHECK(album_sort_from("YEAR") == AlbumSort::Year);
}
