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

bool draw_image(HDC dc, const std::string& path, int x, int y, int w, int h, int alpha) {
    ensure_started();
    Gdiplus::Bitmap* bmp = load(path);
    if (!bmp) return false;
    if (w <= 0) w = (int)bmp->GetWidth();
    if (h <= 0) h = (int)bmp->GetHeight();

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::Rect dst(x, y, w, h);
    if (alpha >= 255) {
        g.DrawImage(bmp, dst, 0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(), Gdiplus::UnitPixel);
    } else {
        Gdiplus::ColorMatrix cm = {};
        cm.m[0][0] = cm.m[1][1] = cm.m[2][2] = 1.0f; cm.m[4][4] = 1.0f;
        cm.m[3][3] = alpha / 255.0f;
        Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
        g.DrawImage(bmp, dst, 0, 0, (int)bmp->GetWidth(), (int)bmp->GetHeight(),
                    Gdiplus::UnitPixel, &ia);
    }
    return true;
}

void images_shutdown() {
    g_cache.clear();
    if (g_started) { Gdiplus::GdiplusShutdown(g_token); g_started = false; }
}

} // namespace pui
