// Native lyrics viewer for the legacy "Lyric Show" panel type. Shows the current track's lyrics
// over its (darkened) cover art: timestamped (LRC) lyrics scroll and highlight in time with
// playback, plain lyrics just scroll. Sources, in order: file tags, sidecar .lrc/.txt, the local
// cache, and (on demand or opt-in) lrclib.net. All colours/size/etc. live in persisted pvars
// ("lyr.*") edited through the right-click menu (show_settings_menu).
#pragma once
#include "win_sdk.h"
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LyricsPanel {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    static void register_class();

    // Settings popup, shared by the panel's own right-click and the CD-case frame around it.
    static void show_settings_menu(SkinEngine* engine, HWND owner);

private:
    struct Line { double t; std::string text; }; // t < 0: no timestamp
    struct Lyrics { std::vector<Line> lines; bool synced = false; };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC dc);
    void on_timer();
    void refresh_track();
    void start_fetch();
    void invalidate() { if (m_wnd) InvalidateRect(m_wnd, nullptr, FALSE); }

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    std::string m_key;      // now-playing path#subsong the loaded lyrics belong to
    unsigned m_version = 0; // last seen cache version (a fetch finished)
    Lyrics m_lyrics;
    double m_scroll = 0;
    unsigned long long m_holdUntil = 0; // manual scroll pauses auto-follow until this tick
    bool m_settling = false, m_overLink = false;
    int m_idle = 0;
    RECT m_link = {};
};

} // namespace pui
