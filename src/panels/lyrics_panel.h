#pragma once
#include "../ui/view.h"
#include "../core/lyrics_parse.h"
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LyricsPanel : public ui::View {
public:
    explicit LyricsPanel(SkinEngine* engine) : m_engine(engine) {}
    ~LyricsPanel() override;

    static void show_settings_menu(SkinEngine* engine, ui::ViewHost* owner, int x, int y);

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override;
    void on_resize(int, int) override { invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    bool on_key_down(int key, unsigned mods) override;

private:
    struct Lyrics { std::vector<LyricLine> lines; bool synced = false; };

    void refresh_track();
    void start_fetch();
    void begin_sync();
    void sync_stamp();
    void sync_undo();
    void show_sync_menu(int x, int y);
    void save_sync(bool sidecar);
    bool paint_karaoke_line(gfx::Canvas& cv, size_t i, const gfx::Rect& r, double pos, int mode,
                            gfx::Color sung, gfx::Color unsung);

    SkinEngine* m_engine = nullptr;
    std::string m_key;
    std::string m_idsKey;
    unsigned m_version = 0;
    Lyrics m_lyrics;
    double m_scroll = 0;
    unsigned long long m_holdUntil = 0;
    bool m_settling = false, m_overLink = false;
    int m_idle = 0;
    gfx::Rect m_link;
    LrcSync m_sync;
};

}
