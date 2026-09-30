// gfx::Canvas over a GDI device context (text/shapes via GDI, images via GDI+).
#pragma once
#include "win_sdk.h"
#include "../../gfx/canvas.h"

namespace pui::gfx {

class GdiCanvas : public Canvas {
public:
    // Draws into `dc` (usually a memory DC holding a w x h bitmap). The DC's selected font is
    // restored on destruction.
    GdiCanvas(HDC dc, int w, int h);
    ~GdiCanvas() override;
    HDC dc() const { return m_dc; }

    int width() const override { return m_w; }
    int height() const override { return m_h; }
    int dpi() const override { return m_dpi; }

    void fill_rect(const Rect& r, Color c) override;
    void fill_rect_alpha(const Rect& r, Color c, int alpha) override;
    void frame_rect(const Rect& r, Color c) override;
    void fill_round_rect(const Rect& r, int rx, int ry, Color c) override;
    void gradient_v(const Rect& r, Color top, Color bottom) override;
    void line(int x0, int y0, int x1, int y1, Color c) override;
    void fill_polygon(const PointF* pts, int n, Color c) override;

    bool set_font(const FontSpec& spec) override;
    void draw_text(std::string_view text, const Rect& r, unsigned flags, Color c) override;
    int text_width(std::string_view text) override;
    int text_height(std::string_view text, int w, unsigned flags) override;
    bool text_coverage(std::string_view text, const Rect& r, unsigned flags,
                       const Rect& area, std::vector<uint8_t>& mask) override;
    void blend_argb(const Rect& dst, const uint32_t* px, int stride) override;

    void draw_image(const Image& img, const RectF& dst, const RectF& src,
                    int alpha, bool flip_v, Interp interp) override;
    ImagePtr snapshot(const Rect& r) override;

private:
    HDC m_dc;
    int m_w, m_h, m_dpi;
    HFONT m_font = nullptr;     // cached (owned by the font cache, never deleted here)
    HGDIOBJ m_oldFont = nullptr;
};

inline COLORREF to_colorref(Color c) { return RGB(c.r, c.g, c.b); }
inline Color from_colorref(COLORREF c) { return Color(GetRValue(c), GetGValue(c), GetBValue(c)); }

// Release cached fonts (component shutdown).
void gdi_fonts_shutdown();

} // namespace pui::gfx
