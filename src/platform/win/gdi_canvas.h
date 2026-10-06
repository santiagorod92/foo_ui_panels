// gfx::Canvas over a GDI device context (text/shapes via GDI, images via GDI+).
#pragma once
#include "win_sdk.h"
#include "../../gfx/canvas.h"

namespace Gdiplus { class Bitmap; }

namespace pui::gfx {

class GdiCanvas : public Canvas {
public:
    // Draws into `dc` (usually a memory DC holding a w x h DEVICE-pixel bitmap). The DC's
    // selected font is restored on destruction. scale != 1 (the zoom): callers draw in skin
    // units and a GDI world transform (GDI+: its own transform) maps them to device pixels —
    // width()/height() are then the device size / scale.
    GdiCanvas(HDC dc, int w, int h, double scale = 1.0);
    ~GdiCanvas() override;
    HDC dc() const { return m_dc; }

    int width() const override { return m_lw; }
    int height() const override { return m_lh; }
    double scale() const { return m_scale; }
    int dpi() const override { return m_dpi; }
    double device_scale() const override { return m_scale; }

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
    // The GDI+ half of draw_image (the DC's world transform is identity meanwhile).
    void draw_image_gdip(Gdiplus::Bitmap& bmp, const RectF& dst, const RectF& src,
                         int alpha, bool flip_v, Interp interp);
    HDC m_dc;
    int m_dpi;
    double m_scale = 1.0;
    int m_lw, m_lh; // logical (skin-unit) size
    int m_oldMode = 0;
    XFORM m_oldXform{};
    HFONT m_font = nullptr;     // cached (owned by the font cache, never deleted here)
    HGDIOBJ m_oldFont = nullptr;
};

inline COLORREF to_colorref(Color c) { return RGB(c.r, c.g, c.b); }
inline Color from_colorref(COLORREF c) { return Color(GetRValue(c), GetGValue(c), GetBValue(c)); }

// Release cached fonts (component shutdown).
void gdi_fonts_shutdown();

} // namespace pui::gfx
