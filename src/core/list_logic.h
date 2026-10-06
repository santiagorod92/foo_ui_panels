// Layout, scrolling and keyboard-navigation arithmetic of the native list panels (playlist, album
// browser grid + cover flow, playlist switcher tree, lyrics). The panels keep the foobar2000 and
// drawing calls; the geometry and the decisions live here so they can be unit-tested. No
// foobar2000 SDK — compiled into the host-side tests (`make test`).
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace pui {

// --- scrolling ---------------------------------------------------------------------------------
// Scroll offsets are content-space px of the top edge; 0..max(0, content - view).
int clamp_scroll(int scroll, int content_h, int view_h);
// The scroll that brings [top, bottom) into view, moving as little as possible (the top wins
// when it doesn't fit). Not clamped.
int scroll_into_view(int scroll, int top, int bottom, int view_h);
// A thin scroll-position indicator: where to draw the thumb along a `view_h` track. Hidden when
// everything fits.
struct ScrollThumb { bool visible = false; int y = 0, h = 0; };
ScrollThumb scroll_thumb(int scroll, int content_h, int view_h, int min_h = 20);
// Eases `cur` a step toward `target` (smooth follow); snaps when closer than `snap`. `settled`
// is cleared when it is still moving.
double ease_toward(double cur, double target, double rate, double snap, bool& settled);

// --- keyboard navigation -----------------------------------------------------------------------
// A vertical list: the index ui::kKeyUp/Down/PageUp/PageDown/Home/End moves to from `cur`
// (-1 = nothing focused: Up/Down/PageUp start at the top), clamped to 0..count-1. -1 for other
// keys or an empty list.
int list_nav(int key, int cur, int count, int page);
// A grid with `cols` columns: Left/Right by one, Up/Down by a row, Page Up/Down by `page` items,
// Home/End. Nothing selected (-1) moves from the first item; Right/Down select it. -1 for other
// keys or an empty grid.
int grid_nav(int key, int sel, int count, int cols, int page);

// Find-as-you-type: whether `key` (a ui key code) extends the buffer — letters, digits, and a
// space once something was typed.
bool typeahead_key(int key, bool have_text);
// ASCII case-insensitive: `s` starts with `upper_prefix` (already upper case).
bool starts_with_upper(const std::string& s, const std::string& upper_prefix);
// The next of `n` items `match` accepts, searching from `focus` (a fresh one-character search
// starts after it, so typing the same letter again moves on) and wrapping. -1 when none does.
int typeahead_find(size_t n, size_t focus, size_t typed_len, const std::function<bool(size_t)>& match);

// --- grouped list (the playlist) ---------------------------------------------------------------
// Items 0..n-1 in consecutive groups, each drawn as a header then one row per item. Lookups are
// O(log groups).
class GroupedRows {
public:
    GroupedRows(int header_h, int row_h) : m_header(header_h), m_row(row_h) {}
    // Group sizes in order (empty groups are dropped).
    void set_groups(const std::vector<size_t>& sizes);
    // Groups of equal consecutive keys: keys[i] is item i's group key.
    static std::vector<size_t> runs(const std::vector<std::string>& keys);

    size_t count() const { return m_count; }
    size_t group_count() const { return m_start.size(); }
    size_t group_start(size_t g) const { return m_start[g]; }
    size_t group_size(size_t g) const;
    int group_top(size_t g) const { return (int)g * m_header + (int)m_start[g] * m_row; }
    int header_h() const { return m_header; }
    int row_h() const { return m_row; }
    int content_height() const { return (int)m_start.size() * m_header + (int)m_count * m_row; }

    size_t group_at(int y) const;     // last group whose top is at/above content-y (0 above all)
    int  item_at(int y) const;        // item whose row is at content-y, or -1 (header / past end)
    int  item_top(int idx) const;     // content-y of item idx's row, or -1
    bool is_group_first(int idx) const;
    // Insertion point (0..count) for a drop at content-y: before the row whose upper half is
    // under y (a group header counts as its first row's upper half), after the last row below.
    int  drop_index_at(int y) const;
    // The scroll that shows item idx (with its group header when it is the group's first row).
    int  reveal(int scroll, int idx, int view_h) const;

private:
    size_t group_of(int idx) const;   // group holding item idx (idx < count)
    int m_header, m_row;
    std::vector<size_t> m_start;      // first item of each group
    size_t m_count = 0;
};

// The rating stars in a playlist row: (x, w) of the strip in a `view_w` wide row, and the star
// (1..5) at x — 0 when x is outside.
struct StarStrip { int x = 0, w = 55; };
StarStrip star_strip(int view_w);
int star_at(const StarStrip& s, int x);

// --- tree (the playlist switcher) --------------------------------------------------------------
struct TreeItem {
    std::string label;
    int kind = 0, a = -1, b = -1;     // the owner's payload
    std::vector<int> children;
    bool expanded = false;
    int depth = 0;
};
// Items with their expansion state, flattened into the visible rows.
class TreeRows {
public:
    std::vector<TreeItem> items;
    std::vector<int> roots, rows;     // rows: item indices, in display order

    void clear() { items.clear(); roots.clear(); rows.clear(); }
    int add(int parent, std::string label, int kind, int a = -1, int b = -1);
    void rebuild_rows();              // after adding items / changing expansion
    int row_of(int item) const;       // its visible row, or -1
    bool toggle(int item);            // expand/collapse (+ rebuild); false for a leaf
    int parent_row(int row) const;    // the nearest row above with a smaller depth, or -1
    // Left/Right on the selected visible `row`: Right expands a collapsed item or steps into an
    // expanded one; Left collapses an expanded item or goes to its parent. Sets the item to
    // toggle or the row to select (-1 = neither); false = not Left/Right, or no such row.
    bool horizontal_key(int key, int row, int& select_row, int& toggle_item) const;
};
// Visible row at view-y with fixed-height rows, or -1.
int fixed_row_at(int y, int scroll, int row_h, int rows);

// --- album grid --------------------------------------------------------------------------------
struct GridLayout {
    int cols = 1, cellW = 0, cellH = 0, gutter = 5, margin = 6;
    int cell_x(int i) const { return margin + (i % cols) * (cellW + gutter); }
    int cell_y(int i) const { return margin + (i / cols) * (cellH + gutter); } // content-y
    int content_height(int count) const;
    int page(int view_h) const;                        // items per screenful (whole rows)
    int item_at(int x, int y, int count) const;        // content-space; -1 in gutters / past end
    int reveal(int scroll, int idx, int view_h) const; // scroll that shows tile idx
};
// Tiles about 100px wide (one to eight across), each a case of aspect case_w:case_h (or a square
// cover) plus two caption lines.
GridLayout album_grid(int client_w, int case_w, int case_h);

// --- cover flow --------------------------------------------------------------------------------
// One album's trapezoid: x0..x1, heights h0 (left edge) / h1 (right edge), centred on yMid.
struct FlowSlot { float x0 = 0, x1 = 0, h0 = 0, h1 = 0; };
struct CoverFlow {
    float size = 0, cx = 0, yMid = 0, sideOff = 0, spacing = 0;
    static CoverFlow fit(int w, int h);
    // `d` = album index - animated centre position: 0 face-on, neighbours turned away with the
    // taller edge outside.
    FlowSlot slot(float d) const;
};
// One animation tick of the glide toward `target`; true once it arrived (pos == target).
bool coverflow_step(float& pos, int target);

// --- lyrics ------------------------------------------------------------------------------------
// The line being sung at `pos` seconds: the last whose time is at/before it (times ascending),
// -1 before the first.
template <class Line>
int line_at(const std::vector<Line>& lines, double pos) {
    int cur = -1;
    for (size_t i = 0; i < lines.size(); ++i) if (lines[i].t <= pos) cur = (int)i; else break;
    return cur;
}

} // namespace pui
