#include "test.h"
#include "core/ui_logic.h"
#include <cstring>

using namespace pui;

TEST("zoom_step: presets up and down, clamped at the ends") {
    CHECK_EQ(zoom_step(100, +1), 110);
    CHECK_EQ(zoom_step(100, -1), 90);
    CHECK_EQ(zoom_step(130, +1), 150);
    CHECK_EQ(zoom_step(130, -1), 125);
    CHECK_EQ(zoom_step(300, +1), 300);
    CHECK_EQ(zoom_step(75, -1), 75);
    CHECK_EQ(zoom_step(60, -1), 60);
    CHECK_EQ(zoom_step(100, 0), 100);
}

TEST("zoom_factor_for: automatic follows the system dpi") {
    CHECK_EQ(zoom_factor_for(0, 96), 1.0);
    CHECK_EQ(zoom_factor_for(0, 144), 1.5);
    CHECK_EQ(zoom_factor_for(0, 0), 1.0);
    CHECK_EQ(zoom_factor_for(125, 144), 1.25);
    CHECK_EQ(zoom_factor_for(1000, 96), 4.0);
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
    CHECK(!mini_mode_plan(430, 172, 430, 172, 0, 0).act);
    CHECK(!mini_mode_plan(430, 172, 430, 172, 430, 172).act);
    CHECK(!mini_mode_plan(950, 750, 0, 0, 0, 0).act);
}

TEST("filter_matches: every word, case-insensitive") {
    CHECK(filter_matches("", "anything"));
    CHECK(filter_matches("pink", "Pink Floyd|The Wall"));
    CHECK(filter_matches("wall PINK", "Pink Floyd|The Wall"));
    CHECK(!filter_matches("pink zeppelin", "Pink Floyd|The Wall"));
    CHECK(filter_matches("  floyd  ", "Pink Floyd"));
}

TEST("album_sort_from / album_sort_name round-trip") {
    for (AlbumSort s : { AlbumSort::Artist, AlbumSort::Album, AlbumSort::Year, AlbumSort::Decade, AlbumSort::Added })
        CHECK(album_sort_from(album_sort_name(s)) == s);
    CHECK(album_sort_from("") == AlbumSort::Artist);
    CHECK(album_sort_from("YEAR") == AlbumSort::Year);
}

TEST("format_time / seek helpers") {
    CHECK_EQ(format_time(0), std::string("0:00"));
    CHECK_EQ(format_time(5.9), std::string("0:05"));
    CHECK_EQ(format_time(83), std::string("1:23"));
    CHECK_EQ(format_time(3723), std::string("1:02:03"));
    CHECK_EQ(format_time(-3), std::string("0:00"));
    CHECK_EQ(seek_position_at(50, 100, 200.0), 100.0);
    CHECK_EQ(seek_position_at(-10, 100, 200.0), 0.0);
    CHECK_EQ(seek_position_at(150, 100, 200.0), 200.0);
    CHECK_EQ(seek_position_at(50, 0, 200.0), 0.0);
    CHECK_EQ(seek_by(10, 200, -5), 5.0);
    CHECK_EQ(seek_by(3, 200, -5), 0.0);
    CHECK_EQ(seek_by(198, 200, 5), 200.0);
    CHECK_EQ(seek_by(10, 0, 5), 0.0);
    CHECK_EQ(seek_tooltip(83, 296), std::string("1:23 / 4:56"));
    CHECK_EQ(seek_tooltip(10, 0), std::string());
}

TEST("volume_label") {
    CHECK_EQ(volume_label(-100.0f), std::string("Muted"));
    CHECK_EQ(volume_label(0.0f), std::string("0 dB"));
    CHECK_EQ(volume_label(-6.0f), std::string("-6.0 dB"));
    CHECK_EQ(volume_label(-12.34f), std::string("-12.3 dB"));
}

TEST("volume_step: 2.5 dB per step, clamped") {
    CHECK_EQ(volume_step(-10.0f, 1), -7.5f);
    CHECK_EQ(volume_step(-10.0f, -2), -15.0f);
    CHECK_EQ(volume_step(-1.0f, 1), 0.0f);
    CHECK_EQ(volume_step(-99.0f, -1), -100.0f);
}

TEST("wheel_steps: whole notches pass through, fractions accumulate, reversal resets") {
    float acc = 0;
    CHECK_EQ(wheel_steps(acc, 2.0f), 2);
    CHECK_EQ(wheel_steps(acc, 0.4f), 0);
    CHECK_EQ(wheel_steps(acc, 0.4f), 0);
    CHECK_EQ(wheel_steps(acc, 0.4f), 1);
    CHECK_EQ(wheel_steps(acc, -0.5f), 0);
    CHECK_EQ(wheel_steps(acc, -0.6f), -1);
    CHECK_EQ(wheel_steps(acc, -3.0f), -3);
}

TEST("folder_picker_start: skip the wizard's own folder") {
    CHECK_EQ(folder_picker_start("", "/p/skins"), std::string());
    CHECK_EQ(folder_picker_start("/p/skins", "/p/skins"), std::string());
    CHECK_EQ(folder_picker_start("/p/skins/Playlist", "/p/skins"), std::string());
    CHECK_EQ(folder_picker_start("C:\\p\\skins\\x", "C:\\p\\skins"), std::string());
    CHECK_EQ(folder_picker_start("/p/skins2", "/p/skins"), std::string("/p/skins2"));
    CHECK_EQ(folder_picker_start("/home/me/Documents", "/p/skins"), std::string("/home/me/Documents"));
    CHECK_EQ(folder_picker_start("/home/me/Documents", ""), std::string("/home/me/Documents"));
}

TEST("resize_rect: dragged edges move, the others stay, minimum size kept") {
    const EdgeRect s{ 100, 100, 500, 400 };
    EdgeRect r = resize_rect(s, kEdgeRight | kEdgeBottom, 50, -20, 200, 100);
    CHECK_EQ(r.l, 100); CHECK_EQ(r.t, 100); CHECK_EQ(r.r, 550); CHECK_EQ(r.b, 380);
    r = resize_rect(s, kEdgeLeft | kEdgeTop, -30, 40, 200, 100);
    CHECK_EQ(r.l, 70); CHECK_EQ(r.t, 140); CHECK_EQ(r.r, 500); CHECK_EQ(r.b, 400);
    r = resize_rect(s, kEdgeLeft, 390, 0, 200, 100);
    CHECK_EQ(r.l, 300); CHECK_EQ(r.r, 500);
    r = resize_rect(s, kEdgeBottom, 0, -500, 200, 100);
    CHECK_EQ(r.b, 200);
    r = resize_rect(s, 0, 50, 50, 200, 100);
    CHECK_EQ(r.r, 500); CHECK_EQ(r.b, 400);
}

TEST("cache_victims: under budget nothing goes") {
    std::vector<CacheItem> items{ { 1, 40, false }, { 2, 40, false } };
    CHECK(cache_victims(items, 80, 100).empty());
}

TEST("cache_victims: least recently used first, down to three quarters of the budget") {
    std::vector<CacheItem> items{ { 3, 30, false }, { 1, 30, false }, { 2, 30, false }, { 4, 30, false } };
    std::vector<size_t> v = cache_victims(items, 120, 100);
    CHECK_EQ(v.size(), (size_t)2);
    CHECK_EQ(v[0], (size_t)1);
    CHECK_EQ(v[1], (size_t)2);
}

TEST("cache_victims: images still on screen are never evicted, even over budget") {
    std::vector<CacheItem> items{ { 1, 36, true }, { 2, 36, true }, { 3, 36, false }, { 4, 36, true } };
    std::vector<size_t> v = cache_victims(items, 144, 100);
    CHECK_EQ(v.size(), (size_t)1);
    CHECK_EQ(v[0], (size_t)2);
    for (auto& i : items) i.in_use = true;
    CHECK(cache_victims(items, 144, 100).empty());
}

TEST("thumb_size: fits the longer side, keeps the aspect, never upscales") {
    Size2 t = thumb_size(3000, 3000, 600);
    CHECK_EQ(t.w, 600); CHECK_EQ(t.h, 600);
    t = thumb_size(3000, 1500, 600);
    CHECK_EQ(t.w, 600); CHECK_EQ(t.h, 300);
    t = thumb_size(1000, 4000, 600);
    CHECK_EQ(t.w, 150); CHECK_EQ(t.h, 600);
    t = thumb_size(400, 300, 600);
    CHECK_EQ(t.w, 400); CHECK_EQ(t.h, 300);
    t = thumb_size(0, 300, 600);
    CHECK_EQ(t.w, 0); CHECK_EQ(t.h, 0);
}
