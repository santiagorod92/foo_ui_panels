// Notification-area ("tray") icon for the main window — what a skin's $settray(tooltip) asks for.
// The owner window receives kMessage (lParam = the mouse message) and re-adds the icon when
// Explorer restarts (taskbar_created_message()).
#pragma once
#include "win_sdk.h"
#include <shellapi.h>
#include <string>

namespace pui::win {

class TrayIcon {
public:
    static constexpr UINT kMessage = WM_APP + 0x51;
    static UINT taskbar_created_message() {
        static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
        return msg;
    }

    ~TrayIcon() { remove(); }

    bool active() const { return m_added; }

    // Adds the icon, or updates its tooltip (same text: nothing to do — called on every repaint).
    void set(HWND owner, HICON icon, const std::wstring& tip) {
        if (m_added && tip == m_tip) return;
        m_tip = tip;
        fill(owner, icon);
        m_added = Shell_NotifyIconW(m_added ? NIM_MODIFY : NIM_ADD, &m_nid) != FALSE;
    }

    void remove() {
        if (!m_added) return;
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }

    // Explorer restarted: its notification area forgot us.
    void readd() {
        if (!m_added) return;
        m_added = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    }

private:
    void fill(HWND owner, HICON icon) {
        m_nid = NOTIFYICONDATAW{};
        m_nid.cbSize = sizeof(m_nid);
        m_nid.hWnd = owner;
        m_nid.uID = 1;
        m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        m_nid.uCallbackMessage = kMessage;
        m_nid.hIcon = icon;
        wcsncpy(m_nid.szTip, m_tip.c_str(), _countof(m_nid.szTip) - 1);
    }

    NOTIFYICONDATAW m_nid{};
    std::wstring m_tip;
    bool m_added = false;
};

} // namespace pui::win
