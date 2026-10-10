#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pui::gfx {

struct Color {
    uint8_t r = 0, g = 0, b = 0;
    constexpr Color() = default;
    constexpr Color(int r_, int g_, int b_) : r((uint8_t)r_), g((uint8_t)g_), b((uint8_t)b_) {}
    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
    constexpr Color scaled(int pct) const {
        auto s = [pct](int v) { int x = v * pct / 100; return x < 0 ? 0 : (x > 255 ? 255 : x); };
        return Color(s(r), s(g), s(b));
    }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool empty() const { return w <= 0 || h <= 0; }
    constexpr bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    static constexpr Rect ltrb(int l, int t, int r, int b) { return Rect{ l, t, r - l, b - t }; }
};

struct RectF { float x = 0, y = 0, w = 0, h = 0; };
struct PointF { float x = 0, y = 0; };

enum TextFlags : unsigned {
    kAlignLeft    = 0,
    kAlignCenter  = 1u << 0,
    kAlignRight   = 1u << 1,
    kVCenter      = 1u << 2,
    kSingleLine   = 1u << 3,
    kNoClip       = 1u << 4,
    kEndEllipsis  = 1u << 5,
    kWordEllipsis = 1u << 6,
    kWordWrap     = 1u << 7,
};

struct FontSpec {
    std::string face;
    float size = 9;
    bool points = true;
    bool bold = false, italic = false, underline = false;
    bool operator==(const FontSpec& o) const {
        return face == o.face && size == o.size && points == o.points && bold == o.bold &&
               italic == o.italic && underline == o.underline;
    }
};

class Image {
public:
    virtual ~Image() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual Color average_color() const = 0;
};
using ImagePtr = std::shared_ptr<Image>;

enum class Interp { High, Bilinear };

class Canvas {
public:
    virtual ~Canvas() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int dpi() const = 0;
    virtual double device_scale() const { return 1.0; }

    virtual void fill_rect(const Rect& r, Color c) = 0;
    virtual void fill_rect_alpha(const Rect& r, Color c, int alpha) = 0;
    virtual void frame_rect(const Rect& r, Color c) = 0;
    virtual void fill_round_rect(const Rect& r, int rx, int ry, Color c) = 0;
    virtual void gradient_v(const Rect& r, Color top, Color bottom) = 0;
    virtual void line(int x0, int y0, int x1, int y1, Color c) = 0;
    virtual void fill_polygon(const PointF* pts, int n, Color c) = 0;

    virtual bool set_font(const FontSpec& spec) = 0;
    virtual void draw_text(std::string_view text, const Rect& r, unsigned flags, Color c) = 0;
    virtual int text_width(std::string_view text) = 0;
    virtual int text_height(std::string_view text, int w, unsigned flags) = 0;
    virtual bool text_coverage(std::string_view text, const Rect& r, unsigned flags,
                               const Rect& area, std::vector<uint8_t>& mask) = 0;
    virtual void blend_argb(const Rect& dst, const uint32_t* px, int stride) = 0;

    virtual void draw_image(const Image& img, const RectF& dst, const RectF& src,
                            int alpha = 255, bool flip_v = false, Interp interp = Interp::High) = 0;
    virtual ImagePtr snapshot(const Rect& r) = 0;
};

ImagePtr decode_image_file(const std::string& utf8_path);
ImagePtr decode_image_memory(const void* data, size_t size);
void platform_images_shutdown();
ImagePtr resample_image(const Image& img, int pw, int ph);
bool encode_png_file(const Image& img, const std::string& utf8_path, int maxWidth);

}
