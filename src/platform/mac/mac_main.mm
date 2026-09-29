// foo_ui_panels (reborn) — macOS entry point.
// foobar2000 for Mac has no replaceable UI module: Panels UI registers a layout element
// ("Panels UI", ui_element_mac). Add it in View > Layout > Edit Layout and let it fill the
// window; the element's view is the skin canvas (SkinEngine renders into it) and implements
// ui::MainWindow for the script actions that act on the window.
#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "mac_view.h"
#include "mac_canvas.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/image_cache.h"
#include "../../core/fs_util.h"

namespace {

// {3E5B7C21-9A4D-4F6E-8B12-7D0C4A9E6F53}
const GUID g_element_guid = { 0x3e5b7c21, 0x9a4d, 0x4f6e, { 0x8b, 0x12, 0x7d, 0x0c, 0x4a, 0x9e, 0x6f, 0x53 } };

// The root canvas: a ui::View whose paint runs the master skin script, hosted as the element's
// view, and the ui::MainWindow the engine talks to.
class PanelsRoot : public pui::ui::View, public pui::ui::MainWindow {
public:
    PanelsRoot() {
        m_host = pui::ui::mac::create_root_view(this, pui::ui::ViewOptions{});
        m_skin.set_main_window(this);
        std::string dir = pui::resolve_skin_dir();
        m_skin.set_base_dir(dir);
        std::string skin = pui::read_file(dir + "/fooava.txt");
        console::printf("Panels UI: fooava.txt read %u bytes from %s", (unsigned)skin.size(), dir.c_str());
        bool ok = m_skin.load(skin.empty() ? pui::builtin_test_skin() : skin.c_str());
        console::printf("Panels UI: compile -> %s", ok ? "ok" : "FAILED");
    }
    ~PanelsRoot() override {
        m_skin.save_pvars();
        m_skin.destroy_panels(); // child views go before the view they live in
        m_host.reset();
    }
    NSView* view() const { return (__bridge NSView*)m_host->native(); }

    // --- ui::View (the canvas) ---
    void on_attached() override { host()->set_timer(1, 500); } // progress bar / time readout
    void on_timer(int) override { invalidate(); }
    void paint(pui::gfx::Canvas& cv) override {
        const int w = cv.width(), h = cv.height();
        cv.fill_rect(pui::gfx::Rect{ 0, 0, w, h }, pui::gfx::Color());
        m_skin.render(cv, w, h);
        m_skin.snapshot_canvas(cv, w, h);
        m_skin.refresh_bars();
    }
    void on_mouse_down(const pui::ui::MouseEvent& e) override {
        if (e.button == pui::ui::MouseButton::Left) m_skin.handle_click(e.x, e.y);
    }
    void on_mouse_move(int x, int y, unsigned, bool) override { if (m_skin.update_hover(x, y)) invalidate(); }
    void on_mouse_leave() override { if (m_skin.update_hover(-1, -1)) invalidate(); }

    // --- ui::MainWindow ---
    void* native() const override { return m_host ? m_host->native() : nullptr; }
    pui::gfx::Rect client_rect() const override {
        NSRect b = view().bounds;
        return pui::gfx::Rect{ 0, 0, (int)b.size.width, (int)b.size.height };
    }
    void invalidate() override { [view() setNeedsDisplay:YES]; }
    // The window chrome belongs to foobar2000's own layout here; a skin hiding it would leave
    // the window unmovable.
    void set_titlebar_visible(bool) override {}
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

private:
    pui::SkinEngine m_skin;
    std::unique_ptr<pui::ui::ViewHost> m_host;
};

} // namespace

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

class panels_initquit : public initquit {
public:
    void on_quit() override { pui::images_shutdown(); }
};
FB2K_SERVICE_FACTORY(panels_initquit);

} // namespace
