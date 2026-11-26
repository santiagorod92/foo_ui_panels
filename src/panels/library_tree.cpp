#include "library_tree.h"
#include "../core/skin_engine.h"
#include "../core/log.h"
#include "../core/navidrome_link.h"
#include <algorithm>
#include <map>

namespace pui {

static const int kRowH = 20, kIndent = 18, kPad = 4;
static const gfx::FontSpec kFont{ "Segoe UI", 13, false };

static std::string lower_str(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static titleformat_object::ptr compile(const char* script) {
    titleformat_object::ptr obj;
    titleformat_compiler::get()->compile_safe(obj, script);
    return obj;
}

static std::string fmt(const metadb_handle_ptr& h, const titleformat_object::ptr& obj) {
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, obj, nullptr);
    return o.c_str();
}

struct TrackFields {
    titleformat_object::ptr album_artist = compile("[%album artist%]"), artist = compile("[%artist%]"),
                            album = compile("[%album%]"), order = compile("$num(%discnumber%,3)$num(%tracknumber%,4)%title%"),
                            label = compile("[%tracknumber%. ]%title%");
    std::string artist_of(const metadb_handle_ptr& h) const {
        std::string a = fmt(h, album_artist);
        if (a.empty()) a = fmt(h, artist);
        return a.empty() ? std::string(tr("Unknown artist")) : a;
    }
    std::string album_of(const metadb_handle_ptr& h) const {
        std::string a = fmt(h, album);
        return a.empty() ? std::string(tr("(no album)")) : a;
    }
};

void LibraryTree::on_attached() {
    load_local();
    start_remote_load();
    finish_data();
    m_loaded = true;
}

void LibraryTree::on_destroy() {
    m_alive->store(false);
    m_abort->abort();
}

void LibraryTree::on_visibility(bool shown) {
    if (shown && m_loaded) refresh_tree();
}

LibraryTree::Artist& LibraryTree::artist_for(const std::string& name) {
    auto [it, added] = m_artistIndex.emplace(lower_str(name), m_artists.size());
    if (added) m_artists.push_back({ name, "", {} });
    return m_artists[it->second];
}

void LibraryTree::load_local() {
    m_artists.clear();
    m_artistIndex.clear();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);
    const TrackFields tf;
    for (t_size i = 0; i < all.get_count(); ++i) {
        const std::string artist = tf.artist_of(all[i]), album = tf.album_of(all[i]);
        Artist& ar = artist_for(artist);
        Album* found = nullptr;
        for (auto& al : ar.albums) if (!al.remote && lower_str(al.name) == lower_str(album)) { found = &al; break; }
        if (!found) { ar.albums.push_back({ album, "", false, {} }); found = &ar.albums.back(); }
        found->items.push_back(all[i]);
    }
}

void LibraryTree::start_remote_load() {
    ++m_gen;
    m_remote_err.clear(); m_remote_loading = false;
    auto api = navidrome_library();
    if (api.is_empty()) return;
    if (!api->is_configured()) return;
    m_remote_loading = true;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; LibraryTree* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_album_sink {
            std::vector<RemoteAlbum> all;
            void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char*, int, int) override {
                all.push_back({ artistName, albumName, albumId, artistId });
            }
        } sink;
        pfc::string8 err; bool ok = false;
        try { ok = api->list_albums(sink, *abort, err); } catch (...) { err = N_("Navidrome library request failed."); }
        auto shared = std::make_shared<std::vector<RemoteAlbum>>(std::move(sink.all));
        std::string errs = ok ? std::string() : std::string(err.c_str());
        if (!ok) log::warn("navidrome", "album list request failed: " + errs);
        else log::info("navidrome", "album list loaded");
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->m_remote_loading = false; self->m_remote_err = errs;
            self->merge_remote(std::move(*shared));
            if (errs.empty()) self->start_remote_tracks();
        });
    });
}

void LibraryTree::start_remote_tracks() {
    auto api = navidrome_library_as<navidrome::navidrome_library_api_v3>();
    if (api.is_empty()) return;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; LibraryTree* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_track_sink {
            metadb_handle_list tracks;
            void on_track(const metadb_handle_ptr& t) override { if (t.is_valid()) tracks.add_item(t); }
        } sink;
        pfc::string8 err; bool ok = false;
        try { ok = api->list_tracks(sink, *abort, err); } catch (...) { err = "exception"; }
        if (abort->is_aborting()) return;
        if (!ok) { log::warn("navidrome", std::string("track list request failed: ") + err.c_str()); return; }
        log::info("navidrome", "library tree: " + std::to_string(sink.tracks.get_count()) + " tracks");
        auto shared = std::make_shared<metadb_handle_list>(sink.tracks);
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->merge_remote_tracks(*shared);
        });
    });
}

void LibraryTree::merge_remote_tracks(const metadb_handle_list& tracks) {
    const TrackFields tf;
    for (t_size i = 0; i < tracks.get_count(); ++i) {
        Artist& ar = artist_for(tf.artist_of(tracks[i]));
        const std::string album = tf.album_of(tracks[i]), key = lower_str(album);
        Album* found = nullptr;
        for (auto& al : ar.albums) if (lower_str(al.name) == key) { found = &al; break; }
        if (!found) { ar.albums.push_back({ album, "", true, {} }); found = &ar.albums.back(); }
        if (found->remote) found->items.push_back(tracks[i]);
    }
    finish_data();
}

void LibraryTree::merge_remote(std::vector<RemoteAlbum>&& add) {
    for (auto& r : add) {
        Artist& ar = artist_for(r.artist);
        if (ar.remote_id.empty()) ar.remote_id = r.artist_id;
        bool have = false;
        for (auto& al : ar.albums) if (lower_str(al.name) == lower_str(r.album)) { have = true; break; }
        if (!have) ar.albums.push_back({ r.album, r.id, true, {} });
    }
    finish_data();
}

void LibraryTree::finish_data() {
    auto byName = [](auto& items) {
        std::vector<std::string> names;
        names.reserve(items.size());
        for (const auto& it : items) names.push_back(it.name);
        apply_order(items, sort_order(ui::collation_ranks(names), false));
    };
    const TrackFields tf;
    for (auto& ar : m_artists) {
        byName(ar.albums);
        for (auto& al : ar.albums) {
            if (al.items.size() > 1) {
                metadb_handle_list list;
                for (auto& h : al.items) list.add_item(h);
                list.sort_by_format(tf.order, nullptr);
                for (t_size i = 0; i < list.get_count(); ++i) al.items[i] = list[i];
            }
            al.labels.clear();
            for (auto& h : al.items) al.labels.push_back(fmt(h, tf.label));
        }
    }
    byName(m_artists);
    m_artistIndex.clear();
    for (size_t i = 0; i < m_artists.size(); ++i) m_artistIndex.emplace(lower_str(m_artists[i].name), i);
    refresh_tree();
}

std::string LibraryTree::item_key(int i) const {
    const TreeItem& it = m_tree.items[i];
    switch (it.kind) {
    case kPlaylists: return "P";
    case kPlaylist:  return "p:" + it.label;
    case kArtist:    return "a:" + m_artists[it.a].name;
    case kAlbum:     return "b:" + m_artists[it.a].name + "|" + m_artists[it.a].albums[it.b].name;
    case kTrack:     return "t:" + m_artists[it.a].name + "|" + m_artists[it.a].albums[it.b].name + "|" + std::to_string(it.c);
    default:         return "l:" + it.label;
    }
}

void LibraryTree::refresh_tree() {
    const std::string selKey = m_sel >= 0 && m_sel < (int)m_tree.items.size() ? item_key(m_sel) : std::string();
    m_tree.clear();
    m_tree.items.reserve(m_artists.size() * 8 + 64);
    auto add = [&](int parent, std::string text, Kind kind, int a = -1, int b = -1) {
        return m_tree.add(parent, std::move(text), kind, a, b);
    };

    auto pm = playlist_manager::get();
    const t_size np = pm->get_playlist_count();
    int pls = add(-1, tr("Playlists"), kPlaylists);
    for (t_size i = 0; i < np; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        add(pls, std::string(nm.c_str()) + "  (" + std::to_string(pm->playlist_get_item_count(i)) + ")", kPlaylist, (int)i);
    }
    if (m_remote_loading) add(-1, tr("Loading Navidrome library..."), kLoading);
    else if (!m_remote_err.empty()) add(-1, tr(m_remote_err), kLoading);
    if (m_artists.empty() && !m_remote_loading)
        add(-1, tr("No music found (Media Library empty, Navidrome not available)"), kLoading);

    for (size_t a = 0; a < m_artists.size(); ++a) {
        const Artist& ar = m_artists[a];
        int ai = add(-1, ar.name + "  (" + std::to_string(ar.albums.size()) + ")", kArtist, (int)a);
        for (size_t b = 0; b < ar.albums.size(); ++b) {
            const int bi = add(ai, ar.albums[b].name, kAlbum, (int)a, (int)b);
            const auto& labels = ar.albums[b].labels;
            for (size_t t = 0; t < labels.size(); ++t) {
                const int ti = add(bi, labels[t], kTrack, (int)a, (int)b);
                m_tree.items[ti].c = (int)t;
            }
        }
    }
    m_sel = -1;
    for (int i = 0; i < (int)m_tree.items.size(); ++i) {
        const std::string k = item_key(i);
        if (m_expandedKeys.count(k)) m_tree.items[i].expanded = true;
        if (!selKey.empty() && k == selKey) m_sel = i;
    }
    m_tree.rebuild_rows();
    clamp_scroll();
    invalidate();
}

void LibraryTree::clamp_scroll() {
    m_scroll = pui::clamp_scroll(m_scroll, (int)m_tree.rows.size() * kRowH, host() ? host()->bounds().h : 0);
}

int LibraryTree::row_at(int y) const { return fixed_row_at(y, m_scroll, kRowH, (int)m_tree.rows.size()); }

void LibraryTree::select_row(int row) {
    if (m_tree.rows.empty()) return;
    row = std::max(0, std::min((int)m_tree.rows.size() - 1, row));
    m_sel = m_tree.rows[row];
    m_scroll = scroll_into_view(m_scroll, row * kRowH, (row + 1) * kRowH, host()->bounds().h);
    clamp_scroll();
    invalidate();
}

void LibraryTree::toggle(int item) {
    if (!m_tree.toggle(item)) return;
    if (m_tree.items[item].expanded) m_expandedKeys.insert(item_key(item)); else m_expandedKeys.erase(item_key(item));
    clamp_scroll();
    invalidate();
}

void LibraryTree::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    const gfx::Color bg(20, 21, 26), text(228, 231, 240), lines(80, 85, 100);
    gfx::Color accent(0, 120, 215); if (m_engine) m_engine->theme_color(accent);
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, bg);
    cv.set_font(kFont);
    const int first = m_scroll / kRowH;
    for (int r = first; r < (int)m_tree.rows.size(); ++r) {
        const int y = r * kRowH - m_scroll;
        if (y > H) break;
        const TreeItem& it = m_tree.items[m_tree.rows[r]];
        const int x = kPad + it.depth * kIndent;
        if (m_tree.rows[r] == m_sel) cv.fill_rect_alpha(gfx::Rect{ 0, y, W, kRowH }, accent, 150);
        if (!it.children.empty()) {
            const int bx = x, by = y + (kRowH - 9) / 2;
            cv.frame_rect(gfx::Rect{ bx, by, 9, 9 }, lines);
            cv.line(bx + 2, by + 4, bx + 7, by + 4, text);
            if (!it.expanded) cv.line(bx + 4, by + 2, bx + 4, by + 7, text);
        }
        cv.draw_text(it.label, gfx::Rect{ x + 14, y, W - x - 14 - kPad, kRowH },
                     gfx::kSingleLine | gfx::kVCenter | gfx::kEndEllipsis, text);
    }
    const ScrollThumb th = scroll_thumb(m_scroll, (int)m_tree.rows.size() * kRowH, H);
    if (th.visible) cv.fill_rect_alpha(gfx::Rect{ W - 5, th.y, 4, th.h }, lines, 200);
}

void LibraryTree::on_wheel(int, int, float notches) {
    m_scroll -= (int)(notches * kRowH * 3);
    clamp_scroll();
    invalidate();
}

void LibraryTree::on_mouse_down(const ui::MouseEvent& e) {
    host()->focus();
    const int row = row_at(e.y);
    if (row < 0) return;
    const int item = m_tree.rows[row];
    const TreeItem& it = m_tree.items[item];
    const int x = kPad + it.depth * kIndent;
    m_sel = item;
    invalidate();
    if (e.button == ui::MouseButton::Right) { context_menu(item, e.x, e.y); return; }
    if (e.button != ui::MouseButton::Left) return;
    if (!it.children.empty() && e.x >= x - 2 && e.x < x + 12) { toggle(item); return; }
    if (e.double_click) {
        const int k = it.kind;
        if (k == kAlbum || k == kPlaylist || k == kTrack) activate(item);
        else toggle(item);
    }
}

bool LibraryTree::on_key_down(int key, unsigned) {
    if (key == ui::kKeyF5) { load_local(); start_remote_load(); finish_data(); return true; }
    const int row = m_tree.row_of(m_sel);
    const int page = std::max(1, host()->bounds().h / kRowH - 1);
    const int to = list_nav(key, row, (int)m_tree.rows.size(), page);
    if (to >= 0) { select_row(to); return true; }
    if (m_sel < 0) return false;
    int selRow, toggleItem;
    if (m_tree.horizontal_key(key, row, selRow, toggleItem)) {
        if (toggleItem >= 0) toggle(toggleItem);
        else if (selRow >= 0) select_row(selRow);
        return true;
    }
    const TreeItem& it = m_tree.items[m_sel];
    switch (key) {
    case ui::kKeyEnter: {
        const int k = it.kind;
        if (k == kAlbum || k == kPlaylist || k == kArtist || k == kTrack) activate(m_sel);
        else toggle(m_sel);
        return true;
    }
    }
    return false;
}

void LibraryTree::play_album(const Album& al, bool replace, int start) {
    if (al.remote && al.items.empty()) {
        auto api = navidrome_library();
        if (api.is_valid()) api->play_album(al.id.c_str(), replace, replace);
        return;
    }
    metadb_handle_list list; for (auto& h : al.items) list.add_item(h);
    auto pm = playlist_manager::get();
    if (replace) {
        const char* kName = tr("Album Browser");
        t_size n = pm->get_playlist_count(), target = pfc_infinite;
        for (t_size i = 0; i < n; ++i) { pfc::string8 nm; pm->playlist_get_name(i, nm); if (nm == kName) { target = i; break; } }
        if (target == pfc_infinite) target = pm->create_playlist(kName, strlen(kName), pfc_infinite);
        if (target == pfc_infinite) return;
        pm->playlist_remove_items(target, bit_array_true());
        pm->playlist_insert_items(target, 0, list, bit_array_true());
        pm->set_active_playlist(target);
        pm->playlist_execute_default_action(target, (t_size)std::max(0, start));
    } else {
        pm->activeplaylist_add_items(list, bit_array_false());
    }
}

void LibraryTree::play_artist(const Artist& ar, bool replace) {
    if (!ar.remote_id.empty()) {
        auto api = navidrome_library();
        if (api.is_valid()) api->play_artist(ar.remote_id.c_str(), replace, replace);
        return;
    }
    metadb_handle_list list;
    for (auto& al : ar.albums) for (auto& h : al.items) list.add_item(h);
    if (!list.get_count()) return;
    auto pm = playlist_manager::get();
    if (replace) {
        pm->activeplaylist_clear();
        pm->activeplaylist_add_items(list, bit_array_false());
        pm->playlist_execute_default_action(pm->get_active_playlist(), 0);
    } else pm->activeplaylist_add_items(list, bit_array_false());
}

void LibraryTree::activate(int item) {
    const TreeItem& n = m_tree.items[item];
    if (n.kind == kAlbum) play_album(m_artists[n.a].albums[n.b], true);
    else if (n.kind == kTrack) play_album(m_artists[n.a].albums[n.b], true, n.c);
    else if (n.kind == kArtist) play_artist(m_artists[n.a], true);
    else if (n.kind == kPlaylist) {
        playlist_manager::get()->set_active_playlist((t_size)n.a);
        if (m_engine) m_engine->repaint_all();
    }
}

void LibraryTree::context_menu(int item, int x, int y) {
    const TreeItem n = m_tree.items[item];
    if (n.kind != kAlbum && n.kind != kPlaylist && n.kind != kArtist && n.kind != kTrack) return;
    auto mi = [](const char* label, int id) { ui::MenuItem m; m.label = label; m.id = id; return m; };
    ui::Menu menu;
    if (n.kind == kArtist) {
        menu.push_back(mi(tr("Play artist"), 4));
        menu.push_back(mi(tr("Add artist to current playlist"), 5));
    } else if (n.kind == kTrack) {
        menu.push_back(mi(tr("Play"), 6));
        menu.push_back(mi(tr("Add to current playlist"), 7));
    } else if (n.kind == kAlbum) {
        menu.push_back(mi(tr("Play album"), 1));
        menu.push_back(mi(tr("Add to current playlist"), 2));
    } else menu.push_back(mi(tr("Switch to this playlist"), 3));
    const int cmd = ui::popup_menu(host(), x, y, menu);
    if (cmd == 1 || cmd == 2) play_album(m_artists[n.a].albums[n.b], cmd == 1);
    else if (cmd == 3) activate(item);
    else if (cmd == 4 || cmd == 5) play_artist(m_artists[n.a], cmd == 4);
    else if (cmd == 6) activate(item);
    else if (cmd == 7) {
        metadb_handle_list one; one.add_item(m_artists[n.a].albums[n.b].items[(size_t)n.c]);
        playlist_manager::get()->activeplaylist_add_items(one, bit_array_false());
    }
}

}
