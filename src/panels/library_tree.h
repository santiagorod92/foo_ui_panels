#pragma once
#include "../ui/view.h"
#include "../core/list_logic.h"
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LibraryTree : public ui::View {
public:
    explicit LibraryTree(SkinEngine* engine) : m_engine(engine) {}

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { clamp_scroll(); invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    bool on_key_down(int key, unsigned mods) override;
    void on_visibility(bool shown) override;
    void on_destroy() override;

private:
    struct Album {
        std::string name, id;
        bool remote = false;
        std::vector<metadb_handle_ptr> items;
    };
    struct Artist { std::string name, remote_id; std::vector<Album> albums; };
    enum Kind { kLoading, kPlaylists, kPlaylist, kArtist, kAlbum };
    struct RemoteAlbum { std::string artist, album, id, artist_id; };

    void load_local();
    void start_remote_load();
    void merge_remote(std::vector<RemoteAlbum>&& add);
    void finish_data();
    void refresh_tree();
    int  row_at(int y) const;
    void select_row(int row);
    void clamp_scroll();
    void toggle(int item);
    void activate(int item);
    void context_menu(int item, int x, int y);
    void play_album(const Album& al, bool replace);
    void play_artist(const Artist& ar, bool replace);
    Artist& artist_for(const std::string& name);
    std::string item_key(int item) const;

    SkinEngine* m_engine = nullptr;
    std::vector<Artist> m_artists;
    TreeRows m_tree;
    int m_sel = -1;
    int m_scroll = 0;
    std::set<std::string> m_expandedKeys;
    bool m_loaded = false, m_remote_loading = false;
    std::string m_remote_err;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<shared_abort>();
    unsigned m_gen = 0;
};

}
