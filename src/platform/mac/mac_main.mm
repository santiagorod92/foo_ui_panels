// foo_ui_panels (reborn) — macOS entry point.
// Two ways in, both backed by the same PanelsRoot canvas (SkinEngine renders into it and it
// implements ui::MainWindow for the script actions that act on the window):
//   * user_interface — a full UI module, picked in Preferences > Display > User Interface,
//     owning its own NSWindow. init() returns that NSWindow as fb2k::hwnd_t (SDK/ui.h).
//   * ui_element_mac — a "Panels UI" element for the Default UI's layout, added by name in
//     View > Layout > Edit Layout. Useful to embed the canvas next to stock elements.
#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "mac_view.h"
#include "mac_canvas.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/image_cache.h"
#include "../../core/fs_util.h"
#include <algorithm>
#include <vector>

// Target of the $settray status item's click: the engine's tray menu.
@interface FooUIPanelsTrayTarget : NSObject
@property (nonatomic, assign) pui::SkinEngine* engine;
- (void)clicked:(id)sender;
@end
@implementation FooUIPanelsTrayTarget
- (void)clicked:(id)sender { if (self.engine) self.engine->show_tray_menu(); }
@end

namespace {

class PanelsRoot;
std::vector<PanelsRoot*> g_roots; // live canvases (main thread only)

// {3E5B7C21-9A4D-4F6E-8B12-7D0C4A9E6F53}
const GUID g_element_guid = { 0x3e5b7c21, 0x9a4d, 0x4f6e, { 0x8b, 0x12, 0x7d, 0x0c, 0x4a, 0x9e, 0x6f, 0x53 } };

// The root canvas: a ui::View whose paint runs the master skin script, hosted as the element's
// view, and the ui::MainWindow the engine talks to.
class PanelsRoot : public pui::ui::View, public pui::ui::MainWindow {
public:
    // owns_window: the canvas fills a window of ours (the user_interface module) rather than
    // sitting in foobar2000's own layout — only then may a skin restyle the window chrome.
    explicit PanelsRoot(bool owns_window = false) : m_owns_window(owns_window) {
        pui::ui::ViewOptions opts;
        opts.accept_files = true; // dropped anywhere but the playlist: appended to it
        m_host = pui::ui::mac::create_root_view(this, opts);
        m_skin.set_main_window(this);
        m_skin.load_skin(pui::resolve_skin_dir()); // its main script, else the built-in test skin
        g_roots.push_back(this);
    }
    ~PanelsRoot() override {
        set_tray("");
        g_roots.erase(std::remove(g_roots.begin(), g_roots.end(), this), g_roots.end());
        m_skin.save_pvars();
        m_skin.destroy_panels(); // child views go before the view they live in
        m_host.reset();
    }
    NSView* view() const { return (__bridge NSView*)m_host->native(); }
    pui::SkinEngine& skin() { return m_skin; }

    // Preferences switched the skin folder / main script: drop the old skin's panels and load the
    // newly resolved one in place (destroy_panels also drops the play callback; set_main_window
    // brings it back).
    void reload_skin() {
        m_skin.save_pvars();
        m_skin.destroy_panels();
        m_skin.set_main_window(this);
        m_skin.load_skin(pui::resolve_skin_dir());
        invalidate();
    }

    // --- ui::View (the canvas) ---
    void on_attached() override { host()->set_timer(1, 500); } // progress bar / time readout
    // Only the progress bar / time readout advance on their own; everything else repaints
    // through SkinEngine's play_callback. Stopped/paused: nothing to do.
    void on_timer(int) override {
        m_skin.check_skin_changes(); // hot reload of edited skin files
        if (pui::SkinEngine::playback_ticking()) invalidate();
    }
    void paint(pui::gfx::Canvas& cv) override {
        const int w = cv.width(), h = cv.height();
        cv.fill_rect(pui::gfx::Rect{ 0, 0, w, h }, pui::gfx::Color());
        m_skin.render(cv, w, h);
        m_skin.snapshot_canvas(cv, w, h);
        m_skin.refresh_bars();
    }
    void on_mouse_down(const pui::ui::MouseEvent& e) override {
        if (e.button != pui::ui::MouseButton::Left) return;
        if (!m_skin.handle_click(e.x, e.y)) begin_window_drag();
    }
    void on_mouse_move(int x, int y, unsigned, bool) override { if (m_skin.update_hover(x, y)) invalidate(); }
    void on_mouse_leave() override { if (m_skin.update_hover(-1, -1)) invalidate(); }
    void on_drop_files(const std::vector<std::string>& paths, int, int) override { pui::SkinEngine::add_files(paths); }

    // --- ui::MainWindow ---
    void* native() const override { return m_host ? m_host->native() : nullptr; }
    pui::gfx::Rect client_rect() const override {
        NSRect b = view().bounds;
        return pui::gfx::Rect{ 0, 0, (int)b.size.width, (int)b.size.height };
    }
    void invalidate() override { [view() setNeedsDisplay:YES]; }

    // $windowstyle(hidetitlebar|showtitlebar). Hidden means: no title, no traffic lights, and
    // the canvas extending over the whole frame — the Cocoa equivalent of the Windows build
    // dropping WS_CAPTION. The skin re-runs this on every repaint, so do nothing when already
    // in the wanted state.
    // As a layout element the window is foobar2000's own and shared with other elements, so
    // leave its chrome alone (m_owns_window).
    void set_titlebar_visible(bool visible) override {
        if (!m_owns_window || visible == m_titlebar_visible) return;
        NSWindow* win = view().window;
        if (!win) return;
        m_titlebar_visible = visible;
        if (visible) win.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
        else         win.styleMask |= NSWindowStyleMaskFullSizeContentView;
        win.titlebarAppearsTransparent = !visible;
        win.titleVisibility = visible ? NSWindowTitleVisible : NSWindowTitleHidden;
        // With the style mask alone the titlebar view stays on top of the content and keeps
        // swallowing clicks — which would eat the skin's own close/minimise buttons, drawn in
        // exactly that strip. Hide the whole container.
        NSButton* close = [win standardWindowButton:NSWindowCloseButton];
        for (NSWindowButton b : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton })
            [win standardWindowButton:b].hidden = !visible;
        if (NSView* titlebar = close.superview) {
            if (NSView* container = titlebar.superview) container.hidden = !visible;
        }
        // Nothing is left to drag the window by once the chrome is gone.
        win.movableByWindowBackground = !visible;
        [view() setFrame:[win.contentView bounds]];
        invalidate();
    }
    void begin_window_drag() override {
        // As a layout element the window is foobar2000's own, shared with the other elements:
        // dragging it from our canvas would be a surprise there.
        if (!m_owns_window) return;
        NSWindow* win = view().window;
        NSEvent* ev = NSApp.currentEvent;
        if (win && ev && ev.type == NSEventTypeLeftMouseDown) [win performWindowDragWithEvent:ev];
    }
    void resize_client(int w, int h, const std::string& halign, const std::string& valign) override {
        NSWindow* win = view().window;
        if (!win || w <= 0 || h <= 0) return;
        // Grow/shrink the window by the difference between the requested and current canvas size
        // (the element may share the window with other layout parts).
        NSRect f = win.frame;
        NSSize cur = view().bounds.size;
        CGFloat dw = w - cur.width, dh = h - cur.height;
        NSRect nf = f;
        nf.size.width += dw; nf.size.height += dh;
        // Screen coordinates are y-up: keeping the TOP edge means moving the origin down.
        if (halign == "RIGHT") nf.origin.x -= dw;
        else if (halign == "CENTER") nf.origin.x -= dw / 2;
        if (valign == "BOTTOM") {}
        else if (valign == "CENTER") nf.origin.y -= dh / 2;
        else nf.origin.y -= dh;
        [win setFrame:nf display:YES animate:NO];
    }
    void set_menubar_visible(bool) override {} // the macOS menu bar is global
    void show_main_menu() override {
        // Deferred: triggered from inside a skin button's click handling.
        dispatch_async(dispatch_get_main_queue(), ^{
            NSMenu* pop = [[NSMenu alloc] initWithTitle:@""];
            NSArray<NSMenuItem*>* items = NSApp.mainMenu.itemArray;
            for (NSUInteger i = 1; i < items.count; ++i) { // skip the application menu
                NSMenuItem* src = items[i];
                if (!src.submenu) continue;
                NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:src.title action:nil keyEquivalent:@""];
                mi.submenu = [src.submenu copy];
                [pop addItem:mi];
            }
            [pop popUpMenuPositioningItem:nil atLocation:[NSEvent mouseLocation] inView:nil];
        });
    }
    // As a layout element the window is foobar2000's own: leave its title alone.
    void set_title(const std::string& utf8) override {
        NSWindow* win = view().window;
        if (!m_owns_window || !win) return;
        NSString* t = [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
        if (![t isEqualToString:win.title]) win.title = t;
    }
    void set_tooltip(const std::string& utf8) override { if (m_host) m_host->set_tooltip(utf8); }
    // $settray: macOS has no notification area — a menu-bar status item (the app icon) plays
    // that role, its click opening the same tray menu as on Windows.
    void set_tray(const std::string& utf8) override {
        if (utf8.empty()) {
            if (m_status) [[NSStatusBar systemStatusBar] removeStatusItem:m_status];
            m_status = nil; m_trayTip.clear();
            return;
        }
        if (!m_status) {
            m_status = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
            NSImage* icon = [NSApp.applicationIconImage copy];
            icon.size = NSMakeSize(18, 18);
            m_status.button.image = icon;
            if (!m_trayTarget) { m_trayTarget = [FooUIPanelsTrayTarget new]; m_trayTarget.engine = &m_skin; }
            m_status.button.target = m_trayTarget;
            m_status.button.action = @selector(clicked:);
        }
        if (utf8 == m_trayTip) return;
        m_trayTip = utf8;
        m_status.button.toolTip = [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
    }

private:
    pui::SkinEngine m_skin;
    std::unique_ptr<pui::ui::ViewHost> m_host;
    bool m_owns_window = false;
    bool m_titlebar_visible = true;
    NSStatusItem* m_status = nil;
    FooUIPanelsTrayTarget* m_trayTarget = nil;
    std::string m_trayTip;
};

} // namespace

namespace pui::mac {
void reload_skin_everywhere() { for (auto* r : g_roots) r->reload_skin(); }
void set_pvar_everywhere(const std::string& key, const std::string& value) {
    if (g_roots.empty()) {
        PvarMap m = load_all_pvars();
        if (value.empty()) m.erase(key); else m[key] = value;
        save_all_pvars(m);
        return;
    }
    for (auto* r : g_roots) { r->skin().set_pvar(key, value); r->skin().repaint_all(); }
}
} // namespace pui::mac

@interface FooUIPanelsController : NSViewController
@end

@implementation FooUIPanelsController {
    std::unique_ptr<PanelsRoot> _root;
}
- (void)loadView {
    _root = std::make_unique<PanelsRoot>();
    self.view = _root->view();
}
- (void)dealloc { _root.reset(); }
@end

namespace {

class ui_element_mac_panels : public ui_element_mac {
public:
    service_ptr instantiate(service_ptr) override {
        return fb2k::wrapNSObject([FooUIPanelsController new]);
    }
    bool match_name(const char* name) override {
        return name && (!strcmp(name, "Panels UI") || !strcmp(name, "foo_ui_panels"));
    }
    fb2k::stringRef get_name() override { return fb2k::makeString("Panels UI"); }
    GUID get_guid() override { return g_element_guid; }
};
FB2K_SERVICE_FACTORY(ui_element_mac_panels);

// --- full UI module -------------------------------------------------------------------------
// Same slot as the Default User Interface: selected in Preferences > Display > User Interface,
// stored as the ui.module GUID. The macOS menu bar stays the core's, so unlike the Windows
// build there is no menu of our own to put up.

// {6F0A1B2C-3D4E-4F50-9A1B-2C3D4E5F6071} — same module identity as the Windows build.
const GUID g_panels_ui_guid =
    { 0x6f0a1b2c, 0x3d4e, 0x4f50, { 0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71 } };

class panels_ui_mac;
panels_ui_mac* g_active_ui = nullptr; // the live module, for the initquit teardown below

class panels_ui_mac : public user_interface {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }

    fb2k::hwnd_t init(HookProc_t) override {
        m_root = std::make_unique<PanelsRoot>(/*owns_window*/ true);
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 950, 750)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        win.title = @"foobar2000";
        win.releasedWhenClosed = NO; // we hold the only strong reference, in m_window
        // Restore last session's frame; only a first run gets centred (centring after setting the
        // autosave name would throw the restored position away again).
        if (![win setFrameUsingName:@"foo_ui_panels.mainwindow"]) [win center];
        win.frameAutosaveName = @"foo_ui_panels.mainwindow";
        NSView* canvas = m_root->view();
        canvas.frame = [win.contentView bounds];
        canvas.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [win.contentView addSubview:canvas];
        [win makeKeyAndOrderFront:nil];
        m_window = win;
        g_active_ui = this;
        // The core hands this back as an NSWindow; it does not own it (see shutdown()).
        return (__bridge void*)win;
    }

    void shutdown() override {
        teardown();
        [m_window close];
        m_window = nil;
        pui::images_shutdown();
    }

    // Drop the canvas while the core is still up. ~PanelsRoot saves the pvars through a
    // cfg_var, which needs live services: quitting from a skin button reaches NSApplication
    // terminate: -> exit() without the core ever calling shutdown(), so the only thing left to
    // destroy the root would be this factory's own static destructor — by then the core is gone
    // and the cfg_var write crashes in pfc::crashImpl(). initquit::on_quit() calls this first.
    void teardown() {
        m_root.reset();
        if (g_active_ui == this) g_active_ui = nullptr;
    }

    void activate() override {
        [NSApp activateIgnoringOtherApps:YES];
        [m_window makeKeyAndOrderFront:nil];
    }
    void hide() override { [m_window miniaturize:nil]; }
    bool is_visible() override { return m_window != nil && m_window.isVisible && !m_window.isMiniaturized; }
    GUID get_guid() override { return g_panels_ui_guid; }

    void override_statusbar_text(const char*) override {}
    void revert_statusbar_text() override {}
    void show_now_playing() override {}

private:
    NSWindow* m_window = nil; // strong (ARC): nothing else keeps the window alive
    std::unique_ptr<PanelsRoot> m_root;
};
// service_factory_single_v2_t, not user_interface_factory (= service_factory_single_t): the
// latter holds the instance as a member, so our static destructors — which run before the
// core's at exit() — destroy the UI module while the core still holds a reference to it, and
// releasing that reference calls into a destroyed vtable (__cxa_pure_virtual, abort). The v2
// factory heap-allocates on first access and never frees, which the SDK documents as the fix
// for "dangling references to our object getting invoked [...] during late shutdown".
static service_factory_single_v2_t<panels_ui_mac> g_panels_ui_mac_factory;

class panels_initquit : public initquit {
public:
    void on_quit() override {
        if (g_active_ui) g_active_ui->teardown(); // pvars saved while the core is still alive
        pui::images_shutdown();
    }
};
FB2K_SERVICE_FACTORY(panels_initquit);

} // namespace
