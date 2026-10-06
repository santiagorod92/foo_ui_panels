#include "cover_menu.h"
#include "../core/image_cache.h"
#include "../core/skin_engine.h"
#include <map>
#include <memory>

namespace pui {

namespace {

enum { kView, kOpenFile, kShowFolder, kSearch, kCount };

struct Cover {
    metadb_handle_ptr track;
    std::string file;   // the image file on disk ("" = art from tags / a stream)
    std::string native; // the track's own file ("" = not a local file)
    gfx::ImagePtr img;
};

Cover now_playing_cover() {
    Cover c;
    if (!playback_control::get()->get_now_playing(c.track)) return c;
    static titleformat_object::ptr tf;
    if (tf.is_empty()) titleformat_compiler::get()->compile_safe(tf, "$replace(%path%,%filename_ext%,*folder*.*)");
    pfc::string8 pattern;
    c.track->format_title(nullptr, pattern, tf, nullptr);
    const std::string file = resolve_wildcard(pattern.get_ptr());
    if (file.find_first_of("*?") == std::string::npos && file_exists(file)) c.file = file;
    pfc::string8 native;
    try { if (filesystem::g_get_native_path(c.track->get_path(), native)) c.native = native.get_ptr(); } catch (...) {}
    c.img = cover_image(pattern.get_ptr(), c.track);
    return c;
}

std::string url_encode(const std::string& s) {
    std::string out;
    for (unsigned char ch : s) {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out += (char)ch;
        else if (ch == ' ') out += '+';
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", ch); out += b; }
    }
    return out;
}

// The cover at full size in its own window (up to 800 px a side, resizable), black around it.
class CoverWindow : public ui::View {
public:
    explicit CoverWindow(gfx::ImagePtr img) : m_img(std::move(img)) {}
    void paint(gfx::Canvas& cv) override {
        const int W = cv.width(), H = cv.height();
        cv.fill_rect(gfx::Rect{ 0, 0, W, H }, gfx::Color());
        if (!m_img || m_img->width() <= 0 || m_img->height() <= 0) return;
        const double s = std::min((double)W / m_img->width(), (double)H / m_img->height());
        const float w = (float)(m_img->width() * s), h = (float)(m_img->height() * s);
        cv.draw_image(*m_img, gfx::RectF{ (W - w) / 2, (H - h) / 2, w, h }, gfx::RectF{});
    }
    void on_resize(int, int) override { invalidate(); }
private:
    gfx::ImagePtr m_img;
};

// Open viewers, until their window closes.
struct Viewer { std::unique_ptr<CoverWindow> view; std::unique_ptr<ui::ViewHost> host; };
std::map<int, Viewer> g_viewers;
int g_nextViewer = 0;

void show_viewer(SkinEngine* engine, const Cover& c) {
    if (!engine || !engine->main_window() || !c.img) return;
    const int id = g_nextViewer++;
    Viewer& v = g_viewers[id];
    v.view = std::make_unique<CoverWindow>(c.img);
    // At most 800 px a side (the window is in skin units, so zoom makes it bigger still).
    const double s = std::min(1.0, 800.0 / std::max(c.img->width(), c.img->height()));
    const int w = std::max(100, (int)(c.img->width() * s)), h = std::max(100, (int)(c.img->height() * s));
    pfc::string8 title;
    static titleformat_object::ptr tf;
    if (tf.is_empty()) titleformat_compiler::get()->compile_safe(tf, "[%album artist% - ]%album%");
    c.track->format_title(nullptr, title, tf, nullptr);
    v.host = ui::create_popup_window(*engine->main_window(), v.view.get(), w, h,
                                     title.is_empty() ? "Cover" : title.get_ptr(),
                                     [id] { g_viewers.erase(id); });
    if (!v.host) g_viewers.erase(id);
}

} // namespace

bool add_cover_menu_items(ui::Menu& menu, int firstId) {
    const Cover c = now_playing_cover();
    if (c.track.is_empty()) return false;
    auto item = [&](const char* label, int id, bool enabled) {
        ui::MenuItem m; m.label = label; m.id = firstId + id; m.enabled = enabled; menu.push_back(m);
    };
    item("View cover", kView, c.img != nullptr);
    item("Open cover file", kOpenFile, !c.file.empty());
    item("Show in folder", kShowFolder, !c.native.empty());
    item("Search for the cover online", kSearch, true);
    return true;
}

bool run_cover_menu_item(SkinEngine* engine, int id, int firstId) {
    if (id < firstId || id >= firstId + kCount) return false;
    const Cover c = now_playing_cover();
    if (c.track.is_empty()) return true;
    switch (id - firstId) {
    case kView: show_viewer(engine, c); break;
    case kOpenFile: if (!c.file.empty()) ui::open_file(c.file); break; // the default image viewer
    case kShowFolder: if (!c.native.empty()) ui::reveal_in_file_manager(c.native); break;
    case kSearch: {
        static titleformat_object::ptr tf;
        if (tf.is_empty()) titleformat_compiler::get()->compile_safe(tf, "[%album artist% ][%album%]");
        pfc::string8 q;
        c.track->format_title(nullptr, q, tf, nullptr);
        ui::open_url("https://www.google.com/search?tbm=isch&q=" + url_encode(std::string(q.get_ptr()) + " album cover"));
        break;
    }
    }
    return true;
}

} // namespace pui
