// Preferences page (Display > Panels UI (reborn)), Windows view. Generic skin-manager UI: no
// skin-specific wording. All logic — what is edited, what changed, applying — is PrefsModel
// (src/core/prefs_model.h), shared with the macOS page; this file only maps it onto plain Win32
// child windows (no ATL/WTL in our cross-compile toolchain): a tab control, a ListView for the
// variables grid, an owner-drawn skin preview.
//
// Four tabs:
//   General   - skins root folder (one subfolder per skin), active skin (with a preview), main
//               script, zoom, always on top.
//   Script    - raw editor for the active skin's main script.
//   Variables - grid of the persistent pvars ($getpvar/$setpvar) the skin uses.
//   Overrides - global font/accent-colour fallback used when a skin doesn't set its own.
//
// Everything applies live (PrefsBackend::store): a script edit through the engine's hot reload,
// another skin or main script by reloading the skin in place.
#include "win_sdk.h"
#include "gdi_canvas.h"
#include "../../core/prefs_model.h"
#include "../../core/prefs_store.h"
#include "../../core/skin_paths.h"
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace pui {

namespace {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}


std::wstring get_text(HWND ctl) {
    int n = GetWindowTextLengthW(ctl);
    std::wstring w(n, L'\0');
    if (n > 0) GetWindowTextW(ctl, &w[0], n + 1);
    return w;
}
std::string get_utf8(HWND ctl) { return to_utf8(get_text(ctl)); }
std::wstring w(const char* s) { return to_wide(s); }
// A combo box's entries and selection, from the model's picker lists.
void fill_combo(HWND combo, const std::vector<std::string>& labels, int sel) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (auto& l : labels) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)to_wide(l).c_str());
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}
void set_text(HWND ctl, const std::string& s) { SetWindowTextW(ctl, to_wide(s).c_str()); }
COLORREF colorref_of(gfx::Color c) { return RGB(c.r, c.g, c.b); }

// Control IDs.
enum {
    // General
    kIdRootEdit = 1001, kIdRootBrowse = 1002, kIdSkinCombo = 1003, kIdMainCombo = 1004,
    kIdZoomCombo = 1005, kIdOnTop = 1006, kIdPreview = 1007,
    // Script
    kIdScriptEdit = 1010,
    // Variables
    kIdVarsList = 1020, kIdVarsRescan = 1021, kIdVarsEdit = 1022,
    // Overrides
    kIdFontFaceEdit = 1030, kIdFontSizeEdit = 1031, kIdFontPickBtn = 1032,
    kIdAccentSwatch = 1033, kIdAccentPickBtn = 1034, kIdAccentClearBtn = 1035,
    // Tab control
    kIdTab = 1040,
};

const wchar_t* kPageClass = L"foo_ui_panels_prefs_page";
const wchar_t* kRootClass = L"foo_ui_panels_prefs";

class PrefsInstance : public preferences_page_instance {
public:
    PrefsInstance(HWND parent, preferences_page_callback::ptr callback) : m_callback(callback) {
        register_classes();
        m_model.load();
        m_wnd = CreateWindowExW(0, kRootClass, L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 0, 0, parent, nullptr, core_api::get_my_instance(), this);
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable;
        if (m_model.changed()) s |= preferences_state::changed;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        commit_edit_value(true);
        m_model.apply();
        refresh_all(); // the Script tab may edit another file now
        m_callback->on_state_changed();
    }

    // Back to defaults: no skins root, automatic main script and zoom, no overrides.
    void reset() override {
        m_model.reset();
        refresh_all();
        m_callback->on_state_changed();
    }

private:
    static void register_classes() {
        static bool done = false; if (done) return; done = true;
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
        for (const wchar_t* cls : { kRootClass, kPageClass }) {
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.lpfnWndProc = WndProc;
            wc.hInstance = core_api::get_my_instance();
            wc.lpszClassName = cls;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            RegisterClassExW(&wc);
        }
    }

    HWND child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                                 (HMENU)(INT_PTR)id, core_api::get_my_instance(), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)m_font, TRUE);
        return h;
    }

    // --- construction ----------------------------------------------------------------------
    void create_controls(HWND wnd) {
        m_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_monoFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

        m_tab = child(wnd, WC_TABCONTROLW, L"", WS_TABSTOP, kIdTab);
        for (const char* t : prefs_text::kTabs) {
            const std::wstring name = w(t);
            TCITEMW ti = {}; ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)name.c_str();
            TabCtrl_InsertItem(m_tab, TabCtrl_GetItemCount(m_tab), &ti);
        }
        for (HWND* p : { &m_pageGeneral, &m_pageScript, &m_pageVariables, &m_pageOverrides })
            *p = CreateWindowExW(0, kPageClass, L"", WS_CHILD, 0, 0, 0, 0, wnd, nullptr, core_api::get_my_instance(), this);

        HWND p = m_pageGeneral;
        m_rootLabel = child(p, L"STATIC", w(prefs_text::kRoot).c_str(), 0);
        m_rootEdit = child(p, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kIdRootEdit, WS_EX_CLIENTEDGE);
        m_rootBrowseBtn = child(p, L"BUTTON", L"Browse...", WS_TABSTOP, kIdRootBrowse);
        m_skinLabel = child(p, L"STATIC", w(prefs_text::kSkin).c_str(), 0);
        m_skinCombo = child(p, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, kIdSkinCombo, WS_EX_CLIENTEDGE);
        m_mainLabel = child(p, L"STATIC", w(prefs_text::kMain).c_str(), 0);
        m_mainCombo = child(p, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, kIdMainCombo, WS_EX_CLIENTEDGE);
        m_skinWarning = child(p, L"STATIC", L"", 0);
        m_zoomLabel = child(p, L"STATIC", w(prefs_text::kZoom).c_str(), 0);
        m_zoomCombo = child(p, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, kIdZoomCombo, WS_EX_CLIENTEDGE);
        fill_combo(m_zoomCombo, PrefsModel::zoom_labels(prefs_text::kZoomAutoScaled), 0);
        m_onTopCheck = child(p, L"BUTTON", w(prefs_text::kOnTop).c_str(), WS_TABSTOP | BS_AUTOCHECKBOX, kIdOnTop);
        m_rootNote = child(p, L"STATIC", L"", 0);
        m_preview = child(p, L"STATIC", L"", SS_OWNERDRAW, kIdPreview);

        p = m_pageScript;
        m_scriptLabel = child(p, L"STATIC", w(prefs_text::kScript).c_str(), 0);
        m_scriptEdit = child(p, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                             ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL, kIdScriptEdit, WS_EX_CLIENTEDGE);
        // Plain EDIT controls silently cap input at 30,000 chars without this — real scripts run ~36KB.
        SendMessageW(m_scriptEdit, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(m_scriptEdit, WM_SETFONT, (WPARAM)m_monoFont, TRUE);

        p = m_pageVariables;
        m_varsNote = child(p, L"STATIC", w(prefs_text::kVarsNote).c_str(), 0);
        m_varsList = child(p, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                           kIdVarsList, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(m_varsList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        const std::wstring colVar = w(prefs_text::kVariable), colVal = w(prefs_text::kValue);
        col.cx = 220; col.pszText = (LPWSTR)colVar.c_str(); ListView_InsertColumn(m_varsList, 0, &col);
        col.cx = 300; col.pszText = (LPWSTR)colVal.c_str(); ListView_InsertColumn(m_varsList, 1, &col);
        m_varsRescanBtn = child(p, L"BUTTON", w(prefs_text::kVarsFind).c_str(), WS_TABSTOP, kIdVarsRescan);

        p = m_pageOverrides;
        m_ovNote = child(p, L"STATIC", w(prefs_text::kOverridesNote).c_str(), 0);
        m_fontFaceLabel = child(p, L"STATIC", w(prefs_text::kFontFace).c_str(), 0);
        m_fontFaceEdit = child(p, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kIdFontFaceEdit, WS_EX_CLIENTEDGE);
        m_fontSizeLabel = child(p, L"STATIC", w(prefs_text::kFontSize).c_str(), 0);
        m_fontSizeEdit = child(p, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, kIdFontSizeEdit, WS_EX_CLIENTEDGE);
        m_fontPickBtn = child(p, L"BUTTON", L"Pick font...", WS_TABSTOP, kIdFontPickBtn);
        m_accentLabel = child(p, L"STATIC", w(prefs_text::kAccent).c_str(), 0);
        m_accentSwatch = child(p, L"BUTTON", L"", BS_OWNERDRAW, kIdAccentSwatch, WS_EX_CLIENTEDGE);
        m_accentPickBtn = child(p, L"BUTTON", L"Pick colour...", WS_TABSTOP, kIdAccentPickBtn);
        m_accentClearBtn = child(p, L"BUTTON", w(prefs_text::kClearOverrides).c_str(), WS_TABSTOP, kIdAccentClearBtn);

        ShowWindow(m_pageGeneral, SW_SHOW);
        refresh_all();
    }

    // --- model -> UI -----------------------------------------------------------------------
    // m_updating: the UI is being filled from the model, so its change notifications aren't edits.
    void refresh_all() {
        const bool was = m_updating;
        m_updating = true;
        set_text(m_rootEdit, m_model.pending().root);
        refresh_skin_choice();
        SendMessageW(m_zoomCombo, CB_SETCURSEL, m_model.zoom_index(), 0);
        SendMessageW(m_onTopCheck, BM_SETCHECK, m_model.pending().onTop ? BST_CHECKED : BST_UNCHECKED, 0);
        refresh_vars_list();
        refresh_overrides_ui();
        m_updating = was;
    }

    // Skin + main-script combos, the warning, the preview and the script text.
    void refresh_skin_choice() {
        const bool was = m_updating;
        m_updating = true;
        fill_combo(m_skinCombo, m_model.skin_labels(), m_model.skin_index());
        fill_combo(m_mainCombo, m_model.main_labels(), m_model.main_index());
        set_text(m_rootNote, m_model.root_note());

        const std::string warn = m_model.skin_warning();
        SetWindowTextW(m_skinWarning, warn.empty() ? L"" : (L"⚠ " + to_wide(warn)).c_str());
        m_previewPath = m_model.preview_image();
        InvalidateRect(m_preview, nullptr, TRUE);
        set_text(m_scriptEdit, m_model.script());
        m_updating = was;
    }

    void refresh_vars_list() {
        ListView_DeleteAllItems(m_varsList);
        int row = 0;
        for (auto& [k, v] : m_model.variables()) {
            std::wstring name = to_wide(k), val = to_wide(v);
            LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = row; it.pszText = (LPWSTR)name.c_str();
            ListView_InsertItem(m_varsList, &it);
            ListView_SetItemText(m_varsList, row, 1, (LPWSTR)val.c_str());
            ++row;
        }
    }

    void refresh_overrides_ui() {
        const bool was = m_updating;
        m_updating = true;
        set_text(m_fontFaceEdit, m_model.font_face());
        set_text(m_fontSizeEdit, m_model.font_size());
        InvalidateRect(m_accentSwatch, nullptr, TRUE);
        m_updating = was;
    }

    void changed() { m_callback->on_state_changed(); }

    // --- UI -> model -----------------------------------------------------------------------
    void root_edited() {
        const std::string root = get_utf8(m_rootEdit);
        if (root == m_model.pending().root) return;
        m_model.set_root(root);
        refresh_skin_choice();
        changed();
    }

    void browse_root() {
        wchar_t path[MAX_PATH] = {};
        BROWSEINFOW bi = {}; bi.hwndOwner = m_wnd; bi.lpszTitle = L"Skins root folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return;
        if (SHGetPathFromIDListW(pidl, path)) {
            m_updating = true; SetWindowTextW(m_rootEdit, path); m_updating = false;
            root_edited();
        }
        CoTaskMemFree(pidl);
    }

    // --- variables grid inline edit --------------------------------------------------------
    void begin_edit_value(int row) {
        if (row < 0) return;
        RECT rc;
        if (!ListView_GetSubItemRect(m_varsList, row, 1, LVIR_BOUNDS, &rc)) return;
        wchar_t valBuf[1024] = {};
        ListView_GetItemText(m_varsList, row, 1, valBuf, 1024);
        m_editingRow = row;
        m_varsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", valBuf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            m_varsList, (HMENU)kIdVarsEdit, core_api::get_my_instance(), nullptr);
        SendMessageW(m_varsEdit, WM_SETFONT, (WPARAM)m_font, TRUE);
        SetWindowLongPtrW(m_varsEdit, GWLP_USERDATA, (LONG_PTR)this);
        m_origEditProc = (WNDPROC)SetWindowLongPtrW(m_varsEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        SetFocus(m_varsEdit);
        SendMessageW(m_varsEdit, EM_SETSEL, 0, -1);
    }
    void commit_edit_value(bool save) {
        if (!m_varsEdit) return;
        if (save && m_editingRow >= 0) {
            wchar_t nameBuf[256] = {};
            ListView_GetItemText(m_varsList, m_editingRow, 0, nameBuf, 256);
            std::wstring wval = get_text(m_varsEdit);
            m_model.set_variable(to_utf8(nameBuf), to_utf8(wval));
            ListView_SetItemText(m_varsList, m_editingRow, 1, (LPWSTR)wval.c_str());
            changed();
        }
        HWND e = m_varsEdit; m_varsEdit = nullptr; m_editingRow = -1;
        DestroyWindow(e);
    }
    static LRESULT CALLBACK EditSubclassProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        PrefsInstance* self = reinterpret_cast<PrefsInstance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_KEYDOWN) {
            if (wp == VK_RETURN) { if (self) self->commit_edit_value(true); return 0; }
            if (wp == VK_ESCAPE) { if (self) self->commit_edit_value(false); return 0; }
        }
        if (msg == WM_KILLFOCUS) { if (self) self->commit_edit_value(true); return 0; }
        return CallWindowProcW(self->m_origEditProc, wnd, msg, wp, lp);
    }

    // --- font / colour pickers -------------------------------------------------------------
    void pick_font() {
        LOGFONTW lf = {};
        std::wstring face = get_text(m_fontFaceEdit);
        wcsncpy(lf.lfFaceName, face.empty() ? L"Tahoma" : face.c_str(), LF_FACESIZE - 1);
        int size = _wtoi(get_text(m_fontSizeEdit).c_str());
        HDC dc = GetDC(m_wnd);
        lf.lfHeight = -MulDiv(size > 0 ? size : 9, GetDeviceCaps(dc, LOGPIXELSY), 72);
        CHOOSEFONTW cf = { sizeof(cf) };
        cf.hwndOwner = m_wnd; cf.lpLogFont = &lf; cf.hDC = dc;
        cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST;
        if (ChooseFontW(&cf)) {
            int pt = -MulDiv(lf.lfHeight, 72, GetDeviceCaps(dc, LOGPIXELSY));
            m_model.set_font(to_utf8(lf.lfFaceName), std::to_string(pt < 1 ? 1 : pt));
            refresh_overrides_ui();
            changed();
        }
        ReleaseDC(m_wnd, dc);
    }
    void pick_color() {
        static COLORREF custom[16] = {};
        gfx::Color cur(0, 140, 220);
        m_model.accent(cur);
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner = m_wnd; cc.rgbResult = colorref_of(cur); cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            m_model.set_accent(gfx::Color(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult)));
            InvalidateRect(m_accentSwatch, nullptr, TRUE);
            changed();
        }
    }

    // --- drawing ---------------------------------------------------------------------------
    // The skin's preview, fitted into the box, or a note when there is none yet.
    void draw_preview(const DRAWITEMSTRUCT* di) {
        const RECT& r = di->rcItem;
        const int w = r.right - r.left, h = r.bottom - r.top;
        {
            gfx::GdiCanvas gdi(di->hDC, w, h);
            gfx::Canvas& cv = gdi;
            cv.fill_rect(gfx::Rect{ 0, 0, w, h }, gfx::Color(32, 32, 32));
            gfx::ImagePtr img = m_previewPath.empty() ? nullptr : gfx::decode_image_file(m_previewPath);
            if (img && img->width() > 0 && img->height() > 0) {
                const double s = std::min((double)w / img->width(), (double)h / img->height());
                const float dw = (float)(img->width() * s), dh = (float)(img->height() * s);
                cv.draw_image(*img, gfx::RectF{ (w - dw) / 2, (h - dh) / 2, dw, dh }, gfx::RectF{});
            } else {
                gfx::FontSpec f; f.face = "Segoe UI"; f.size = 9;
                cv.set_font(f);
                cv.draw_text(prefs_text::kNoPreview,
                             gfx::Rect{ 8, 0, w - 16, h }, gfx::kAlignCenter | gfx::kWordWrap,
                             gfx::Color(170, 170, 170));
            }
        }
        FrameRect(di->hDC, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
    }

    // --- layout ----------------------------------------------------------------------------
    void layout() {
        RECT rc; GetClientRect(m_wnd, &rc);
        MoveWindow(m_tab, 0, 0, rc.right, rc.bottom, TRUE);
        RECT disp = rc; TabCtrl_AdjustRect(m_tab, FALSE, &disp);
        for (HWND page : { m_pageGeneral, m_pageScript, m_pageVariables, m_pageOverrides })
            MoveWindow(page, disp.left, disp.top, disp.right - disp.left, disp.bottom - disp.top, TRUE);
        layout_general();
        layout_script();
        layout_variables();
        layout_overrides();
    }
    void layout_general() {
        RECT rc; GetClientRect(m_pageGeneral, &rc);
        const int pad = 8, labelH = 18, editH = 22, btnW = 90, comboW = 260;
        int y = pad;
        MoveWindow(m_rootLabel, pad, y, rc.right - pad * 2, labelH, TRUE); y += labelH + 2;
        MoveWindow(m_rootEdit, pad, y, rc.right - pad * 3 - btnW, editH, TRUE);
        MoveWindow(m_rootBrowseBtn, rc.right - pad - btnW, y, btnW, editH, TRUE); y += editH + 10;
        MoveWindow(m_skinLabel, pad, y, 100, labelH, TRUE);
        MoveWindow(m_skinCombo, pad + 104, y - 2, comboW, editH, TRUE); y += editH + 6;
        MoveWindow(m_mainLabel, pad, y, 100, labelH, TRUE);
        MoveWindow(m_mainCombo, pad + 104, y - 2, comboW, editH, TRUE); y += editH + 4;
        MoveWindow(m_skinWarning, pad, y, rc.right - pad * 2, labelH, TRUE); y += labelH + 8;
        MoveWindow(m_zoomLabel, pad, y, 100, labelH, TRUE);
        MoveWindow(m_zoomCombo, pad + 104, y - 2, comboW, editH, TRUE); y += editH + 8;
        MoveWindow(m_onTopCheck, pad, y, rc.right - pad * 2, labelH + 2, TRUE); y += labelH + 8;
        MoveWindow(m_rootNote, pad, y, rc.right - pad * 2, labelH * 2, TRUE); y += labelH * 2 + 8;
        // The skin's preview fills the rest of the page (4:3 at most).
        const int pw = std::max(0, (int)rc.right - pad * 2), ph = std::max(0, std::min(pw * 3 / 4, (int)rc.bottom - y - pad));
        MoveWindow(m_preview, pad, y, pw, ph, TRUE);
    }
    void layout_script() {
        RECT rc; GetClientRect(m_pageScript, &rc);
        const int pad = 8, labelH = 18;
        MoveWindow(m_scriptLabel, pad, pad, rc.right - pad * 2, labelH, TRUE);
        MoveWindow(m_scriptEdit, pad, pad + labelH + 2, rc.right - pad * 2, rc.bottom - pad * 2 - labelH - 2, TRUE);
    }
    void layout_variables() {
        RECT rc; GetClientRect(m_pageVariables, &rc);
        const int pad = 8, labelH = 18, btnH = 24;
        MoveWindow(m_varsNote, pad, pad, rc.right - pad * 2, labelH, TRUE);
        MoveWindow(m_varsList, pad, pad + labelH + 2, rc.right - pad * 2, rc.bottom - pad * 3 - labelH - btnH, TRUE);
        MoveWindow(m_varsRescanBtn, pad, rc.bottom - pad - btnH, 200, btnH, TRUE);
    }
    void layout_overrides() {
        RECT rc; GetClientRect(m_pageOverrides, &rc);
        const int pad = 8, labelH = 18, editH = 22;
        int y = pad;
        MoveWindow(m_ovNote, pad, y, rc.right - pad * 2, labelH * 2, TRUE); y += labelH * 2 + 8;
        MoveWindow(m_fontFaceLabel, pad, y + 3, 70, labelH, TRUE);
        MoveWindow(m_fontFaceEdit, pad + 74, y, 200, editH, TRUE);
        MoveWindow(m_fontSizeLabel, pad + 284, y + 3, 36, labelH, TRUE);
        MoveWindow(m_fontSizeEdit, pad + 322, y, 50, editH, TRUE);
        MoveWindow(m_fontPickBtn, pad + 382, y, 110, editH, TRUE); y += editH + 10;
        MoveWindow(m_accentLabel, pad, y + 3, 90, labelH, TRUE);
        MoveWindow(m_accentSwatch, pad + 94, y, 40, editH, TRUE);
        MoveWindow(m_accentPickBtn, pad + 144, y, 110, editH, TRUE); y += editH + 16;
        MoveWindow(m_accentClearBtn, pad, y, 140, editH, TRUE);
    }

    // --- WndProc ---------------------------------------------------------------------------
    void on_command(WORD id, WORD code) {
        if (m_updating) return;
        switch (id) {
        case kIdRootBrowse: if (code == BN_CLICKED) browse_root(); break;
        case kIdRootEdit: if (code == EN_KILLFOCUS) root_edited(); break;
        case kIdSkinCombo:
            if (code == CBN_SELCHANGE) {
                m_model.set_skin_index((int)SendMessageW(m_skinCombo, CB_GETCURSEL, 0, 0));
                refresh_skin_choice(); changed();
            }
            break;
        case kIdMainCombo:
            if (code == CBN_SELCHANGE) {
                m_model.set_main_index((int)SendMessageW(m_mainCombo, CB_GETCURSEL, 0, 0));
                refresh_skin_choice(); changed();
            }
            break;
        case kIdScriptEdit: if (code == EN_CHANGE) { m_model.set_script(get_utf8(m_scriptEdit)); changed(); } break;
        case kIdZoomCombo:
            if (code == CBN_SELCHANGE) { m_model.set_zoom_index((int)SendMessageW(m_zoomCombo, CB_GETCURSEL, 0, 0)); changed(); }
            break;
        case kIdOnTop:
            if (code == BN_CLICKED) { m_model.set_on_top(SendMessageW(m_onTopCheck, BM_GETCHECK, 0, 0) == BST_CHECKED); changed(); }
            break;
        case kIdVarsRescan: if (code == BN_CLICKED) { m_model.rescan_variables(); refresh_vars_list(); changed(); } break;
        case kIdFontPickBtn: if (code == BN_CLICKED) pick_font(); break;
        case kIdFontFaceEdit: case kIdFontSizeEdit:
            if (code == EN_CHANGE) { m_model.set_font(get_utf8(m_fontFaceEdit), get_utf8(m_fontSizeEdit)); changed(); }
            break;
        case kIdAccentPickBtn: if (code == BN_CLICKED) pick_color(); break;
        case kIdAccentClearBtn: if (code == BN_CLICKED) { m_model.clear_overrides(); refresh_overrides_ui(); changed(); } break;
        }
    }

    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        PrefsInstance* self = reinterpret_cast<PrefsInstance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = reinterpret_cast<PrefsInstance*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        // Only the root window (holding the tab control) creates and lays out the controls.
        wchar_t cls[64] = {}; GetClassNameW(wnd, cls, 64);
        const bool isRoot = wcscmp(cls, kRootClass) == 0;

        switch (msg) {
        case WM_CREATE: if (self && isRoot) self->create_controls(wnd); return 0;
        case WM_SIZE:   if (self && isRoot) self->layout(); return 0;
        case WM_NOTIFY: {
            if (!self) break;
            auto nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == kIdTab && nm->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(self->m_tab);
                HWND pages[4] = { self->m_pageGeneral, self->m_pageScript, self->m_pageVariables, self->m_pageOverrides };
                for (int i = 0; i < 4; ++i) ShowWindow(pages[i], i == sel ? SW_SHOW : SW_HIDE);
                return 0;
            }
            if (nm->idFrom == kIdVarsList && nm->code == NM_DBLCLK) {
                auto ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (ia->iItem >= 0) self->begin_edit_value(ia->iItem);
                return 0;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (self && di->CtlID == kIdAccentSwatch) {
                gfx::Color c;
                const bool set = self->m_model.accent(c);
                HBRUSH b = CreateSolidBrush(set ? colorref_of(c) : GetSysColor(COLOR_BTNFACE));
                FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
                FrameRect(di->hDC, &di->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return TRUE;
            }
            if (self && di->CtlID == kIdPreview) { self->draw_preview(di); return TRUE; }
            break;
        }
        case WM_COMMAND:
            if (self) self->on_command(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_DESTROY:
            if (self && isRoot) {
                if (self->m_font) DeleteObject(self->m_font);
                if (self->m_monoFont) DeleteObject(self->m_monoFont);
                self->m_font = self->m_monoFont = nullptr;
            }
            return 0;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    PrefsModel m_model{ prefs_backend() };
    bool m_updating = false;
    std::string m_previewPath;

    HWND m_wnd = nullptr, m_tab = nullptr;
    HWND m_pageGeneral = nullptr, m_pageScript = nullptr, m_pageVariables = nullptr, m_pageOverrides = nullptr;
    // General
    HWND m_rootLabel = nullptr, m_rootEdit = nullptr, m_rootBrowseBtn = nullptr,
         m_skinLabel = nullptr, m_skinCombo = nullptr, m_skinWarning = nullptr,
         m_mainLabel = nullptr, m_mainCombo = nullptr,
         m_zoomLabel = nullptr, m_zoomCombo = nullptr, m_onTopCheck = nullptr, m_rootNote = nullptr, m_preview = nullptr;
    // Script
    HWND m_scriptLabel = nullptr, m_scriptEdit = nullptr;
    // Variables
    HWND m_varsNote = nullptr, m_varsList = nullptr, m_varsRescanBtn = nullptr, m_varsEdit = nullptr;
    int m_editingRow = -1; WNDPROC m_origEditProc = nullptr;
    // Overrides
    HWND m_ovNote = nullptr, m_fontFaceLabel = nullptr, m_fontFaceEdit = nullptr,
         m_fontSizeLabel = nullptr, m_fontSizeEdit = nullptr, m_fontPickBtn = nullptr,
         m_accentLabel = nullptr, m_accentSwatch = nullptr, m_accentPickBtn = nullptr, m_accentClearBtn = nullptr;

    HFONT m_font = nullptr, m_monoFont = nullptr;
    const preferences_page_callback::ptr m_callback;
};

class PrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }
    GUID get_guid() override { return pui::prefs_page_guid(); }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        return new service_impl_t<PrefsInstance>(parent, callback);
    }
};

static preferences_page_factory_t<PrefsPage> g_prefs_page_factory;

} // namespace
} // namespace pui
