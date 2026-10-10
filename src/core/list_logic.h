#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace pui {

int clamp_scroll(int scroll, int content_h, int view_h);
int scroll_into_view(int scroll, int top, int bottom, int view_h);
struct ScrollThumb { bool visible = false; int y = 0, h = 0; };
ScrollThumb scroll_thumb(int scroll, int content_h, int view_h, int min_h = 20);
double ease_toward(double cur, double target, double rate, double snap, bool& settled);

int list_nav(int key, int cur, int count, int page);
int grid_nav(int key, int sel, int count, int cols, int page);

bool typeahead_key(int key, bool have_text);
bool starts_with_upper(const std::string& s, const std::string& upper_prefix);
int typeahead_find(size_t n, size_t focus, size_t typed_len, const std::function<bool(size_t)>& match);

class GroupedRows {
public:
    GroupedRows(int header_h, int row_h) : m_header(header_h), m_row(row_h) {}
    void set_groups(const std::vector<size_t>& sizes);
    static std::vector<size_t> runs(const std::vector<std::string>& keys);

    size_t count() const { return m_count; }
    size_t group_count() const { return m_start.size(); }
    size_t group_start(size_t g) const { return m_start[g]; }
    size_t group_size(size_t g) const;
    int group_top(size_t g) const { return (int)g * m_header + (int)m_start[g] * m_row; }
    int header_h() const { return m_header; }
    int row_h() const { return m_row; }
    int content_height() const { return (int)m_start.size() * m_header + (int)m_count * m_row; }

    size_t group_at(int y) const;
    int  item_at(int y) const;
    int  item_top(int idx) const;
    bool is_group_first(int idx) const;
    int  drop_index_at(int y) const;
    int  reveal(int scroll, int idx, int view_h) const;

private:
    size_t group_of(int idx) const;
    int m_header, m_row;
    std::vector<size_t> m_start;
    size_t m_count = 0;
};

struct StarStrip { int x = 0, w = 55; };
StarStrip star_strip(int view_w);
int star_at(const StarStrip& s, int x);

struct TreeItem {
    std::string label;
    int kind = 0, a = -1, b = -1;
    std::vector<int> children;
    bool expanded = false;
    int depth = 0;
};
class TreeRows {
public:
    std::vector<TreeItem> items;
    std::vector<int> roots, rows;

    void clear() { items.clear(); roots.clear(); rows.clear(); }
    int add(int parent, std::string label, int kind, int a = -1, int b = -1);
    void rebuild_rows();
    int row_of(int item) const;
    bool toggle(int item);
    int parent_row(int row) const;
    bool horizontal_key(int key, int row, int& select_row, int& toggle_item) const;
};
int fixed_row_at(int y, int scroll, int row_h, int rows);

struct GridLayout {
    int cols = 1, cellW = 0, cellH = 0, gutter = 5, margin = 6;
    int cell_x(int i) const { return margin + (i % cols) * (cellW + gutter); }
    int cell_y(int i) const { return margin + (i / cols) * (cellH + gutter); }
    int content_height(int count) const;
    int page(int view_h) const;
    int item_at(int x, int y, int count) const;
    int reveal(int scroll, int idx, int view_h) const;
};
GridLayout album_grid(int client_w, int case_w, int case_h);

struct FlowSlot { float x0 = 0, x1 = 0, h0 = 0, h1 = 0; };
struct CoverFlow {
    float size = 0, cx = 0, yMid = 0, sideOff = 0, spacing = 0;
    static CoverFlow fit(int w, int h);
    FlowSlot slot(float d) const;
};
bool coverflow_step(float& pos, int target);

template <class Line>
int line_at(const std::vector<Line>& lines, double pos) {
    int cur = -1;
    for (size_t i = 0; i < lines.size(); ++i) if (lines[i].t <= pos) cur = (int)i; else break;
    return cur;
}

}
