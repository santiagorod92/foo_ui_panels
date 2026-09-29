// Native replacement for the legacy "Playlist switcher" panel (fooAvA's "P" button): a tree of
// the music you can play — Playlists, then Artist > Album — built from the Media Library plus
// everything foo_navidrome publishes (navidrome_library_api.h). Double-click an album to play
// it, double-click a playlist to switch to it, right-click for Play / Add to playlist.
#pragma once
#include "win_sdk.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LibraryTree {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

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

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void load_local();
    void start_remote_load();
    void merge_remote(std::vector<RemoteAlbum>&& add);
    void finish_data();          // sort + refresh
    void refresh_tree();
    Node* node_of(HTREEITEM h);
    void activate(HTREEITEM h);  // double-click / Enter
    void context_menu(HTREEITEM h, POINT screen);
    void play_album(const Album& al, bool replace);
    void play_artist(const Artist& ar, bool replace);
    Artist& artist_for(const std::string& name);

    HWND m_wnd = nullptr, m_tree = nullptr;
    SkinEngine* m_engine = nullptr;
    HFONT m_font = nullptr;
    std::vector<Artist> m_artists;
    std::vector<Node> m_nodes;
    bool m_loaded = false, m_remote_loading = false;
    std::string m_remote_err;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<abort_callback_impl>();
    unsigned m_gen = 0;
};

} // namespace pui
