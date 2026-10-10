#include "win_sdk.h"
#include <windowsx.h>
#include "gdi_canvas.h"
#include "tooltip.h"
#include "drop_files.h"
#include "tray.h"
#include "zoom.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/image_cache.h"
#include "../../core/fs_util.h"
#include "../../core/ui_logic.h"
#include "../../core/ui_settings.h"
#include "../../core/log.h"
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>

VALIDATE_COMPONENT_FILENAME("foo_ui_panels.dll");

namespace {

static const GUID g_panels_ui_guid =
    { 0x6f0a1b2c, 0x3d4e, 0x4f50, { 0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71 } };

static const wchar_t WNDCLASS_NAME[] = L"foo_ui_panels_main";

static const GUID g_placement_guid =
    { 0x5e7a9c31, 0x2b4d, 0x4f6e, { 0x8a, 0x1c, 0x3d, 0x5b, 0x7f, 0x9e, 0x0a, 0x24 } };
static cfg_var_modern::cfg_string g_placement(g_placement_guid, "");

static void save_placement(HWND wnd) {
    WINDOWPLACEMENT wp = { sizeof(wp) };
    if (!GetWindowPlacement(wnd, &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    char buf[96];
    snprintf(buf, sizeof buf, "%ld %ld %ld %ld %u", r.left, r.top, r.right, r.bottom,
             wp.showCmd == SW_SHOWMAXIMIZED ? (unsigned)SW_SHOWMAXIMIZED : (unsigned)SW_SHOWNORMAL);
    g_placement.set(buf);
}

static int restore_placement(HWND wnd) {
    long l, t, r, b; unsigned cmd;
    if (sscanf(g_placement.get().c_str(), "%ld %ld %ld %ld %u", &l, &t, &r, &b, &cmd) != 5) return SW_SHOW;
    RECT rc = { l, t, r, b };
    if (rc.right - rc.left < 100 || rc.bottom - rc.top < 100) return SW_SHOW;
    if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return SW_SHOW;
    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(wnd, &wp);
    wp.rcNormalPosition = rc;
    wp.showCmd = SW_HIDE;
    SetWindowPlacement(wnd, &wp);
    return cmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOW;
}

#define PUI_WM_TOGGLE_MENU (WM_USER + 0x501)
#define PUI_WM_SHOW_MAINMENU (WM_USER + 0x502)

class panels_ui : public user_interface, public pui::ui::MainWindow {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }

    fb2k::hwnd_t init(HookProc_t hook) override {
        m_hook = hook;
        HINSTANCE inst = core_api::get_my_instance();

        m_ole_ok = SUCCEEDED(OleInitialize(nullptr));

        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = inst;
        wc.lpszClassName = WNDCLASS_NAME;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hIcon         = (HICON)ui_control::get()->get_main_icon();
        wc.hIconSm       = (HICON)ui_control::get()->load_main_icon(GetSystemMetrics(SM_CXSMICON),
                                                                    GetSystemMetrics(SM_CYSMICON));
        RegisterClassExW(&wc);

        m_wnd = CreateWindowExW(
            0, WNDCLASS_NAME, L"foobar2000 — Panels UI (reborn)",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 950, 750,
            nullptr, nullptr, inst, this);

        if (!m_wnd) {
            const DWORD err = GetLastError();
            pui::log::error("ui", "main window creation failed, error " + std::to_string(err));
            throw exception_win32(err);
        }
        pui::log::info("ui", "main window created");
        DragAcceptFiles(m_wnd, TRUE);
        build_menu();
        const int show = restore_placement(m_wnd);
        pui::win::refresh_zoom(m_wnd);
        set_always_on_top(pui::always_on_top());
        build_layout();
        ShowWindow(m_wnd, show);
        return m_wnd;
    }

    static const UINT_PTR kCanvasTimer = 77;

    void build_layout() {
        m_skin.set_main_window(this);
        m_skin.load_skin(pui::resolve_skin_dir());
        InvalidateRect(m_wnd, nullptr, FALSE);
        SetTimer(m_wnd, kCanvasTimer, 500, nullptr);
    }

    void resize_layout() { InvalidateRect(m_wnd, nullptr, FALSE); }

    void shutdown() override {
        m_skin.save_pvars();
        if (m_wnd) save_placement(m_wnd);
        m_skin.destroy_panels();
        m_tooltip.destroy();
        m_tray.remove();
        if (m_wnd) { DestroyWindow(m_wnd); m_wnd = nullptr; }
        if (m_menubar) { DestroyMenu(m_menubar); m_menubar = nullptr; }
        m_groups.clear();
        pui::images_shutdown();
        pui::gfx::gdi_fonts_shutdown();
        UnregisterClassW(WNDCLASS_NAME, core_api::get_my_instance());
        if (m_ole_ok) { OleUninitialize(); m_ole_ok = false; }
    }

    void activate() override {
        if (!m_wnd) return;
        ShowWindow(m_wnd, IsIconic(m_wnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(m_wnd);
    }
    void hide() override { if (m_wnd) ShowWindow(m_wnd, SW_MINIMIZE); }
    bool is_visible() override { return m_wnd && IsWindowVisible(m_wnd) && !IsIconic(m_wnd); }
    GUID get_guid() override { return g_panels_ui_guid; }

    void override_statusbar_text(const char*) override {}
    void revert_statusbar_text() override {}
    void show_now_playing() override {}

    void* native() const override { return m_wnd; }
    pui::gfx::Rect client_rect() const override {
        RECT rc = {}; if (m_wnd) GetClientRect(m_wnd, &rc);
        const double z = pui::win::zoom();
        return pui::gfx::Rect{ 0, 0, (int)std::lround(rc.right / z), (int)std::lround(rc.bottom / z) };
    }
    void invalidate() override { if (m_wnd) InvalidateRect(m_wnd, nullptr, TRUE); }
    void set_titlebar_visible(bool visible) override {
        if (!m_wnd) return;
        LONG_PTR style = GetWindowLongPtrW(m_wnd, GWL_STYLE);
        LONG_PTR next = visible ? (style | WS_CAPTION) : (style & ~WS_CAPTION);
        if (next == style) return;
        SetWindowLongPtrW(m_wnd, GWL_STYLE, next);
        SetWindowPos(m_wnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    void begin_window_drag() override {
        if (!m_wnd) return;
        ReleaseCapture();
        SendMessageW(m_wnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    void resize_client(int w, int h, const std::string& halign, const std::string& valign) override {
        if (!m_wnd || w <= 0 || h <= 0) return;
        w = pui::to_device(w, pui::win::zoom()); h = pui::to_device(h, pui::win::zoom());
        RECT rc = { 0, 0, w, h };
        LONG_PTR style = GetWindowLongPtrW(m_wnd, GWL_STYLE);
        LONG_PTR exstyle = GetWindowLongPtrW(m_wnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rc, (DWORD)style, GetMenu(m_wnd) != nullptr, (DWORD)exstyle);
        int outerW = rc.right - rc.left, outerH = rc.bottom - rc.top;
        RECT cur_rc{}; GetWindowRect(m_wnd, &cur_rc);
        int x = cur_rc.left, y = cur_rc.top;
        if (halign == "RIGHT")  x = cur_rc.right - outerW;
        else if (halign == "CENTER") x = cur_rc.left + (cur_rc.right - cur_rc.left - outerW) / 2;
        if (valign == "BOTTOM") y = cur_rc.top - outerH + (cur_rc.bottom - cur_rc.top);
        else if (valign == "CENTER") y = cur_rc.top + (cur_rc.bottom - cur_rc.top - outerH) / 2;
        SetWindowPos(m_wnd, nullptr, x, y, outerW, outerH, SWP_NOZORDER);
    }
    void set_menubar_visible(bool visible) override {
        if (m_wnd) PostMessageW(m_wnd, PUI_WM_TOGGLE_MENU, (WPARAM)(visible ? 1 : 0), 0);
    }
    void show_main_menu() override {
        if (!m_wnd) return;
        POINT pt; GetCursorPos(&pt);
        PostMessageW(m_wnd, PUI_WM_SHOW_MAINMENU, (WPARAM)pt.x, (LPARAM)pt.y);
    }
    void set_title(const std::string& utf8) override {
        if (!m_wnd || utf8 == m_title) return;
        m_title = utf8;
        SetWindowTextW(m_wnd, pfc::stringcvt::string_wide_from_utf8(utf8.c_str()).get_ptr());
    }
    void set_tooltip(const std::string& utf8) override {
        m_tooltip.set(m_wnd, pfc::stringcvt::string_wide_from_utf8(utf8.c_str()).get_ptr());
    }
    void set_tray(const std::string& utf8) override {
        if (!m_wnd) return;
        if (utf8.empty()) {
            if (m_tray.active() && !IsWindowVisible(m_wnd)) activate();
            m_tray.remove();
            return;
        }
        m_tray.set(m_wnd, (HICON)ui_control::get()->get_main_icon(),
                   pfc::stringcvt::string_wide_from_utf8(utf8.c_str()).get_ptr());
    }

    void set_always_on_top(bool on) override {
        if (m_wnd) SetWindowPos(m_wnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    double zoom() const override { return pui::win::zoom(); }
    pui::gfx::ImagePtr capture() override {
        if (!m_wnd || IsIconic(m_wnd)) return nullptr;
        RECT rc = {}; GetClientRect(m_wnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return nullptr;
        HDC wdc = GetDC(m_wnd), mdc = CreateCompatibleDC(wdc);
        HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mdc, bmp);
        FillRect(mdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        { pui::gfx::GdiCanvas cv(mdc, rc.right, rc.bottom, pui::win::zoom()); m_skin.render(cv, cv.width(), cv.height()); }
        std::vector<HWND> kids;
        for (HWND c = GetWindow(m_wnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) kids.push_back(c);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            wchar_t cls[64] = {};
            GetClassNameW(*it, cls, 64);
            RECT r;
            if (!IsWindowVisible(*it) || wcsncmp(cls, L"foo_ui_panels_view", 18) != 0 || !GetWindowRect(*it, &r)) continue;
            MapWindowPoints(nullptr, m_wnd, (POINT*)&r, 2);
            const int w = r.right - r.left, h = r.bottom - r.top;
            if (w <= 0 || h <= 0) continue;
            HDC cdc = CreateCompatibleDC(wdc);
            HBITMAP cb = CreateCompatibleBitmap(wdc, w, h);
            HGDIOBJ cold = SelectObject(cdc, cb);
            BitBlt(cdc, 0, 0, w, h, mdc, r.left, r.top, SRCCOPY);
            SendMessageW(*it, WM_PRINTCLIENT, (WPARAM)cdc, PRF_CLIENT);
            BitBlt(mdc, r.left, r.top, w, h, cdc, 0, 0, SRCCOPY);
            SelectObject(cdc, cold); DeleteObject(cb); DeleteDC(cdc);
        }
        pui::gfx::ImagePtr img;
        { pui::gfx::GdiCanvas cv(mdc, rc.right, rc.bottom); img = cv.snapshot(pui::gfx::Rect{ 0, 0, rc.right, rc.bottom }); }
        SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(m_wnd, wdc);
        return img;
    }
    void apply_zoom() override {
        if (!m_wnd) return;
        const pui::gfx::Rect before = client_rect();
        pui::win::refresh_zoom(m_wnd);
        if (!IsZoomed(m_wnd) && !IsIconic(m_wnd) && before.w > 0 && before.h > 0)
            resize_client(before.w, before.h, "", "");
        InvalidateRect(m_wnd, nullptr, FALSE);
    }

private:
    HWND        m_wnd     = nullptr;
    HookProc_t  m_hook    = nullptr;
    HMENU       m_menubar = nullptr;
    bool        m_ole_ok  = false;
    std::string m_title;
    pui::win::Tooltip m_tooltip;
    pui::win::TrayIcon m_tray;

    pui::SkinEngine m_skin;

    struct MenuGroup { service_ptr_t<mainmenu_manager> mgr; UINT base; HMENU popup; const GUID* guid; };
    std::vector<MenuGroup> m_groups;
    static const UINT kSpan = 4000;
    static const UINT kMenuFlags = mainmenu_manager::flag_show_shortcuts | mainmenu_manager::flag_view_full;

    void refresh_popup(HMENU popup) {
        for (auto& g : m_groups) {
            if (g.popup != popup) continue;
            while (GetMenuItemCount(popup) > 0) DeleteMenu(popup, 0, MF_BYPOSITION);
            g.mgr = mainmenu_manager::get();
            g.mgr->instantiate(*g.guid);
            g.mgr->generate_menu_win32(popup, g.base, kSpan, kMenuFlags);
            return;
        }
    }

    void build_menu() {
        struct Root { const GUID& guid; const wchar_t* label; };
        const Root roots[] = {
            { mainmenu_groups::file,     L"&File" },
            { mainmenu_groups::edit,     L"&Edit" },
            { mainmenu_groups::view,     L"&View" },
            { mainmenu_groups::playback, L"&Playback" },
            { mainmenu_groups::library,  L"&Library" },
            { mainmenu_groups::help,     L"&Help" },
        };
        m_menubar = CreateMenu();
        UINT base = 1;
        for (const auto& r : roots) {
            auto mgr = mainmenu_manager::get();
            mgr->instantiate(r.guid);
            HMENU popup = CreatePopupMenu();
            mgr->generate_menu_win32(popup, base, kSpan, kMenuFlags);
            AppendMenuW(m_menubar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), r.label);
            m_groups.push_back({ mgr, base, popup, &r.guid });
            base += kSpan;
        }
        SetMenu(m_wnd, m_skin.pvar_int("menubar", 1) ? m_menubar : nullptr);
    }

    void show_main_menu(int x, int y) {
        struct Root { const GUID& guid; const wchar_t* label; };
        const Root roots[] = {
            { mainmenu_groups::file, L"File" }, { mainmenu_groups::edit, L"Edit" },
            { mainmenu_groups::view, L"View" }, { mainmenu_groups::playback, L"Playback" },
            { mainmenu_groups::library, L"Library" }, { mainmenu_groups::help, L"Help" },
        };
        std::vector<MenuGroup> groups;
        HMENU pop = CreatePopupMenu();
        UINT base = 1;
        for (const auto& r : roots) {
            auto mgr = mainmenu_manager::get();
            mgr->instantiate(r.guid);
            HMENU sub = CreatePopupMenu();
            mgr->generate_menu_win32(sub, base, kSpan, kMenuFlags);
            AppendMenuW(pop, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), r.label);
            groups.push_back({ mgr, base, sub, &r.guid });
            base += kSpan;
        }
        UINT cmd = TrackPopupMenu(pop, TPM_RETURNCMD | TPM_LEFTBUTTON, x, y, 0, m_wnd, nullptr);
        if (cmd) for (auto& g : groups)
            if (cmd >= g.base && cmd < g.base + kSpan) { g.mgr->execute_command(cmd - g.base); break; }
        DestroyMenu(pop);
    }

    bool exec_command(UINT id) {
        for (auto& g : m_groups) {
            if (id >= g.base && id < g.base + kSpan)
                return g.mgr->execute_command(id - g.base);
        }
        return false;
    }

    static int logical(int device) { return pui::to_logical(device, pui::win::zoom()); }

    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        panels_ui* self = reinterpret_cast<panels_ui*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<panels_ui*>(cs->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && self->m_hook) {
            LRESULT ret = 0;
            if (self->m_hook(wnd, msg, wp, lp, &ret)) return ret;
        }
        if (self && msg == pui::win::TrayIcon::taskbar_created_message()) { self->m_tray.readd(); return 0; }
        switch (msg) {
        case WM_SIZE:
            if (self && wp == SIZE_MINIMIZED && self->m_tray.active()) { ShowWindow(wnd, SW_HIDE); return 0; }
            if (self) self->resize_layout();
            return 0;
        case pui::win::TrayIcon::kMessage:
            if (!self) return 0;
            if (lp == WM_LBUTTONUP) {
                if (IsWindowVisible(wnd) && !IsIconic(wnd)) self->hide(); else self->activate();
            } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                SetForegroundWindow(wnd);
                self->m_skin.show_tray_menu();
                PostMessageW(wnd, WM_NULL, 0, 0);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (self) {
                if (self->m_skin.handle_click(logical(GET_X_LPARAM(lp)), logical(GET_Y_LPARAM(lp)))) return 0;
                self->begin_window_drag();
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme);
            if (self && self->m_skin.update_hover(logical(GET_X_LPARAM(lp)), logical(GET_Y_LPARAM(lp))))
                InvalidateRect(wnd, nullptr, FALSE);
            break;
        }
        case WM_MOUSELEAVE:
            if (self && self->m_skin.update_hover(-1, -1)) InvalidateRect(wnd, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kCanvasTimer) {
                if (self) self->m_skin.check_skin_changes();
                if (pui::SkinEngine::playback_ticking()) InvalidateRect(wnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(wnd, &ps);
            RECT rc; GetClientRect(wnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            FillRect(mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (self) {
                pui::gfx::GdiCanvas cv(mem, rc.right, rc.bottom, pui::win::zoom());
                self->m_skin.render(cv, cv.width(), cv.height());
                self->m_skin.snapshot_canvas(cv, cv.width(), cv.height());
            }
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            if (self) self->m_skin.refresh_bars();
            SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg == WM_KEYDOWN && wp == VK_TAB && self) { self->m_skin.focus_next_panel({}, GetKeyState(VK_SHIFT) < 0); return 0; }
            if (keyboard_shortcut_manager::get()->on_keydown_auto(wp))
                return 0;
            break;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            POINT pt = {};
            auto paths = pui::win::dropped_files(drop, pt);
            DragFinish(drop);
            pui::SkinEngine::add_files(paths);
            return 0;
        }
        case WM_COMMAND:
            if (self && HIWORD(wp) == 0 && lp == 0) {
                if (self->exec_command(LOWORD(wp))) return 0;
            }
            break;
        case WM_INITMENUPOPUP:
            if (self) self->refresh_popup(reinterpret_cast<HMENU>(wp));
            break;
        case PUI_WM_SHOW_MAINMENU:
            if (self) self->show_main_menu((int)wp, (int)lp);
            return 0;
        case PUI_WM_TOGGLE_MENU:
            if (self) { SetMenu(wnd, wp ? self->m_menubar : nullptr); self->resize_layout(); }
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(wnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            pui::win::refresh_zoom(wnd);
            if (self) self->m_skin.repaint_all();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && !wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet")) {
                ui_config_manager_v2::ptr api;
                if (auto base = ui_config_manager::tryGet(); base.is_valid() && base->service_query_t(api))
                    api->notify_system_theme_changed();
            }
            break;
        case WM_CLOSE:
            standard_commands::main_exit();
            return 0;
        case WM_DESTROY:
            return 0;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }
};

static user_interface_factory<panels_ui> g_panels_ui_factory;

}
