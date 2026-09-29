// Native replacement for the legacy "Playlist switcher" panel (fooAvA's "P" button): a tree of
// the music you can play — Playlists, then Artist > Album — built from the Media Library plus
// everything foo_navidrome publishes (navidrome_library_api.h). Double-click an album to play
// it, double-click a playlist to switch to it, right-click for Play / Add to playlist.
// Self-drawn (no native tree control), so it looks the same on every platform.
#pragma once
#include "../ui/view.h"
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
        std::string name, id; // id: Navidrome album id (remote) — empty for local albums
        bool remote = false;
        std::vector<metadb_handle_ptr> items;
    };
    struct Artist { std::string name, remote_id; std::vector<Album> albums; };
    enum Kind { kLoading, kPlaylists, kPlaylist, kArtist, kAlbum };
    struct Node { Kind kind; int a = -1, b = -1; };
    struct RemoteAlbum { std::string artist, album, id, artist_id; };
    struct Item {
        std::string label;
        Node node;
        std::vector<int> children;
        bool expanded = false;
        int depth = 0;
    };

    void load_local();
    void start_remote_load();
    void merge_remote(std::vector<RemoteAlbum>&& add);
    void finish_data();          // sort + refresh
    void refresh_tree();         // rebuild the items from the data (keeps expansion/selection)
    void rebuild_rows();         // visible rows from the expanded items
    int  row_at(int y) const;    // visible row index at view-y, or -1
    void select_row(int row);    // select + scroll into view
    void clamp_scroll();
    void toggle(int item);
    void activate(int item);     // double-click / Enter
    void context_menu(int item, int x, int y);
    void play_album(const Album& al, bool replace);
    void play_artist(const Artist& ar, bool replace);
    Artist& artist_for(const std::string& name);
    std::string item_key(int item) const; // stable identity across refreshes

    SkinEngine* m_engine = nullptr;
    std::vector<Artist> m_artists;
    std::vector<Item> m_items;
    std::vector<int> m_roots, m_rows;
    int m_sel = -1;    // selected item index
    int m_scroll = 0;  // px
    std::set<std::string> m_expandedKeys;
    bool m_loaded = false, m_remote_loading = false;
    std::string m_remote_err;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<abort_callback_impl>();
    unsigned m_gen = 0;
};

} // namespace pui
