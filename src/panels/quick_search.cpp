#include "quick_search.h"
#include "../core/skin_engine.h"
#include <algorithm>

namespace pui {

static const char* kResultsPlaylist = "Search results";
enum { kTimerDebounce = 1, kDebounceMs = 350 };

void QuickSearch::on_attached() {
    ui::TextFieldStyle st;
    st.text = gfx::Color(236, 238, 245);
    st.background = gfx::Color(26, 27, 32);
    st.font = gfx::FontSpec{ "Segoe UI", 13, false };
    st.placeholder = "Search...";
    m_edit = ui::create_text_field(*host(), this, st);
    gfx::Rect b = host()->bounds();
    on_resize(b.w, b.h);
}

void QuickSearch::paint(gfx::Canvas& cv) {
    gfx::Rect r{ 0, 0, cv.width(), cv.height() };
    cv.fill_rect(r, gfx::Color(26, 27, 32));
    cv.frame_rect(r, gfx::Color(90, 96, 110)); // 1px frame drawn by the panel, edit inset inside it
}

void QuickSearch::on_resize(int w, int h) {
    if (m_edit) m_edit->set_bounds(gfx::Rect{ 4, 2, std::max(10, w - 8), std::max(10, h - 4) });
}

void QuickSearch::on_timer(int id) {
    if (id == kTimerDebounce) run_search();
}

void QuickSearch::on_visibility(bool shown) {
    if (shown && m_edit) m_edit->focus(); // fresh open: type right away
}

void QuickSearch::on_focus() {
    if (m_edit) m_edit->focus();
}

void QuickSearch::on_text_changed() {
    host()->set_timer(kTimerDebounce, kDebounceMs);
}

void QuickSearch::on_escape() {
    if (m_edit) m_edit->set_text("");
    host()->kill_timer(kTimerDebounce);
    if (m_engine) m_engine->run_button_action("PVAR:SET:showsr:0");
}

// Everything the user could be looking for: the Media Library plus whatever sits in playlists
// (tracks streamed from foo_navidrome only become metadb entries once queued).
void QuickSearch::run_search() {
    host()->kill_timer(kTimerDebounce);
    std::string query = m_edit ? m_edit->text() : std::string();
    if (query.empty()) return;
    search_filter::ptr flt;
    try { flt = search_filter_manager::get()->create(query.c_str()); } catch (...) { return; }

    auto pm = playlist_manager::get();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);
    t_size resultsIdx = pfc_infinite;
    for (t_size i = 0, n = pm->get_playlist_count(); i < n; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        if (nm == kResultsPlaylist) { resultsIdx = i; continue; } // don't search our own results
        metadb_handle_list items; pm->playlist_get_all_items(i, items);
        all.add_items(items);
    }
    all.sort_by_pointer_remove_duplicates();

    pfc::array_t<bool> mask; mask.set_size(all.get_count());
    if (all.get_count()) flt->test_multi(all, mask.get_ptr());
    metadb_handle_list hits;
    for (t_size i = 0; i < all.get_count(); ++i) if (mask[i]) hits.add_item(all[i]);

    if (resultsIdx == pfc_infinite)
        resultsIdx = pm->create_playlist(kResultsPlaylist, strlen(kResultsPlaylist), pfc_infinite);
    if (resultsIdx == pfc_infinite) return;
    pm->playlist_clear(resultsIdx);
    pm->playlist_add_items(resultsIdx, hits, bit_array_false());
    pm->set_active_playlist(resultsIdx);
    if (m_engine) m_engine->repaint_all();
}

} // namespace pui
