// foo_ui_panels (reborn) — Phase 1 skeleton.
// Registers a full UI replacement (user_interface) that creates a bare main window.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>   // before windows.h so WS2 wins over legacy winsock
#include <windows.h>
#include <objbase.h>    // defines the COM `interface` macro used by SDK headers
#include <mmsystem.h>   // timeGetTime (pulled by pfc/timers.h)
#include "../sdk/foobar2000/SDK/foobar2000.h"

DECLARE_COMPONENT_VERSION(
    "Panels UI (reborn)",
    "0.1.0",
    "Reimplementation of the discontinued foo_ui_panels for foobar2000 v2.\n"
    "Phase 1: bare main window. https://github.com/ (WIP)\n");

VALIDATE_COMPONENT_FILENAME("foo_ui_panels.dll");

namespace {

// {6F0A1B2C-3D4E-4F50-9A1B-2C3D4E5F6071}
static const GUID g_panels_ui_guid =
    { 0x6f0a1b2c, 0x3d4e, 0x4f50, { 0x9a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71 } };

static const wchar_t WNDCLASS_NAME[] = L"foo_ui_panels_main";

class panels_ui : public user_interface {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }

    fb2k::hwnd_t init(HookProc_t hook) override {
        m_hook = hook;
        HINSTANCE inst = core_api::get_my_instance();

        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = inst;
        wc.lpszClassName = WNDCLASS_NAME;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);

        m_wnd = CreateWindowExW(
            0, WNDCLASS_NAME, L"foobar2000 — Panels UI (reborn)",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
            nullptr, nullptr, inst, this);

        if (!m_wnd) throw exception_win32(GetLastError());
        ShowWindow(m_wnd, SW_SHOW);
        return m_wnd;
    }

    void shutdown() override {
        if (m_wnd) { DestroyWindow(m_wnd); m_wnd = nullptr; }
        UnregisterClassW(WNDCLASS_NAME, core_api::get_my_instance());
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

private:
    HWND        m_wnd  = nullptr;
    HookProc_t  m_hook = nullptr;

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
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            // Dispatch configured keyboard shortcuts (Ctrl+P -> Preferences, etc.).
            if (keyboard_shortcut_manager::get()->on_keydown_auto(wp))
                return 0;
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

} // namespace
