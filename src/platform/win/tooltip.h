// One tooltip control per window, covering its whole client area: the skin's buttons aren't
// child windows, so the text simply follows whatever button the mouse is over (set() from the
// window's mouse-move handling). Empty text shows nothing.
#pragma once
#include "win_sdk.h"
#include <commctrl.h>
#include <string>

namespace pui::win {

class Tooltip {
public:
    Tooltip() = default;
    Tooltip(const Tooltip&) = delete;
    Tooltip& operator=(const Tooltip&) = delete;
    ~Tooltip() { destroy(); }

    void set(HWND owner, const std::wstring& text) {
        if (text == m_text || !owner) return;
        if (!m_tip) {
            if (text.empty()) return;
            m_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                    WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    owner, nullptr, nullptr, nullptr);
            if (!m_tip) return;
            m_owner = owner;
            TTTOOLINFOW ti = tool(L"");
            // TTF_SUBCLASS: the control watches the owner's mouse messages itself.
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            SendMessageW(m_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(m_tip, TTM_SETMAXTIPWIDTH, 0, 400);
        }
        m_text = text;
        TTTOOLINFOW ti = tool(m_text.c_str());
        SendMessageW(m_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        // Hide the previous button's tip; the new one shows after the usual hover delay.
        SendMessageW(m_tip, TTM_POP, 0, 0);
    }

    void destroy() {
        if (m_tip) DestroyWindow(m_tip);
        m_tip = nullptr; m_owner = nullptr; m_text.clear();
    }

private:
    TTTOOLINFOW tool(const wchar_t* text) const {
        TTTOOLINFOW ti = {};
        ti.cbSize = sizeof(ti);
        ti.hwnd = m_owner;
        ti.uId = (UINT_PTR)m_owner;
        ti.lpszText = const_cast<wchar_t*>(text);
        return ti;
    }

    HWND m_tip = nullptr, m_owner = nullptr;
    std::wstring m_text;
};

} // namespace pui::win
