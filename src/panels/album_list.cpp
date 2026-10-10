#include "album_list.h"
#include "../core/skin_engine.h"
#include "../core/log.h"
#include "../core/image_cache.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace pui {

static const char* kPlaylistName = "Album Browser";
static const unsigned kCaption = gfx::kAlignCenter | gfx::kSingleLine | gfx::kEndEllipsis;

struct ALScripts {
    service_ptr_t<titleformat_object> key, artist, album, cover, year, added;
    bool ok = false;
    void ensure() {
        if (ok) return;
        auto c = titleformat_compiler::get();
        c->compile_safe(key,    "[%album artist%]|[%album%]");
        c->compile_safe(artist, "[%album artist%]");
        c->compile_safe(album,  "[%album%]");
        c->compile_safe(cover,  "$replace(%path%,%filename_ext%,*folder*.*)");
        c->compile_safe(year,   "[$left(%date%,4)]");
        c->compile_safe(added,  "[%added%]");
        ok = true;
    }
};

static const char* kSortPvar = "_albumlist.sort";
static ALScripts g_al;

static pfc::string8 fmt(const metadb_handle_ptr& h, const service_ptr_t<titleformat_object>& s) {
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, s, nullptr); return o;
}

static navidrome::navidrome_library_api::ptr navidrome_api() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

static std::string lower_str(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

void AlbumList::ensure_built() { if (!m_built) rebuild(); }

void AlbumList::rebuild() {
    m_built = true;
    g_al.ensure();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);

    std::vector<std::pair<pfc::string8, t_size>> keyed;
    keyed.reserve(all.get_count());
    for (t_size i = 0; i < all.get_count(); ++i)
        keyed.push_back({ fmt(all[i], g_al.key), i });
    std::sort(keyed.begin(), keyed.end(),
              [](auto& a, auto& b) { return strcmp(a.first, b.first) < 0; });

    m_all.clear();
    for (auto& kv : keyed) {
        metadb_handle_ptr h = all[kv.second];
        if (m_all.empty() || strcmp(m_all.back().key, kv.first) != 0) {
            Group g; g.key = kv.first; g.artist = fmt(h, g_al.artist); g.album = fmt(h, g_al.album);
            g.year = fmt(h, g_al.year).c_str(); g.added = fmt(h, g_al.added).c_str();
            g.cover_track = h;
            m_all.push_back(std::move(g));
        }
        Group& g = m_all.back();
        g.items.push_back(h);
        if (g.items.size() > 1) {
            pfc::string8 a = fmt(h, g_al.added);
            if (strcmp(a.c_str(), g.added.c_str()) > 0) g.added = a.c_str();
        }
    }
    m_selected = -1; m_hover = -1; m_scroll = 0;
    apply_view();
    start_remote_load();
}

void AlbumList::apply_view() {
    if (!m_sortLoaded && m_engine) { m_sortLoaded = true; m_sort = album_sort_from(m_engine->get_pvar(kSortPvar)); }
    auto key_at = [&](int i) { return (i >= 0 && i < (int)m_groups.size()) ? std::string(m_groups[i].key.c_str()) : std::string(); };
    std::string selKey = key_at(m_selected), cfKey = key_at(m_cf_target);
    if (m_restoreSel && selKey.empty() && m_engine) {
        selKey = m_engine->view_state(view_key());
        if (m_coverflow && !selKey.empty() && m_filter.empty()) { cfKey = selKey; m_cf_user = true; }
    }

    std::vector<AlbumKeys> keys;
    keys.reserve(m_all.size());
    for (const Group& g : m_all) keys.push_back({ g.artist.c_str(), g.album.c_str(), g.year, g.added });
    m_groups.clear();
    for (size_t i : album_view(keys, m_sort, m_filter)) m_groups.push_back(m_all[i]);

    m_hover = -1; m_selected = -1;
    int cf = -1;
    for (size_t i = 0; i < m_groups.size(); ++i) {
        const std::string k = m_groups[i].key.c_str();
        if (!selKey.empty() && k == selKey) m_selected = (int)i;
        if (!cfKey.empty() && k == cfKey) cf = (int)i;
    }
    if (m_coverflow) {
        if (cf < 0) cf = m_cf_user || !m_filter.empty() ? 0 : group_index_for_now_playing();
        m_cf_target = cf; m_cf_pos = (float)cf; m_cf_init = true;
    }
    if (m_restoreSel && m_selected >= 0) {
        m_restoreSel = false;
        if (!m_coverflow) ensure_visible(m_selected);
    }
    invalidate();
}

void AlbumList::set_filter(const std::string& f) {
    if (f == m_filter) return;
    m_filter = f;
    m_scroll = 0;
    apply_view();
    if (m_selected < 0 && !m_groups.empty() && !m_filter.empty()) m_selected = 0;
    if (m_coverflow && !m_filter.empty()) m_cf_user = true;
}

void AlbumList::set_sort(AlbumSort s) {
    m_sort = s; m_sortLoaded = true;
    if (m_engine) m_engine->set_pvar(kSortPvar, album_sort_name(s));
    m_scroll = 0;
    apply_view();
    if (m_selected >= 0) ensure_visible(m_selected);
}

void AlbumList::ensure_visible(int idx) {
    if (m_coverflow || idx < 0 || !host()) return;
    const gfx::Rect b = host()->bounds();
    m_scroll = grid(b.w).reveal(m_scroll, idx, b.h);
}

void AlbumList::start_remote_load() {
    ++m_gen;
    m_remote_err.clear(); m_remote_loading = false;
    m_cover_failed.clear();
    auto api = navidrome_api();
    if (api.is_empty()) return;
    if (!api->is_configured()) { m_remote_err = "Navidrome is not configured (Preferences > Tools > Navidrome)."; return; }
    m_remote_loading = true;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; AlbumList* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_album_sink {
            std::vector<Group> batch;
            std::function<void(std::vector<Group>&&)> post;
            void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char*, const char* coverArtId, int year, int) override {
                Group g; g.remote = true; g.remote_id = albumId; g.remote_cover = coverArtId;
                g.artist = artistName; g.album = albumName;
                if (year > 0) g.year = std::to_string(year);
                g.key = (lower_str(artistName) + "|" + lower_str(albumName)).c_str();
                batch.push_back(std::move(g));
                if (batch.size() >= 120) flush();
            }
            void flush() { if (!batch.empty()) { post(std::move(batch)); batch.clear(); } }
        } sink;
        sink.post = [=](std::vector<Group>&& b) {
            auto shared = std::make_shared<std::vector<Group>>(std::move(b));
            fb2k::inMainThread([=]() {
                if (!alive->load() || self->m_gen != gen) return;
                self->merge_remote(std::move(*shared));
            });
        };
        pfc::string8 err;
        bool ok = false;
        try { ok = api->list_albums(sink, *abort, err); } catch (...) { err = "Navidrome library request failed."; }
        sink.flush();
        std::string errs = ok ? std::string() : std::string(err.c_str());
        if (!ok) log::warn("navidrome", "album list request failed: " + errs);
        else log::info("navidrome", "album list loaded");
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->m_remote_loading = false; self->m_remote_err = errs;
            self->invalidate();
        });
    });
}

void AlbumList::merge_remote(std::vector<Group>&& add) {
    std::set<std::string> have;
    for (auto& g : m_all) have.insert(lower_str(g.key.c_str()));
    for (auto& g : add) {
        if (have.count(lower_str(g.key.c_str()))) continue;
        have.insert(lower_str(g.key.c_str()));
        m_all.push_back(std::move(g));
    }
    std::stable_sort(m_all.begin(), m_all.end(), [](const Group& a, const Group& b) {
        return lower_str(a.key.c_str()) < lower_str(b.key.c_str());
    });
    if (m_coverflow && !m_cf_user && m_filter.empty()) m_cf_target = -1;
    apply_view();
}

void AlbumList::request_cover(const std::string& coverId, int size) {
    const std::string ck = coverId + "@" + std::to_string(size);
    if (coverId.empty() || m_covers.count(ck) || m_cover_pending.count(ck) ||
        m_cover_failed.count(ck) || m_cover_inflight >= 4) return;
    auto api = navidrome_api();
    if (api.is_empty()) return;
    m_cover_pending.insert(ck); ++m_cover_inflight;
    auto alive = m_alive; auto abort = m_abort; AlbumList* self = this;
    fb2k::splitTask([=]() {
        album_art_data_ptr data;
        try { data = api->fetch_cover(coverId.c_str(), size, *abort); } catch (...) {}
        fb2k::inMainThread([=]() {
            if (!alive->load()) return;
            --self->m_cover_inflight; self->m_cover_pending.erase(ck);
            if (data.is_valid()) self->m_covers[ck] = data; else self->m_cover_failed.insert(ck);
            self->invalidate();
        });
    });
}

AlbumList::CaseArt AlbumList::case_art() const {
    CaseArt a;
    if (!m_engine) return a;
    std::string img = m_engine->asset("cd_case");
    int iw = 0, ih = 0;
    if (img.empty() || !image_natural_size(img, iw, ih) || iw <= 0 || ih <= 0) return a;
    a.img = img; a.iw = iw; a.ih = ih; a.ww = iw; a.wh = ih;
    std::vector<int> w = m_engine->config().nums("asset.cd_case_window");
    if (w.size() == 4 && w[2] > 0 && w[3] > 0) { a.wx = w[0]; a.wy = w[1]; a.ww = w[2]; a.wh = w[3]; }
    return a;
}

GridLayout AlbumList::grid(int clientW) const {
    const CaseArt ca = case_art();
    return album_grid(clientW, ca.iw, ca.ih);
}

int AlbumList::item_at(int x, int y) const {
    if (m_coverflow) {
        for (auto it = m_cf_hits.rbegin(); it != m_cf_hits.rend(); ++it) if (it->second.contains(x, y)) return it->first;
        return -1;
    }
    return grid(host()->bounds().w).item_at(x, y + m_scroll, (int)m_groups.size());
}

gfx::Color AlbumList::color_of(const char* role, gfx::Color def) const {
    return m_engine ? m_engine->color("album_list", role, def) : def;
}

void AlbumList::paint(gfx::Canvas& cv) {
    ensure_built();
    if (m_engine && m_selected >= 0 && m_selected < (int)m_groups.size()) {
        m_restoreSel = false;
        const std::string k = m_groups[(size_t)m_selected].key.c_str();
        if (k != m_savedSel) { m_savedSel = k; m_engine->set_view_state(view_key(), k); }
    }
    const int W = cv.width(), H = cv.height();
    bool drewbg = m_engine && m_engine->draw_canvas_background(cv, *host());
    if (!drewbg) fill_gradient_v(cv, 0, 0, W, H, color_of("background", gfx::Color(28, 28, 32)));
    else if (m_coverflow) fill_alpha(cv, 0, 0, W, H, color_of("overlay", gfx::Color(20, 20, 22)), 90);

    gfx::Color accent(0, 140, 220);
    if (m_engine && !m_engine->configured_color("album_list", "highlight", accent)) m_engine->theme_color(accent);
    static const gfx::FontSpec fTitle{ "Segoe UI", 11, false, true };
    static const gfx::FontSpec fArtist{ "Segoe UI", 10, false };

    const GridLayout gl = grid(W);
    const int cellW = gl.cellW, cellH = gl.cellH;
    const std::string nocover = m_engine ? m_engine->asset("nocover") : std::string();
    const CaseArt ca = case_art();

    if (m_groups.empty()) {
        cv.set_font(fArtist);
        pfc::string8 msg;
        if (!m_filter.empty() && !m_all.empty()) msg << "No albums match \"" << m_filter.c_str() << "\"";
        else if (m_remote_loading) msg = "Loading Navidrome library...";
        else if (!m_remote_err.empty()) msg = m_remote_err.c_str();
        else msg = library_manager::get()->is_library_enabled()
            ? "No albums found in the Media Library." : "Media Library is not configured (Preferences > Media Library).";
        cv.draw_text(msg.get_ptr(), gfx::Rect{ 0, 0, W, H }, gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine,
                     color_of("text_secondary", gfx::Color(200, 205, 220)));
    }

    const bool cf = m_coverflow && !m_groups.empty();
    if (cf) paint_coverflow(cv, W, H);
    for (size_t i = 0; !cf && i < m_groups.size(); ++i) {
        const int x = gl.cell_x((int)i), y = gl.cell_y((int)i) - m_scroll;
        if (y + cellH < 0 || y > H) continue;

        const auto& g = m_groups[i];
        if ((int)i == m_selected || (int)i == m_hover) {
            gfx::Color hlc = (int)i == m_selected ? accent : color_of("hover", gfx::Color(255, 255, 255));
            int a = (int)i == m_selected ? 70 : 32;
            fill_alpha(cv, x, y, cellW, cellH, hlc, a);
        }
        const int inset = 5, caseW = cellW - inset * 2, caseH = caseW * ca.ih / ca.iw;
        const int ax = x + inset, ay = y + inset;
        const int cx = ax + caseW * ca.wx / ca.iw, cy = ay + caseH * ca.wy / ca.ih;
        const int cw = caseW * ca.ww / ca.iw, ch = caseH * ca.wh / ca.ih;
        bool drew = false;
        if (g.remote) {
            const std::string ck = g.remote_cover + "@160";
            auto it = m_covers.find(ck);
            if (it != m_covers.end()) {
                drew = draw_image_data(cv, ck, it->second->data(), it->second->size(), cx, cy, cw, ch);
            } else {
                request_cover(g.remote_cover, 160);
                if (m_cover_failed.count(ck) && !nocover.empty()) drew = draw_image(cv, nocover, cx, cy, cw, ch);
                if (!drew) fill_alpha(cv, cx, cy, cw, ch, color_of("placeholder", gfx::Color(40, 42, 52)), 200);
                drew = true;
            }
        } else {
            pfc::string8 cov = fmt(g.cover_track, g_al.cover);
            drew = draw_cover_art(cv, cov.get_ptr(), g.cover_track, cx, cy, cw, ch);
        }
        if (!drew && !nocover.empty()) drew = draw_image(cv, nocover, cx, cy, cw, ch);
        if (!drew) fill_alpha(cv, cx, cy, cw, ch, color_of("placeholder", gfx::Color(40, 42, 52)), 200);
        if (!ca.img.empty()) draw_image(cv, ca.img, ax, ay, caseW, caseH);
        const int art = caseH;

        cv.set_font(fTitle);
        cv.draw_text(pfc::stringToUpper(g.album).get_ptr(),
                     gfx::Rect::ltrb(x + 3, ay + art + 3, x + cellW - 3, ay + art + 17), kCaption, color_of("text", gfx::Color(235, 240, 255)));
        cv.set_font(fArtist);
        cv.draw_text(pfc::stringToUpper(g.artist).get_ptr(),
                     gfx::Rect::ltrb(x + 3, ay + art + 17, x + cellW - 3, ay + art + 30), kCaption, color_of("text_dim", gfx::Color(150, 160, 185)));
    }
    m_content_h = gl.content_height((int)m_groups.size());
    paint_filter_bar(cv, W);
}

void AlbumList::paint_filter_bar(gfx::Canvas& cv, int W) {
    if (m_filter.empty()) return;
    static const gfx::FontSpec f{ "Segoe UI", 10, false, true };
    cv.set_font(f);
    char count[48]; snprintf(count, sizeof count, "   %u of %u", (unsigned)m_groups.size(), (unsigned)m_all.size());
    const std::string text = "Search: " + m_filter + count;
    const int h = 22, w = std::min(W - 12, cv.text_width(text) + 20);
    const gfx::Rect r{ (W - w) / 2, 6, w, h };
    gfx::Color accent(0, 140, 220);
    if (m_engine && !m_engine->configured_color("album_list", "highlight", accent)) m_engine->theme_color(accent);
    fill_alpha(cv, r.x, r.y, r.w, r.h, color_of("overlay", gfx::Color(16, 16, 20)), 210);
    cv.fill_rect(gfx::Rect{ r.x, r.bottom() - 2, r.w, 2 }, accent);
    cv.draw_text(text, r, gfx::kAlignCenter | gfx::kVCenter | gfx::kSingleLine | gfx::kEndEllipsis,
                 color_of("text", gfx::Color(235, 240, 255)));
}

int AlbumList::group_index_for_now_playing() const {
    metadb_handle_ptr np; playback_control::get()->get_now_playing(np);
    if (np.is_valid() && !m_groups.empty()) {
        g_al.ensure();
        std::string ar = lower_str(fmt(np, g_al.artist).c_str()), al = lower_str(fmt(np, g_al.album).c_str());
        for (size_t i = 0; i < m_groups.size(); ++i)
            if (lower_str(m_groups[i].album.c_str()) == al && lower_str(m_groups[i].artist.c_str()) == ar) return (int)i;
    }
    return 0;
}

void AlbumList::set_target(int idx) {
    if (m_groups.empty()) return;
    idx = std::max(0, std::min((int)m_groups.size() - 1, idx));
    m_cf_user = true;
    m_cf_target = idx; m_selected = idx;
    host()->set_timer(1, 16);
    invalidate();
}

void AlbumList::paint_coverflow(gfx::Canvas& cv, int W, int H) {
    const int n = (int)m_groups.size();
    if (!m_cf_init) { m_cf_init = true; m_cf_target = group_index_for_now_playing(); m_cf_pos = (float)m_cf_target; }
    m_cf_target = std::max(0, std::min(n - 1, m_cf_target));
    const float pos = m_cf_pos;
    const int c = (int)std::lround(pos), range = 5;
    const CoverFlow flow = CoverFlow::fit(W, H);
    const float yMid = flow.yMid;
    m_cf_hits.clear();

    auto draw_one = [&](int idx) {
        if (idx < 0 || idx >= n) return;
        const Group& g = m_groups[idx];
        const FlowSlot sl = flow.slot(idx - pos);
        const float x0 = sl.x0, x1 = sl.x1, h0 = sl.h0, h1 = sl.h1;

        gfx::ImagePtr im;
        if (g.remote) {
            const std::string ck = g.remote_cover + "@300";
            auto it = m_covers.find(ck);
            if (it != m_covers.end()) im = data_image(ck, it->second->data(), it->second->size());
            else request_cover(g.remote_cover, 300);
        } else {
            pfc::string8 cov = fmt(g.cover_track, g_al.cover);
            im = cover_image(cov.get_ptr(), g.cover_track);
        }
        if (im) {
            draw_image_strips(cv, *im, x0, x1, yMid, h0, h1, 255, false);
            draw_image_strips(cv, *im, x0, x1, yMid, h0, h1, 70, true);
        } else {
            gfx::PointF q[4] = { { x0, yMid - h0 / 2 }, { x1, yMid - h1 / 2 },
                                 { x1, yMid + h1 / 2 }, { x0, yMid + h0 / 2 } };
            cv.fill_polygon(q, 4, color_of("placeholder", gfx::Color(58, 62, 76)));
        }
        gfx::Rect hit = gfx::Rect::ltrb((int)x0, (int)(yMid - std::max(h0, h1) / 2), (int)x1, (int)(yMid + std::max(h0, h1) / 2));
        m_cf_hits.push_back({ idx, hit });
    };
    for (int k = range; k >= 1; --k) draw_one(c - k);
    for (int k = range; k >= 1; --k) draw_one(c + k);
    draw_one(c);

    const Group& cg = m_groups[std::clamp(c, 0, n - 1)];
    static const gfx::FontSpec fTitle{ "Segoe UI", 17, false, true };
    static const gfx::FontSpec fSub{ "Segoe UI", 13, false };
    cv.set_font(fTitle);
    cv.draw_text(cg.album.get_ptr(), gfx::Rect::ltrb(12, H - 50, W - 12, H - 28), kCaption, color_of("text", gfx::Color(245, 247, 255)));
    cv.set_font(fSub);
    char sub[64]; snprintf(sub, sizeof sub, "  -  %d / %d", c + 1, n);
    pfc::string8 line = cg.artist; line << sub;
    cv.draw_text(line.get_ptr(), gfx::Rect::ltrb(12, H - 28, W - 12, H - 8), kCaption, color_of("text_dim", gfx::Color(175, 185, 210)));
}

void AlbumList::play_group(int idx) {
    if (idx < 0 || idx >= (int)m_groups.size()) return;
    if (m_groups[idx].remote) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_album(m_groups[idx].remote_id.c_str(), true, true);
        return;
    }
    auto pm = playlist_manager::get();
    t_size n = pm->get_playlist_count(), target = pfc_infinite;
    for (t_size i = 0; i < n; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        if (nm == kPlaylistName) { target = i; break; }
    }
    if (target == pfc_infinite) target = pm->create_playlist(kPlaylistName, strlen(kPlaylistName), pfc_infinite);
    if (target == pfc_infinite) return;
    pm->playlist_remove_items(target, bit_array_true());
    metadb_handle_list list; for (auto& h : m_groups[idx].items) list.add_item(h);
    pm->playlist_insert_items(target, 0, list, bit_array_true());
    pm->set_active_playlist(target);
    pm->playlist_execute_default_action(target, 0);
}

void AlbumList::on_click(int x, int y, bool dbl) {
    host()->focus();
    int idx = item_at(x, y);
    if (idx < 0) return;
    m_selected = idx;
    invalidate();
    if (dbl) play_group(idx);
    else if (m_coverflow && idx != m_cf_target) set_target(idx);
}

void AlbumList::on_rclick(int x, int y) {
    host()->focus();
    int idx = item_at(x, y);
    if (idx < 0) {
        enum { kArtist = 1, kAlbum, kYear, kAdded, kClear, kRefresh };
        auto item = [](const char* label, int id, bool checked = false, bool enabled = true) {
            ui::MenuItem m; m.label = label; m.id = id; m.checked = checked; m.enabled = enabled; return m;
        };
        ui::MenuItem sort; sort.label = "Sort by";
        sort.children = { item("Artist", kArtist, m_sort == AlbumSort::Artist),
                          item("Album", kAlbum, m_sort == AlbumSort::Album),
                          item("Year (newest first)", kYear, m_sort == AlbumSort::Year),
                          item("Recently added", kAdded, m_sort == AlbumSort::Added) };
        ui::Menu menu = { sort, ui::MenuItem::sep(),
                          item(m_filter.empty() ? "Type to search" : "Clear search (Esc)", kClear, false, !m_filter.empty()),
                          item("Refresh (F5)", kRefresh) };
        switch (ui::popup_menu(host(), x, y, menu)) {
        case kArtist: set_sort(AlbumSort::Artist); break;
        case kAlbum: set_sort(AlbumSort::Album); break;
        case kYear: set_sort(AlbumSort::Year); break;
        case kAdded: set_sort(AlbumSort::Added); break;
        case kClear: set_filter(""); break;
        case kRefresh: rebuild(); break;
        }
        return;
    }
    m_selected = idx;
    invalidate();
    if (m_groups[idx].remote) {
        ui::Menu menu;
        ui::MenuItem play; play.label = "Play album"; play.id = 1; menu.push_back(play);
        ui::MenuItem add; add.label = "Add to current playlist"; add.id = 2; menu.push_back(add);
        int cmd = ui::popup_menu(host(), x, y, menu);
        auto api = navidrome_api();
        if (cmd && api.is_valid()) api->play_album(m_groups[idx].remote_id.c_str(), cmd == 1, cmd == 1);
        return;
    }
    metadb_handle_list sel; for (auto& h : m_groups[idx].items) sel.add_item(h);
    ui::track_context_menu(*host(), x, y, sel);
}

void AlbumList::on_destroy() {
    m_alive->store(false);
    m_abort->abort();
}

void AlbumList::on_timer(int) {
    if (!m_coverflow) return;
    if (coverflow_step(m_cf_pos, m_cf_target)) host()->kill_timer(1);
    invalidate();
}

void AlbumList::on_wheel(int, int, float notches) {
    if (m_coverflow) { set_target(m_cf_target - (int)notches); return; }
    m_scroll = clamp_scroll(m_scroll - (int)(notches * 60), m_content_h, host()->bounds().h);
    invalidate();
}

void AlbumList::on_mouse_move(int x, int y, unsigned, bool) {
    int h = item_at(x, y);
    if (h != m_hover) { m_hover = h; invalidate(); }
}

void AlbumList::on_mouse_leave() {
    if (m_hover != -1) { m_hover = -1; invalidate(); }
}

void AlbumList::on_mouse_down(const ui::MouseEvent& e) {
    if (e.button == ui::MouseButton::Left) on_click(e.x, e.y, e.double_click);
    else if (e.button == ui::MouseButton::Right) on_rclick(e.x, e.y);
}

bool AlbumList::on_key_down(int key, unsigned mods) {
    if (!(mods & (ui::kCtrl | ui::kAlt))) {
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key == ' ' && !m_filter.empty())) {
            set_filter(m_filter + (char)tolower(key));
            return true;
        }
        if (key == ui::kKeyBackspace && !m_filter.empty()) { set_filter(m_filter.substr(0, m_filter.size() - 1)); return true; }
        if (key == ui::kKeyEscape && !m_filter.empty()) { set_filter(""); return true; }
    }
    if (m_coverflow) {
        int t = m_cf_target;
        switch (key) {
        case ui::kKeyLeft:     set_target(t - 1); return true;
        case ui::kKeyRight:    set_target(t + 1); return true;
        case ui::kKeyPageUp:   set_target(t - 5); return true;
        case ui::kKeyPageDown: set_target(t + 5); return true;
        case ui::kKeyHome:     set_target(0); return true;
        case ui::kKeyEnd:      set_target((int)m_groups.size() - 1); return true;
        case ui::kKeyEnter:    play_group(t); return true;
        }
    }
    if (key == ui::kKeyEnter && m_selected >= 0) { play_group(m_selected); return true; }
    if (key == ui::kKeyF5) { rebuild(); invalidate(); return true; }
    if (!m_coverflow && !m_groups.empty()) {
        const gfx::Rect b = host()->bounds();
        const GridLayout gl = grid(b.w);
        const int next = grid_nav(key, m_selected, (int)m_groups.size(), gl.cols, gl.page(b.h));
        if (next < 0) return false;
        m_selected = next;
        ensure_visible(m_selected);
        invalidate();
        return true;
    }
    return false;
}

}
