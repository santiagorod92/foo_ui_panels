#include "quick_search.h"
#include "skin_engine.h"
#include <commctrl.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_quicksearch";
static const char* kResultsPlaylist = "Search results";
enum { kTimerDebounce = 1, kDebounceMs = 350, kEditId = 101 };

void QuickSearch::register_class() {
    static bool done = false; if (done) return; done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    RegisterClassExW(&wc);
}

HWND QuickSearch::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    m_bg = CreateSolidBrush(RGB(26, 27, 32));
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_wnd;
}

std::wstring QuickSearch::text() const {
    int n = GetWindowTextLengthW(m_edit);
    std::wstring w(n, L'\0');
    if (n > 0) GetWindowTextW(m_edit, &w[0], n + 1);
    return w;
}

void QuickSearch::layout() {
    RECT rc; GetClientRect(m_wnd, &rc);
    // 1px frame drawn by the panel, edit inset inside it.
    MoveWindow(m_edit, 4, 2, std::max(10L, rc.right - 8), std::max(10L, rc.bottom - 4), TRUE);
}

// Everything the user could be looking for: the Media Library plus whatever sits in playlists
// (tracks streamed from foo_navidrome only become metadb entries once queued).
void QuickSearch::run_search() {
    KillTimer(m_wnd, kTimerDebounce);
    std::wstring w = text();
    if (w.empty()) return;
    pfc::stringcvt::string_utf8_from_wide query(w.c_str());
    search_filter::ptr flt;
    try { flt = search_filter_manager::get()->create(query); } catch (...) { return; }

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

LRESULT CALLBACK QuickSearch::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<QuickSearch*>(ref);
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { self->run_search(); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        SetWindowTextW(h, L"");
        if (self->m_engine) self->m_engine->run_button_action("PVAR:SET:showsr:0");
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0; // no beep
    return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK QuickSearch::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    QuickSearch* self = reinterpret_cast<QuickSearch*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<QuickSearch*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        self->m_wnd = wnd;
        self->m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       0, 0, 0, 0, wnd, (HMENU)(INT_PTR)kEditId, GetModuleHandleW(nullptr), nullptr);
        self->m_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SendMessageW(self->m_edit, WM_SETFONT, (WPARAM)self->m_font, TRUE);
        SendMessageW(self->m_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search...");
        SetWindowSubclass(self->m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(self));
        return 0;
    case WM_SIZE: self->layout(); return 0;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(wnd, &rc);
        FillRect((HDC)wp, &rc, self->m_bg);
        HBRUSH fr = CreateSolidBrush(RGB(90, 96, 110)); FrameRect((HDC)wp, &rc, fr); DeleteObject(fr);
        return 1;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(236, 238, 245)); SetBkColor(dc, RGB(26, 27, 32));
        return (LRESULT)self->m_bg;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == kEditId && HIWORD(wp) == EN_CHANGE) SetTimer(wnd, kTimerDebounce, kDebounceMs, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == kTimerDebounce) self->run_search();
        return 0;
    case WM_SHOWWINDOW: if (wp) PostMessageW(wnd, WM_SETFOCUS, 0, 0); return 0; // fresh open: type right away
    case WM_SETFOCUS: SetFocus(self->m_edit); return 0;
    case WM_DESTROY:
        if (self->m_font) DeleteObject(self->m_font);
        if (self->m_bg) DeleteObject(self->m_bg);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
