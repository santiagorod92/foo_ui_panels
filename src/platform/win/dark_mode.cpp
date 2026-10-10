#include "dark_mode.h"
#include <commctrl.h>
#include <uxtheme.h>

namespace pui::win::dark {

bool enabled() { return ui_config_manager::g_is_dark_mode(); }

const Palette& palette(bool dark) {
    static Palette light = {}, darkp = {};
    if (!light.bgBrush) {
        light = { GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_GRAYTEXT),
                  GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNSHADOW),
                  GetSysColor(COLOR_WINDOW), GetSysColorBrush(COLOR_BTNFACE), GetSysColorBrush(COLOR_WINDOW) };
        darkp = { RGB(32, 32, 32), RGB(230, 230, 230), RGB(150, 150, 150),
                  RGB(25, 25, 25), RGB(230, 230, 230), RGB(90, 90, 90),
                  RGB(56, 56, 56), nullptr, nullptr };
        darkp.bgBrush = CreateSolidBrush(darkp.bg);
        darkp.fieldBrush = CreateSolidBrush(darkp.field);
    }
    return dark ? darkp : light;
}

namespace {

LRESULT CALLBACK list_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR dark) {
    if (msg == WM_NOTIFY && dark) {
        auto cd = reinterpret_cast<NMCUSTOMDRAW*>(lp);
        if (cd->hdr.hwndFrom == ListView_GetHeader(wnd) && cd->hdr.code == NM_CUSTOMDRAW) {
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, palette(true).text);
                return CDRF_DODEFAULT;
            }
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(wnd, list_proc, id);
    return DefSubclassProc(wnd, msg, wp, lp);
}

}

void theme_control(HWND ctl, bool dark) {
    wchar_t cls[64] = {};
    GetClassNameW(ctl, cls, 64);
    if (!dark) {
        SetWindowTheme(ctl, nullptr, nullptr);
    } else if (!_wcsicmp(cls, L"Button")) {
        const LONG type = GetWindowLongW(ctl, GWL_STYLE) & BS_TYPEMASK;
        if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX) SetWindowTheme(ctl, L"", L"");
        else SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    } else if (!_wcsicmp(cls, L"ComboBox") || !_wcsicmp(cls, L"Edit")) {
        SetWindowTheme(ctl, L"DarkMode_CFD", nullptr);
    } else if (!_wcsicmp(cls, WC_LISTVIEWW)) {
        SetWindowTheme(ctl, L"DarkMode_Explorer", nullptr);
    }
    if (!_wcsicmp(cls, WC_LISTVIEWW)) {
        const Palette& p = palette(dark);
        ListView_SetBkColor(ctl, p.field);
        ListView_SetTextBkColor(ctl, p.field);
        ListView_SetTextColor(ctl, p.fieldText);
        ListView_SetExtendedListViewStyleEx(ctl, LVS_EX_GRIDLINES, dark ? 0 : LVS_EX_GRIDLINES);
        if (HWND header = ListView_GetHeader(ctl))
            SetWindowTheme(header, dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
        SetWindowSubclass(ctl, list_proc, 1, dark);
    }
    InvalidateRect(ctl, nullptr, TRUE);
}

void theme_children(HWND parent, bool dark) {
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) theme_control(c, dark);
}

LRESULT ctl_color(UINT msg, HDC dc, bool grayText) {
    const Palette& p = palette(true);
    const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
    SetTextColor(dc, grayText ? p.grayText : field ? p.fieldText : p.text);
    SetBkColor(dc, field ? p.field : p.bg);
    return (LRESULT)(field ? p.fieldBrush : p.bgBrush);
}

namespace {

void paint_tab(HWND tab, HDC dc) {
    const Palette& p = palette(true);
    RECT rc; GetClientRect(tab, &rc);
    FillRect(dc, &rc, p.bgBrush);
    HBRUSH border = CreateSolidBrush(p.border);
    RECT body = rc; TabCtrl_AdjustRect(tab, FALSE, &body);
    InflateRect(&body, 1, 1);
    FrameRect(dc, &body, border);

    HGDIOBJ oldFont = SelectObject(dc, (HFONT)SendMessageW(tab, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    const int sel = TabCtrl_GetCurSel(tab), n = TabCtrl_GetItemCount(tab);
    for (int i = 0; i < n; ++i) {
        RECT r; TabCtrl_GetItemRect(tab, i, &r);
        wchar_t text[128] = {};
        TCITEMW ti = {}; ti.mask = TCIF_TEXT; ti.pszText = text; ti.cchTextMax = 128;
        TabCtrl_GetItem(tab, i, &ti);
        if (i == sel) {
            r.top -= 2; r.bottom += 1;
            HBRUSH b = CreateSolidBrush(p.tabSel);
            FillRect(dc, &r, b); DeleteObject(b);
        }
        FrameRect(dc, &r, border);
        SetTextColor(dc, i == sel ? p.text : p.grayText);
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
    DeleteObject(border);
}

LRESULT CALLBACK tab_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    const bool dark = *reinterpret_cast<const bool*>(data);
    switch (msg) {
    case WM_ERASEBKGND:
        if (dark) return 1;
        break;
    case WM_PAINT:
        if (dark) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(wnd, &ps);
            paint_tab(wnd, dc);
            EndPaint(wnd, &ps);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(wnd, tab_proc, id);
        break;
    }
    return DefSubclassProc(wnd, msg, wp, lp);
}

}

void subclass_tab(HWND tab, const bool* dark) {
    SetWindowSubclass(tab, tab_proc, 1, reinterpret_cast<DWORD_PTR>(dark));
}

}
