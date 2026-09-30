// Windows implementation of the ui:: layer (src/ui/view.h): every native panel is a ui::View
// hosted in a plain child HWND (WinViewHost), painted double-buffered through a GdiCanvas.
#include "win_sdk.h"
#include <windowsx.h>
#include <commdlg.h>
#include "gdi_canvas.h"
#include "panel_host.h"
#include "../../ui/view.h"
#include "../../core/skin_paths.h"
#include <atomic>
#include <chrono>
#include <map>
#include <thread>

namespace pui::ui {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

HINSTANCE module() { return core_api::get_my_instance(); }

unsigned key_mods() {
    unsigned m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= kShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= kCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) m |= kAlt;
    return m;
}

int map_key(WPARAM vk) {
    switch (vk) {
    case VK_RETURN: return kKeyEnter;   case VK_ESCAPE: return kKeyEscape;
    case VK_TAB: return kKeyTab;        case VK_BACK: return kKeyBackspace;
    case VK_DELETE: return kKeyDelete;
    case VK_LEFT: return kKeyLeft;      case VK_RIGHT: return kKeyRight;
    case VK_UP: return kKeyUp;          case VK_DOWN: return kKeyDown;
    case VK_PRIOR: return kKeyPageUp;   case VK_NEXT: return kKeyPageDown;
    case VK_HOME: return kKeyHome;      case VK_END: return kKeyEnd;
    case VK_F1: return kKeyF1; case VK_F2: return kKeyF2; case VK_F3: return kKeyF3;
    case VK_F4: return kKeyF4; case VK_F5: return kKeyF5;
    case VK_SPACE: return ' ';
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return (int)vk;
    return kKeyUnknown;
}

HCURSOR load_cursor(Cursor c) {
    switch (c) {
    case Cursor::Hand: return LoadCursor(nullptr, IDC_HAND);
    case Cursor::IBeam: return LoadCursor(nullptr, IDC_IBEAM);
    default: return LoadCursor(nullptr, IDC_ARROW);
    }
}

// ---------------------------------------------------------------------------------------------
class WinTextField;
const wchar_t* kTextFieldProp = L"pui.textfield";

class WinViewHost : public ViewHost {
public:
    WinViewHost(View* view, const ViewOptions& opts) : m_view(view), m_opts(opts) {
        m_cursor = load_cursor(opts.cursor);
    }
    ~WinViewHost() override {
        m_onClosed = nullptr;
        if (m_wnd) DestroyWindow(m_wnd);
    }

    bool create_child(HWND parent) {
        register_classes();
        m_root = parent;
        m_wnd = CreateWindowExW(0, m_opts.double_clicks ? kClassDbl : kClass, L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0,
                                parent, nullptr, module(), this);
        return m_wnd != nullptr;
    }

    bool create_popup(HWND owner, int w, int h, const std::string& title, std::function<void()> onClosed) {
        register_classes();
        m_popup = true;
        m_onClosed = std::move(onClosed);
        DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        RECT rc = { 0, 0, w, h };
        AdjustWindowRect(&rc, style, FALSE);
        int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
        RECT orc = {}; if (owner) GetWindowRect(owner, &orc); // centre on the owner window
        int x = orc.left + ((orc.right - orc.left) - ww) / 2;
        int y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
        m_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, m_opts.double_clicks ? kClassDbl : kClass,
                                widen(title).c_str(), style, x, y, ww, wh, owner, nullptr, module(), this);
        return m_wnd != nullptr;
    }

    void start() {
        m_view->attach_host(this);
        m_view->on_attached();
        if (m_opts.render_fps > 0) {
            // timeBeginPeriod sharpens the frame sleep (default Windows/Wine granularity is
            // ~15.6ms, which makes frames land unevenly and animations stutter).
            m_highResTimer = timeBeginPeriod(1) != TIMERR_NOCANDO;
            m_render = std::thread([this] { render_loop(); });
        }
    }

    // --- ViewHost ---
    void invalidate() override { if (m_wnd && m_opts.render_fps <= 0) InvalidateRect(m_wnd, nullptr, FALSE); }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        if (!m_wnd) return;
        SetWindowPos(m_wnd, to_top ? HWND_TOP : nullptr, r.x, r.y, r.w, r.h,
                     SWP_NOACTIVATE | (to_top ? 0 : SWP_NOZORDER));
    }
    gfx::Rect bounds() const override {
        if (!m_wnd) return {};
        RECT rc; GetClientRect(m_wnd, &rc);
        POINT org = { 0, 0 };
        if (!m_popup && m_root) MapWindowPoints(m_wnd, m_root, &org, 1);
        return gfx::Rect{ org.x, org.y, rc.right, rc.bottom };
    }
    void show(bool v) override { if (m_wnd) ShowWindow(m_wnd, v ? SW_SHOW : SW_HIDE); }
    bool visible() const override { return m_wnd && IsWindowVisible(m_wnd); }
    void set_timer(int id, int ms) override { if (m_wnd) SetTimer(m_wnd, (UINT_PTR)id, (UINT)ms, nullptr); }
    void kill_timer(int id) override { if (m_wnd) KillTimer(m_wnd, (UINT_PTR)id); }
    void capture_mouse(bool c) override { if (c && m_wnd) SetCapture(m_wnd); else if (!c && GetCapture() == m_wnd) ReleaseCapture(); }
    void focus() override {
        if (!m_wnd) return;
        if (m_popup) SetForegroundWindow(m_wnd);
        SetFocus(m_wnd);
    }
    void set_cursor(Cursor c) override {
        m_cursor = load_cursor(c);
        POINT pt; GetCursorPos(&pt);
        if (m_wnd && WindowFromPoint(pt) == m_wnd) SetCursor(m_cursor);
    }
    void* native() const override { return m_wnd; }

private:
    static constexpr const wchar_t* kClass = L"foo_ui_panels_view";
    static constexpr const wchar_t* kClassDbl = L"foo_ui_panels_view_dbl";

    static void register_classes() {
        static bool done = false;
        if (done) return;
        done = true;
        for (int i = 0; i < 2; ++i) {
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.lpfnWndProc = WndProc;
            wc.hInstance = module();
            wc.lpszClassName = i ? kClassDbl : kClass;
            wc.hCursor = nullptr; // WM_SETCURSOR picks the view's cursor
            wc.style = i ? CS_DBLCLKS : 0;
            RegisterClassExW(&wc);
        }
    }

    void paint_now() {
        RECT rc; GetClientRect(m_wnd, &rc);
        PAINTSTRUCT ps; HDC dc = BeginPaint(m_wnd, &ps);
        if (m_opts.render_fps <= 0 && rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            { gfx::GdiCanvas cv(mem, rc.right, rc.bottom); m_view->paint(cv); }
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        }
        EndPaint(m_wnd, &ps);
    }

    void render_loop() {
        // Nothing in here sends messages to the UI thread (GetDC/BitBlt don't), so WM_DESTROY
        // can join us without risking a deadlock.
        using clock = std::chrono::steady_clock;
        const auto frame = std::chrono::microseconds(1000000 / m_opts.render_fps);
        auto next = clock::now();
        HDC mem = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; int bw = 0, bh = 0;
        while (!m_stop.load()) {
            RECT rc; GetClientRect(m_wnd, &rc);
            if (IsWindowVisible(m_wnd) && rc.right > 0 && rc.bottom > 0) {
                if (HDC dc = GetDC(m_wnd)) {
                    // Cached backbuffer: recreate only on resize (per-frame DC/bitmap churn is a
                    // classic GDI bottleneck that makes the animation stutter).
                    if (!mem || bw != rc.right || bh != rc.bottom) {
                        if (old) SelectObject(mem, old);
                        if (bmp) DeleteObject(bmp);
                        if (mem) DeleteDC(mem);
                        mem = CreateCompatibleDC(dc);
                        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
                        old = SelectObject(mem, bmp);
                        bw = rc.right; bh = rc.bottom;
                    }
                    { gfx::GdiCanvas cv(mem, bw, bh); m_view->paint(cv); }
                    BitBlt(dc, 0, 0, bw, bh, mem, 0, 0, SRCCOPY);
                    ReleaseDC(m_wnd, dc);
                }
            }
            next += frame;
            auto now = clock::now();
            if (next > now) std::this_thread::sleep_for(next - now);
            else next = now; // fell behind (e.g. system stall): don't try to catch up in a burst
        }
        if (old) SelectObject(mem, old);
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
    }

    void stop_render() {
        m_stop = true;
        if (m_render.joinable()) m_render.join();
        if (m_highResTimer) { timeEndPeriod(1); m_highResTimer = false; }
    }

    MouseEvent mouse_event(LPARAM lp, MouseButton b, bool dbl) const {
        MouseEvent e;
        e.x = GET_X_LPARAM(lp); e.y = GET_Y_LPARAM(lp);
        e.button = b; e.mods = key_mods(); e.double_click = dbl;
        return e;
    }

    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_wnd = nullptr, m_root = nullptr;
    View* m_view;
    ViewOptions m_opts;
    HCURSOR m_cursor = nullptr;
    bool m_tracking = false, m_popup = false, m_highResTimer = false;
    std::function<void()> m_onClosed;
    std::thread m_render;
    std::atomic<bool> m_stop{ false };

    friend class WinTextField;
};

// ---------------------------------------------------------------------------------------------
class WinTextField : public TextField {
public:
    WinTextField(HWND parent, TextFieldDelegate* d, const TextFieldStyle& st) : m_delegate(d), m_style(st) {
        m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, parent, nullptr, module(), nullptr);
        const gfx::FontSpec& f = st.font;
        m_font = CreateFontW(f.points ? -MulDiv((int)f.size, 96, 72) : -(int)f.size, 0, 0, 0,
                             f.bold ? FW_BOLD : FW_NORMAL, f.italic, f.underline, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, widen(f.face).c_str());
        m_bg = CreateSolidBrush(gfx::to_colorref(st.background));
        SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_font, TRUE);
        if (!st.placeholder.empty()) SendMessageW(m_edit, EM_SETCUEBANNER, TRUE, (LPARAM)widen(st.placeholder).c_str());
        SetPropW(m_edit, kTextFieldProp, this);
        m_prevProc = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    }
    ~WinTextField() override {
        if (m_edit && IsWindow(m_edit)) {
            SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)m_prevProc);
            RemovePropW(m_edit, kTextFieldProp);
            DestroyWindow(m_edit);
        }
        if (m_font) DeleteObject(m_font);
        if (m_bg) DeleteObject(m_bg);
    }
    void set_bounds(const gfx::Rect& r) override { MoveWindow(m_edit, r.x, r.y, r.w, r.h, TRUE); }
    std::string text() const override {
        int n = GetWindowTextLengthW(m_edit);
        std::wstring w(n, L'\0');
        if (n > 0) GetWindowTextW(m_edit, &w[0], n + 1);
        return narrow(w);
    }
    void set_text(const std::string& s) override { SetWindowTextW(m_edit, widen(s).c_str()); }
    void focus() override { SetFocus(m_edit); }

    // Parent-window notifications, routed here by WinViewHost.
    static WinTextField* from(HWND edit) { return edit ? (WinTextField*)GetPropW(edit, kTextFieldProp) : nullptr; }
    void on_command(WORD code) { if (code == EN_CHANGE && m_delegate) m_delegate->on_text_changed(); }
    LRESULT on_ctlcolor(HDC dc) {
        SetTextColor(dc, gfx::to_colorref(m_style.text));
        SetBkColor(dc, gfx::to_colorref(m_style.background));
        return (LRESULT)m_bg;
    }

private:
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        WinTextField* self = from(h);
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        if (msg == WM_KEYDOWN && wp == VK_RETURN) { if (self->m_delegate) self->m_delegate->on_enter(); return 0; }
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { if (self->m_delegate) self->m_delegate->on_escape(); return 0; }
        if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0; // no beep
        return CallWindowProcW(self->m_prevProc, h, msg, wp, lp);
    }

    HWND m_edit = nullptr;
    WNDPROC m_prevProc = nullptr;
    HFONT m_font = nullptr;
    HBRUSH m_bg = nullptr;
    TextFieldDelegate* m_delegate;
    TextFieldStyle m_style;
};

LRESULT CALLBACK WinViewHost::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    WinViewHost* self = reinterpret_cast<WinViewHost*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<WinViewHost*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->m_wnd = wnd;
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);
    View* v = self->m_view;
    const bool live = v->host() == self; // attached (start() ran)
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: if (live) { self->paint_now(); return 0; } break;
    case WM_SIZE: if (live) v->on_resize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_TIMER: if (live) v->on_timer((int)wp); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(self->m_cursor); return TRUE; }
        break;
    case WM_MOUSEMOVE:
        if (!self->m_tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme); self->m_tracking = true;
        }
        if (live) v->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), key_mods(), (wp & MK_LBUTTON) != 0);
        return 0;
    case WM_MOUSELEAVE:
        self->m_tracking = false;
        if (live) v->on_mouse_leave();
        return 0;
    // Take keyboard focus on a press: a panel that handles keys (playlist Ctrl+A / Delete)
    // only gets WM_KEYDOWN while it holds focus, and clicking it is what says "this one".
    case WM_LBUTTONDOWN:   SetFocus(wnd); if (live) v->on_mouse_down(self->mouse_event(lp, MouseButton::Left, false)); return 0;
    case WM_LBUTTONDBLCLK: if (live) v->on_mouse_down(self->mouse_event(lp, MouseButton::Left, true)); return 0;
    case WM_RBUTTONDOWN:   SetFocus(wnd); if (live) v->on_mouse_down(self->mouse_event(lp, MouseButton::Right, false)); return 0;
    case WM_RBUTTONDBLCLK: if (live) v->on_mouse_down(self->mouse_event(lp, MouseButton::Right, true)); return 0;
    case WM_MBUTTONDOWN:   if (live) v->on_mouse_down(self->mouse_event(lp, MouseButton::Middle, false)); return 0;
    case WM_LBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Left, false)); return 0;
    case WM_RBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Right, false)); return 0;
    case WM_MBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Middle, false)); return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(wnd, &pt);
        if (live) v->on_wheel(pt.x, pt.y, (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (live && v->on_key_down(map_key(wp), key_mods())) return 0;
        // Unhandled: configured keyboard shortcuts still work while a panel has focus.
        if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
        break;
    case WM_SHOWWINDOW: if (live) v->on_visibility(wp != 0); break;
    case WM_SETFOCUS: if (live) v->on_focus(); return 0;
    case WM_COMMAND:
        if (WinTextField* tf = WinTextField::from((HWND)lp)) { tf->on_command(HIWORD(wp)); return 0; }
        break;
    case WM_CTLCOLOREDIT:
        if (WinTextField* tf = WinTextField::from((HWND)lp)) return tf->on_ctlcolor((HDC)wp);
        break;
    case WM_CLOSE:
        if (self->m_popup) { DestroyWindow(wnd); return 0; }
        break;
    case WM_DESTROY:
        self->stop_render();
        if (live) v->on_destroy();
        return 0;
    case WM_NCDESTROY: {
        SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        self->m_wnd = nullptr;
        auto cb = std::move(self->m_onClosed);
        self->m_onClosed = nullptr;
        if (cb) cb(); // may delete `self`
        return 0;
    }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------
class WinEmbeddedPanel : public EmbeddedPanel {
public:
    ~WinEmbeddedPanel() override { m_host.destroy(); }
    bool create(HWND parent, const char* name) { return m_host.create(parent, name) != nullptr; }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        if (HWND w = m_host.wnd())
            SetWindowPos(w, to_top ? HWND_TOP : nullptr, r.x, r.y, r.w, r.h, SWP_NOACTIVATE | (to_top ? 0 : SWP_NOZORDER));
    }
    void show(bool v) override { if (HWND w = m_host.wnd()) ShowWindow(w, v ? SW_SHOW : SW_HIDE); }
private:
    PanelHost m_host;
};

// ---------------------------------------------------------------------------------------------
void append_menu(HMENU m, const Menu& items) {
    for (const auto& it : items) {
        if (it.separator) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); continue; }
        UINT flags = MF_STRING | (it.checked ? MF_CHECKED : 0) | (it.enabled ? 0 : MF_GRAYED);
        if (!it.children.empty()) {
            HMENU sub = CreatePopupMenu();
            append_menu(sub, it.children);
            AppendMenuW(m, flags | MF_POPUP, (UINT_PTR)sub, widen(it.label).c_str());
        } else {
            AppendMenuW(m, flags, (UINT_PTR)it.id, widen(it.label).c_str());
        }
    }
}

int track_menu(HWND owner, POINT screen, const Menu& menu, UINT extra) {
    HMENU m = CreatePopupMenu();
    append_menu(m, menu);
    SetForegroundWindow(owner);
    int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | extra, screen.x, screen.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m); // also destroys the submenus
    return cmd;
}

// "Edit code..." windows, by key.
std::map<std::string, HWND> g_editors;
enum { IDC_CODE = 100, IDC_APPLY, IDC_OK, IDC_CANCEL };
const wchar_t* kEditorClass = L"foo_ui_panels_codeedit";

struct EditorState {
    std::string key;
    std::function<bool(const std::string&)> apply;
    HFONT mono = nullptr;
};

LRESULT CALLBACK EditorProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<EditorState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(wnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_SIZE: {
        const int w = LOWORD(lp), h = HIWORD(lp), bw = 80, bh = 26, pad = 8;
        MoveWindow(GetDlgItem(wnd, IDC_CODE), pad, pad, w - 2 * pad, h - 3 * pad - bh, TRUE);
        int x = w - pad - bw, y = h - pad - bh;
        MoveWindow(GetDlgItem(wnd, IDC_CANCEL), x, y, bw, bh, TRUE); x -= bw + pad;
        MoveWindow(GetDlgItem(wnd, IDC_OK),     x, y, bw, bh, TRUE); x -= bw + pad;
        MoveWindow(GetDlgItem(wnd, IDC_APPLY),  x, y, bw, bh, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if ((id == IDC_APPLY || id == IDC_OK) && st) {
            HWND ed = GetDlgItem(wnd, IDC_CODE);
            int n = GetWindowTextLengthW(ed);
            std::wstring w(n + 1, L'\0');
            GetWindowTextW(ed, &w[0], n + 1); w.resize(n);
            if (st->apply && !st->apply(narrow(w))) return 0;
            if (id == IDC_OK) DestroyWindow(wnd);
            return 0;
        }
        if (id == IDC_CANCEL) { DestroyWindow(wnd); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(wnd); return 0;
    case WM_NCDESTROY:
        if (st) {
            g_editors.erase(st->key);
            if (st->mono) DeleteObject(st->mono);
            delete st;
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace

// --- factories --------------------------------------------------------------------------------
std::unique_ptr<ViewHost> create_child_view(MainWindow& root, View* view, const ViewOptions& opts) {
    auto h = std::make_unique<WinViewHost>(view, opts);
    if (!h->create_child((HWND)root.native())) return nullptr;
    h->start();
    return h;
}

std::unique_ptr<ViewHost> create_popup_window(MainWindow& root, View* view, int w, int h,
                                              const std::string& title, std::function<void()> on_closed) {
    auto host = std::make_unique<WinViewHost>(view, ViewOptions{});
    if (!host->create_popup((HWND)root.native(), w, h, title, std::move(on_closed))) return nullptr;
    host->start();
    return host;
}

std::unique_ptr<TextField> create_text_field(ViewHost& owner, TextFieldDelegate* delegate, const TextFieldStyle& style) {
    return std::make_unique<WinTextField>((HWND)owner.native(), delegate, style);
}

std::unique_ptr<EmbeddedPanel> create_embedded_ui_element(MainWindow& root, const char* name) {
    auto p = std::make_unique<WinEmbeddedPanel>();
    if (!p->create((HWND)root.native(), name)) return nullptr;
    return p;
}

int popup_menu(ViewHost* anchor, int x, int y, const Menu& menu) {
    HWND owner = anchor ? (HWND)anchor->native() : (HWND)core_api::get_main_window();
    POINT pt = { x, y };
    if (anchor) ClientToScreen(owner, &pt); else GetCursorPos(&pt);
    return track_menu(owner, pt, menu, TPM_RIGHTBUTTON);
}

int popup_menu_at_cursor(MainWindow& root, const Menu& menu) {
    POINT pt; GetCursorPos(&pt);
    ReleaseCapture();
    return track_menu((HWND)root.native(), pt, menu, TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN);
}

void track_context_menu(ViewHost& anchor, int x, int y, const metadb_handle_list& tracks) {
    HWND wnd = (HWND)anchor.native();
    POINT pt = { x, y }; ClientToScreen(wnd, &pt);
    contextmenu_manager::win32_run_menu_context(wnd, tracks, &pt, contextmenu_manager::flag_show_shortcuts);
}

bool choose_color(ViewHost* owner, gfx::Color& c) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = owner ? (HWND)owner->native() : (HWND)core_api::get_main_window();
    cc.rgbResult = gfx::to_colorref(c); cc.lpCustColors = custom;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    c = gfx::from_colorref(cc.rgbResult);
    return true;
}

void message_box(ViewHost* owner, const std::string& title, const std::string& text) {
    MessageBoxW(owner ? (HWND)owner->native() : (HWND)core_api::get_main_window(),
                widen(text).c_str(), widen(title).c_str(), MB_ICONERROR);
}

void open_text_editor(MainWindow& root, const std::string& key, const std::string& title,
                      const std::string& text, std::function<bool(const std::string&)> apply) {
    auto it = g_editors.find(key);
    if (it != g_editors.end()) { SetForegroundWindow(it->second); return; }

    static bool reg = false;
    if (!reg) {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = EditorProc;
        wc.hInstance     = module();
        wc.lpszClassName = kEditorClass;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
    }
    auto* st = new EditorState{ key, std::move(apply) };
    HWND wnd = CreateWindowExW(0, kEditorClass, widen(title).c_str(),
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
                               (HWND)root.native(), nullptr, module(), st);
    if (!wnd) { delete st; return; }
    g_editors[key] = wnd;
    // Multi-line, word-wrapped (the extracted scripts are mostly one very long line).
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(text).c_str(),
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_CODE, module(), nullptr);
    SendMessageW(ed, EM_SETLIMITTEXT, 0, 0); // no 32K cap
    st->mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    SendMessageW(ed, WM_SETFONT, (WPARAM)st->mono, TRUE);
    HFONT ui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    const struct { int id; const wchar_t* label; } btns[] = {
        { IDC_APPLY, L"Apply" }, { IDC_OK, L"OK" }, { IDC_CANCEL, L"Cancel" } };
    for (auto& b : btns) {
        HWND h = CreateWindowExW(0, L"BUTTON", b.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 0, 0, wnd, (HMENU)(INT_PTR)b.id, module(), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)ui, TRUE);
    }
    RECT rc; GetClientRect(wnd, &rc);
    SendMessageW(wnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    SetFocus(ed);
}

void close_text_editor(const std::string& key) {
    auto it = g_editors.find(key);
    if (it != g_editors.end()) DestroyWindow(it->second); // WM_NCDESTROY erases the entry
}

} // namespace pui::ui

namespace pui {

std::string component_dir() {
    wchar_t mod[MAX_PATH] = {};
    GetModuleFileNameW(core_api::get_my_instance(), mod, MAX_PATH);
    std::wstring wd(mod);
    auto s = wd.find_last_of(L"\\/");
    if (s != std::wstring::npos) wd.resize(s);
    return ui::narrow(wd);
}

} // namespace pui
