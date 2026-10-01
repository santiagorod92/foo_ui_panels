#include "gdi_canvas.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <map>
#include <mutex>

namespace pui::gfx {

namespace {

std::wstring widen(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

UINT dt_flags(unsigned f) {
    UINT d = DT_NOPREFIX;
    if (f & kAlignCenter) d |= DT_CENTER;
    if (f & kAlignRight) d |= DT_RIGHT;
    if (f & kVCenter) d |= DT_VCENTER;
    if (f & kSingleLine) d |= DT_SINGLELINE;
    if (f & kNoClip) d |= DT_NOCLIP;
    if (f & kEndEllipsis) d |= DT_END_ELLIPSIS;
    if (f & kWordEllipsis) d |= DT_WORD_ELLIPSIS;
    if (f & kWordWrap) d |= DT_WORDBREAK;
    return d;
}

RECT to_rect(const Rect& r) { return RECT{ r.x, r.y, r.x + r.w, r.y + r.h }; }

// --- GDI+ -----------------------------------------------------------------
ULONG_PTR g_gdipToken = 0;
bool g_gdipStarted = false;
std::mutex g_gdipMx;

void ensure_gdiplus() {
    std::lock_guard<std::mutex> lk(g_gdipMx);
    if (g_gdipStarted) return;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_gdipToken, &in, nullptr) == Gdiplus::Ok) g_gdipStarted = true;
}

// A decoded file/memory image.
class GdipImage : public Image {
public:
    explicit GdipImage(std::unique_ptr<Gdiplus::Bitmap> b) : m_bmp(std::move(b)) {}
    int width() const override { return (int)m_bmp->GetWidth(); }
    int height() const override { return (int)m_bmp->GetHeight(); }
    Color average_color() const override {
        Gdiplus::Bitmap tiny(1, 1, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&tiny);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(m_bmp.get(), Gdiplus::Rect(0, 0, 1, 1), 0, 0, width(), height(), Gdiplus::UnitPixel);
        Gdiplus::Color c;
        if (tiny.GetPixel(0, 0, &c) != Gdiplus::Ok) return Color();
        return Color(c.GetR(), c.GetG(), c.GetB());
    }
    Gdiplus::Bitmap* bitmap() const { return m_bmp.get(); }
private:
    std::unique_ptr<Gdiplus::Bitmap> m_bmp;
};

// A copy of canvas pixels (top-down 32bpp DIB). Drawn with StretchDIBits, which reads the bits
// directly — no DC selection — so a render thread may draw it while the UI thread makes another.
class DibImage : public Image {
public:
    DibImage(int w, int h) : m_w(w), m_h(h), m_px((size_t)w * h) {}
    int width() const override { return m_w; }
    int height() const override { return m_h; }
    Color average_color() const override {
        unsigned long long r = 0, g = 0, b = 0;
        for (uint32_t p : m_px) { r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; }
        const size_t n = m_px.empty() ? 1 : m_px.size();
        return Color((int)(r / n), (int)(g / n), (int)(b / n));
    }
    BITMAPINFO info() const {
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = m_w; bi.bmiHeader.biHeight = -m_h; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        return bi;
    }
    uint32_t* bits() { return m_px.data(); }
    const uint32_t* bits() const { return m_px.data(); }
private:
    int m_w, m_h;
    std::vector<uint32_t> m_px;
};

// --- font cache: the skin re-selects the same handful of fonts every frame ---
std::map<std::string, HFONT> g_fonts;
std::mutex g_fontMx;

HFONT cached_font(const std::wstring& face, int height, bool bold, bool italic, bool underline) {
    std::string key;
    key.reserve(face.size() * 2 + 16);
    for (wchar_t c : face) { key.push_back((char)(c & 0xFF)); key.push_back((char)(c >> 8)); }
    key += '|' + std::to_string(height) + (bold ? "b" : "") + (italic ? "i" : "") + (underline ? "u" : "");
    std::lock_guard<std::mutex> lk(g_fontMx);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    HFONT f = CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic ? TRUE : FALSE,
                          underline ? TRUE : FALSE, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face.c_str());
    g_fonts[key] = f;
    return f;
}

bool same_face(const wchar_t* got, const std::wstring& want) {
    auto lower = [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; };
    size_t i = 0;
    for (; got[i] && i < want.size(); ++i) if (lower(got[i]) != lower(want[i])) return false;
    return !got[i] && i == want.size();
}

} // namespace

// --- decoding (gfx::decode_image_*) -----------------------------------------
ImagePtr decode_image_file(const std::string& utf8_path) {
    ensure_gdiplus();
    auto bmp = std::make_unique<Gdiplus::Bitmap>(widen(utf8_path).c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) return nullptr;
    return std::make_shared<GdipImage>(std::move(bmp));
}

ImagePtr decode_image_memory(const void* data, size_t size) {
    ensure_gdiplus();
    ImagePtr out;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hg) return nullptr;
    if (void* p = GlobalLock(hg)) { memcpy(p, data, size); GlobalUnlock(hg); }
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hg, TRUE, &stream) == S_OK) {
        auto b = std::make_unique<Gdiplus::Bitmap>(stream);
        if (b->GetLastStatus() == Gdiplus::Ok) out = std::make_shared<GdipImage>(std::move(b));
        stream->Release(); // owns hg now (TRUE above), releases it too
    } else {
        GlobalFree(hg);
    }
    return out;
}

void platform_images_shutdown() {
    std::lock_guard<std::mutex> lk(g_gdipMx);
    if (g_gdipStarted) { Gdiplus::GdiplusShutdown(g_gdipToken); g_gdipStarted = false; }
}

void gdi_fonts_shutdown() {
    std::lock_guard<std::mutex> lk(g_fontMx);
    for (auto& kv : g_fonts) if (kv.second) DeleteObject(kv.second);
    g_fonts.clear();
}

// --- GdiCanvas -----------------------------------------------------------------
GdiCanvas::GdiCanvas(HDC dc, int w, int h) : m_dc(dc), m_w(w), m_h(h) {
    // The skin's design size, same as the macOS canvas: foobar2000 is DPI-aware, so at 150%
    // scaling LOGPIXELSY is 144 — point-sized fonts grew 1.5x while every coordinate the skin
    // lays them out in stayed in 96-dpi pixels, and text overflowed its boxes.
    m_dpi = 96;
    SetBkMode(m_dc, TRANSPARENT);
}

GdiCanvas::~GdiCanvas() {
    if (m_oldFont) SelectObject(m_dc, m_oldFont);
}

void GdiCanvas::fill_rect(const Rect& r, Color c) {
    if (r.empty()) return;
    RECT rc = to_rect(r);
    HBRUSH b = CreateSolidBrush(to_colorref(c)); FillRect(m_dc, &rc, b); DeleteObject(b);
}

void GdiCanvas::fill_rect_alpha(const Rect& r, Color c, int alpha) {
    if (r.empty() || alpha <= 0) return;
    if (alpha >= 255) { fill_rect(r, c); return; }
    HDC md = CreateCompatibleDC(m_dc);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) { unsigned char* px = (unsigned char*)bits; px[0] = c.b; px[1] = c.g; px[2] = c.r; px[3] = 255; }
    HGDIOBJ ob = SelectObject(md, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 }; // const alpha, no per-pixel
    AlphaBlend(m_dc, r.x, r.y, r.w, r.h, md, 0, 0, 1, 1, bf);
    SelectObject(md, ob); DeleteObject(bmp); DeleteDC(md);
}

void GdiCanvas::frame_rect(const Rect& r, Color c) {
    RECT rc = to_rect(r);
    HBRUSH b = CreateSolidBrush(to_colorref(c)); FrameRect(m_dc, &rc, b); DeleteObject(b);
}

void GdiCanvas::fill_round_rect(const Rect& r, int rx, int ry, Color c) {
    HBRUSH b = CreateSolidBrush(to_colorref(c)); HPEN pen = CreatePen(PS_SOLID, 1, to_colorref(c));
    HGDIOBJ ob = SelectObject(m_dc, b), op = SelectObject(m_dc, pen);
    RoundRect(m_dc, r.x, r.y, r.x + r.w, r.y + r.h, rx, ry);
    SelectObject(m_dc, ob); SelectObject(m_dc, op); DeleteObject(b); DeleteObject(pen);
}

void GdiCanvas::gradient_v(const Rect& r, Color c1, Color c2) {
    if (r.empty()) return;
    TRIVERTEX v[2] = {
        { r.x,       r.y,       (COLOR16)(c1.r << 8), (COLOR16)(c1.g << 8), (COLOR16)(c1.b << 8), 0 },
        { r.right(), r.bottom(), (COLOR16)(c2.r << 8), (COLOR16)(c2.g << 8), (COLOR16)(c2.b << 8), 0 },
    };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(m_dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void GdiCanvas::line(int x0, int y0, int x1, int y1, Color c) {
    HPEN pen = CreatePen(PS_SOLID, 1, to_colorref(c));
    HGDIOBJ op = SelectObject(m_dc, pen);
    MoveToEx(m_dc, x0, y0, nullptr); LineTo(m_dc, x1, y1);
    SelectObject(m_dc, op); DeleteObject(pen);
}

void GdiCanvas::fill_polygon(const PointF* pts, int n, Color c) {
    if (n < 3) return;
    std::vector<POINT> q(n);
    for (int i = 0; i < n; ++i) q[i] = POINT{ (LONG)pts[i].x, (LONG)pts[i].y };
    HBRUSH b = CreateSolidBrush(to_colorref(c)); HGDIOBJ ob = SelectObject(m_dc, b);
    HGDIOBJ op = SelectObject(m_dc, GetStockObject(NULL_PEN));
    Polygon(m_dc, q.data(), n);
    SelectObject(m_dc, ob); SelectObject(m_dc, op); DeleteObject(b);
}

bool GdiCanvas::set_font(const FontSpec& spec) {
    int h = spec.points ? -MulDiv((int)(spec.size + 0.5f), m_dpi, 72) : -(int)(spec.size + 0.5f);
    std::wstring face = widen(spec.face);
    HFONT f = cached_font(face, h, spec.bold, spec.italic, spec.underline);
    if (!f) return false;
    HGDIOBJ old = SelectObject(m_dc, f);
    if (!m_oldFont) m_oldFont = old;
    m_font = f;
    // GDI silently swaps in an arbitrary default for a face it can't find: report whether the
    // DC actually got the requested one.
    wchar_t got[LF_FACESIZE]; got[0] = 0;
    GetTextFaceW(m_dc, LF_FACESIZE, got);
    return same_face(got, face);
}

void GdiCanvas::draw_text(std::string_view text, const Rect& r, unsigned flags, Color c) {
    std::wstring w = widen(text);
    RECT rc = to_rect(r);
    SetTextColor(m_dc, to_colorref(c));
    DrawTextW(m_dc, w.c_str(), (int)w.size(), &rc, dt_flags(flags));
}

int GdiCanvas::text_width(std::string_view text) {
    std::wstring w = widen(text);
    SIZE sz{}; GetTextExtentPoint32W(m_dc, w.c_str(), (int)w.size(), &sz);
    return sz.cx;
}

int GdiCanvas::text_height(std::string_view text, int w, unsigned flags) {
    std::wstring ws = widen(text);
    if (ws.empty()) ws = L" ";
    RECT rc = { 0, 0, w, 0 };
    DrawTextW(m_dc, ws.c_str(), (int)ws.size(), &rc, dt_flags(flags) | DT_CALCRECT);
    return rc.bottom - rc.top;
}

bool GdiCanvas::text_coverage(std::string_view text, const Rect& r, unsigned flags,
                              const Rect& area, std::vector<uint8_t>& mask) {
    const int W = area.w, H = area.h;
    if (W <= 0 || H <= 0 || !m_font) return false;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC md = CreateCompatibleDC(m_dc);
    void* mbits = nullptr;
    HBITMAP mb = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &mbits, nullptr, 0);
    if (!mb) { DeleteDC(md); return false; }
    HGDIOBJ omb = SelectObject(md, mb);
    memset(mbits, 0, (size_t)W * H * 4);
    HGDIOBJ of = SelectObject(md, m_font);
    SetBkMode(md, TRANSPARENT); SetTextColor(md, RGB(255, 255, 255));
    RECT tr = { r.x - area.x, r.y - area.y, r.right() - area.x, r.bottom() - area.y };
    std::wstring w = widen(text);
    DrawTextW(md, w.c_str(), (int)w.size(), &tr, dt_flags(flags));
    SelectObject(md, of);
    // Coverage = max channel (ClearType renders coloured fringes).
    const uint32_t* px = (const uint32_t*)mbits;
    mask.assign((size_t)W * H, 0);
    for (size_t i = 0; i < mask.size(); ++i) {
        uint32_t c = px[i];
        mask[i] = (uint8_t)std::max({ (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF });
    }
    SelectObject(md, omb); DeleteObject(mb); DeleteDC(md);
    return true;
}

void GdiCanvas::blend_argb(const Rect& dst, const uint32_t* src, int stride) {
    if (dst.empty()) return;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = dst.w; bi.bmiHeader.biHeight = -dst.h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC md = CreateCompatibleDC(m_dc);
    void* bits = nullptr;
    HBITMAP mb = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mb) { DeleteDC(md); return; }
    for (int y = 0; y < dst.h; ++y)
        memcpy((uint32_t*)bits + (size_t)y * dst.w, src + (size_t)y * stride, (size_t)dst.w * 4);
    HGDIOBJ omb = SelectObject(md, mb);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(m_dc, dst.x, dst.y, dst.w, dst.h, md, 0, 0, dst.w, dst.h, bf);
    SelectObject(md, omb); DeleteObject(mb); DeleteDC(md);
}

void GdiCanvas::draw_image(const Image& img, const RectF& dst, const RectF& srcIn,
                           int alpha, bool flip_v, Interp interp) {
    if (dst.w <= 0 || dst.h <= 0 || alpha <= 0) return;
    RectF src = srcIn;
    if (src.w <= 0 || src.h <= 0) src = RectF{ 0, 0, (float)img.width(), (float)img.height() };

    std::unique_ptr<Gdiplus::Bitmap> tmp;
    Gdiplus::Bitmap* bmp = nullptr;
    if (auto* g = dynamic_cast<const GdipImage*>(&img)) {
        bmp = g->bitmap();
    } else if (auto* d = dynamic_cast<const DibImage*>(&img)) {
        // Canvas snapshots: plain copies go straight through GDI.
        if (alpha >= 255 && !flip_v) {
            // Point the DIB header at the source rows only (ySrc = 0 over the full header
            // height sidesteps StretchDIBits' bottom-up/top-down ySrc ambiguity).
            int sy = std::clamp((int)src.y, 0, d->height()), sh = std::min((int)src.h, d->height() - sy);
            if (sh <= 0) return;
            BITMAPINFO bi = d->info();
            bi.bmiHeader.biHeight = -sh;
            SetStretchBltMode(m_dc, COLORONCOLOR);
            StretchDIBits(m_dc, (int)dst.x, (int)dst.y, (int)dst.w, (int)dst.h,
                          (int)src.x, 0, (int)src.w, sh,
                          d->bits() + (size_t)sy * d->width(), &bi, DIB_RGB_COLORS, SRCCOPY);
            return;
        }
        ensure_gdiplus();
        tmp = std::make_unique<Gdiplus::Bitmap>(d->width(), d->height(), d->width() * 4,
                                                PixelFormat32bppRGB, (BYTE*)d->bits());
        bmp = tmp.get();
    }
    if (!bmp) return;

    Gdiplus::Graphics g(m_dc);
    Gdiplus::ImageAttributes ia; Gdiplus::ImageAttributes* pia = nullptr;
    if (interp == Interp::High) {
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    } else {
        g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        ia.SetWrapMode(Gdiplus::WrapModeTileFlipXY); // no edge bleed between adjacent strips
        pia = &ia;
    }
    if (alpha < 255) {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f;
        cm.m[3][3] = alpha / 255.0f;
        ia.SetColorMatrix(&cm); pia = &ia;
    }
    // Destination parallelogram: top-left, top-right, bottom-left of the source mapped to dst
    // (swapped vertically for flip_v, so the cached bitmap is never mutated).
    Gdiplus::PointF p[3];
    if (flip_v) {
        p[0] = Gdiplus::PointF(dst.x, dst.y + dst.h);
        p[1] = Gdiplus::PointF(dst.x + dst.w, dst.y + dst.h);
        p[2] = Gdiplus::PointF(dst.x, dst.y);
    } else {
        p[0] = Gdiplus::PointF(dst.x, dst.y);
        p[1] = Gdiplus::PointF(dst.x + dst.w, dst.y);
        p[2] = Gdiplus::PointF(dst.x, dst.y + dst.h);
    }
    g.DrawImage(bmp, p, 3, src.x, src.y, src.w, src.h, Gdiplus::UnitPixel, pia);
}

ImagePtr GdiCanvas::snapshot(const Rect& r) {
    if (r.empty()) return nullptr;
    auto img = std::make_shared<DibImage>(r.w, r.h);
    BITMAPINFO bi = img->info();
    HDC md = CreateCompatibleDC(m_dc);
    void* bits = nullptr;
    HBITMAP mb = CreateDIBSection(md, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mb) { DeleteDC(md); return nullptr; }
    HGDIOBJ omb = SelectObject(md, mb);
    BitBlt(md, 0, 0, r.w, r.h, m_dc, r.x, r.y, SRCCOPY);
    GdiFlush();
    memcpy(img->bits(), bits, (size_t)r.w * r.h * 4);
    SelectObject(md, omb); DeleteObject(mb); DeleteDC(md);
    return img;
}

} // namespace pui::gfx
