// gfx::Canvas over a CoreGraphics bitmap context (text via CoreText, images via ImageIO).
// The context is set up top-left-origin / y-down so coordinates match the Windows backend and
// the skin scripts; all drawing code compensates for CoreGraphics' native y-up where needed.
#pragma once
#include "../../gfx/canvas.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

namespace pui::gfx {

class CGCanvas : public Canvas {
public:
    // A w x h RGBA bitmap canvas in points (1 canvas unit = 1 point), backed by w*scale x h*scale
    // pixels: scale = the window's backingScaleFactor, so Retina displays get full resolution
    // while every coordinate the portable code sees stays in points.
    CGCanvas(int w, int h, double scale = 1.0);
    ~CGCanvas() override;
    CGContextRef context() const { return m_ctx; }
    // Current contents as an image (for putting on screen).
    CGImageRef copy_image() const { return CGBitmapContextCreateImage(m_ctx); }

    int width() const override { return m_w; }
    int height() const override { return m_h; }
    int dpi() const override { return 96; } // the skin's design size: 1pt font = 96/72 px, like Windows at 100%

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
    CGContextRef m_ctx = nullptr;
    int m_w, m_h;
    double m_scale = 1.0;
    CTFontRef m_font = nullptr;   // current font (owned)
    bool m_underline = false;
};

// Draw a CGImage into `dst` of a top-left-origin context (optionally mirrored vertically).
void draw_cgimage(CGContextRef ctx, CGImageRef img, CGRect dst, bool flip_v);

} // namespace pui::gfx
