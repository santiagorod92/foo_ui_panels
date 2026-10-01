// Native viewer for the legacy "Album Art" panel type (fooAvA's cover inside the CD case when its
// `albumart` setting is 1): the now-playing track's front cover — folder image, else the
// album-art pipeline, like every other cover here — fitted to the panel without distortion,
// over what lies beneath it. Nothing playing / no art: the skin's `asset.nocover`, if any.
#pragma once
#include "../ui/view.h"

namespace pui {

class SkinEngine;

class AlbumArt : public ui::View {
public:
    explicit AlbumArt(SkinEngine* engine) : m_engine(engine) {}
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { invalidate(); }

private:
    SkinEngine* m_engine = nullptr;
};

} // namespace pui
