#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "mac_view.h"
#include "mac_canvas.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/image_cache.h"
#include "../../core/fs_util.h"
#include "../../core/ui_settings.h"
#include <algorithm>
#include <vector>

@interface FooUIPanelsTrayTarget : NSObject
@property (nonatomic, assign) pui::SkinEngine* engine;
- (void)clicked:(id)sender;
@end
@implementation FooUIPanelsTrayTarget
- (void)clicked:(id)sender { if (self.engine) self.engine->show_tray_menu(); }
@end

namespace {

class PanelsRoot;
std::vector<PanelsRoot*> g_roots;

const GUID g_element_guid = { 0x3e5b7c21, 0x9a4d, 0x4f6e, { 0x8b, 0x12, 0x7d, 0x0c, 0x4a, 0x9e, 0x6f, 0x53 } };

class PanelsRoot : public pui::ui::View, public pui::ui::MainWindow {
public:
    explicit PanelsRoot(bool owns_window = false) : m_owns_window(owns_window) {
        pui::ui::ViewOptions opts;
        opts.accept_files = true;
        m_host = pui::ui::mac::create_root_view(this, opts);
        m_zoom = pui::ui::mac::zoom_factor();
        pui::ui::mac::set_root_zoom(*m_host, m_zoom);
        m_skin.set_main_window(this);
        m_skin.load_skin(pui::resolve_skin_dir());
        g_roots.push_back(this);
    }
    ~PanelsRoot() override {
        set_tray("");
        g_roots.erase(std::remove(g_roots.begin(), g_roots.end(), this), g_roots.end());
        m_skin.save_pvars();
        m_skin.destroy_panels();
        m_host.reset();
    }
    NSView* view() const { return (__bridge NSView*)m_host->native(); }
    pui::SkinEngine& skin() { return m_skin; }

    void reload_skin() { m_skin.reload_skin(); }

    void on_attached() override { host()->set_timer(1, 500); }
    bool on_key_down(int key, unsigned mods) override {
        if (key != pui::ui::kKeyTab) return false;
        m_skin.focus_next_panel({}, (mods & pui::ui::kShift) != 0);
        return true;
    }
    void on_timer(int) override {
        m_skin.check_skin_changes();
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

    void* native() const override { return m_host ? m_host->native() : nullptr; }
    pui::gfx::Rect client_rect() const override {
        NSRect b = view().bounds;
        return pui::gfx::Rect{ 0, 0, (int)b.size.width, (int)b.size.height };
    }
    void invalidate() override { [view() setNeedsDisplay:YES]; }

    void set_titlebar_visible(bool visible) override {
        if (!m_owns_window || visible == m_titlebar_visible) return;
        NSWindow* win = view().window;
        if (!win) return;
        m_titlebar_visible = visible;
        if (visible) win.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
        else         win.styleMask |= NSWindowStyleMaskFullSizeContentView;
        win.titlebarAppearsTransparent = !visible;
        win.titleVisibility = visible ? NSWindowTitleVisible : NSWindowTitleHidden;
        NSButton* close = [win standardWindowButton:NSWindowCloseButton];
        for (NSWindowButton b : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton })
            [win standardWindowButton:b].hidden = !visible;
        if (NSView* titlebar = close.superview) {
            if (NSView* container = titlebar.superview) container.hidden = !visible;
        }
        win.movableByWindowBackground = !visible;
        [view() setFrame:[win.contentView bounds]];
        invalidate();
    }
    void begin_window_drag() override {
        if (!m_owns_window) return;
        NSWindow* win = view().window;
        NSEvent* ev = NSApp.currentEvent;
        if (win && ev && ev.type == NSEventTypeLeftMouseDown) [win performWindowDragWithEvent:ev];
    }
    void resize_client(int w, int h, const std::string& halign, const std::string& valign) override {
        NSWindow* win = view().window;
        if (!win || w <= 0 || h <= 0) return;
        NSRect f = win.frame;
        NSSize cur = view().bounds.size;
        CGFloat dw = (w - cur.width) * m_zoom, dh = (h - cur.height) * m_zoom;
        NSRect nf = f;
        nf.size.width += dw; nf.size.height += dh;
        if (halign == "RIGHT") nf.origin.x -= dw;
        else if (halign == "CENTER") nf.origin.x -= dw / 2;
        if (valign == "BOTTOM") {}
        else if (valign == "CENTER") nf.origin.y -= dh / 2;
        else nf.origin.y -= dh;
        [win setFrame:nf display:YES animate:NO];
    }
    void set_menubar_visible(bool) override {}
    void show_main_menu() override {
        dispatch_async(dispatch_get_main_queue(), ^{
            NSMenu* pop = [[NSMenu alloc] initWithTitle:@""];
            NSArray<NSMenuItem*>* items = NSApp.mainMenu.itemArray;
            for (NSUInteger i = 1; i < items.count; ++i) {
                NSMenuItem* src = items[i];
                if (!src.submenu) continue;
                NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:src.title action:nil keyEquivalent:@""];
                mi.submenu = [src.submenu copy];
                [pop addItem:mi];
            }
            [pop popUpMenuPositioningItem:nil atLocation:[NSEvent mouseLocation] inView:nil];
        });
    }
    void set_title(const std::string& utf8) override {
        NSWindow* win = view().window;
        if (!m_owns_window || !win) return;
        NSString* t = [NSString stringWithUTF8String:utf8.c_str()] ?: @"";
        if (![t isEqualToString:win.title]) win.title = t;
    }
    void set_tooltip(const std::string& utf8) override { if (m_host) m_host->set_tooltip(utf8); }
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

    void set_always_on_top(bool on) override {
        if (NSWindow* win = view().window) win.level = on ? NSFloatingWindowLevel : NSNormalWindowLevel;
    }
    double zoom() const override { return m_zoom; }
    pui::gfx::ImagePtr capture() override {
        NSView* v = view();
        if (!v || NSIsEmptyRect(v.bounds)) return nullptr;
        NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        if (!rep) return nullptr;
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        CGImageRef img = rep.CGImage;
        return img ? pui::gfx::image_from_cgimage(CGImageRetain(img)) : nullptr;
    }
    void apply_zoom() override {
        const double z = pui::ui::mac::zoom_factor();
        if (z == m_zoom) return;
        const pui::gfx::Rect before = client_rect();
        NSWindow* win = view().window;
        const bool keep = m_owns_window && win && !(win.styleMask & NSWindowStyleMaskFullScreen) && !win.isZoomed;
        if (keep) {
            NSRect f = win.frame;
            const CGFloat dw = before.w * (z - m_zoom), dh = before.h * (z - m_zoom);
            f.size.width += dw; f.size.height += dh; f.origin.y -= dh;
            m_zoom = z;
            [win setFrame:f display:NO animate:NO];
        }
        m_zoom = z;
        pui::ui::mac::set_root_zoom(*m_host, z);
        invalidate();
    }
    void on_window_attached() { set_always_on_top(pui::always_on_top()); }

private:
    pui::SkinEngine m_skin;
    std::unique_ptr<pui::ui::ViewHost> m_host;
    double m_zoom = 1.0;
    bool m_owns_window = false;
    bool m_titlebar_visible = true;
    NSStatusItem* m_status = nil;
    FooUIPanelsTrayTarget* m_trayTarget = nil;
    std::string m_trayTip;
};

}

@interface FooUIPanelsController : NSViewController
@end

@implementation FooUIPanelsController {
    std::unique_ptr<PanelsRoot> _root;
}
- (void)loadView {
    _root = std::make_unique<PanelsRoot>();
    self.view = _root->view();
}
- (void)viewDidAppear { [super viewDidAppear]; if (_root) _root->on_window_attached(); }
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

const GUID g_panels_ui_guid =
    { 0x6f0a1b2c, 0x3d4e, 0x4f50, { 0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71 } };

class panels_ui_mac;
panels_ui_mac* g_active_ui = nullptr;

class panels_ui_mac : public user_interface {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }

    fb2k::hwnd_t init(HookProc_t) override {
        m_root = std::make_unique<PanelsRoot>( true);
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 950, 750)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        win.title = @"foobar2000";
        win.releasedWhenClosed = NO;
        if (![win setFrameUsingName:@"foo_ui_panels.mainwindow"]) [win center];
        win.frameAutosaveName = @"foo_ui_panels.mainwindow";
        NSView* canvas = m_root->view();
        canvas.frame = [win.contentView bounds];
        canvas.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [win.contentView addSubview:canvas];
        m_root->on_window_attached();
        [win makeKeyAndOrderFront:nil];
        m_window = win;
        g_active_ui = this;
        return (__bridge void*)win;
    }

    void shutdown() override {
        teardown();
        [m_window close];
        m_window = nil;
        pui::images_shutdown();
    }

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
    NSWindow* m_window = nil;
    std::unique_ptr<PanelsRoot> m_root;
};
static service_factory_single_v2_t<panels_ui_mac> g_panels_ui_mac_factory;

class panels_initquit : public initquit {
public:
    void on_quit() override {
        if (g_active_ui) g_active_ui->teardown();
        pui::images_shutdown();
    }
};
FB2K_SERVICE_FACTORY(panels_initquit);

}
