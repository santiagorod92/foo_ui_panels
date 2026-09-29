// Platform-free 2D drawing interface. Everything that paints (the skin interpreter, the native
// panels) talks to a gfx::Canvas; the platform layer supplies the implementation (Windows: GDI +
// GDI+ in src/platform/win/gdi_canvas.cpp; macOS: CoreGraphics/CoreText) and image decoding.
// No HDC/HWND/CGContext may leak through this header.
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
    // Scale every channel by pct/100 (clamped) — darken (<100) or brighten (>100).
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

// Text layout flags (a subset of what DrawText offers, the ones the skin + panels use).
enum TextFlags : unsigned {
    kAlignLeft    = 0,
    kAlignCenter  = 1u << 0,
    kAlignRight   = 1u << 1,
    kVCenter      = 1u << 2, // only meaningful together with kSingleLine
    kSingleLine   = 1u << 3,
    kNoClip       = 1u << 4,
    kEndEllipsis  = 1u << 5,
    kWordEllipsis = 1u << 6,
    kWordWrap     = 1u << 7,
};

// Font request. `size` is in typographic points when `points` is true (converted with the
// canvas' dpi(), like the legacy skin's $font sizes), else in pixels (character height).
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

// A decoded bitmap (platform-owned pixels). Shared, immutable once created.
class Image {
public:
    virtual ~Image() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    // Mean colour of the whole image (e.g. to tint a highlight to a wallpaper).
    virtual Color average_color() const = 0;
};
using ImagePtr = std::shared_ptr<Image>;

enum class Interp { High, Bilinear };

class Canvas {
public:
    virtual ~Canvas() = default;

    // Size of the backing surface in pixels (drawing outside it is clipped).
    virtual int width() const = 0;
    virtual int height() const = 0;
    // Dots per inch used to turn point font sizes into pixels (96 = the skin's design size).
    virtual int dpi() const = 0;

    // --- shapes ---
    virtual void fill_rect(const Rect& r, Color c) = 0;
    virtual void fill_rect_alpha(const Rect& r, Color c, int alpha) = 0; // alpha 0..255
    virtual void frame_rect(const Rect& r, Color c) = 0;                 // 1px outline inside r
    virtual void fill_round_rect(const Rect& r, int rx, int ry, Color c) = 0;
    virtual void gradient_v(const Rect& r, Color top, Color bottom) = 0;
    virtual void line(int x0, int y0, int x1, int y1, Color c) = 0;      // 1px, end point excluded
    virtual void fill_polygon(const PointF* pts, int n, Color c) = 0;

    // --- text (UTF-8) ---
    // Selects the font for subsequent text calls. Returns false if the platform had to substitute
    // a different face than `spec.face` (the caller may then retry with an alias).
    virtual bool set_font(const FontSpec& spec) = 0;
    virtual void draw_text(std::string_view text, const Rect& r, unsigned flags, Color c) = 0;
    virtual int text_width(std::string_view text) = 0; // single line, current font
    // Height the text needs when laid out in width `w` with `flags` (kWordWrap wraps).
    virtual int text_height(std::string_view text, int w, unsigned flags) = 0;
    // Glyph coverage of `text` laid out in `r` (canvas coords), rendered into an 8-bit mask
    // covering `area` (canvas coords, row-major, area.w*area.h). Used for glow halos.
    virtual bool text_coverage(std::string_view text, const Rect& r, unsigned flags,
                               const Rect& area, std::vector<uint8_t>& mask) = 0;
    // Alpha-blend premultiplied 0xAARRGGBB pixels (stride in pixels) over `dst`.
    virtual void blend_argb(const Rect& dst, const uint32_t* px, int stride) = 0;

    // --- images ---
    // Draw the `src` part (image pixels; w/h <= 0 = whole image) of `img` into `dst`.
    // flip_v mirrors vertically (reflections). alpha 0..255.
    virtual void draw_image(const Image& img, const RectF& dst, const RectF& src,
                            int alpha = 255, bool flip_v = false, Interp interp = Interp::High) = 0;
    // Copy of the current contents of `r` (e.g. the master canvas frame that transparent panels
    // show through).
    virtual ImagePtr snapshot(const Rect& r) = 0;
};

// --- platform-supplied image decoding ---
ImagePtr decode_image_file(const std::string& utf8_path);            // nullptr on failure
ImagePtr decode_image_memory(const void* data, size_t size);         // jpeg/png/... bytes
void platform_images_shutdown();                                     // release decoder state

} // namespace pui::gfx
