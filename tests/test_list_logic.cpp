#include "test.h"
#include "core/list_logic.h"
#include "core/ui_logic.h"
#include "ui/keys.h"

using namespace pui;

TEST("clamp_scroll / scroll_into_view") {
    CHECK_EQ(clamp_scroll(50, 300, 100), 50);
    CHECK_EQ(clamp_scroll(250, 300, 100), 200);
    CHECK_EQ(clamp_scroll(-5, 300, 100), 0);
    CHECK_EQ(clamp_scroll(30, 80, 100), 0);
    CHECK_EQ(scroll_into_view(100, 40, 60, 100), 40);
    CHECK_EQ(scroll_into_view(0, 120, 140, 100), 40);
    CHECK_EQ(scroll_into_view(50, 60, 80, 100), 50);
    CHECK_EQ(scroll_into_view(0, 120, 300, 100), 120);
}

TEST("scroll_thumb: size and position along the track") {
    CHECK(!scroll_thumb(0, 100, 100).visible);
    CHECK(!scroll_thumb(0, 500, 0).visible);
    ScrollThumb t = scroll_thumb(0, 400, 100);
    CHECK(t.visible); CHECK_EQ(t.h, 25); CHECK_EQ(t.y, 0);
    t = scroll_thumb(300, 400, 100);
    CHECK_EQ(t.y, 75);
    t = scroll_thumb(0, 100000, 100);
    CHECK_EQ(t.h, 20);
    t = scroll_thumb(999999, 400, 100);
    CHECK_EQ(t.y, 75);
}

TEST("ease_toward: approaches, then snaps") {
    bool settled = true;
    double v = ease_toward(0, 100, 0.2, 1.0, settled);
    CHECK_EQ(v, 20.0); CHECK(!settled);
    settled = true;
    v = ease_toward(99.5, 100, 0.2, 1.0, settled);
    CHECK_EQ(v, 100.0); CHECK(settled);
}

TEST("list_nav: arrows, pages, ends, nothing focused") {
    CHECK_EQ(list_nav(ui::kKeyDown, 3, 10, 4), 4);
    CHECK_EQ(list_nav(ui::kKeyUp, 3, 10, 4), 2);
    CHECK_EQ(list_nav(ui::kKeyUp, 0, 10, 4), 0);
    CHECK_EQ(list_nav(ui::kKeyDown, 9, 10, 4), 9);
    CHECK_EQ(list_nav(ui::kKeyPageDown, 8, 10, 4), 9);
    CHECK_EQ(list_nav(ui::kKeyPageUp, 2, 10, 4), 0);
    CHECK_EQ(list_nav(ui::kKeyHome, 5, 10, 4), 0);
    CHECK_EQ(list_nav(ui::kKeyEnd, 5, 10, 4), 9);
    CHECK_EQ(list_nav(ui::kKeyUp, -1, 10, 4), 0);
    CHECK_EQ(list_nav(ui::kKeyDown, -1, 10, 4), 0);
    CHECK_EQ(list_nav(ui::kKeyEnter, 3, 10, 4), -1);
    CHECK_EQ(list_nav(ui::kKeyDown, 0, 0, 4), -1);
}

TEST("grid_nav: rows, columns, nothing selected") {
    CHECK_EQ(grid_nav(ui::kKeyRight, 1, 10, 4, 8), 2);
    CHECK_EQ(grid_nav(ui::kKeyDown, 1, 10, 4, 8), 5);
    CHECK_EQ(grid_nav(ui::kKeyDown, 7, 10, 4, 8), 9);
    CHECK_EQ(grid_nav(ui::kKeyUp, 2, 10, 4, 8), 0);
    CHECK_EQ(grid_nav(ui::kKeyPageDown, 0, 10, 4, 8), 8);
    CHECK_EQ(grid_nav(ui::kKeyEnd, 0, 10, 4, 8), 9);
    CHECK_EQ(grid_nav(ui::kKeyRight, -1, 10, 4, 8), 0);
    CHECK_EQ(grid_nav(ui::kKeyDown, -1, 10, 4, 8), 0);
    CHECK_EQ(grid_nav(ui::kKeyLeft, -1, 10, 4, 8), 0);
    CHECK_EQ(grid_nav('A', 1, 10, 4, 8), -1);
    CHECK_EQ(grid_nav(ui::kKeyRight, 0, 0, 4, 8), -1);
}

TEST("typeahead: keys, prefix, search order") {
    CHECK(typeahead_key('Q', false));
    CHECK(typeahead_key('7', false));
    CHECK(!typeahead_key(' ', false));
    CHECK(typeahead_key(' ', true));
    CHECK(!typeahead_key(ui::kKeyUp, true));
    CHECK(starts_with_upper("Beatles", "BEA"));
    CHECK(!starts_with_upper("Be", "BEA"));
    CHECK(!starts_with_upper("Abba", "B"));
    std::vector<std::string> t = { "Abba", "Blur", "Beck", "Bjork" };
    auto match = [&](const std::string& p) { return [&, p](size_t i) { return starts_with_upper(t[i], p); }; };
    CHECK_EQ(typeahead_find(t.size(), 1, 1, match("B")), 2);
    CHECK_EQ(typeahead_find(t.size(), 3, 1, match("B")), 1);
    CHECK_EQ(typeahead_find(t.size(), 2, 2, match("BE")), 2);
    CHECK_EQ(typeahead_find(t.size(), 0, 1, match("Z")), -1);
    CHECK_EQ(typeahead_find(0, 0, 1, match("A")), -1);
}

TEST("GroupedRows: runs and layout") {
    auto r = GroupedRows::runs({ "a", "a", "b", "c", "c", "c" });
    CHECK_EQ(r.size(), (size_t)3);
    CHECK(r[0] == 2 && r[1] == 1 && r[2] == 3);
    CHECK(GroupedRows::runs({}).empty());

    GroupedRows L(40, 10);
    L.set_groups({ 2, 0, 3 });
    CHECK_EQ(L.count(), (size_t)5);
    CHECK_EQ(L.group_count(), (size_t)2);
    CHECK_EQ(L.content_height(), 2 * 40 + 5 * 10);
    CHECK_EQ(L.item_top(0), 40);
    CHECK_EQ(L.item_top(1), 50);
    CHECK_EQ(L.item_top(2), 100);
    CHECK_EQ(L.item_top(4), 120);
    CHECK_EQ(L.item_top(5), -1);
    CHECK_EQ(L.item_at(-1), -1);
    CHECK_EQ(L.item_at(10), -1);
    CHECK_EQ(L.item_at(45), 0);
    CHECK_EQ(L.item_at(59), 1);
    CHECK_EQ(L.item_at(70), -1);
    CHECK_EQ(L.item_at(105), 2);
    CHECK_EQ(L.item_at(129), 4);
    CHECK_EQ(L.item_at(130), -1);
    CHECK_EQ(L.group_at(0), (size_t)0);
    CHECK_EQ(L.group_at(59), (size_t)0);
    CHECK_EQ(L.group_at(60), (size_t)1);
    CHECK(L.is_group_first(0) && !L.is_group_first(1) && L.is_group_first(2));
}

TEST("GroupedRows: drop index and reveal") {
    GroupedRows L(40, 10);
    L.set_groups({ 2, 3 });
    CHECK_EQ(L.drop_index_at(-3), 0);
    CHECK_EQ(L.drop_index_at(20), 0);
    CHECK_EQ(L.drop_index_at(44), 0);
    CHECK_EQ(L.drop_index_at(45), 1);
    CHECK_EQ(L.drop_index_at(57), 2);
    CHECK_EQ(L.drop_index_at(70), 2);
    CHECK_EQ(L.drop_index_at(126), 5);
    CHECK_EQ(L.drop_index_at(500), 5);
    GroupedRows empty(40, 10);
    CHECK_EQ(empty.drop_index_at(10), 0);

    CHECK_EQ(L.reveal(200, 2, 50), 60);
    CHECK_EQ(L.reveal(200, 3, 50), 110);
    CHECK_EQ(L.reveal(0, 4, 50), 80);
    CHECK_EQ(L.reveal(40, 1, 50), 40);
    CHECK_EQ(L.reveal(7, 99, 50), 7);
}

TEST("star_at: the strip left of the duration") {
    const StarStrip s = star_strip(300);
    CHECK_EQ(s.x, 194); CHECK_EQ(s.w, 55);
    CHECK_EQ(star_at(s, 193), 0);
    CHECK_EQ(star_at(s, 194), 1);
    CHECK_EQ(star_at(s, 194 + 54), 5);
    CHECK_EQ(star_at(s, 194 + 55), 0);
    CHECK_EQ(star_at(s, 194 + 27), 3);
}

TEST("TreeRows: flatten, toggle, parent, Left/Right") {
    TreeRows t;
    const int a = t.add(-1, "A", 1), a1 = t.add(a, "A1", 2), a2 = t.add(a, "A2", 2);
    const int a1x = t.add(a1, "A1x", 3), b = t.add(-1, "B", 1);
    CHECK_EQ(t.items[a1x].depth, 2);
    t.rebuild_rows();
    CHECK_EQ(t.rows.size(), (size_t)2);
    CHECK(t.toggle(a));
    CHECK_EQ(t.rows.size(), (size_t)4);
    CHECK_EQ(t.row_of(a2), 2);
    CHECK_EQ(t.row_of(a1x), -1);
    CHECK(!t.toggle(a2));
    CHECK_EQ(t.parent_row(2), 0);
    CHECK_EQ(t.parent_row(0), -1);

    int sel, tog;
    CHECK(t.horizontal_key(ui::kKeyRight, 1, sel, tog));
    CHECK(tog == a1 && sel == -1);
    t.toggle(a1);
    CHECK(t.horizontal_key(ui::kKeyRight, 1, sel, tog));
    CHECK(sel == 2 && tog == -1);
    CHECK(t.horizontal_key(ui::kKeyLeft, 2, sel, tog));
    CHECK(sel == 1 && tog == -1);
    CHECK(t.horizontal_key(ui::kKeyLeft, 0, sel, tog));
    CHECK(tog == a && sel == -1);
    CHECK(t.horizontal_key(ui::kKeyRight, t.row_of(b), sel, tog));
    CHECK(sel == -1 && tog == -1);
    CHECK(!t.horizontal_key(ui::kKeyUp, 0, sel, tog));
    CHECK(!t.horizontal_key(ui::kKeyLeft, 99, sel, tog));

    CHECK_EQ(fixed_row_at(25, 0, 20, 4), 1);
    CHECK_EQ(fixed_row_at(25, 40, 20, 4), 3);
    CHECK_EQ(fixed_row_at(25, 60, 20, 4), -1);
    CHECK_EQ(fixed_row_at(-1, 0, 20, 4), -1);
}

TEST("album_grid: columns, hit testing, reveal") {
    GridLayout g = album_grid(430, 1, 1);
    CHECK_EQ(g.cols, 4);
    CHECK_EQ(g.cellW, (430 - 12 - 15) / 4);
    CHECK_EQ(g.cellH, 90 + 10 + 32);
    CHECK_EQ(album_grid(50, 1, 1).cols, 1);
    CHECK_EQ(album_grid(5000, 1, 1).cols, 8);
    CHECK_EQ(album_grid(430, 0, 0).cellH, g.cellH);
    CHECK_EQ(album_grid(430, 2, 1).cellH, 45 + 10 + 32);

    CHECK_EQ(g.cell_x(5), 6 + 105);
    CHECK_EQ(g.cell_y(5), 6 + 137);
    CHECK_EQ(g.item_at(10, 10, 10), 0);
    CHECK_EQ(g.item_at(6 + 105 + 1, 6 + 137 + 1, 10), 5);
    CHECK_EQ(g.item_at(6 + 102, 10, 10), -1);
    CHECK_EQ(g.item_at(3, 10, 10), -1);
    CHECK_EQ(g.item_at(10, 6 + 2 * 137 + 1, 10), 8);
    CHECK_EQ(g.item_at(6 + 3 * 105 + 1, 6 + 2 * 137 + 1, 10), -1);
    CHECK_EQ(g.content_height(10), 12 + 3 * 137);
    CHECK_EQ(g.page(300), 8);
    CHECK_EQ(g.page(10), 4);
    CHECK_EQ(g.reveal(500, 0, 200), 0);
    CHECK_EQ(g.reveal(0, 8, 200), 6 + 2 * 137 + 132 - 200 + 6);
    CHECK_EQ(g.reveal(5, 1, 200), 5);
}

TEST("cover flow: slots and glide") {
    CoverFlow f = CoverFlow::fit(400, 300);
    CHECK_EQ(f.size, 128.0f);
    FlowSlot c = f.slot(0);
    CHECK_EQ(c.x1 - c.x0, 128.0f);
    CHECK_EQ(c.h0, c.h1);
    FlowSlot l = f.slot(-1), r = f.slot(1);
    CHECK(l.x1 < c.x1 && r.x0 > c.x0);
    CHECK(l.h0 > l.h1 && r.h1 > r.h0);
    CHECK(f.slot(3).x0 > r.x0);

    float pos = 0;
    CHECK(!coverflow_step(pos, 4));
    CHECK(pos > 0 && pos < 4);
    for (int i = 0; i < 200 && !coverflow_step(pos, 4); ++i) {}
    CHECK_EQ(pos, 4.0f);
}

TEST("line_at: the last line already reached") {
    struct L { double t; };
    std::vector<L> ls = { { 1.0 }, { 2.0 }, { 5.0 } };
    CHECK_EQ(line_at(ls, 0.5), -1);
    CHECK_EQ(line_at(ls, 1.0), 0);
    CHECK_EQ(line_at(ls, 4.9), 1);
    CHECK_EQ(line_at(ls, 99.0), 2);
    CHECK_EQ(line_at(std::vector<L>{}, 1.0), -1);
}

TEST("album_view: filter and sort orders") {
    std::vector<AlbumKeys> all = {
        { "ABBA", "Waterloo", "1974", "2024-01-01" },
        { "Blur", "Parklife", "1994", "" },
        { "Blur", "Blur", "", "2025-03-01" },
        { "Cake", "Prolonging the Magic", "1998", "2023-05-05" },
    };
    auto v = album_view(all, AlbumSort::Artist, "");
    CHECK(v == std::vector<size_t>({ 0, 1, 2, 3 }));
    v = album_view(all, AlbumSort::Artist, "blur");
    CHECK(v == std::vector<size_t>({ 1, 2 }));
    v = album_view(all, AlbumSort::Album, "");
    CHECK(v == std::vector<size_t>({ 2, 1, 3, 0 }));
    v = album_view(all, AlbumSort::Year, "");
    CHECK(v == std::vector<size_t>({ 3, 1, 0, 2 }));
    v = album_view(all, AlbumSort::Added, "");
    CHECK(v == std::vector<size_t>({ 2, 0, 3, 1 }));
    CHECK(album_view(all, AlbumSort::Year, "zeppelin").empty());
}
