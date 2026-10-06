// Platform-free view/windowing layer. A native panel is a ui::View (its logic: paint + input,
// written once) hosted by a ui::ViewHost (the platform's child window: an HWND on Windows, an
// NSView on macOS). Everything a panel needs from the windowing system — repaint, timers, menus,
// the track context menu, a text field — goes through the interfaces here; the implementations
// live under src/platform/<os>/.
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
    int x = 0, y = 0;          // view-local
    MouseButton button = MouseButton::Left;
    unsigned mods = 0;
    bool double_click = false;
};

class ViewHost;

class View {
public:
    virtual ~View() = default;
    // paint() must fully cover the view (hosts don't clear it). Views with
    // ViewOptions::render_fps set are painted from a background thread — keep paint() thread-safe.
    virtual void paint(gfx::Canvas&) {}
    virtual void on_mouse_down(const MouseEvent&) {}
    virtual void on_mouse_up(const MouseEvent&) {}
    virtual void on_mouse_move(int /*x*/, int /*y*/, unsigned /*mods*/, bool /*left_down*/) {}
    virtual void on_mouse_leave() {}
    virtual void on_wheel(int /*x*/, int /*y*/, float /*notches*/) {} // +1 = one notch away from the user
    virtual bool on_key_down(int /*key*/, unsigned /*mods*/) { return false; }
    virtual void on_timer(int /*id*/) {}
    virtual void on_resize(int /*w*/, int /*h*/) {}
    virtual void on_visibility(bool /*shown*/) {}
    virtual void on_focus() {}
    // Files/folders dropped from the OS file manager (UTF-8 native paths), at view-local (x,y).
    // Only delivered to views created with ViewOptions::accept_files.
    virtual void on_drop_files(const std::vector<std::string>& /*paths*/, int /*x*/, int /*y*/) {}
    // The host exists now (attach_host done): start timers etc. here, not in the constructor.
    virtual void on_attached() {}
    // The host is about to be destroyed: stop anything that could still call back into it.
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
    // > 0: the host repaints the view at this rate from a dedicated render thread instead of on
    // demand (the spectrum analyser — UI-thread repaints of the other panels stalled it).
    int render_fps = 0;
    // Files dragged from Explorer/Finder can be dropped on this view (View::on_drop_files).
    // Elsewhere in the window they go to the main window, which adds them to the active playlist.
    bool accept_files = false;
    // Accessibility: what a screen reader calls this panel ("Playlist", "Album browser"…).
    std::string accessible_name;
    // Tab / Shift+Tab move the keyboard focus on (back) when the view doesn't use the key itself.
    std::function<void(bool back)> on_tab;
};

// The keyboard-focus marker a host draws over a view that got the focus from the keyboard.
inline void draw_focus_ring(gfx::Canvas& cv) {
    const gfx::Color c(0, 120, 215);
    cv.frame_rect(gfx::Rect{ 0, 0, cv.width(), cv.height() }, c);
    cv.frame_rect(gfx::Rect{ 1, 1, cv.width() - 2, cv.height() - 2 }, c);
}

class ViewHost {
public:
    virtual ~ViewHost() = default;
    virtual void invalidate() = 0;
    // Position within the root window's client area; to_top also raises it above its siblings.
    virtual void set_bounds(const gfx::Rect& r, bool to_top) = 0;
    virtual gfx::Rect bounds() const = 0;
    virtual void show(bool visible) = 0;
    virtual bool visible() const = 0;
    virtual void set_timer(int id, int ms) = 0;
    virtual void kill_timer(int id) = 0;
    virtual void capture_mouse(bool capture) = 0;
    virtual void focus() = 0;
    virtual void set_cursor(Cursor c) = 0; // the pointer shape over this view from now on
    // Show the focus ring (draw_focus_ring) while this view keeps the focus: set when the focus
    // arrives from the keyboard; a click or losing the focus clears it.
    virtual void set_focus_ring(bool on) = 0;
    // Tooltip shown while the mouse rests over this view (UTF-8; empty = none). Views update it
    // as the mouse moves between their buttons; setting the same text again is a no-op.
    virtual void set_tooltip(const std::string& utf8) = 0;
    // The platform window (HWND / NSView*) — only for platform code (menus, dialogs).
    virtual void* native() const = 0;
};

// --- native text field (a platform edit control embedded in a view) ---
struct TextFieldDelegate {
    virtual ~TextFieldDelegate() = default;
    virtual void on_text_changed() {}
    virtual void on_enter() {}
    virtual void on_escape() {}
};
class TextField {
public:
    virtual ~TextField() = default;
    virtual void set_bounds(const gfx::Rect& r) = 0; // in the owning view's coordinates
    virtual std::string text() const = 0;             // UTF-8
    virtual void set_text(const std::string& utf8) = 0;
    virtual void focus() = 0;
};
struct TextFieldStyle {
    gfx::Color text{ 236, 238, 245 }, background{ 26, 27, 32 };
    gfx::FontSpec font{ "Segoe UI", 13, false };
    std::string placeholder;
};

// --- menus ---
struct MenuItem {
    std::string label; // UTF-8; empty + separator=true for a separator
    int id = 0;        // returned by popup_menu (must be > 0)
    bool checked = false, enabled = true, separator = false;
    std::vector<MenuItem> children; // non-empty = submenu
    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
};
using Menu = std::vector<MenuItem>;

// The player's main window, as seen by the skin engine (script actions that affect it).
class MainWindow {
public:
    virtual ~MainWindow() = default;
    virtual void* native() const = 0;               // root window (HWND / NSWindow*)
    virtual gfx::Rect client_rect() const = 0;
    virtual void invalidate() = 0;
    virtual void set_titlebar_visible(bool visible) = 0;
    // Start a native move-the-window drag for the left button press being handled right now.
    // A skin that hides the title bar leaves nothing to drag the window by, so a press that no
    // $button consumed moves the window instead — the platform runs its own drag loop, and
    // nothing happens if the press turns out not to be a drag.
    virtual void begin_window_drag() = 0;
    // Resize to a client size of w x h, keeping the halign ("LEFT"/"RIGHT"/"CENTER") /
    // valign ("TOP"/"BOTTOM"/"CENTER") edge of the current frame fixed.
    virtual void resize_client(int w, int h, const std::string& halign, const std::string& valign) = 0;
    virtual void set_menubar_visible(bool visible) = 0;
    virtual void show_main_menu() = 0;             // the full File/Edit/.../Help menu, at the cursor
    virtual void set_title(const std::string& utf8) = 0;   // window/taskbar title ($settitle)
    virtual void set_tooltip(const std::string& utf8) = 0; // as ViewHost::set_tooltip, over the canvas
    // $settray: a notification-area icon (Windows) / menu-bar status item (macOS) with this
    // tooltip, whose click shows SkinEngine::show_tray_menu(). "" removes it. Called on every
    // repaint: the same text again must be a no-op.
    virtual void set_tray(const std::string& utf8) = 0;
    // Keep the player window above other windows (View > Panels UI > Always on top).
    virtual void set_always_on_top(bool on) = 0;
    // The zoom setting changed (ui_settings.h): re-read it and re-lay out at the new scale.
    // Every coordinate the engine and the views see stays in skin units — 1 unit = zoom x the
    // platform's own unit (a device pixel on Windows, a point on macOS).
    virtual void apply_zoom() = 0;
    virtual double zoom() const = 0; // the scale in effect (1.0 = 100%)
    // What the window shows right now — the canvas with every hosted panel on it — e.g. for the
    // skin's preview in Preferences. nullptr if the platform can't.
    virtual gfx::ImagePtr capture() { return nullptr; }
};

// A foreign (non-View) panel embedded in the root window — e.g. a hosted Default UI element.
class EmbeddedPanel {
public:
    virtual ~EmbeddedPanel() = default;
    virtual void set_bounds(const gfx::Rect& r, bool to_top) = 0;
    virtual void show(bool visible) = 0;
};

// --- platform factories / services (src/platform/<os>/) ---
std::unique_ptr<ViewHost> create_child_view(MainWindow& root, View* view, const ViewOptions& opts);
// Floating top-level window (owned by `root`) with a client area of w x h, centred on it.
// on_closed runs after the window is gone (the host must not be used afterwards).
std::unique_ptr<ViewHost> create_popup_window(MainWindow& root, View* view, int w, int h,
                                              const std::string& title, std::function<void()> on_closed);
std::unique_ptr<TextField> create_text_field(ViewHost& owner, TextFieldDelegate* delegate,
                                             const TextFieldStyle& style);
// Default UI element hosted by name (nullptr where unsupported / not found).
std::unique_ptr<EmbeddedPanel> create_embedded_ui_element(MainWindow& root, const char* name);

// Popup menu at (x,y) in `anchor` coordinates (anchor may be null = at the mouse cursor).
// Returns the chosen item id, 0 if dismissed.
int popup_menu(ViewHost* anchor, int x, int y, const Menu& menu);
// A popup menu at the mouse cursor, owned by the main window.
int popup_menu_at_cursor(MainWindow& root, const Menu& menu);
// foobar2000's context menu (Properties, tagging, ...) for `tracks`, at (x,y) in `anchor` coords.
void track_context_menu(ViewHost& anchor, int x, int y, const metadb_handle_list& tracks);
// Colour picker; true if the user confirmed a colour (written to `c`).
bool choose_color(ViewHost* owner, gfx::Color& c);
void message_box(ViewHost* owner, const std::string& title, const std::string& text);
// Folder picker over the main window, starting at `start`; "" if cancelled.
std::string choose_folder(MainWindow& root, const std::string& title, const std::string& start);
// Opens a web address in the default browser.
void open_url(const std::string& url);
// Opens a file (UTF-8 native path) in the application the system associates with it.
void open_file(const std::string& path);
// Shows `path` in the file manager (Explorer / Finder): a file selected in its folder, a folder
// opened.
void reveal_in_file_manager(const std::string& path);
// Multi-line text editor window (panel script "Edit code..."). apply() gets the edited text and
// returns false to keep the window open (e.g. the save failed). One window per `key`.
void open_text_editor(MainWindow& root, const std::string& key, const std::string& title,
                      const std::string& text, std::function<bool(const std::string&)> apply);
void close_text_editor(const std::string& key);

inline void View::invalidate() { if (m_host) m_host->invalidate(); }

} // namespace pui::ui
