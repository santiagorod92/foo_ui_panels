// Native lyrics viewer for the legacy "Lyric Show" panel type. Shows the current track's lyrics
// over its (darkened) cover art: timestamped (LRC) lyrics scroll and highlight in time with
// playback, plain lyrics just scroll. Sources, in order: file tags, sidecar .lrc/.txt, the local
// cache, and (on demand or opt-in) lrclib.net. All colours/size/etc. live in persisted pvars
// ("lyr.*") edited through the right-click menu (show_settings_menu).
#pragma once
#include "../ui/view.h"
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LyricsPanel : public ui::View {
public:
    explicit LyricsPanel(SkinEngine* engine) : m_engine(engine) {}
    ~LyricsPanel() override;

    // Settings popup, shared by the panel's own right-click and the CD-case frame around it.
    // (x,y): where to open it, in `owner` coordinates.
    static void show_settings_menu(SkinEngine* engine, ui::ViewHost* owner, int x, int y);

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override;
    void on_resize(int, int) override { invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_down(const ui::MouseEvent& e) override;

private:
    struct Line { double t; std::string text; }; // t < 0: no timestamp
    struct Lyrics { std::vector<Line> lines; bool synced = false; };

    void refresh_track();
    void start_fetch();

    SkinEngine* m_engine = nullptr;
    std::string m_key;      // now-playing path#subsong the loaded lyrics belong to
    unsigned m_version = 0; // last seen cache version (a fetch finished)
    Lyrics m_lyrics;
    double m_scroll = 0;
    unsigned long long m_holdUntil = 0; // manual scroll pauses auto-follow until this tick
    bool m_settling = false, m_overLink = false;
    int m_idle = 0;
    gfx::Rect m_link;
};

} // namespace pui
