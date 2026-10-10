#pragma once
#include "../fb2k.h"
#include "../gfx/canvas.h"
#include "keys.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pui::ui {

struct MouseEvent {
    int x = 0, y = 0;
    MouseButton button = MouseButton::Left;
    unsigned mods = 0;
    bool double_click = false;
};

class ViewHost;

class View {
public:
    virtual ~View() = default;
    virtual void paint(gfx::Canvas&) {}
    virtual void on_mouse_down(const MouseEvent&) {}
    virtual void on_mouse_up(const MouseEvent&) {}
    virtual void on_mouse_move(int , int , unsigned , bool ) {}
    virtual void on_mouse_leave() {}
    virtual void on_wheel(int , int , float ) {}
    virtual bool on_key_down(int , unsigned ) { return false; }
    virtual void on_timer(int ) {}
    virtual void on_resize(int , int ) {}
    virtual void on_visibility(bool ) {}
    virtual void on_focus() {}
    virtual void on_drop_files(const std::vector<std::string>& , int , int ) {}
    virtual void on_attached() {}
    virtual void on_destroy() {}

    ViewHost* host() const { return m_host; }
    void attach_host(ViewHost* h) { m_host = h; }
    void invalidate();

private:
    ViewHost* m_host = nullptr;
};

struct ViewOptions {
    Cursor cursor = Cursor::Arrow;
    bool double_clicks = false;
    int render_fps = 0;
    bool accept_files = false;
    std::string accessible_name;
    std::function<void(bool back)> on_tab;
};

inline void draw_focus_ring(gfx::Canvas& cv) {
    const gfx::Color c(0, 120, 215);
    cv.frame_rect(gfx::Rect{ 0, 0, cv.width(), cv.height() }, c);
    cv.frame_rect(gfx::Rect{ 1, 1, cv.width() - 2, cv.height() - 2 }, c);
}

class ViewHost {
public:
    virtual ~ViewHost() = default;
    virtual void invalidate() = 0;
    virtual void set_bounds(const gfx::Rect& r, bool to_top) = 0;
    virtual gfx::Rect bounds() const = 0;
    virtual void show(bool visible) = 0;
    virtual bool visible() const = 0;
    virtual void set_timer(int id, int ms) = 0;
    virtual void kill_timer(int id) = 0;
    virtual void capture_mouse(bool capture) = 0;
    virtual void focus() = 0;
    virtual void set_cursor(Cursor c) = 0;
    virtual void set_focus_ring(bool on) = 0;
    virtual void set_tooltip(const std::string& utf8) = 0;
    virtual void* native() const = 0;
};

struct TextFieldDelegate {
    virtual ~TextFieldDelegate() = default;
    virtual void on_text_changed() {}
    virtual void on_enter() {}
    virtual void on_escape() {}
};
class TextField {
public:
    virtual ~TextField() = default;
    virtual void set_bounds(const gfx::Rect& r) = 0;
    virtual std::string text() const = 0;
    virtual void set_text(const std::string& utf8) = 0;
    virtual void focus() = 0;
};
struct TextFieldStyle {
    gfx::Color text{ 236, 238, 245 }, background{ 26, 27, 32 };
    gfx::FontSpec font{ "Segoe UI", 13, false };
    std::string placeholder;
};

struct MenuItem {
    std::string label;
    int id = 0;
    bool checked = false, enabled = true, separator = false;
    std::vector<MenuItem> children;
    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
};
using Menu = std::vector<MenuItem>;

class MainWindow {
public:
    virtual ~MainWindow() = default;
    virtual void* native() const = 0;
    virtual gfx::Rect client_rect() const = 0;
    virtual void invalidate() = 0;
    virtual void set_titlebar_visible(bool visible) = 0;
    virtual void begin_window_drag() = 0;
    virtual void resize_client(int w, int h, const std::string& halign, const std::string& valign) = 0;
    virtual void set_menubar_visible(bool visible) = 0;
    virtual void show_main_menu() = 0;
    virtual void set_title(const std::string& utf8) = 0;
    virtual void set_tooltip(const std::string& utf8) = 0;
    virtual void set_tray(const std::string& utf8) = 0;
    virtual void set_always_on_top(bool on) = 0;
    virtual void apply_zoom() = 0;
    virtual double zoom() const = 0;
    virtual gfx::ImagePtr capture() { return nullptr; }
};

class EmbeddedPanel {
public:
    virtual ~EmbeddedPanel() = default;
    virtual void set_bounds(const gfx::Rect& r, bool to_top) = 0;
    virtual void show(bool visible) = 0;
};

std::unique_ptr<ViewHost> create_child_view(MainWindow& root, View* view, const ViewOptions& opts);
std::unique_ptr<ViewHost> create_popup_window(MainWindow& root, View* view, int w, int h,
                                              const std::string& title, std::function<void()> on_closed);
std::unique_ptr<TextField> create_text_field(ViewHost& owner, TextFieldDelegate* delegate,
                                             const TextFieldStyle& style);
std::unique_ptr<EmbeddedPanel> create_embedded_ui_element(MainWindow& root, const char* name);

int popup_menu(ViewHost* anchor, int x, int y, const Menu& menu);
int popup_menu_at_cursor(MainWindow& root, const Menu& menu);
void track_context_menu(ViewHost& anchor, int x, int y, const metadb_handle_list& tracks);
bool choose_color(ViewHost* owner, gfx::Color& c);
void message_box(ViewHost* owner, const std::string& title, const std::string& text);
std::string choose_folder(MainWindow& root, const std::string& title, const std::string& start);
void open_url(const std::string& url);
void open_file(const std::string& path);
void reveal_in_file_manager(const std::string& path);
bool copy_to_clipboard(const std::string& text);
std::string os_version();
std::string wine_version();
std::string home_dir();
void open_text_editor(MainWindow& root, const std::string& key, const std::string& title,
                      const std::string& text, std::function<bool(const std::string&)> apply);
void close_text_editor(const std::string& key);

inline void View::invalidate() { if (m_host) m_host->invalidate(); }

}
