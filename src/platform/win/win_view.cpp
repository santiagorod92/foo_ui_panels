#include "win_sdk.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <richedit.h>
#include "../../core/skin_lint.h"
#include "../../core/fs_util.h"
#include "gdi_canvas.h"
#include "panel_host.h"
#include "tooltip.h"
#include "drop_files.h"
#include "zoom.h"
#include "../../ui/host_input.h"
#include "../../core/skin_paths.h"
#include "../../core/ui_logic.h"
#include "../../core/ui_settings.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

namespace pui::win {

namespace { std::atomic<double> g_zoom{ 1.0 }; }

double zoom() { return g_zoom.load(std::memory_order_relaxed); }

UINT window_dpi(HWND w) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && w) { const UINT d = fn(w); if (d) return d; }
    HDC dc = GetDC(nullptr);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d > 0 ? (UINT)d : 96;
}

void refresh_zoom(HWND main) {
    g_zoom.store(zoom_factor_for(zoom_setting(), (int)window_dpi(main)), std::memory_order_relaxed);
}

}

namespace pui::ui {

namespace {

int dev(int v) { return to_device(v, win::zoom()); }
int logi(int v) { return to_logical(v, win::zoom()); }
int logi_round(int v) { return (int)std::lround(v / win::zoom()); }

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

class WinTextField;
const wchar_t* kTextFieldProp = L"pui.textfield";

class WinViewHost : public ViewHost {
public:
    WinViewHost(View* view, const ViewOptions& opts) : m_view(view), m_opts(opts) {
        m_cursor = load_cursor(opts.cursor);
    }
    ~WinViewHost() override {
        m_onClosed = nullptr;
        m_tooltip.destroy();
        if (m_wnd) DestroyWindow(m_wnd);
    }

    bool create_child(HWND parent) {
        register_classes();
        m_root = parent;
        m_wnd = CreateWindowExW(0, m_opts.double_clicks ? kClassDbl : kClass, widen(m_opts.accessible_name).c_str(),
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0,
                                parent, nullptr, module(), this);
        if (m_wnd && m_opts.accept_files) DragAcceptFiles(m_wnd, TRUE);
        return m_wnd != nullptr;
    }

    bool create_popup(HWND owner, int w, int h, const std::string& title, std::function<void()> onClosed) {
        register_classes();
        m_popup = true;
        m_onClosed = std::move(onClosed);
        DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        RECT rc = { 0, 0, dev(w), dev(h) };
        AdjustWindowRect(&rc, style, FALSE);
        int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
        RECT orc = {}; if (owner) GetWindowRect(owner, &orc);
        int x = orc.left + ((orc.right - orc.left) - ww) / 2;
        int y = orc.top + ((orc.bottom - orc.top) - wh) / 2;
        m_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, m_opts.double_clicks ? kClassDbl : kClass,
                                widen(title).c_str(), style, x, y, ww, wh, owner, nullptr, module(), this);
        return m_wnd != nullptr;
    }

    void start() {
        start_view(*m_view, *this);
        if (m_opts.render_fps > 0) {
            m_highResTimer = timeBeginPeriod(1) != TIMERR_NOCANDO;
            m_render = std::thread([this] { render_loop(); });
        }
    }

    void invalidate() override { if (m_wnd && m_opts.render_fps <= 0) InvalidateRect(m_wnd, nullptr, FALSE); }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        if (!m_wnd) return;
        const int x0 = dev(r.x), y0 = dev(r.y);
        SetWindowPos(m_wnd, to_top ? HWND_TOP : nullptr, x0, y0, dev(r.right()) - x0, dev(r.bottom()) - y0,
                     SWP_NOACTIVATE | (to_top ? 0 : SWP_NOZORDER));
    }
    gfx::Rect bounds() const override {
        if (!m_wnd) return {};
        RECT rc; GetClientRect(m_wnd, &rc);
        POINT org = { 0, 0 };
        if (!m_popup && m_root) MapWindowPoints(m_wnd, m_root, &org, 1);
        return gfx::Rect{ logi_round(org.x), logi_round(org.y), logi_round(rc.right), logi_round(rc.bottom) };
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
    void set_focus_ring(bool on) override {
        if (m_ring == on) return;
        m_ring = on;
        if (m_wnd) InvalidateRect(m_wnd, nullptr, FALSE);
    }
    void set_cursor(Cursor c) override {
        m_cursor = load_cursor(c);
        POINT pt; GetCursorPos(&pt);
        if (m_wnd && WindowFromPoint(pt) == m_wnd) SetCursor(m_cursor);
    }
    void set_tooltip(const std::string& utf8) override { m_tooltip.set(m_wnd, widen(utf8)); }
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
            wc.hCursor = nullptr;
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
            { gfx::GdiCanvas cv(mem, rc.right, rc.bottom, win::zoom());
              paint_view(*m_view, cv, m_ring && GetFocus() == m_wnd); }
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        }
        EndPaint(m_wnd, &ps);
    }

    void render_loop() {
        using clock = std::chrono::steady_clock;
        const auto frame = std::chrono::microseconds(1000000 / m_opts.render_fps);
        auto next = clock::now();
        HDC mem = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; int bw = 0, bh = 0;
        while (!m_stop.load()) {
            RECT rc; GetClientRect(m_wnd, &rc);
            if (IsWindowVisible(m_wnd) && rc.right > 0 && rc.bottom > 0) {
                if (HDC dc = GetDC(m_wnd)) {
                    if (!mem || bw != rc.right || bh != rc.bottom) {
                        if (old) SelectObject(mem, old);
                        if (bmp) DeleteObject(bmp);
                        if (mem) DeleteDC(mem);
                        mem = CreateCompatibleDC(dc);
                        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
                        old = SelectObject(mem, bmp);
                        bw = rc.right; bh = rc.bottom;
                    }
                    { gfx::GdiCanvas cv(mem, bw, bh, win::zoom()); m_view->paint(cv); }
                    BitBlt(dc, 0, 0, bw, bh, mem, 0, 0, SRCCOPY);
                    ReleaseDC(m_wnd, dc);
                }
            }
            next += frame;
            auto now = clock::now();
            if (next > now) std::this_thread::sleep_for(next - now);
            else next = now;
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
        e.x = logi(GET_X_LPARAM(lp)); e.y = logi(GET_Y_LPARAM(lp));
        e.button = b; e.mods = key_mods(); e.double_click = dbl;
        return e;
    }
    void press(LPARAM lp, MouseButton b, bool dbl, bool live) {
        if (!dbl && press_takes_focus(b)) { SetFocus(m_wnd); set_focus_ring(false); }
        if (live) m_view->on_mouse_down(mouse_event(lp, b, dbl));
    }

    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_wnd = nullptr, m_root = nullptr;
    View* m_view;
    ViewOptions m_opts;
    HCURSOR m_cursor = nullptr;
    win::Tooltip m_tooltip;
    bool m_tracking = false, m_popup = false, m_highResTimer = false;
    bool m_ring = false;
    std::function<void()> m_onClosed;
    std::thread m_render;
    std::atomic<bool> m_stop{ false };

    friend class WinTextField;
};

class WinTextField : public TextField {
public:
    WinTextField(HWND parent, TextFieldDelegate* d, const TextFieldStyle& st) : m_delegate(d), m_style(st) {
        m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, parent, nullptr, module(), nullptr);
        const gfx::FontSpec& f = st.font;
        const int px = f.points ? MulDiv((int)f.size, 96, 72) : (int)f.size;
        m_font = CreateFontW(-dev(px), 0, 0, 0,
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
    void set_bounds(const gfx::Rect& r) override {
        const int x0 = dev(r.x), y0 = dev(r.y);
        MoveWindow(m_edit, x0, y0, dev(r.right()) - x0, dev(r.bottom()) - y0, TRUE);
    }
    std::string text() const override {
        int n = GetWindowTextLengthW(m_edit);
        std::wstring w(n, L'\0');
        if (n > 0) GetWindowTextW(m_edit, &w[0], n + 1);
        return narrow(w);
    }
    void set_text(const std::string& s) override { SetWindowTextW(m_edit, widen(s).c_str()); }
    void focus() override { SetFocus(m_edit); }

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
        if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
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
    const bool live = v->host() == self;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: if (live) { self->paint_now(); return 0; } break;
    case WM_PRINTCLIENT:
        if (live && self->m_opts.render_fps <= 0) {
            RECT rc; GetClientRect(wnd, &rc);
            gfx::GdiCanvas cv((HDC)wp, rc.right, rc.bottom, win::zoom());
            v->paint(cv);
        }
        return 0;
    case WM_SIZE: if (live) v->on_resize(logi_round(LOWORD(lp)), logi_round(HIWORD(lp))); return 0;
    case WM_TIMER: if (live) v->on_timer((int)wp); return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(self->m_cursor); return TRUE; }
        break;
    case WM_MOUSEMOVE:
        if (!self->m_tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme); self->m_tracking = true;
        }
        if (live) v->on_mouse_move(logi(GET_X_LPARAM(lp)), logi(GET_Y_LPARAM(lp)), key_mods(), (wp & MK_LBUTTON) != 0);
        return 0;
    case WM_MOUSELEAVE:
        self->m_tracking = false;
        if (live) v->on_mouse_leave();
        return 0;
    case WM_LBUTTONDOWN:   self->press(lp, MouseButton::Left, false, live); return 0;
    case WM_LBUTTONDBLCLK: self->press(lp, MouseButton::Left, true, live); return 0;
    case WM_RBUTTONDOWN:   self->press(lp, MouseButton::Right, false, live); return 0;
    case WM_RBUTTONDBLCLK: self->press(lp, MouseButton::Right, true, live); return 0;
    case WM_MBUTTONDOWN:   self->press(lp, MouseButton::Middle, false, live); return 0;
    case WM_LBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Left, false)); return 0;
    case WM_RBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Right, false)); return 0;
    case WM_MBUTTONUP:     if (live) v->on_mouse_up(self->mouse_event(lp, MouseButton::Middle, false)); return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(wnd, &pt);
        if (live) v->on_wheel(logi(pt.x), logi(pt.y), (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (dispatch_key(*v, live, self->m_opts, map_key(wp), key_mods())) return 0;
        if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
        break;
    case WM_SHOWWINDOW: if (live) v->on_visibility(wp != 0); break;
    case WM_SETFOCUS: if (live) v->on_focus(); return 0;
    case WM_KILLFOCUS: self->set_focus_ring(false); return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        POINT pt = {};
        auto paths = win::dropped_files(drop, pt);
        DragFinish(drop);
        if (live && !paths.empty()) v->on_drop_files(paths, logi(pt.x), logi(pt.y));
        return 0;
    }
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
        if (cb) cb();
        return 0;
    }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

class WinEmbeddedPanel : public EmbeddedPanel {
public:
    ~WinEmbeddedPanel() override { m_host.destroy(); }
    bool create(HWND parent, const char* name) { return m_host.create(parent, name) != nullptr; }
    void set_bounds(const gfx::Rect& r, bool to_top) override {
        const int x0 = dev(r.x), y0 = dev(r.y);
        if (HWND w = m_host.wnd())
            SetWindowPos(w, to_top ? HWND_TOP : nullptr, x0, y0, dev(r.right()) - x0, dev(r.bottom()) - y0,
                         SWP_NOACTIVATE | (to_top ? 0 : SWP_NOZORDER));
    }
    void show(bool v) override { if (HWND w = m_host.wnd()) ShowWindow(w, v ? SW_SHOW : SW_HIDE); }
private:
    PanelHost m_host;
};

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
    DestroyMenu(m);
    return cmd;
}

std::map<std::string, HWND> g_editors;
enum { IDC_CODE = 100, IDC_APPLY, IDC_OK, IDC_CANCEL, IDC_REVERT, IDC_STATUS };
const wchar_t* kEditorClass = L"foo_ui_panels_codeedit";
enum { kTimerCheck = 1 };

struct EditorState {
    std::string key;
    std::function<bool(const std::string&)> apply;
    std::wstring original;
    HFONT ui = nullptr;
    WNDPROC editProc = nullptr;
    std::string check;
    bool plain = false;
};

std::wstring editor_text(HWND ed) {
    GETTEXTLENGTHEX gl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    const LONG n = (LONG)SendMessageW(ed, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    std::wstring w((size_t)n + 1, L'\0');
    GETTEXTEX gt = { (DWORD)((n + 1) * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr };
    const LONG got = (LONG)SendMessageW(ed, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&w[0]);
    w.resize((size_t)std::max<LONG>(0, got));
    return w;
}

size_t highlight_limit() {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine ? 8 * 1024 : kEditorHighlightMax;
}

bool highlight_editor(HWND ed) {
    const std::string text = narrow(editor_text(ed));
    if (text.size() > highlight_limit()) return false;
    const std::string rtf = script_to_rtf(text);
    SendMessageW(ed, WM_SETREDRAW, FALSE, 0);
    const LRESULT mask = SendMessageW(ed, EM_SETEVENTMASK, 0, 0);
    CHARRANGE sel; SendMessageW(ed, EM_EXGETSEL, 0, (LPARAM)&sel);
    const LRESULT firstLine = SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0);
    SETTEXTEX st = { ST_DEFAULT, CP_ACP };
    SendMessageW(ed, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)rtf.c_str());
    SendMessageW(ed, EM_EXSETSEL, 0, (LPARAM)&sel);
    const LRESULT nowFirst = SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(ed, EM_LINESCROLL, 0, firstLine - nowFirst);
    SendMessageW(ed, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(ed, EM_SETEVENTMASK, 0, mask);
    SendMessageW(ed, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ed, nullptr, TRUE);
    return true;
}

void update_editor_status(HWND wnd, EditorState* st) {
    HWND ed = GetDlgItem(wnd, IDC_CODE);
    CHARRANGE sel; SendMessageW(ed, EM_EXGETSEL, 0, (LPARAM)&sel);
    int line, col;
    line_col_utf16(editor_text(ed), (size_t)std::max<LONG>(0, sel.cpMin), line, col);
    SetWindowTextW(GetDlgItem(wnd, IDC_STATUS), widen(editor_status(line, col, st->check, "Ctrl+S: apply", st->plain)).c_str());
}

void recheck_editor(HWND wnd, EditorState* st) {
    st->check = problems_summary(check_script(narrow(editor_text(GetDlgItem(wnd, IDC_CODE)))));
    update_editor_status(wnd, st);
}

bool editor_apply(HWND wnd, EditorState* st) {
    HWND ed = GetDlgItem(wnd, IDC_CODE);
    if (st->apply && !st->apply(narrow(editor_text(ed)))) return false;
    st->plain = !highlight_editor(ed);
    recheck_editor(wnd, st);
    return true;
}

LRESULT CALLBACK EditorTextProc(HWND ed, UINT msg, WPARAM wp, LPARAM lp) {
    HWND wnd = GetParent(ed);
    auto* st = reinterpret_cast<EditorState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && st) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (ctrl && wp == 'S') { editor_apply(wnd, st); return 0; }
        if (ctrl && wp == VK_RETURN) { if (editor_apply(wnd, st)) DestroyWindow(wnd); return 0; }
        if (wp == VK_ESCAPE) { DestroyWindow(wnd); return 0; }
    }
    if (msg == WM_CHAR && GetKeyState(VK_CONTROL) < 0 && (wp == 0x13 || wp == '\n')) return 0;
    return CallWindowProcW(st ? st->editProc : DefWindowProcW, ed, msg, wp, lp);
}

LRESULT CALLBACK EditorProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<EditorState*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(wnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_SIZE: {
        const int w = LOWORD(lp), h = HIWORD(lp), bw = 80, bh = 26, pad = 8, sh = 18;
        MoveWindow(GetDlgItem(wnd, IDC_CODE), pad, pad, w - 2 * pad, h - 4 * pad - bh - sh, TRUE);
        MoveWindow(GetDlgItem(wnd, IDC_STATUS), pad, h - 2 * pad - bh - sh, w - 2 * pad, sh, TRUE);
        int x = w - pad - bw, y = h - pad - bh;
        MoveWindow(GetDlgItem(wnd, IDC_CANCEL), x, y, bw, bh, TRUE); x -= bw + pad;
        MoveWindow(GetDlgItem(wnd, IDC_OK),     x, y, bw, bh, TRUE); x -= bw + pad;
        MoveWindow(GetDlgItem(wnd, IDC_APPLY),  x, y, bw, bh, TRUE);
        MoveWindow(GetDlgItem(wnd, IDC_REVERT), pad, y, bw, bh, TRUE);
        return 0;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (st && nm->idFrom == IDC_CODE && nm->code == EN_SELCHANGE) update_editor_status(wnd, st);
        break;
    }
    case WM_TIMER:
        if (wp == kTimerCheck && st) { KillTimer(wnd, kTimerCheck); recheck_editor(wnd, st); }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == IDC_CODE && HIWORD(wp) == EN_CHANGE) { SetTimer(wnd, kTimerCheck, 400, nullptr); return 0; }
        if ((id == IDC_APPLY || id == IDC_OK) && st) {
            if (editor_apply(wnd, st) && id == IDC_OK) DestroyWindow(wnd);
            return 0;
        }
        if (id == IDC_REVERT && st) {
            SetWindowTextW(GetDlgItem(wnd, IDC_CODE), st->original.c_str());
            editor_apply(wnd, st);
            return 0;
        }
        if (id == IDC_CANCEL) { DestroyWindow(wnd); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(wnd); return 0;
    case WM_NCDESTROY:
        if (st) {
            g_editors.erase(st->key);
            if (st->ui) DeleteObject(st->ui);
            delete st;
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}

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
    POINT pt = { dev(x), dev(y) };
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
    POINT pt = { dev(x), dev(y) }; ClientToScreen(wnd, &pt);
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
                widen(text).c_str(), widen(title).c_str(), MB_ICONWARNING);
}

static int CALLBACK browse_start(HWND wnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data) SendMessageW(wnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

std::string choose_folder(MainWindow& root, const std::string& title, const std::string& start) {
    const std::wstring wtitle = widen(title), wstart = widen(start);
    BROWSEINFOW bi = {};
    bi.hwndOwner = (HWND)root.native();
    bi.lpszTitle = wtitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browse_start;
    bi.lParam = wstart.empty() ? 0 : (LPARAM)wstart.c_str();
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? narrow(path) : std::string();
}

void open_url(const std::string& url) {
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_file(const std::string& path) {
    ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void reveal_in_file_manager(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(fs_path(path), ec))
        ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + widen(path) + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

bool copy_to_clipboard(const std::string& text) {
    const std::wstring w = widen(text);
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    EmptyClipboard();
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

std::string os_version() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))))
            if (fn(&v) == 0)
                return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." +
                       std::to_string(v.dwBuildNumber);
    return "unknown";
}

std::string wine_version() {
    using WineVersionFn = const char* (*)();
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<WineVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version"))))
            if (const char* v = fn()) return v;
    return {};
}

std::string home_dir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? narrow(buf) : std::string();
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
    auto* st = new EditorState{ key, std::move(apply), widen(text) };
    HWND wnd = CreateWindowExW(0, kEditorClass, widen(title).c_str(),
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 820, 560,
                               (HWND)root.native(), nullptr, module(), st);
    if (!wnd) { delete st; return; }
    g_editors[key] = wnd;
    static const bool msftedit = LoadLibraryW(L"Msftedit.dll") != nullptr;
    if (!msftedit) LoadLibraryW(L"Riched20.dll");
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, msftedit ? MSFTEDIT_CLASS : RICHEDIT_CLASSW, L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                              0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_CODE, module(), nullptr);
    SendMessageW(ed, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO, 0);
    SendMessageW(ed, EM_EXLIMITTEXT, 0, 16 << 20);
    CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    wcscpy(cf.szFaceName, L"Consolas"); cf.yHeight = 200; cf.crTextColor = RGB(30, 30, 30);
    SendMessageW(ed, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    SendMessageW(ed, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SetWindowTextW(ed, st->original.c_str());
    SendMessageW(ed, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    st->editProc = (WNDPROC)SetWindowLongPtrW(ed, GWLP_WNDPROC, (LONG_PTR)EditorTextProc);
    st->ui = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HWND status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS,
                                  0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_STATUS, module(), nullptr);
    SendMessageW(status, WM_SETFONT, (WPARAM)st->ui, TRUE);
    const struct { int id; const wchar_t* label; } btns[] = {
        { IDC_APPLY, L"Apply" }, { IDC_OK, L"OK" }, { IDC_CANCEL, L"Cancel" }, { IDC_REVERT, L"Revert" } };
    for (auto& b : btns) {
        HWND h = CreateWindowExW(0, L"BUTTON", b.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 0, 0, wnd, (HMENU)(INT_PTR)b.id, module(), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)st->ui, TRUE);
    }
    st->plain = !highlight_editor(ed);
    recheck_editor(wnd, st);
    RECT rc; GetClientRect(wnd, &rc);
    SendMessageW(wnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    SetFocus(ed);
}

void close_text_editor(const std::string& key) {
    auto it = g_editors.find(key);
    if (it != g_editors.end()) DestroyWindow(it->second);
}

}

namespace pui {

std::string component_dir() {
    wchar_t mod[MAX_PATH] = {};
    GetModuleFileNameW(core_api::get_my_instance(), mod, MAX_PATH);
    std::wstring wd(mod);
    auto s = wd.find_last_of(L"\\/");
    if (s != std::wstring::npos) wd.resize(s);
    return ui::narrow(wd);
}

}
