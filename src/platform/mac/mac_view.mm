#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "mac_view.h"
#include "mac_canvas.h"
#include "../../ui/host_input.h"
#include "../../core/skin_paths.h"
#include "../../core/ui_logic.h"
#include "../../core/ui_settings.h"
#include "../../core/skin_lint.h"
#include <map>
#include <string>
#include <sys/stat.h>

using namespace pui;
using namespace pui::ui;

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

unsigned mods_of(NSEvent* e) {
    unsigned m = 0;
    NSEventModifierFlags f = e.modifierFlags;
    if (f & NSEventModifierFlagShift) m |= kShift;
    if (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) m |= kCtrl;
    if (f & NSEventModifierFlagOption) m |= kAlt;
    return m;
}

int map_key(NSEvent* e) {
    switch (e.keyCode) {
    case 36: case 76: return kKeyEnter;  case 53: return kKeyEscape;
    case 48: return kKeyTab;             case 51: return kKeyBackspace;
    case 117: return kKeyDelete;
    case 123: return kKeyLeft;  case 124: return kKeyRight;
    case 126: return kKeyUp;    case 125: return kKeyDown;
    case 116: return kKeyPageUp; case 121: return kKeyPageDown;
    case 115: return kKeyHome;  case 119: return kKeyEnd;
    case 122: return kKeyF1; case 120: return kKeyF2; case 99: return kKeyF3;
    case 118: return kKeyF4; case 96: return kKeyF5;
    }
    NSString* c = e.charactersIgnoringModifiers.uppercaseString;
    if (c.length == 1) {
        unichar u = [c characterAtIndex:0];
        if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == ' ') return (int)u;
    }
    return kKeyUnknown;
}

NSCursor* cursor_of(Cursor c) {
    switch (c) {
    case Cursor::Hand: return [NSCursor pointingHandCursor];
    case Cursor::IBeam: return [NSCursor IBeamCursor];
    default: return [NSCursor arrowCursor];
    }
}

NSFont* font_of(const gfx::FontSpec& f) {
    CGFloat px = f.points ? f.size * 96.0 / 72.0 : f.size;
    NSFont* font = [NSFont fontWithName:ns(f.face) size:px];
    if (!font) font = [NSFont systemFontOfSize:px];
    if (f.bold) font = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSBoldFontMask];
    return font;
}

NSColor* color_of(gfx::Color c) {
    return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:1];
}

class MacViewHost;

}

@interface FooUIPanelsView : NSView
@property (nonatomic, assign) void* host;
@property (nonatomic, strong) NSCursor* cursor;
@property (nonatomic, assign) CGFloat zoom;
- (void)applyZoom;
@end

@interface FooUIPanelsFieldDelegate : NSObject <NSTextFieldDelegate>
@property (nonatomic, assign) pui::ui::TextFieldDelegate* target;
@end

@interface FooUIPanelsMenuTarget : NSObject
@property (nonatomic, assign) NSInteger chosen;
- (void)picked:(NSMenuItem*)item;
@end

@interface FooUIPanelsWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, copy) void (^onClose)(void);
@end

namespace {

class MacViewHost : public ViewHost {
public:
    MacViewHost(View* view, const ViewOptions& opts) : m_view(view), m_opts(opts) {
        m_ns = [[FooUIPanelsView alloc] initWithFrame:NSZeroRect];
        m_ns.host = this;
        m_ns.cursor = cursor_of(opts.cursor);
        m_ns.wantsLayer = YES;
        if (opts.accept_files) [m_ns registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        if (!opts.accessible_name.empty()) {
            m_ns.accessibilityElement = YES;
            m_ns.accessibilityRole = NSAccessibilityGroupRole;
            m_ns.accessibilityLabel = ns(opts.accessible_name);
        }
    }
    const ViewOptions& options() const { return m_opts; }
    ~MacViewHost() override {
        for (auto& kv : m_timers) [kv.second invalidate];
        m_timers.clear();
        if (m_frameTimer) [m_frameTimer invalidate];
        if (m_view->host() == this) m_view->on_destroy();
        m_ns.host = nullptr;
        [m_ns removeFromSuperview];
        if (m_window) {
            m_windowDelegate.onClose = nil;
            m_window.delegate = nil;
            [m_window close];
        }
    }

    void attach_to(NSView* parent) {
        [parent addSubview:m_ns];
        m_root = parent;
        start();
    }

    void open_as_popup(NSView* owner, int w, int h, const std::string& title, std::function<void()> onClosed) {
        m_popup = true;
        const double z = pui::ui::mac::zoom_factor();
        m_ns.zoom = z;
        m_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w * z, h * z)
                                               styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                 backing:NSBackingStoreBuffered defer:NO];
        m_window.releasedWhenClosed = NO;
        m_window.title = ns(title);
        m_window.contentView = m_ns;
        m_windowDelegate = [FooUIPanelsWindowDelegate new];
        auto cb = std::make_shared<std::function<void()>>(std::move(onClosed));
        m_windowDelegate.onClose = ^{
            dispatch_async(dispatch_get_main_queue(), ^{ if (*cb) (*cb)(); });
        };
        m_window.delegate = m_windowDelegate;
        if (NSWindow* ow = owner.window) {
            NSRect of = ow.frame, wf = m_window.frame;
            [m_window setFrameOrigin:NSMakePoint(NSMidX(of) - wf.size.width / 2, NSMidY(of) - wf.size.height / 2)];
            [ow addChildWindow:m_window ordered:NSWindowAbove];
        }
        start();
        [m_window makeKeyAndOrderFront:nil];
    }

    void invalidate() override { [m_ns setNeedsDisplay:YES]; }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        m_ns.frame = NSMakeRect(r.x, r.y, r.w, r.h);
        if (to_top && m_ns.superview) {
            NSView* sv = m_ns.superview;
            [sv addSubview:m_ns positioned:NSWindowAbove relativeTo:nil];
        }
    }
    gfx::Rect bounds() const override {
        NSRect f = m_ns.frame;
        if (m_popup) { NSSize b = m_ns.bounds.size; return gfx::Rect{ 0, 0, (int)b.width, (int)b.height }; }
        return gfx::Rect{ (int)f.origin.x, (int)f.origin.y, (int)f.size.width, (int)f.size.height };
    }
    void show(bool v) override { m_ns.hidden = !v; }
    bool visible() const override { return m_ns.window != nil && !m_ns.isHiddenOrHasHiddenAncestor; }
    void set_timer(int id, int ms) override {
        kill_timer(id);
        View* v = m_view;
        NSTimer* t = [NSTimer timerWithTimeInterval:ms / 1000.0 repeats:YES block:^(NSTimer*) { v->on_timer(id); }];
        [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
        m_timers[id] = t;
    }
    void kill_timer(int id) override {
        auto it = m_timers.find(id);
        if (it == m_timers.end()) return;
        [it->second invalidate];
        m_timers.erase(it);
    }
    void capture_mouse(bool) override {}
    void focus() override {
        if (m_window) [m_window makeKeyAndOrderFront:nil];
        [m_ns.window makeFirstResponder:m_ns];
    }
    void set_focus_ring(bool on) override {
        if (m_ring == on) return;
        m_ring = on;
        [m_ns setNeedsDisplay:YES];
    }
    void set_cursor(Cursor c) override {
        m_ns.cursor = cursor_of(c);
        [m_ns.window invalidateCursorRectsForView:m_ns];
        NSPoint p = [m_ns convertPoint:[m_ns.window mouseLocationOutsideOfEventStream] fromView:nil];
        if (NSPointInRect(p, m_ns.bounds)) [m_ns.cursor set];
    }
    void set_tooltip(const std::string& utf8) override {
        NSString* t = utf8.empty() ? nil : ns(utf8);
        if (t == m_ns.toolTip || [t isEqualToString:m_ns.toolTip]) return;
        m_ns.toolTip = t;
    }
    void* native() const override { return (__bridge void*)m_ns; }

    View* view() const { return m_view; }
    bool live() const { return m_view->host() == this; }
    void paint() {
        NSRect b = m_ns.bounds;
        const int w = (int)b.size.width, h = (int)b.size.height;
        if (w <= 0 || h <= 0 || !live()) return;
        double scale = [m_ns convertSizeToBacking:NSMakeSize(1, 1)].width;
        if (!(scale > 0)) scale = m_ns.window ? m_ns.window.backingScaleFactor : 1.0;
        gfx::CGCanvas cv(w, h, scale);
        paint_view(*m_view, cv, m_ring && m_ns.window.firstResponder == m_ns);
        CGImageRef img = cv.copy_image();
        if (!img) return;
        CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
        gfx::draw_cgimage(ctx, img, CGRectMake(0, 0, w, h), false);
        CGImageRelease(img);
    }

private:
    void start() {
        start_view(*m_view, *this);
        if (m_opts.render_fps > 0) {
            FooUIPanelsView* v = m_ns;
            m_frameTimer = [NSTimer timerWithTimeInterval:1.0 / m_opts.render_fps repeats:YES
                                                    block:^(NSTimer*) { if (!v.isHiddenOrHasHiddenAncestor) [v setNeedsDisplay:YES]; }];
            [[NSRunLoop mainRunLoop] addTimer:m_frameTimer forMode:NSRunLoopCommonModes];
        }
    }

    View* m_view;
    ViewOptions m_opts;
    FooUIPanelsView* m_ns = nil;
    NSView* __weak m_root = nil;
    NSWindow* m_window = nil;
    FooUIPanelsWindowDelegate* m_windowDelegate = nil;
    std::map<int, NSTimer*> m_timers;
    NSTimer* m_frameTimer = nil;
    bool m_popup = false;
    bool m_ring = false;
};

MacViewHost* host_of(FooUIPanelsView* v) { return (MacViewHost*)v.host; }

}

@implementation FooUIPanelsView {
    NSTrackingArea* _tracking;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (BOOL)wantsUpdateLayer { return NO; }
- (void)drawRect:(NSRect)dirty { if (auto* h = host_of(self)) h->paint(); }
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; [self setNeedsDisplay:YES]; }
- (void)applyZoom {
    const CGFloat z = self.zoom > 0 ? self.zoom : 1;
    NSSize f = self.frame.size;
    [self setBoundsSize:NSMakeSize(f.width / z, f.height / z)];
    [self setBoundsOrigin:NSZeroPoint];
}
- (void)setFrameSize:(NSSize)s {
    [super setFrameSize:s];
    if (self.zoom > 0) [self applyZoom];
    NSSize b = self.bounds.size;
    if (auto* h = host_of(self); h && h->live()) h->view()->on_resize((int)b.width, (int)b.height);
    [self setNeedsDisplay:YES];
}
- (void)viewDidHide { if (auto* h = host_of(self); h && h->live()) h->view()->on_visibility(false); }
- (void)viewDidUnhide { if (auto* h = host_of(self); h && h->live()) h->view()->on_visibility(true); }
- (BOOL)becomeFirstResponder { if (auto* h = host_of(self); h && h->live()) h->view()->on_focus(); return YES; }
- (BOOL)resignFirstResponder { if (auto* h = host_of(self)) h->set_focus_ring(false); return YES; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                NSTrackingInVisibleRect | NSTrackingCursorUpdate
        owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (void)cursorUpdate:(NSEvent*)e { [self.cursor set]; }
- (NSPoint)local:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }
- (MouseEvent)mouse:(NSEvent*)e button:(MouseButton)b {
    NSPoint p = [self local:e];
    MouseEvent m; m.x = (int)p.x; m.y = (int)p.y; m.button = b; m.mods = mods_of(e);
    m.double_click = e.clickCount == 2;
    return m;
}
- (void)move:(NSEvent*)e {
    auto* h = host_of(self); if (!h || !h->live()) return;
    NSPoint p = [self local:e];
    h->view()->on_mouse_move((int)p.x, (int)p.y, mods_of(e), (NSEvent.pressedMouseButtons & 1) != 0);
}
- (void)mouseMoved:(NSEvent*)e { [self move:e]; }
- (void)mouseDragged:(NSEvent*)e { [self move:e]; }
- (void)mouseExited:(NSEvent*)e { if (auto* h = host_of(self); h && h->live()) h->view()->on_mouse_leave(); }
- (void)press:(NSEvent*)e button:(MouseButton)b {
    auto* h = host_of(self);
    if (!h) return;
    if (press_takes_focus(b)) {
        NSWindow* w = self.window;
        if (w && w.firstResponder != self) [w makeFirstResponder:self];
        h->set_focus_ring(false);
    }
    if (h->live()) h->view()->on_mouse_down([self mouse:e button:b]);
}
- (void)mouseDown:(NSEvent*)e { [self press:e button:MouseButton::Left]; }
- (void)mouseUp:(NSEvent*)e { if (auto* h = host_of(self); h && h->live()) h->view()->on_mouse_up([self mouse:e button:MouseButton::Left]); }
- (void)rightMouseDown:(NSEvent*)e { [self press:e button:MouseButton::Right]; }
- (void)rightMouseUp:(NSEvent*)e { if (auto* h = host_of(self); h && h->live()) h->view()->on_mouse_up([self mouse:e button:MouseButton::Right]); }
- (void)otherMouseDown:(NSEvent*)e { [self press:e button:MouseButton::Middle]; }
- (void)otherMouseUp:(NSEvent*)e { if (auto* h = host_of(self); h && h->live()) h->view()->on_mouse_up([self mouse:e button:MouseButton::Middle]); }
- (void)scrollWheel:(NSEvent*)e {
    auto* h = host_of(self); if (!h || !h->live()) return;
    CGFloat d = e.scrollingDeltaY;
    if (e.hasPreciseScrollingDeltas) d /= 10.0;
    if (d == 0) return;
    NSPoint p = [self local:e];
    h->view()->on_wheel((int)p.x, (int)p.y, (float)d);
}
- (void)keyDown:(NSEvent*)e {
    auto* h = host_of(self);
    if (h && dispatch_key(*h->view(), h->live(), h->options(), map_key(e), mods_of(e))) return;
    [super keyDown:e];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)s {
    auto* h = host_of(self);
    return h && h->live() ? NSDragOperationCopy : NSDragOperationNone;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)s { return [self draggingEntered:s]; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)s {
    auto* h = host_of(self);
    if (!h || !h->live()) return NO;
    NSArray<NSURL*>* urls = [s.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    std::vector<std::string> paths;
    for (NSURL* u in urls) if (const char* p = u.fileSystemRepresentation) paths.emplace_back(p);
    if (paths.empty()) return NO;
    NSPoint p = [self convertPoint:s.draggingLocation fromView:nil];
    h->view()->on_drop_files(paths, (int)p.x, (int)p.y);
    return YES;
}
@end

@implementation FooUIPanelsFieldDelegate
- (void)controlTextDidChange:(NSNotification*)n { if (self.target) self.target->on_text_changed(); }
- (BOOL)control:(NSControl*)c textView:(NSTextView*)tv doCommandBySelector:(SEL)sel {
    if (sel == @selector(insertNewline:)) { if (self.target) self.target->on_enter(); return YES; }
    if (sel == @selector(cancelOperation:)) { if (self.target) self.target->on_escape(); return YES; }
    return NO;
}
@end

@implementation FooUIPanelsMenuTarget
- (void)picked:(NSMenuItem*)item { self.chosen = item.tag; }
@end

@implementation FooUIPanelsWindowDelegate
- (void)windowWillClose:(NSNotification*)n { if (self.onClose) { auto cb = self.onClose; self.onClose = nil; cb(); } }
@end

namespace {

class MacTextField : public TextField {
public:
    MacTextField(NSView* owner, TextFieldDelegate* d, const TextFieldStyle& st) {
        m_field = [[NSTextField alloc] initWithFrame:NSZeroRect];
        m_field.bordered = NO;
        m_field.bezeled = NO;
        m_field.drawsBackground = YES;
        m_field.backgroundColor = color_of(st.background);
        m_field.textColor = color_of(st.text);
        m_field.font = font_of(st.font);
        m_field.focusRingType = NSFocusRingTypeNone;
        if (!st.placeholder.empty())
            m_field.placeholderAttributedString = [[NSAttributedString alloc] initWithString:ns(st.placeholder)
                attributes:@{ NSForegroundColorAttributeName: [NSColor grayColor], NSFontAttributeName: m_field.font }];
        m_delegate = [FooUIPanelsFieldDelegate new];
        m_delegate.target = d;
        m_field.delegate = m_delegate;
        [owner addSubview:m_field];
    }
    ~MacTextField() override { m_delegate.target = nullptr; m_field.delegate = nil; [m_field removeFromSuperview]; }
    void set_bounds(const gfx::Rect& r) override { m_field.frame = NSMakeRect(r.x, r.y, r.w, r.h); }
    std::string text() const override { return m_field.stringValue.UTF8String ?: ""; }
    void set_text(const std::string& s) override { m_field.stringValue = ns(s); }
    void focus() override { [m_field.window makeFirstResponder:m_field]; }
private:
    NSTextField* m_field;
    FooUIPanelsFieldDelegate* m_delegate;
};

class MacEmbeddedPanel : public EmbeddedPanel {
public:
    MacEmbeddedPanel(NSViewController* vc, NSView* parent) : m_vc(vc) { [parent addSubview:vc.view]; }
    ~MacEmbeddedPanel() override { [m_vc.view removeFromSuperview]; }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        NSView* v = m_vc.view;
        v.frame = NSMakeRect(r.x, r.y, r.w, r.h);
        if (to_top && v.superview) { NSView* sv = v.superview; [sv addSubview:v positioned:NSWindowAbove relativeTo:nil]; }
    }
    void show(bool v) override { m_vc.view.hidden = !v; }
private:
    NSViewController* m_vc;
};

void build_menu(NSMenu* m, const Menu& items, FooUIPanelsMenuTarget* target) {
    m.autoenablesItems = NO;
    for (const auto& it : items) {
        if (it.separator) { [m addItem:[NSMenuItem separatorItem]]; continue; }
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(it.label) action:nil keyEquivalent:@""];
        mi.enabled = it.enabled;
        mi.state = it.checked ? NSControlStateValueOn : NSControlStateValueOff;
        if (!it.children.empty()) {
            NSMenu* sub = [[NSMenu alloc] initWithTitle:ns(it.label)];
            build_menu(sub, it.children, target);
            mi.submenu = sub;
        } else {
            mi.tag = it.id; mi.target = target; mi.action = @selector(picked:);
        }
        [m addItem:mi];
    }
}

int run_menu(NSMenu* m, FooUIPanelsMenuTarget* target, NSView* view, NSPoint pt) {
    target.chosen = 0;
    [m popUpMenuPositioningItem:nil atLocation:pt inView:view];
    return (int)target.chosen;
}

struct Editor {
    NSWindow* window; NSTextView* text; FooUIPanelsWindowDelegate* delegate;
    std::function<bool(const std::string&)> apply;
    NSTextField* status = nil;
    std::string original;
    std::string check;
};
std::map<std::string, std::shared_ptr<Editor>> g_editors;

void highlight_editor(NSTextView* tv) {
    const std::string u = tv.string.UTF8String ?: "";
    if (u.size() > pui::kEditorHighlightMax) return;
    std::vector<NSUInteger> at;
    at.reserve(u.size() + 1);
    NSUInteger idx = 0;
    for (size_t i = 0; i < u.size();) {
        const unsigned char c = (unsigned char)u[i];
        const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        for (size_t k = 0; k < n && i + k < u.size(); ++k) at.push_back(idx);
        idx += n == 4 ? 2 : 1;
        i += n;
    }
    at.push_back(idx);
    NSTextStorage* ts = tv.textStorage;
    [ts beginEditing];
    [ts addAttribute:NSForegroundColorAttributeName value:NSColor.textColor range:NSMakeRange(0, ts.length)];
    for (const pui::ScriptToken& t : pui::tokenize_script(u)) {
        NSColor* c = NSColor.systemOrangeColor;
        switch (t.kind) {
        case pui::ScriptToken::Kind::Function: c = NSColor.systemBlueColor; break;
        case pui::ScriptToken::Kind::Field:    c = NSColor.systemPurpleColor; break;
        case pui::ScriptToken::Kind::Quoted:   c = NSColor.systemGreenColor; break;
        case pui::ScriptToken::Kind::Paren:    break;
        }
        const NSUInteger a = at[t.start], b = at[std::min(t.start + t.len, at.size() - 1)];
        if (b > a && b <= ts.length) [ts addAttribute:NSForegroundColorAttributeName value:c range:NSMakeRange(a, b - a)];
    }
    [ts endEditing];
}

void update_editor_status(Editor& ed) {
    NSString* str = ed.text.string;
    std::u16string u(str.length, u'\0');
    [str getCharacters:(unichar*)u.data() range:NSMakeRange(0, str.length)];
    int line, col;
    pui::line_col_utf16(u, ed.text.selectedRange.location, line, col);
    const bool plain = [str lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > pui::kEditorHighlightMax;
    ed.status.stringValue = ns(pui::editor_status(line, col, ed.check, "\xe2\x8c\x98S: apply", plain));
}

void recheck_editor(Editor& ed) {
    ed.check = pui::problems_summary(pui::check_script(ed.text.string.UTF8String ?: ""));
    update_editor_status(ed);
}

}

@interface FooUIPanelsEditorActions : NSObject <NSTextViewDelegate>
@property (nonatomic, assign) std::string* key;
- (void)apply:(id)sender;
- (void)ok:(id)sender;
- (void)cancel:(id)sender;
- (void)revert:(id)sender;
@end

@implementation FooUIPanelsEditorActions
- (std::shared_ptr<Editor>)editor {
    auto it = g_editors.find(*self.key);
    return it == g_editors.end() ? nullptr : it->second;
}
- (BOOL)run {
    auto ed = [self editor];
    if (!ed) return NO;
    if (ed->apply && !ed->apply(ed->text.string.UTF8String ?: "")) return NO;
    highlight_editor(ed->text);
    recheck_editor(*ed);
    return YES;
}
- (void)apply:(id)sender { [self run]; }
- (void)ok:(id)sender { if ([self run]) pui::ui::close_text_editor(*self.key); }
- (void)cancel:(id)sender { pui::ui::close_text_editor(*self.key); }
- (void)revert:(id)sender {
    if (auto ed = [self editor]) { ed->text.string = ns(ed->original); [self run]; }
}
- (void)textDidChange:(NSNotification*)n {
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(recheck) object:nil];
    [self performSelector:@selector(recheck) withObject:nil afterDelay:0.4];
}
- (void)recheck { if (auto ed = [self editor]) { highlight_editor(ed->text); recheck_editor(*ed); } }
- (void)textViewDidChangeSelection:(NSNotification*)n { if (auto ed = [self editor]) update_editor_status(*ed); }
- (void)dealloc { [NSObject cancelPreviousPerformRequestsWithTarget:self]; delete self.key; }
@end

namespace pui::ui {

namespace mac {
std::unique_ptr<ViewHost> create_root_view(View* view, const ViewOptions& opts) {
    auto h = std::make_unique<MacViewHost>(view, opts);
    NSView* ns = (__bridge NSView*)h->native();
    ns.frame = NSMakeRect(0, 0, 640, 480);
    ns.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    h->attach_to(nil);
    return h;
}

void set_root_zoom(ViewHost& root, double zoom) {
    FooUIPanelsView* v = (__bridge FooUIPanelsView*)root.native();
    v.zoom = zoom;
    [v applyZoom];
    NSSize b = v.bounds.size;
    if (auto* h = host_of(v); h && h->live()) h->view()->on_resize((int)b.width, (int)b.height);
    [v setNeedsDisplay:YES];
    for (NSView* sub in v.subviews) [sub setNeedsDisplay:YES];
}

double zoom_factor() { return zoom_factor_for(zoom_setting(), 96); }
}

std::unique_ptr<ViewHost> create_child_view(MainWindow& root, View* view, const ViewOptions& opts) {
    auto h = std::make_unique<MacViewHost>(view, opts);
    h->attach_to((__bridge NSView*)root.native());
    return h;
}

std::unique_ptr<ViewHost> create_popup_window(MainWindow& root, View* view, int w, int h,
                                              const std::string& title, std::function<void()> on_closed) {
    auto host = std::make_unique<MacViewHost>(view, ViewOptions{});
    host->open_as_popup((__bridge NSView*)root.native(), w, h, title, std::move(on_closed));
    return host;
}

std::unique_ptr<TextField> create_text_field(ViewHost& owner, TextFieldDelegate* delegate, const TextFieldStyle& style) {
    return std::make_unique<MacTextField>((__bridge NSView*)owner.native(), delegate, style);
}

std::unique_ptr<EmbeddedPanel> create_embedded_ui_element(MainWindow& root, const char* name) {
    if (!name || strcasecmp(name, "Panels UI") == 0) return nullptr;
    NSString* want = ns(name).lowercaseString;
    service_enum_t<ui_element_mac> e;
    ui_element_mac::ptr el;
    while (e.next(el)) {
        NSString* got = [NSString stringWithUTF8String:el->get_name()->c_str()].lowercaseString;
        if (!el->match_name(name) && ![got containsString:want]) continue;
        if (strcasecmp(el->get_name()->c_str(), "Panels UI") == 0) continue;
        try {
            id obj = fb2k::unwrapNSObject(el->instantiate(fb2k::wrapNSObject(@{})));
            if ([obj isKindOfClass:[NSViewController class]])
                return std::make_unique<MacEmbeddedPanel>((NSViewController*)obj, (__bridge NSView*)root.native());
        } catch (...) {}
    }
    return nullptr;
}

int popup_menu(ViewHost* anchor, int x, int y, const Menu& menu) {
    FooUIPanelsMenuTarget* t = [FooUIPanelsMenuTarget new];
    NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
    build_menu(m, menu, t);
    if (!anchor) return run_menu(m, t, nil, [NSEvent mouseLocation]);
    return run_menu(m, t, (__bridge NSView*)anchor->native(), NSMakePoint(x, y));
}

int popup_menu_at_cursor(MainWindow&, const Menu& menu) {
    return popup_menu(nullptr, 0, 0, menu);
}

void track_context_menu(ViewHost& anchor, int x, int y, const metadb_handle_list& tracks) {
    auto cm = contextmenu_manager::g_create();
    cm->init_context(tracks, contextmenu_manager::flag_show_shortcuts);
    contextmenu_node* root = cm->get_root();
    if (!root) return;
    std::vector<contextmenu_node*> cmds;
    FooUIPanelsMenuTarget* t = [FooUIPanelsMenuTarget new];
    std::function<void(NSMenu*, contextmenu_node*)> build = [&](NSMenu* m, contextmenu_node* parent) {
        m.autoenablesItems = NO;
        for (t_size i = 0; i < parent->get_num_children(); ++i) {
            contextmenu_node* n = parent->get_child(i);
            switch (n->get_type()) {
            case contextmenu_item_node::type_separator: [m addItem:[NSMenuItem separatorItem]]; break;
            case contextmenu_item_node::type_group: {
                NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(n->get_name()) action:nil keyEquivalent:@""];
                NSMenu* sub = [[NSMenu alloc] initWithTitle:mi.title];
                build(sub, n);
                mi.submenu = sub;
                [m addItem:mi];
                break;
            }
            case contextmenu_item_node::type_command: {
                NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(n->get_name()) action:@selector(picked:) keyEquivalent:@""];
                const unsigned f = n->get_display_flags();
                mi.enabled = !(f & (contextmenu_item_node::FLAG_DISABLED | contextmenu_item_node::FLAG_GRAYED));
                mi.state = (f & (contextmenu_item_node::FLAG_CHECKED | contextmenu_item_node::FLAG_RADIOCHECKED))
                               ? NSControlStateValueOn : NSControlStateValueOff;
                cmds.push_back(n);
                mi.tag = (NSInteger)cmds.size(); mi.target = t;
                [m addItem:mi];
                break;
            }
            default: break;
            }
        }
    };
    NSMenu* m = [[NSMenu alloc] initWithTitle:@""];
    build(m, root);
    int id = run_menu(m, t, (__bridge NSView*)anchor.native(), NSMakePoint(x, y));
    if (id > 0 && id <= (int)cmds.size()) cmds[id - 1]->execute();
}

bool choose_color(ViewHost*, gfx::Color& c) {
    NSAlert* a = [NSAlert new];
    a.messageText = @"Choose a colour";
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    NSColorWell* well = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 120, 32)];
    well.color = color_of(c);
    a.accessoryView = well;
    [NSColorPanel sharedColorPanel].worksWhenModal = YES;
    if ([a runModal] != NSAlertFirstButtonReturn) return false;
    NSColor* s = [well.color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (!s) return false;
    c = gfx::Color((int)std::lround(s.redComponent * 255), (int)std::lround(s.greenComponent * 255),
                   (int)std::lround(s.blueComponent * 255));
    return true;
}

void message_box(ViewHost*, const std::string& title, const std::string& text) {
    NSAlert* a = [NSAlert new];
    a.alertStyle = NSAlertStyleWarning;
    a.messageText = ns(title);
    a.informativeText = ns(text);
    [a runModal];
}

std::string choose_folder(MainWindow&, const std::string& title, const std::string& start) {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.canChooseDirectories = YES;
    p.canChooseFiles = NO;
    p.allowsMultipleSelection = NO;
    p.message = ns(title);
    if (!start.empty()) p.directoryURL = [NSURL fileURLWithPath:ns(start)];
    if ([p runModal] != NSModalResponseOK || !p.URL.fileSystemRepresentation) return {};
    return p.URL.fileSystemRepresentation;
}

void open_url(const std::string& url) {
    if (NSURL* u = [NSURL URLWithString:ns(url)]) [[NSWorkspace sharedWorkspace] openURL:u];
}

void open_file(const std::string& path) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns(path)]];
}

void reveal_in_file_manager(const std::string& path) {
    BOOL dir = NO;
    NSString* p = ns(path);
    if ([[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&dir] && dir)
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p]];
    else
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:p] ]];
}

bool copy_to_clipboard(const std::string& text) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:ns(text) forType:NSPasteboardTypeString];
}

std::string os_version() {
    NSOperatingSystemVersion v = [NSProcessInfo processInfo].operatingSystemVersion;
    return std::to_string(v.majorVersion) + "." + std::to_string(v.minorVersion) + "." + std::to_string(v.patchVersion);
}

std::string wine_version() { return {}; }

std::string home_dir() {
    NSString* h = NSHomeDirectory();
    return h.fileSystemRepresentation ? h.fileSystemRepresentation : "";
}

void open_text_editor(MainWindow&, const std::string& key, const std::string& title,
                      const std::string& text, std::function<bool(const std::string&)> apply) {
    auto it = g_editors.find(key);
    if (it != g_editors.end()) { [it->second->window makeKeyAndOrderFront:nil]; return; }
    auto ed = std::make_shared<Editor>();
    ed->apply = std::move(apply);
    ed->original = text;
    const CGFloat W = 820, H = 560;
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, W, H)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO;
    w.title = ns(title);
    NSView* content = w.contentView;
    const CGFloat pad = 8, bw = 80, bh = 28, sh = 18;
    NSScrollView* sc = [[NSScrollView alloc] initWithFrame:NSMakeRect(pad, bh + sh + 3 * pad, W - 2 * pad, H - bh - sh - 4 * pad)];
    sc.hasVerticalScroller = YES;
    sc.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    NSTextView* tv = [[NSTextView alloc] initWithFrame:sc.contentView.bounds];
    tv.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    tv.string = ns(text);
    tv.richText = NO;
    tv.allowsUndo = YES;
    tv.automaticQuoteSubstitutionEnabled = NO;
    tv.automaticDashSubstitutionEnabled = NO;
    tv.automaticTextReplacementEnabled = NO;
    tv.autoresizingMask = NSViewWidthSizable;
    sc.documentView = tv;
    [content addSubview:sc];
    NSTextField* status = [NSTextField labelWithString:@""];
    status.frame = NSMakeRect(pad, bh + 2 * pad, W - 2 * pad, sh);
    status.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    status.lineBreakMode = NSLineBreakByTruncatingTail;
    status.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    status.textColor = NSColor.secondaryLabelColor;
    [content addSubview:status];
    FooUIPanelsEditorActions* act = [FooUIPanelsEditorActions new];
    act.key = new std::string(key);
    tv.delegate = act;
    struct { NSString* t; SEL s; NSString* key; NSEventModifierFlags mods; } btns[] = {
        { @"Cancel", @selector(cancel:), @"\e", 0 },
        { @"OK", @selector(ok:), @"\r", NSEventModifierFlagCommand },
        { @"Apply", @selector(apply:), @"s", NSEventModifierFlagCommand } };
    CGFloat x = W - pad - bw;
    for (auto& b : btns) {
        NSButton* btn = [NSButton buttonWithTitle:b.t target:act action:b.s];
        btn.frame = NSMakeRect(x, pad, bw, bh);
        btn.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
        btn.keyEquivalent = b.key;
        btn.keyEquivalentModifierMask = b.mods;
        [content addSubview:btn];
        x -= bw + pad;
    }
    NSButton* revert = [NSButton buttonWithTitle:@"Revert" target:act action:@selector(revert:)];
    revert.frame = NSMakeRect(pad, pad, bw, bh);
    revert.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    [content addSubview:revert];
    ed->window = w; ed->text = tv; ed->status = status;
    ed->delegate = [FooUIPanelsWindowDelegate new];
    std::string k = key;
    __block FooUIPanelsEditorActions* keep = act;
    ed->delegate.onClose = ^{ keep = nil; dispatch_async(dispatch_get_main_queue(), ^{ g_editors.erase(k); }); };
    w.delegate = ed->delegate;
    g_editors[key] = ed;
    highlight_editor(tv);
    recheck_editor(*ed);
    [w center];
    [w makeKeyAndOrderFront:nil];
}

void close_text_editor(const std::string& key) {
    auto it = g_editors.find(key);
    if (it != g_editors.end()) [it->second->window close];
}

}

namespace pui {

std::string component_dir() {
    pfc::string8 native;
    try { filesystem::g_get_native_path(core_api::get_profile_path(), native); } catch (...) {}
    std::string d = std::string(native.c_str()) + "/foo_ui_panels";
    mkdir(d.c_str(), 0755);
    return d;
}

}
