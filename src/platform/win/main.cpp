// foo_ui_panels (reborn) — Windows entry point.
// Registers a full UI replacement (user_interface): the main window hosts the skin canvas
// (SkinEngine renders into it) and implements ui::MainWindow for the script actions that act
// on it (resize, title bar, menu bar, main menu).
#include "win_sdk.h"
#include <windowsx.h>
#include "gdi_canvas.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/image_cache.h"
#include "../../core/fs_util.h"
#include <vector>
#include <string>
#include <cstdio>

VALIDATE_COMPONENT_FILENAME("foo_ui_panels.dll");

namespace {

// {6F0A1B2C-3D4E-4F50-9A1B-2C3D4E5F6071}
static const GUID g_panels_ui_guid =
    { 0x6f0a1b2c, 0x3d4e, 0x4f50, { 0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71 } };

static const wchar_t WNDCLASS_NAME[] = L"foo_ui_panels_main";

// Posted to the top-level window to show/hide the native menu bar (wParam: 1 show, 0 hide).
#define PUI_WM_TOGGLE_MENU (WM_USER + 0x501)
#define PUI_WM_SHOW_MAINMENU (WM_USER + 0x502) // wp = screen x, lp = screen y

class panels_ui : public user_interface, public pui::ui::MainWindow {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }

    fb2k::hwnd_t init(HookProc_t hook) override {
        m_hook = hook;
        HINSTANCE inst = core_api::get_my_instance();

        // OLE drag&drop (RegisterDragDrop/DoDragDrop in playlist_view.cpp) needs this on the
        // UI thread. Safe to call even if fb2k's core already did — OleInitialize ref-counts.
        m_ole_ok = SUCCEEDED(OleInitialize(nullptr));

        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = inst;
        wc.lpszClassName = WNDCLASS_NAME;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);

        m_wnd = CreateWindowExW(
            0, WNDCLASS_NAME, L"foobar2000 — Panels UI (reborn)",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, // don't paint over hosted child panels
            CW_USEDEFAULT, CW_USEDEFAULT, 950, 750,
            nullptr, nullptr, inst, this);

        if (!m_wnd) throw exception_win32(GetLastError());
        build_menu();
        build_layout();
        ShowWindow(m_wnd, SW_SHOW);
        return m_wnd;
    }

    static const UINT_PTR kCanvasTimer = 77;

    void build_layout() {
        m_skin.set_main_window(this);
        // Preferences-page skin folder override if set, else the component's own folder.
        std::string dir = pui::resolve_skin_dir();
        m_skin.set_base_dir(dir);
        // Load the real fooAvA master script if present, else the built-in test skin.
        std::string skin = pui::read_file(dir + "\\fooava.txt");
        console::printf("Panels UI: fooava.txt read %u bytes from %s", (unsigned)skin.size(), dir.c_str());
        bool ok = m_skin.load(skin.empty() ? pui::builtin_test_skin() : skin.c_str());
        console::printf("Panels UI: compile -> %s", ok ? "ok" : "FAILED");
        InvalidateRect(m_wnd, nullptr, FALSE);
        SetTimer(m_wnd, kCanvasTimer, 500, nullptr); // progress bar / time readout advance with playback
    }

    void resize_layout() { InvalidateRect(m_wnd, nullptr, FALSE); }

    void shutdown() override {
        m_skin.save_pvars();
        m_skin.destroy_panels(); // child views go before the window they live in
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
        ShowWindow(m_wnd, SW_SHOW);
        SetForegroundWindow(m_wnd);
    }
    void hide() override { if (m_wnd) ShowWindow(m_wnd, SW_MINIMIZE); }
    bool is_visible() override { return m_wnd && IsWindowVisible(m_wnd) && !IsIconic(m_wnd); }
    GUID get_guid() override { return g_panels_ui_guid; }

    void override_statusbar_text(const char*) override {}
    void revert_statusbar_text() override {}
    void show_now_playing() override {}

    // --- pui::ui::MainWindow ---
    void* native() const override { return m_wnd; }
    pui::gfx::Rect client_rect() const override {
        RECT rc = {}; if (m_wnd) GetClientRect(m_wnd, &rc);
        return pui::gfx::Rect{ 0, 0, rc.right, rc.bottom };
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
        // Hand the press to the window manager as if it had landed on the caption.
        ReleaseCapture();
        SendMessageW(m_wnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    void resize_client(int w, int h, const std::string& halign, const std::string& valign) override {
        if (!m_wnd || w <= 0 || h <= 0) return;
        // SetWindowPos takes the OUTER window rect — convert via AdjustWindowRectEx, or every
        // resize silently loses the title bar/menu/border overhead from the requested client
        // size. Uncorrected, repeated clicks on a script's own toggle button (e.g. fooAvA's
        // onepanel one/two-panel switch) drift smaller by that overhead each time instead of
        // landing on a stable pair of sizes.
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
    // Both deferred: they are triggered from inside a skin button's click handling.
    void set_menubar_visible(bool visible) override {
        if (m_wnd) PostMessageW(m_wnd, PUI_WM_TOGGLE_MENU, (WPARAM)(visible ? 1 : 0), 0);
    }
    void show_main_menu() override {
        if (!m_wnd) return;
        POINT pt; GetCursorPos(&pt);
        PostMessageW(m_wnd, PUI_WM_SHOW_MAINMENU, (WPARAM)pt.x, (LPARAM)pt.y);
    }

private:
    HWND        m_wnd     = nullptr;
    HookProc_t  m_hook    = nullptr;
    HMENU       m_menubar = nullptr;
    bool        m_ole_ok  = false;

    pui::SkinEngine m_skin;

    // One mainmenu_manager per top-level group; WM_COMMAND ids are partitioned
    // into [base, base+kSpan) ranges so we can route back to the right manager.
    struct MenuGroup { service_ptr_t<mainmenu_manager> mgr; UINT base; HMENU popup; const GUID* guid; };
    std::vector<MenuGroup> m_groups;
    static const UINT kSpan = 4000;
    static const UINT kMenuFlags = mainmenu_manager::flag_show_shortcuts | mainmenu_manager::flag_view_full;

    // Re-generate a top-level popup so its check/enable state (e.g. Playback>Order radio) is current.
    // The menu bar is built once; without this the radios only refresh on restart.
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
        // Apply the persisted "show menu bar" setting (toggled from the fooAvA settings popup).
        SetMenu(m_wnd, m_skin.pvar_int("menubar", 1) ? m_menubar : nullptr);
    }

    // The logo button's menu: the same six top-level menus as the bar, as one popup. Fresh
    // popups every time (an HMENU can only have one parent, and this keeps check states current).
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
        DestroyMenu(pop); // also destroys the submenus
    }

    bool exec_command(UINT id) {
        for (auto& g : m_groups) {
            if (id >= g.base && id < g.base + kSpan)
                return g.mgr->execute_command(id - g.base);
        }
        return false;
    }

    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        panels_ui* self = reinterpret_cast<panels_ui*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<panels_ui*>(cs->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        // Let the core intercept first (media keys, etc.).
        if (self && self->m_hook) {
            LRESULT ret = 0;
            if (self->m_hook(wnd, msg, wp, lp, &ret)) return ret;
        }
        switch (msg) {
        case WM_SIZE:
            if (self) self->resize_layout();
            return 0;
        case WM_LBUTTONDOWN:
            if (self) {
                if (self->m_skin.handle_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
                self->begin_window_drag(); // bare canvas doubles as the window's drag handle
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme); // arms WM_MOUSELEAVE so hover clears when the cursor exits
            if (self && self->m_skin.update_hover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)))
                InvalidateRect(wnd, nullptr, FALSE);
            break;
        }
        case WM_MOUSELEAVE:
            if (self && self->m_skin.update_hover(-1, -1)) InvalidateRect(wnd, nullptr, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1; // we fully paint in WM_PAINT (no flicker)
        case WM_TIMER:
            if (wp == kCanvasTimer) { InvalidateRect(wnd, nullptr, FALSE); return 0; }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(wnd, &ps);
            RECT rc; GetClientRect(wnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            FillRect(mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (self) {
                pui::gfx::GdiCanvas cv(mem, rc.right, rc.bottom);
                self->m_skin.render(cv, rc.right, rc.bottom);
                self->m_skin.snapshot_canvas(cv, rc.right, rc.bottom);
            }
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            if (self) self->m_skin.refresh_bars();
            SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            // Dispatch configured keyboard shortcuts (Ctrl+P -> Preferences, etc.).
            if (keyboard_shortcut_manager::get()->on_keydown_auto(wp))
                return 0;
            break;
        case WM_COMMAND:
            if (self && HIWORD(wp) == 0 && lp == 0) { // menu item
                if (self->exec_command(LOWORD(wp))) return 0;
            }
            break;
        case WM_INITMENUPOPUP: // refresh the opening popup so radios/checks (e.g. Order) are current
            if (self) self->refresh_popup(reinterpret_cast<HMENU>(wp));
            break;
        case PUI_WM_SHOW_MAINMENU:
            if (self) self->show_main_menu((int)wp, (int)lp);
            return 0;
        case PUI_WM_TOGGLE_MENU: // show/hide the menu bar (from settings popup)
            if (self) { SetMenu(wnd, wp ? self->m_menubar : nullptr); self->resize_layout(); }
            return 0;
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

} // namespace
