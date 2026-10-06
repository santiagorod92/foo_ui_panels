// Native viewer for the legacy "Album Art" panel type (fooAvA's cover inside the CD case when its
// `albumart` setting is 1): the now-playing track's front cover — folder image, else the
// album-art pipeline, like every other cover here — fitted to the panel without distortion,
// over what lies beneath it. Nothing playing / no art: the skin's `asset.nocover`, if any.
// A new cover cross-fades over the previous one instead of popping in.
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class AlbumArt : public ui::View {
public:
    explicit AlbumArt(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }
    void on_timer(int) override;
    void on_mouse_up(const ui::MouseEvent& e) override; // right-click: the cover menu

private:
    gfx::ImagePtr current_image() const;
    SkinEngine* m_engine = nullptr;
    gfx::ImagePtr m_cur, m_prev;          // shown now / fading out
    unsigned long long m_fadeStart = 0;   // tick_ms() when m_cur started fading in (0 = none)
};

} // namespace pui
