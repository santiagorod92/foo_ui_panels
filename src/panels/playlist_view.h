#pragma once
#include "../ui/view.h"
#include "../fb2k.h"
#include "../core/list_logic.h"
#include <memory>
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class PlaylistView : public ui::View {
public:
    explicit PlaylistView(SkinEngine* engine) : m_engine(engine) {}

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { clamp_scroll(); invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_leave() override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_up(const ui::MouseEvent& e) override;
    bool on_key_down(int key, unsigned mods) override;
    void on_timer(int id) override;
    void on_drop_files(const std::vector<std::string>& paths, int x, int y) override;

private:
    void on_click(int x, int y, bool dbl, bool shift, bool ctrl);
    void on_rclick(int x, int y);
    int  item_at(int y);
    int  drop_index_at(int y);
    bool has_stars() const;
    int  star_under(int x) const;
    void clamp_scroll();
    void ensure_visible(int idx);
    void move_focus(int idx, unsigned mods);
    bool find_as_you_type(int key);
    void finish_drag(int y);

    const GroupedRows& layout();
    struct Events : playlist_callback_single_impl_base {
        explicit Events(PlaylistView* v);
        PlaylistView* m_v;
        void changed() { m_v->m_dirty = true; m_v->invalidate(); }
        void on_items_added(t_size, metadb_handle_list_cref, const bit_array&) override { changed(); }
        void on_items_reordered(const t_size*, t_size) override { changed(); }
        void on_items_removed(const bit_array&, t_size, t_size) override { m_v->m_anchor = -1; changed(); }
        void on_items_modified(const bit_array&) override { changed(); }
        void on_items_replaced(const bit_array&, const pfc::list_base_const_t<playlist_callback::t_on_items_replaced_entry>&) override { changed(); }
        void on_playlist_switch() override { m_v->m_restoreScroll = true; m_v->m_anchor = -1; changed(); }
        void on_items_selection_change(const bit_array&, const bit_array&) override { m_v->invalidate(); }
        void on_item_focus_change(t_size, t_size) override { m_v->invalidate(); }
        void on_item_ensure_visible(t_size idx) override { m_v->ensure_visible((int)idx); }
    };

    SkinEngine* m_engine = nullptr;
    std::unique_ptr<Events> m_events;
    GroupedRows m_layout{ 46, 19 };
    bool m_dirty = true;
    int m_scroll = 0;
    bool m_restoreScroll = true;
    int m_savedScroll = -1;
    std::string scroll_state_key() const;
    int m_anchor = -1;
    int m_hover_row = -1;
    int m_hover_stars = 0;
    std::string m_find;

    int m_press_idx = -1, m_press_y = 0;
    bool m_press_deferred = false;
    bool m_dragging = false;
    int m_drop_idx = -1;
};

}
