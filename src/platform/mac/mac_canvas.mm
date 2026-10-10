#include "mac_canvas.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

namespace pui::gfx {

namespace {

CGColorSpaceRef srgb() {
    static CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return cs;
}

CGColorRef make_color(Color c, CGFloat a = 1.0) {
    return CGColorCreateSRGB(c.r / 255.0, c.g / 255.0, c.b / 255.0, a);
}

void set_fill(CGContextRef ctx, Color c, CGFloat a = 1.0) {
    CGContextSetRGBFillColor(ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, a);
}

CGRect to_cg(const Rect& r) { return CGRectMake(r.x, r.y, r.w, r.h); }

CGContextRef make_bitmap_context(int w, int h, double scale = 1.0) {
    const size_t pw = (size_t)std::max(1L, std::lround(w * scale)), ph = (size_t)std::max(1L, std::lround(h * scale));
    CGContextRef ctx = CGBitmapContextCreate(nullptr, pw, ph, 8, 0, srgb(),
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!ctx) return nullptr;
    CGContextTranslateCTM(ctx, 0, (CGFloat)ph);
    CGContextScaleCTM(ctx, scale, -scale);
    CGContextSetShouldSmoothFonts(ctx, false);
    return ctx;
}

class MacImage : public Image {
public:
    explicit MacImage(CGImageRef img, double scale = 1.0) : m_img(img), m_scale(scale) {}
    ~MacImage() override { if (m_img) CGImageRelease(m_img); }
    int width() const override { return (int)std::lround(CGImageGetWidth(m_img) / m_scale); }
    int height() const override { return (int)std::lround(CGImageGetHeight(m_img) / m_scale); }
    double scale() const { return m_scale; }
    Color average_color() const override {
        uint8_t px[4] = {};
        CGContextRef ctx = CGBitmapContextCreate(px, 1, 1, 8, 4, srgb(),
                                                 (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        if (!ctx) return Color();
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextDrawImage(ctx, CGRectMake(0, 0, 1, 1), m_img);
        CGContextRelease(ctx);
        return Color(px[2], px[1], px[0]);
    }
    CGImageRef image() const { return m_img; }
private:
    CGImageRef m_img;
    double m_scale;
};

std::map<std::string, std::pair<CTFontRef, bool>> g_fonts;
std::mutex g_fontMx;

bool same_name(CFStringRef a, const std::string& b) {
    if (!a) return false;
    NSString* s = (__bridge NSString*)a;
    return [s caseInsensitiveCompare:[NSString stringWithUTF8String:b.c_str()]] == NSOrderedSame;
}

bool windows_ui_face(const std::string& f) {
    static const char* faces[] = { "Segoe UI", "Tahoma", "MS Sans Serif", "Microsoft Sans Serif", "Verdana" };
    for (auto* x : faces) if (strcasecmp(f.c_str(), x) == 0) return true;
    return false;
}

std::pair<CTFontRef, bool> cached_font(const std::string& face, CGFloat px, bool bold, bool italic) {
    std::string key = face + "|" + std::to_string((int)std::lround(px * 4)) + (bold ? "b" : "") + (italic ? "i" : "");
    std::lock_guard<std::mutex> lk(g_fontMx);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    CFStringRef name = CFStringCreateWithCString(nullptr, face.c_str(), kCFStringEncodingUTF8);
    CTFontRef f = CTFontCreateWithName(name ? name : CFSTR("Helvetica"), px, nullptr);
    if (name) CFRelease(name);
    CFStringRef fam = CTFontCopyFamilyName(f), full = CTFontCopyFullName(f);
    bool matched = same_name(fam, face) || same_name(full, face);
    if (fam) CFRelease(fam);
    if (full) CFRelease(full);
    if (!matched && windows_ui_face(face)) {
        CFRelease(f);
        f = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, px, nullptr);
        matched = true;
    }
    CTFontSymbolicTraits traits = (bold ? kCTFontBoldTrait : 0) | (italic ? kCTFontItalicTrait : 0);
    if (traits) {
        if (CTFontRef t = CTFontCreateCopyWithSymbolicTraits(f, px, nullptr, traits, traits)) { CFRelease(f); f = t; }
    }
    g_fonts[key] = { f, matched };
    return { f, matched };
}

struct Layout {
    std::vector<CTLineRef> lines;
    CGFloat ascent = 0, lineH = 0;
    ~Layout() { for (auto l : lines) if (l) CFRelease(l); }
};

CFAttributedStringRef make_attr(NSString* s, CTFontRef font, CGColorRef color, bool underline) {
    NSMutableDictionary* a = [NSMutableDictionary dictionary];
    a[(__bridge id)kCTFontAttributeName] = (__bridge id)font;
    a[(__bridge id)kCTForegroundColorAttributeName] = (__bridge id)color;
    if (underline) a[(__bridge id)kCTUnderlineStyleAttributeName] = @(kCTUnderlineStyleSingle);
    return (CFAttributedStringRef)CFBridgingRetain([[NSAttributedString alloc] initWithString:s attributes:a]);
}

void build_layout(Layout& L, std::string_view text, CTFontRef font, bool underline, CGColorRef color,
                  int w, unsigned flags) {
    L.ascent = std::ceil(CTFontGetAscent(font));
    L.lineH = L.ascent + std::ceil(CTFontGetDescent(font));
    NSString* all = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    if (!all) all = @"";
    all = [all stringByReplacingOccurrencesOfString:@"\r" withString:@""];
    NSArray<NSString*>* paras = (flags & kSingleLine)
        ? @[ [all stringByReplacingOccurrencesOfString:@"\n" withString:@" "] ]
        : [all componentsSeparatedByString:@"\n"];
    const bool ellipsis = (flags & (kEndEllipsis | kWordEllipsis)) && !(flags & kWordWrap);
    for (NSString* p in paras) {
        if (p.length == 0) { L.lines.push_back(nullptr); continue; }
        CFAttributedStringRef as = make_attr(p, font, color, underline);
        if ((flags & kWordWrap) && w > 0) {
            CTTypesetterRef ts = CTTypesetterCreateWithAttributedString(as);
            CFIndex start = 0, len = (CFIndex)p.length;
            while (start < len) {
                CFIndex n = CTTypesetterSuggestLineBreak(ts, start, w);
                if (n <= 0) n = 1;
                L.lines.push_back(CTTypesetterCreateLine(ts, CFRangeMake(start, n)));
                start += n;
            }
            CFRelease(ts);
        } else {
            CTLineRef line = CTLineCreateWithAttributedString(as);
            if (ellipsis && w > 0 && CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) > w) {
                CFAttributedStringRef tok = make_attr(@"…", font, color, underline);
                CTLineRef token = CTLineCreateWithAttributedString(tok);
                CTLineRef t = CTLineCreateTruncatedLine(line, w, kCTLineTruncationEnd, token);
                CFRelease(token); CFRelease(tok);
                if (t) { CFRelease(line); line = t; }
            }
            L.lines.push_back(line);
        }
        CFRelease(as);
    }
}

void draw_layout(CGContextRef ctx, const Layout& L, const Rect& r, unsigned flags) {
    CGContextSaveGState(ctx);
    if (!(flags & kNoClip)) CGContextClipToRect(ctx, to_cg(r));
    CGFloat y0 = r.y;
    if ((flags & kVCenter) && (flags & kSingleLine)) y0 = r.y + std::floor((r.h - L.lineH) / 2);
    CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));
    for (size_t i = 0; i < L.lines.size(); ++i) {
        CTLineRef line = L.lines[i];
        if (!line) continue;
        CGFloat lw = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
        CGFloat x = r.x;
        if (flags & kAlignCenter) x = r.x + std::floor((r.w - lw) / 2);
        else if (flags & kAlignRight) x = r.x + r.w - lw;
        CGContextSetTextPosition(ctx, x, y0 + i * L.lineH + L.ascent);
        CTLineDraw(line, ctx);
    }
    CGContextRestoreGState(ctx);
}

}

void draw_cgimage(CGContextRef ctx, CGImageRef img, CGRect dst, bool flip_v) {
    if (flip_v) { CGContextDrawImage(ctx, dst, img); return; }
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, dst.origin.y + dst.size.height);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, CGRectMake(dst.origin.x, 0, dst.size.width, dst.size.height), img);
    CGContextRestoreGState(ctx);
}

static ImagePtr from_source(CGImageSourceRef src) {
    if (!src) return nullptr;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    return img ? std::make_shared<MacImage>(img) : nullptr;
}

ImagePtr decode_image_file(const std::string& utf8_path) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)utf8_path.c_str(),
                                                           (CFIndex)utf8_path.size(), false);
    if (!url) return nullptr;
    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    return from_source(src);
}

ImagePtr resample_image(const Image& img, int pw, int ph) {
    auto* mi = dynamic_cast<const MacImage*>(&img);
    if (!mi || !mi->image() || pw <= 0 || ph <= 0) return nullptr;
    CGContextRef ctx = CGBitmapContextCreate(nullptr, (size_t)pw, (size_t)ph, 8, 0, srgb(),
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!ctx) return nullptr;
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, pw, ph), mi->image());
    CGImageRef out = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return out ? std::make_shared<MacImage>(out) : nullptr;
}

ImagePtr image_from_cgimage(CGImageRef img, double scale) {
    return img ? std::make_shared<MacImage>(img, scale) : nullptr;
}

bool encode_png_file(const Image& img, const std::string& utf8_path, int maxWidth) {
    auto* mi = dynamic_cast<const MacImage*>(&img);
    if (!mi || !mi->image()) return false;
    CGImageRef src = mi->image();
    const size_t sw = CGImageGetWidth(src), sh = CGImageGetHeight(src);
    if (!sw || !sh) return false;
    const size_t tw = std::min(sw, (size_t)std::max(1, maxWidth)), th = std::max<size_t>(1, sh * tw / sw);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, tw, th, 8, 0, srgb(),
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!ctx) return false;
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, tw, th), src);
    CGImageRef thumb = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (!thumb) return false;
    bool ok = false;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)utf8_path.c_str(),
                                                           (CFIndex)utf8_path.size(), false);
    if (url) {
        if (CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr)) {
            CGImageDestinationAddImage(dst, thumb, nullptr);
            ok = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
        CFRelease(url);
    }
    CGImageRelease(thumb);
    return ok;
}

ImagePtr decode_image_memory(const void* data, size_t size) {
    CFDataRef d = CFDataCreate(nullptr, (const UInt8*)data, (CFIndex)size);
    if (!d) return nullptr;
    CGImageSourceRef src = CGImageSourceCreateWithData(d, nullptr);
    CFRelease(d);
    return from_source(src);
}

void platform_images_shutdown() {
    std::lock_guard<std::mutex> lk(g_fontMx);
    for (auto& kv : g_fonts) if (kv.second.first) CFRelease(kv.second.first);
    g_fonts.clear();
}

CGCanvas::CGCanvas(int w, int h, double scale) : m_w(w), m_h(h), m_scale(scale > 0 ? scale : 1.0) {
    m_ctx = make_bitmap_context(w, h, m_scale);
}

CGCanvas::~CGCanvas() {
    if (m_ctx) CGContextRelease(m_ctx);
}

void CGCanvas::fill_rect(const Rect& r, Color c) {
    if (r.empty()) return;
    set_fill(m_ctx, c);
    CGContextFillRect(m_ctx, to_cg(r));
}

void CGCanvas::fill_rect_alpha(const Rect& r, Color c, int alpha) {
    if (r.empty() || alpha <= 0) return;
    set_fill(m_ctx, c, std::min(alpha, 255) / 255.0);
    CGContextFillRect(m_ctx, to_cg(r));
}

void CGCanvas::frame_rect(const Rect& r, Color c) {
    if (r.empty()) return;
    fill_rect(Rect{ r.x, r.y, r.w, 1 }, c);
    fill_rect(Rect{ r.x, r.bottom() - 1, r.w, 1 }, c);
    fill_rect(Rect{ r.x, r.y, 1, r.h }, c);
    fill_rect(Rect{ r.right() - 1, r.y, 1, r.h }, c);
}

void CGCanvas::fill_round_rect(const Rect& r, int rx, int ry, Color c) {
    if (r.empty()) return;
    CGFloat cw = std::min<CGFloat>(rx / 2.0, r.w / 2.0), ch = std::min<CGFloat>(ry / 2.0, r.h / 2.0);
    CGPathRef p = CGPathCreateWithRoundedRect(to_cg(r), std::max<CGFloat>(0, cw), std::max<CGFloat>(0, ch), nullptr);
    set_fill(m_ctx, c);
    CGContextAddPath(m_ctx, p);
    CGContextFillPath(m_ctx);
    CGPathRelease(p);
}

void CGCanvas::gradient_v(const Rect& r, Color top, Color bottom) {
    if (r.empty()) return;
    CGFloat comps[8] = { top.r / 255.0, top.g / 255.0, top.b / 255.0, 1,
                         bottom.r / 255.0, bottom.g / 255.0, bottom.b / 255.0, 1 };
    CGFloat locs[2] = { 0, 1 };
    CGGradientRef g = CGGradientCreateWithColorComponents(srgb(), comps, locs, 2);
    CGContextSaveGState(m_ctx);
    CGContextClipToRect(m_ctx, to_cg(r));
    CGContextDrawLinearGradient(m_ctx, g, CGPointMake(0, r.y), CGPointMake(0, r.bottom()), 0);
    CGContextRestoreGState(m_ctx);
    CGGradientRelease(g);
}

void CGCanvas::line(int x0, int y0, int x1, int y1, Color c) {
    if (y0 == y1) { fill_rect(Rect{ std::min(x0, x1), y0, std::abs(x1 - x0), 1 }, c); return; }
    if (x0 == x1) { fill_rect(Rect{ x0, std::min(y0, y1), 1, std::abs(y1 - y0) }, c); return; }
    CGContextSetRGBStrokeColor(m_ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1);
    CGContextSetLineWidth(m_ctx, 1);
    CGContextMoveToPoint(m_ctx, x0 + 0.5, y0 + 0.5);
    CGContextAddLineToPoint(m_ctx, x1 + 0.5, y1 + 0.5);
    CGContextStrokePath(m_ctx);
}

void CGCanvas::fill_polygon(const PointF* pts, int n, Color c) {
    if (n < 3) return;
    CGContextMoveToPoint(m_ctx, pts[0].x, pts[0].y);
    for (int i = 1; i < n; ++i) CGContextAddLineToPoint(m_ctx, pts[i].x, pts[i].y);
    CGContextClosePath(m_ctx);
    set_fill(m_ctx, c);
    CGContextFillPath(m_ctx);
}

bool CGCanvas::set_font(const FontSpec& spec) {
    CGFloat px = spec.points ? spec.size * dpi() / 72.0 : spec.size;
    auto [font, matched] = cached_font(spec.face, std::max<CGFloat>(1, std::round(px)), spec.bold, spec.italic);
    if (m_font) CFRelease(m_font);
    m_font = (CTFontRef)CFRetain(font);
    m_underline = spec.underline;
    return matched;
}

void CGCanvas::draw_text(std::string_view text, const Rect& r, unsigned flags, Color c) {
    if (!m_font) set_font(FontSpec{ "Helvetica", 12, false });
    CGColorRef col = make_color(c);
    Layout L;
    build_layout(L, text, m_font, m_underline, col, r.w, flags);
    draw_layout(m_ctx, L, r, flags);
    CGColorRelease(col);
}

int CGCanvas::text_width(std::string_view text) {
    if (!m_font) set_font(FontSpec{ "Helvetica", 12, false });
    CGColorRef col = make_color(Color());
    Layout L;
    build_layout(L, text, m_font, false, col, 0, kSingleLine);
    CGColorRelease(col);
    return L.lines.empty() || !L.lines[0] ? 0
         : (int)std::ceil(CTLineGetTypographicBounds(L.lines[0], nullptr, nullptr, nullptr));
}

int CGCanvas::text_height(std::string_view text, int w, unsigned flags) {
    if (!m_font) set_font(FontSpec{ "Helvetica", 12, false });
    CGColorRef col = make_color(Color());
    Layout L;
    build_layout(L, text.empty() ? std::string_view(" ") : text, m_font, false, col, w, flags);
    CGColorRelease(col);
    return (int)std::ceil(std::max<size_t>(1, L.lines.size()) * L.lineH);
}

bool CGCanvas::text_coverage(std::string_view text, const Rect& r, unsigned flags,
                             const Rect& area, std::vector<uint8_t>& mask) {
    const int W = area.w, H = area.h;
    if (W <= 0 || H <= 0 || !m_font) return false;
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef g = CGBitmapContextCreate(nullptr, W, H, 8, 0, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if (!g) return false;
    CGContextTranslateCTM(g, 0, H);
    CGContextScaleCTM(g, 1, -1);
    CGContextTranslateCTM(g, -area.x, -area.y);
    CGColorRef white = make_color(Color(255, 255, 255));
    Layout L;
    build_layout(L, text, m_font, m_underline, white, r.w, flags);
    draw_layout(g, L, r, flags);
    CGColorRelease(white);
    const uint8_t* px = (const uint8_t*)CGBitmapContextGetData(g);
    const size_t stride = CGBitmapContextGetBytesPerRow(g);
    mask.assign((size_t)W * H, 0);
    for (int y = 0; y < H; ++y) memcpy(&mask[(size_t)y * W], px + (size_t)y * stride, (size_t)W);
    CGContextRelease(g);
    return true;
}

void CGCanvas::blend_argb(const Rect& dst, const uint32_t* src, int stride) {
    if (dst.empty()) return;
    std::vector<uint32_t> buf((size_t)dst.w * dst.h);
    for (int y = 0; y < dst.h; ++y) memcpy(&buf[(size_t)y * dst.w], src + (size_t)y * stride, (size_t)dst.w * 4);
    CFDataRef d = CFDataCreate(nullptr, (const UInt8*)buf.data(), (CFIndex)(buf.size() * 4));
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(d);
    CGImageRef img = CGImageCreate(dst.w, dst.h, 8, 32, (size_t)dst.w * 4, srgb(),
                                   (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
                                   prov, nullptr, false, kCGRenderingIntentDefault);
    if (img) { draw_cgimage(m_ctx, img, to_cg(dst), false); CGImageRelease(img); }
    CGDataProviderRelease(prov);
    CFRelease(d);
}

void CGCanvas::draw_image(const Image& img, const RectF& dst, const RectF& srcIn,
                          int alpha, bool flip_v, Interp interp) {
    auto* mi = dynamic_cast<const MacImage*>(&img);
    if (!mi || dst.w <= 0 || dst.h <= 0 || alpha <= 0) return;
    CGImageRef ci = mi->image();
    CGImageRef sub = nullptr;
    if (srcIn.w > 0 && srcIn.h > 0 &&
        (srcIn.x > 0 || srcIn.y > 0 || srcIn.w < img.width() || srcIn.h < img.height())) {
        const CGFloat s = mi->scale();
        CGRect sr = CGRectMake(std::floor(srcIn.x * s), std::floor(srcIn.y * s),
                               std::max<CGFloat>(1, std::round(srcIn.w * s)), std::max<CGFloat>(1, std::round(srcIn.h * s)));
        sub = CGImageCreateWithImageInRect(ci, sr);
        if (sub) ci = sub;
    }
    CGContextSaveGState(m_ctx);
    CGContextSetAlpha(m_ctx, std::min(alpha, 255) / 255.0);
    CGContextSetInterpolationQuality(m_ctx, interp == Interp::High ? kCGInterpolationHigh : kCGInterpolationMedium);
    draw_cgimage(m_ctx, ci, CGRectMake(dst.x, dst.y, dst.w, dst.h), flip_v);
    CGContextRestoreGState(m_ctx);
    if (sub) CGImageRelease(sub);
}

ImagePtr CGCanvas::snapshot(const Rect& r) {
    if (r.empty() || !m_ctx) return nullptr;
    CGImageRef full = CGBitmapContextCreateImage(m_ctx);
    if (!full) return nullptr;
    const CGFloat s = m_scale;
    CGImageRef part = (r.x == 0 && r.y == 0 && r.w == m_w && r.h == m_h)
        ? (CGImageRef)CGImageRetain(full)
        : CGImageCreateWithImageInRect(full, CGRectMake(r.x * s, r.y * s, r.w * s, r.h * s));
    CGImageRelease(full);
    return part ? std::make_shared<MacImage>(part, m_scale) : nullptr;
}

}
