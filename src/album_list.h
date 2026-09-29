// Native album grid browser — fills the gap left by "Graphical Browser"/"Album list" (fooAvA's
// legacy panel types, see FORMAT.md's mapping table): no stock foobar2000 v2 Default UI element
// matches that name (that's a Columns UI panel type we deliberately don't depend on), so
// panel_host.cpp's generic by-name hosting silently finds nothing for it. This is the native
// replacement, same idea as PlaylistView/TrackDisplay: not a pixel-exact recreation of fooAvA's
// own per-item gp_* draw script (see skin_engine.cpp's GDI+ stub functions — that script text is
// unreachable/likely-corrupted extraction leftovers, not something we can faithfully replay).
//
// Scans the whole Media Library, groups by album artist + album, and draws a scrollable grid of
// cover-art tiles. Double-click (or Enter) replaces a dedicated "Album Browser" playlist with that
// album and plays it. Right-click gives the standard foobar context menu for the album's tracks.
#pragma once
#include "win_sdk.h"
#include "navidrome_library_api.h"
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <string>

namespace pui {

class SkinEngine;

class AlbumList {
public:
    // coverflow=true: the legacy "Chronflow" panel type (cover-flow carousel) instead of the grid.
    HWND create(HWND parent, SkinEngine* engine, bool coverflow = false);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    struct Group {
        pfc::string8 key, artist, album;
        metadb_handle_ptr cover_track;
        std::vector<metadb_handle_ptr> items;
        // Albums published by foo_navidrome that have no metadb entries yet (see
        // navidrome_library_api.h): drawn from fetched cover bytes, played through the service.
        bool remote = false;
        std::string remote_id, remote_cover;
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void rebuild();          // re-scan the library (on create + manual refresh)
    void layout_metrics(int clientW, int& cols, int& cellW, int& cellH, int& gutter, int& margin) const;
    int  item_at(int x, int y) const; // group index under client (x,y), or -1
    void on_click(int x, int y, bool dbl);
    void on_rclick(int x, int y);
    void play_group(int idx);
    void ensure_built();
    void start_remote_load();                       // Navidrome albums, off the UI thread
    void merge_remote(std::vector<Group>&& add);    // UI thread; keeps selection
    void request_cover(const std::string& coverId, int size); // async fetch for a visible remote tile
    void paint_coverflow(HDC mem, const RECT& rc);
    void set_target(int idx);   // cover-flow: glide to album idx
    int  group_index_for_now_playing() const;

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    std::vector<Group> m_groups;
    bool m_built = false;
    int m_scroll = 0, m_content_h = 0;
    int m_selected = -1, m_hover = -1;
    bool m_tracking = false;

    // Cover-flow mode state.
    bool m_coverflow = false;
    float m_cf_pos = 0;         // animated centre position (fractional album index)
    int m_cf_target = 0;
    bool m_cf_user = false;     // user navigated; stop auto-centring on the playing album
    bool m_cf_init = false;
    std::vector<std::pair<int, RECT>> m_cf_hits; // drawn covers, back-to-front

    // Remote (Navidrome) library state. m_alive/m_abort outlive the window so worker callbacks
    // can tell whether `this` is still there before touching it.
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<abort_callback_impl>();
    unsigned m_gen = 0;
    bool m_remote_loading = false;
    std::string m_remote_err;
    std::map<std::string, album_art_data_ptr> m_covers;
    std::set<std::string> m_cover_pending, m_cover_failed;
    int m_cover_inflight = 0;
};

} // namespace pui
