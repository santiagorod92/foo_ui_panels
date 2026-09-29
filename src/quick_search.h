// Native "Quick Search Toolbar" (legacy fooAvA panel type; no stock DUI equivalent): a small
// text box in the skin's top bar. Typing (or Enter) fills a "Search results" playlist with every
// track from the Media Library and from all playlists that matches the query, using foobar2000's
// own search syntax; Esc clears it and closes the box (showsr pvar).
#pragma once
#include "win_sdk.h"
#include <string>

namespace pui {

class SkinEngine;

class QuickSearch {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void layout();
    void run_search();
    std::wstring text() const;

    HWND m_wnd = nullptr, m_edit = nullptr;
    SkinEngine* m_engine = nullptr;
    HBRUSH m_bg = nullptr;
    HFONT m_font = nullptr;
};

} // namespace pui
