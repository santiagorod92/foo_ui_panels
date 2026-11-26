#include "quick_search.h"
#include "../core/skin_engine.h"
#include "../core/log.h"
#include "../core/navidrome_link.h"
#include "../core/ui_logic.h"
#include <algorithm>
#include <set>

namespace pui {

static const char* kResultsPlaylist = N_("Search results");
enum { kTimerDebounce = 1, kDebounceMs = 350 };

gfx::Color QuickSearch::col(const char* role, gfx::Color def) const {
    return m_engine ? m_engine->color("quick_search", role, def) : def;
}

void QuickSearch::on_attached() {
    ui::TextFieldStyle st;
    st.text = col("text", gfx::Color(236, 238, 245));
    st.background = col("background", gfx::Color(26, 27, 32));
    st.font = gfx::FontSpec{ "Segoe UI", 13, false };
    st.placeholder = tr("Search...");
    m_edit = ui::create_text_field(*host(), this, st);
    gfx::Rect b = host()->bounds();
    on_resize(b.w, b.h);
}

void QuickSearch::paint(gfx::Canvas& cv) {
    gfx::Rect r{ 0, 0, cv.width(), cv.height() };
    cv.fill_rect(r, col("background", gfx::Color(26, 27, 32)));
    cv.frame_rect(r, col("frame", gfx::Color(90, 96, 110)));
}

void QuickSearch::on_resize(int w, int h) {
    if (m_edit) m_edit->set_bounds(gfx::Rect{ 4, 2, std::max(10, w - 8), std::max(10, h - 4) });
}

void QuickSearch::on_timer(int id) {
    if (id == kTimerDebounce) run_search();
}

void QuickSearch::on_visibility(bool shown) {
    if (shown && m_edit) m_edit->focus();
}

void QuickSearch::on_focus() {
    if (m_edit) m_edit->focus();
}

void QuickSearch::on_destroy() {
    m_alive->store(false);
    if (m_abort) m_abort->abort();
    m_edit.reset();
}

static t_size results_playlist() {
    auto pm = playlist_manager::get();
    for (t_size i = 0, n = pm->get_playlist_count(); i < n; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        if (nm == tr(kResultsPlaylist)) return i;
    }
    return pfc_infinite;
}

void QuickSearch::on_text_changed() {
    host()->set_timer(kTimerDebounce, kDebounceMs);
}

void QuickSearch::on_escape() {
    if (m_edit) m_edit->set_text("");
    host()->kill_timer(kTimerDebounce);
    if (m_engine) m_engine->run_button_action("PVAR:SET:showsr:0");
}

void QuickSearch::run_search() {
    host()->kill_timer(kTimerDebounce);
    if (m_abort) m_abort->abort();
    ++m_gen;
    std::string query = m_edit ? m_edit->text() : std::string();
    if (query.empty()) return;
    search_filter::ptr flt;
    try { flt = search_filter_manager::get()->create(query.c_str()); } catch (...) { return; }

    auto pm = playlist_manager::get();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);
    t_size resultsIdx = results_playlist();
    for (t_size i = 0, n = pm->get_playlist_count(); i < n; ++i) {
        if (i == resultsIdx) continue;
        metadb_handle_list items; pm->playlist_get_all_items(i, items);
        all.add_items(items);
    }
    all.sort_by_pointer_remove_duplicates();

    pfc::array_t<bool> mask; mask.set_size(all.get_count());
    if (all.get_count()) flt->test_multi(all, mask.get_ptr());
    metadb_handle_list hits;
    for (t_size i = 0; i < all.get_count(); ++i) if (mask[i]) hits.add_item(all[i]);

    if (resultsIdx == pfc_infinite)
        resultsIdx = pm->create_playlist(tr(kResultsPlaylist), strlen(tr(kResultsPlaylist)), pfc_infinite);
    if (resultsIdx == pfc_infinite) return;
    pm->playlist_clear(resultsIdx);
    pm->playlist_add_items(resultsIdx, hits, bit_array_false());
    pm->set_active_playlist(resultsIdx);
    if (m_engine) m_engine->repaint_all();
    search_server(query);
}

void QuickSearch::search_server(const std::string& query) {
    auto api = navidrome_library_as<navidrome::navidrome_library_api_v4>();
    if (api.is_empty()) return;
    const unsigned gen = m_gen;
    auto alive = m_alive;
    auto abort = std::make_shared<shared_abort>();
    m_abort = abort;
    QuickSearch* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_search_sink {
            metadb_handle_list tracks;
            void on_artist(const char*, const char*, const char*, int) override {}
            void on_album(const char*, const char*, const char*, const char*, const char*, int, int, const char*) override {}
            void on_track(const metadb_handle_ptr& track) override { if (track.is_valid()) tracks.add_item(track); }
        } sink;
        pfc::string8 err;
        bool ok = false;
        try { ok = api->search(query.c_str(), sink, *abort, err); } catch (...) { err = "exception"; }
        if (abort->is_aborting()) return;
        if (!ok) log::warn("navidrome", std::string("search failed: ") + err.c_str());
        else log::info("navidrome", "search: " + std::to_string(sink.tracks.get_count()) + " tracks");
        auto hits = std::make_shared<metadb_handle_list>(sink.tracks);
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->add_server_hits(*hits);
        });
    });
}

void QuickSearch::add_server_hits(const metadb_handle_list& hits) {
    if (!hits.get_count()) return;
    const t_size idx = results_playlist();
    if (idx == pfc_infinite) return;
    auto pm = playlist_manager::get();
    metadb_handle_list have;
    pm->playlist_get_all_items(idx, have);
    std::set<std::string> seen;
    for (t_size i = 0; i < have.get_count(); ++i) seen.insert(track_identity(have[i]->get_path()));
    metadb_handle_list add;
    for (t_size i = 0; i < hits.get_count(); ++i)
        if (seen.insert(track_identity(hits[i]->get_path())).second) add.add_item(hits[i]);
    if (!add.get_count()) return;
    pm->playlist_add_items(idx, add, bit_array_false());
    if (m_engine) m_engine->repaint_all();
}

}
