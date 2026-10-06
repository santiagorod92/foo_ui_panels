#include "test.h"
#include "core/script_util.h"

using namespace pui;

static bool same(gfx::Color c, int r, int g, int b) { return c.r == r && c.g == g && c.b == b; }

TEST("eval_expr: precedence, grouping, signs") {
    CHECK_EQ(eval_expr("1+2*3"), 7L);
    CHECK_EQ(eval_expr("(1+2)*3"), 9L);
    CHECK_EQ(eval_expr("{10}-3"), 7L);
    CHECK_EQ(eval_expr(" 4 * ( 2 + 1 ) "), 12L);
    CHECK_EQ(eval_expr("-5+2"), -3L);
    CHECK_EQ(eval_expr("100/7"), 14L);
    CHECK_EQ(eval_expr("10 % 3"), 1L);
    CHECK_EQ(eval_expr("12abc"), 12L); // parsing stops at the first character it doesn't know
    CHECK_EQ(eval_expr(""), 0L);
}

TEST("eval_expr: division and modulo by zero give 0") {
    CHECK_EQ(eval_expr("7/0"), 0L);
    CHECK_EQ(eval_expr("7%0"), 0L);
    CHECK_EQ(eval_expr("3+7/(2-2)"), 3L);
}

TEST("parse_rgb: dashes, prefixes, alpha and glued tails") {
    CHECK(same(parse_rgb("10-20-30"), 10, 20, 30));
    CHECK(same(parse_rgb("glow-1-2-3-4"), 1, 2, 3));
    CHECK(same(parse_rgb("114-114-114glowexpand-0"), 114, 114, 114));
    CHECK(same(parse_rgb(""), 0, 0, 0));
}

TEST("parse_config_color: the ini formats") {
    gfx::Color c;
    CHECK(parse_config_color("#ff8000", c) && same(c, 255, 128, 0));
    CHECK(parse_config_color("  1 2 3", c) && same(c, 1, 2, 3));
    CHECK(parse_config_color("4-5-6", c) && same(c, 4, 5, 6));
    CHECK(parse_config_color("7,8,9", c) && same(c, 7, 8, 9));
    CHECK(!parse_config_color("#12", c));
    CHECK(!parse_config_color("#zzzzzz", c));
    CHECK(!parse_config_color("1 2", c));
    CHECK(!parse_config_color("", c));
    CHECK(!parse_config_color("   ", c));
}

TEST("find_color: present, null (transparent), absent") {
    gfx::Color c;
    CHECK(find_color("brushcolor-10-20-30 pencolor-null", "brushcolor", c) && same(c, 10, 20, 30));
    CHECK(!find_color("brushcolor-10-20-30 pencolor-null", "pencolor", c));
    CHECK(!find_color("brushcolor-10-20-30", "pencolor", c));
}

TEST("hex COLORREF round trip") {
    CHECK_EQ(parse_hex_colorref("0000ff"), 255UL);
    CHECK_EQ(hex_colorref(parse_hex_colorref("ff0000")), std::string("ff0000"));
    CHECK_EQ(hex_colorref(0x1ff00ffUL), std::string("ff00ff")); // masked to 24 bits
}

TEST("trailing_literal: label after balanced $func prefixes") {
    CHECK_EQ(trailing_literal("$font(a,b,c)Label"), std::string("Label"));
    CHECK_EQ(trailing_literal("$font(a,$rgb(1,2,3))$x()  Two words"), std::string("Two words"));
    CHECK_EQ(trailing_literal("$imageabs2(1,2)"), std::string());
    CHECK_EQ(trailing_literal("plain"), std::string("plain"));
    CHECK_EQ(trailing_literal("$font(unclosed"), std::string());
    CHECK_EQ(trailing_literal("$noparen"), std::string());
}

TEST("clean_action / unquote") {
    CHECK_EQ(clean_action("  'Next'  "), std::string("Next"));
    CHECK_EQ(clean_action("PVAR:SET:x:1"), std::string("PVAR:SET:x:1"));
    CHECK_EQ(clean_action("''"), std::string());
    CHECK_EQ(clean_action("   "), std::string());
    CHECK_EQ(unquote("'736'"), std::string("736"));
    CHECK_EQ(unquote("'"), std::string("'"));
    CHECK_EQ(unquote("bare"), std::string("bare"));
}

TEST("parse_img_opts / parse_glow_tail leave absent keys alone") {
    int alpha = 255, flip = 0;
    parse_img_opts("alpha-200nokeepaspectROTATEFLIP-6", alpha, flip);
    CHECK_EQ(alpha, 200); CHECK_EQ(flip, 6);
    alpha = 255; flip = 0;
    parse_img_opts("nokeepaspect", alpha, flip);
    CHECK_EQ(alpha, 255); CHECK_EQ(flip, 0);

    int expand = 1, galpha = 128;
    parse_glow_tail("glow-1-2-3glowexpand-4 glowalpha-90", expand, galpha);
    CHECK_EQ(expand, 4); CHECK_EQ(galpha, 90);
    expand = 1; galpha = 128;
    parse_glow_tail("glow-1-2-3", expand, galpha);
    CHECK_EQ(expand, 1); CHECK_EQ(galpha, 128);
}

TEST("text_opts") {
    CHECK_EQ(text_opts(""), (unsigned)gfx::kEndEllipsis);
    CHECK_EQ(text_opts("right"), (unsigned)(gfx::kEndEllipsis | gfx::kAlignRight));
    CHECK_EQ(text_opts("wrap"), (unsigned)(gfx::kEndEllipsis | gfx::kWordWrap));
    CHECK_EQ(text_opts("vcenter wrap") & gfx::kWordWrap, 0u); // one line wins
    // legacy: "vcenter" also matches "center"
    CHECK_EQ(text_opts("vcenter"),
             (unsigned)(gfx::kEndEllipsis | gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine));
}

TEST("map_type: legacy panel types") {
    CHECK_EQ(std::string(map_type("Channel spectrum panel")), std::string("Spectrum"));
    CHECK_EQ(std::string(map_type("Single Column Playlist")), std::string("Playlist View"));
    CHECK(map_type("Peakmeter") == nullptr); // native PeakMeter
    CHECK(map_type("Album Art") == nullptr); // native AlbumArt
    CHECK(map_type("Track Display") == nullptr);
    CHECK(map_type("Album list") == nullptr);
    CHECK(map_type("Chronflow") == nullptr);
    const std::string other = "Some Element";
    CHECK(map_type(other) == other.c_str());
}

TEST("menu_label: lower-case, accelerators dropped") {
    CHECK_EQ(menu_label("&Playback"), std::string("playback"));
    CHECK_EQ(menu_label("Rock && Roll"), std::string("rock & roll"));
    CHECK_EQ(menu_label(""), std::string());
}

TEST("best_menu_match: full path, shortest suffix, no partial words") {
    const std::vector<std::string> paths = {
        "playback/random", "playback/order/random", "library/album list", "file/restart", "view/album list",
    };
    CHECK_EQ(best_menu_match(paths, "Playback/Random"), 0);
    CHECK_EQ(best_menu_match(paths, "Random"), 0);              // shortest of the two "…/random"
    CHECK_EQ(best_menu_match(paths, "Order/Random"), 1);
    CHECK_EQ(best_menu_match(paths, "Library/Album List"), 2);  // exact beats shorter suffix
    CHECK_EQ(best_menu_match(paths, "Album List"), 4);
    CHECK_EQ(best_menu_match(paths, "&File/Restart"), 3);
    CHECK_EQ(best_menu_match(paths, "dom"), -1);                // "/dom" isn't a path segment
    CHECK_EQ(best_menu_match(paths, "Nope"), -1);
    CHECK_EQ(best_menu_match({}, "Random"), -1);
}

TEST("parse_argb: A-R-G-B, opaque R-G-B, malformed") {
    gfx::Color c; int a = -1;
    CHECK(parse_argb("120-190-220-255", c, a) && a == 120 && same(c, 190, 220, 255));
    CHECK(parse_argb("255-0-0-0", c, a) && a == 255 && same(c, 0, 0, 0));
    CHECK(parse_argb("10-20-30", c, a) && a == 255 && same(c, 10, 20, 30));
    a = 7;
    CHECK(!parse_argb("10-20", c, a) && a == 7); // untouched on failure
    CHECK(!parse_argb("", c, a));
}
