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
// album and plays it. Right-click gives the standard foobar context menu for the album's tracks;
// on empty space, the sort order (artist, album, year, recently added — remembered). Typing
// filters the albums by artist/album (Backspace edits, Esc clears); arrows move the selection.
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
    // coverflow=true: the legacy "Chronflow" panel type (cover-flow carousel) instead of the grid.
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
        std::string year, added; // sort keys: "1997", "2024-05-01 10:00:00" ("" = unknown)
        metadb_handle_ptr cover_track;
        std::vector<metadb_handle_ptr> items;
        // Albums published by foo_navidrome that have no metadb entries yet (see
        // navidrome_library_api.h): drawn from fetched cover bytes, played through the service.
        bool remote = false;
        std::string remote_id, remote_cover;
    };

    void rebuild();          // re-scan the library (on create + manual refresh)
    // m_groups = m_all filtered by m_filter and in m_sort order; keeps the selection (and the
    // cover-flow centre) on the same album when it's still shown.
    void apply_view();
    void set_filter(const std::string& f);
    void set_sort(AlbumSort s);
    void ensure_visible(int idx);           // grid: scroll so album idx is in view
    void paint_filter_bar(gfx::Canvas& cv, int W);
    GridLayout grid(int clientW) const;
    // The skin's album case art (asset.cd_case) and the cover's window inside it
    // (asset.cd_case_window = x y w h, in the case image's pixels). No case: a bare square cover.
    struct CaseArt { std::string img; int iw = 1, ih = 1, wx = 0, wy = 0, ww = 1, wh = 1; };
    CaseArt case_art() const;
    int  item_at(int x, int y) const; // group index under client (x,y), or -1
    void on_click(int x, int y, bool dbl);
    void on_rclick(int x, int y);
    void play_group(int idx);
    void ensure_built();
    void start_remote_load();                       // Navidrome albums, off the UI thread
    void merge_remote(std::vector<Group>&& add);    // UI thread; keeps selection
    void request_cover(const std::string& coverId, int size); // async fetch for a visible remote tile
    void paint_coverflow(gfx::Canvas& cv, int W, int H);
    // The skin config's `color.album_list.<role>` / `color.<role>`, else `def`.
    gfx::Color color_of(const char* role, gfx::Color def) const;
    void set_target(int idx);   // cover-flow: glide to album idx
    int  group_index_for_now_playing() const;

    SkinEngine* m_engine = nullptr;
    std::vector<Group> m_all;    // every album (library + Navidrome), by key
    std::vector<Group> m_groups; // what's shown: m_all filtered + sorted (indices below are into this)
    std::string m_filter;        // typed search ("" = all)
    AlbumSort m_sort = AlbumSort::Artist;
    bool m_sortLoaded = false;
    bool m_built = false;
    int m_scroll = 0, m_content_h = 0;
    int m_selected = -1, m_hover = -1;

    // Cover-flow mode state.
    bool m_coverflow = false;
    float m_cf_pos = 0;         // animated centre position (fractional album index)
    int m_cf_target = 0;
    bool m_cf_user = false;     // user navigated; stop auto-centring on the playing album
    bool m_cf_init = false;
    std::vector<std::pair<int, gfx::Rect>> m_cf_hits; // drawn covers, back-to-front
    // The album last selected (cover flow: the one the user moved to) is kept per mode
    // (SkinEngine::view_state) and selected again when the browser is rebuilt or reopened.
    bool m_restoreSel = true;
    std::string m_savedSel;
    std::string view_key() const { return m_coverflow ? "album_list.coverflow.album" : "album_list.grid.album"; }

    // Remote (Navidrome) library state. m_alive/m_abort outlive the window so worker callbacks
    // can tell whether `this` is still there before touching it.
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<shared_abort>();
    unsigned m_gen = 0;
    bool m_remote_loading = false;
    std::string m_remote_err;
    std::map<std::string, album_art_data_ptr> m_covers;
    std::set<std::string> m_cover_pending, m_cover_failed;
    int m_cover_inflight = 0;
};

} // namespace pui
