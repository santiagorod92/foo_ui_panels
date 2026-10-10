#pragma once
#include "../ui/view.h"
#include "../core/navidrome_library_api.h"
#include "../core/list_logic.h"
#include "../core/ui_logic.h"
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <string>

namespace pui {

class SkinEngine;

class AlbumList : public ui::View {
public:
    AlbumList(SkinEngine* engine, bool coverflow = false) : m_engine(engine), m_coverflow(coverflow) {}

    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }
    void on_timer(int) override;
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_leave() override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    bool on_key_down(int key, unsigned mods) override;
    void on_destroy() override;

private:
    struct Group {
        pfc::string8 key, artist, album;
        std::string year, added;
        metadb_handle_ptr cover_track;
        std::vector<metadb_handle_ptr> items;
        bool remote = false;
        std::string remote_id, remote_cover;
    };

    void rebuild();
    void apply_view();
    void set_filter(const std::string& f);
    void set_sort(AlbumSort s);
    void ensure_visible(int idx);
    void paint_filter_bar(gfx::Canvas& cv, int W);
    GridLayout grid(int clientW) const;
    struct CaseArt { std::string img; int iw = 1, ih = 1, wx = 0, wy = 0, ww = 1, wh = 1; };
    CaseArt case_art() const;
    int  item_at(int x, int y) const;
    void on_click(int x, int y, bool dbl);
    void on_rclick(int x, int y);
    void play_group(int idx);
    void ensure_built();
    void start_remote_load();
    void merge_remote(std::vector<Group>&& add);
    void request_cover(const std::string& coverId, int size);
    void paint_coverflow(gfx::Canvas& cv, int W, int H);
    gfx::Color color_of(const char* role, gfx::Color def) const;
    void set_target(int idx);
    int  group_index_for_now_playing() const;

    SkinEngine* m_engine = nullptr;
    std::vector<Group> m_all;
    std::vector<Group> m_groups;
    std::string m_filter;
    AlbumSort m_sort = AlbumSort::Artist;
    bool m_sortLoaded = false;
    bool m_built = false;
    int m_scroll = 0, m_content_h = 0;
    int m_selected = -1, m_hover = -1;

    bool m_coverflow = false;
    float m_cf_pos = 0;
    int m_cf_target = 0;
    bool m_cf_user = false;
    bool m_cf_init = false;
    std::vector<std::pair<int, gfx::Rect>> m_cf_hits;
    bool m_restoreSel = true;
    std::string m_savedSel;
    std::string view_key() const { return m_coverflow ? "album_list.coverflow.album" : "album_list.grid.album"; }

    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<shared_abort>();
    unsigned m_gen = 0;
    bool m_remote_loading = false;
    std::string m_remote_err;
    std::map<std::string, album_art_data_ptr> m_covers;
    std::set<std::string> m_cover_pending, m_cover_failed;
    int m_cover_inflight = 0;
};

}
