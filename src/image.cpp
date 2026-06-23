#include "image.h"
#include <objidl.h>
#include <gdiplus.h>
#include <map>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

namespace pui {

namespace {
ULONG_PTR g_token = 0;
bool g_started = false;
std::map<std::string, std::shared_ptr<Gdiplus::Bitmap>> g_cache;

void ensure_started() {
    if (g_started) return;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_token, &in, nullptr) == Gdiplus::Ok)
        g_started = true;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Resolve a wildcard path (e.g. C:\Album\*folder*.jpg -> C:\Album\Folder.jpg).
std::string resolve_wildcard(const std::string& p) {
    if (p.find('*') == std::string::npos && p.find('?') == std::string::npos) return p;
    std::string q = p; for (auto& c : q) if (c == '/') c = '\\';
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(q.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return p;
    FindClose(h);
    auto slash = q.find_last_of('\\');
    std::string dir = (slash == std::string::npos) ? std::string() : q.substr(0, slash + 1);
    return dir + fd.cFileName;
}

Gdiplus::Bitmap* load(const std::string& rawpath) {
    std::string path = resolve_wildcard(rawpath);
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return it->second.get();
    auto bmp = std::make_shared<Gdiplus::Bitmap>(widen(path).c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) { g_cache[path] = nullptr; return nullptr; }
    g_cache[path] = bmp;
    return bmp.get();
}
} // namespace

bool draw_image(HDC dc, const std::string& path, int x, int y, int w, int h, int alpha, int rotateflip) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp) return false;
    const int iw = (int)bmp->GetWidth(), ih = (int)bmp->GetHeight();
    if (w <= 0) w = iw;
    if (h <= 0) h = ih;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    Gdiplus::ImageAttributes ia; Gdiplus::ImageAttributes* pia = nullptr;
    if (alpha < 255) {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f;
        cm.m[3][3] = alpha / 255.0f;
        ia.SetColorMatrix(&cm); pia = &ia;
    }

    // RotateFlipType 6 == Rotate180FlipX == vertical mirror (used for reflections).
    // Map via a destination parallelogram so the cached bitmap isn't mutated.
    Gdiplus::Point dest[3];
    if (rotateflip == 6) {
        dest[0] = Gdiplus::Point(x, y + h);   // image top-left -> bottom
        dest[1] = Gdiplus::Point(x + w, y + h);
        dest[2] = Gdiplus::Point(x, y);       // image bottom-left -> top
    } else {
        dest[0] = Gdiplus::Point(x, y);
        dest[1] = Gdiplus::Point(x + w, y);
        dest[2] = Gdiplus::Point(x, y + h);
    }
    g.DrawImage(bmp, dest, 3, 0, 0, iw, ih, Gdiplus::UnitPixel, pia);
    return true;
}

void fill_gradient_v(HDC dc, int x, int y, int w, int h, COLORREF base) {
    if (w <= 0 || h <= 0) return;
    auto sc = [](int v, double f) { int r = (int)(v * f); return r < 0 ? 0 : (r > 255 ? 255 : r); };
    COLORREF top = RGB(sc(GetRValue(base),1.7), sc(GetGValue(base),1.7), sc(GetBValue(base),1.7));
    COLORREF bot = RGB(sc(GetRValue(base),0.55), sc(GetGValue(base),0.55), sc(GetBValue(base),0.55));
    TRIVERTEX v[2] = {
        { x,     y,     (COLOR16)(GetRValue(top)<<8), (COLOR16)(GetGValue(top)<<8), (COLOR16)(GetBValue(top)<<8), 0 },
        { x + w, y + h, (COLOR16)(GetRValue(bot)<<8), (COLOR16)(GetGValue(bot)<<8), (COLOR16)(GetBValue(bot)<<8), 0 },
    };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

void images_shutdown() {
    g_cache.clear();
    if (g_started) { Gdiplus::GdiplusShutdown(g_token); g_started = false; }
}

} // namespace pui
