// Preferences page (Display > Panels UI (reborn)). Generic skin-manager UI: no fooAvA-specific
// wording — this drives ANY skin script in the recovered Panels-UI format (see FORMAT.md), not
// just the bundled reference skin. No ATL/WTL (not in our cross-compile toolchain) — plain Win32
// child windows + a native tab control (SysTabControl32) + a ListView for the variables grid,
// same low-level WndProc pattern as the rest of this component (see TrackDisplay/Popup).
//
// Four tabs:
//   General   - skins root folder (one subfolder per skin) + active skin + main script picker.
//   Script    - raw editor for the active skin's main script.
//   Variables - grid of the persistent pvars ($getpvar/$setpvar) the active script references.
//   Overrides - global font/accent-colour fallback used when a skin doesn't set its own.
//
// Applying writes the script file + persists root/active-skin/main-script + the pvar store. A
// script edit shows up live (the engine hot-reloads changed skin files); switching to another
// skin or main script doesn't — needs_restart tells foobar2000 to prompt for a restart.
#include "win_sdk.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/skin_config.h"
#include "../../core/fs_util.h"
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <set>
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
} // namespace

namespace {

// {9C1D9F3A-2B7E-4A6C-9F0D-7E3C5A8B1D40}
const GUID g_prefs_page_guid =
    { 0x9c1d9f3a, 0x2b7e, 0x4a6c, { 0x9f, 0x0d, 0x7e, 0x3c, 0x5a, 0x8b, 0x1d, 0x40 } };

std::wstring get_text(HWND ctl) {
    int n = GetWindowTextLengthW(ctl);
    std::wstring w(n, L'\0');
    if (n > 0) GetWindowTextW(ctl, &w[0], n + 1);
    return w;
}
void set_text(HWND ctl, const std::string& s) { SetWindowTextW(ctl, to_wide(s).c_str()); }

// List immediate subfolders of `root` (skin names for the General-tab picker).
std::vector<std::string> list_skin_folders(const std::wstring& root) {
    std::vector<std::string> out;
    if (root.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
            out.push_back(to_utf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// Best-effort scan for $getpvar(name)/$setpvar(name,...) references in a script — feeds the
// Variables tab's grid (the modern equivalent of the original's "setup panel").
std::vector<std::string> scan_pvar_names(const std::string& script) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const char* fn : { "getpvar", "setpvar" }) {
        std::string needle = std::string("$") + fn + "(";
        size_t pos = 0;
        while ((pos = script.find(needle, pos)) != std::string::npos) {
            size_t start = pos + needle.size(), end = start;
            while (end < script.size() && script[end] != ',' && script[end] != ')') ++end;
            std::string name = script.substr(start, end - start);
            size_t b = name.find_first_not_of(" \t");
            if (b == std::string::npos) { pos = end + 1; continue; }
            size_t e = name.find_last_not_of(" \t");
            name = name.substr(b, e - b + 1);
            if (!name.empty() && name.find('$') == std::string::npos && seen.insert(name).second)
                names.push_back(name);
            pos = end + 1;
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string serialize_pvars(const PvarMap& m) {
    std::string out;
    for (auto& kv : m) { out += kv.first; out += '='; out += kv.second; out += '\n'; }
    return out;
}

// "r-g-b" (0..255 each) — same dash format skin_engine.cpp's parse_rgb/find_color use.
COLORREF parse_rgb_dash(const std::string& s) {
    int v[3] = { 0,0,0 }, n = 0; const char* p = s.c_str();
    while (*p && n < 3) {
        while (*p && (*p < '0' || *p > '9')) ++p;
        if (!*p) break;
        int x = 0; while (*p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); ++p; }
        v[n++] = x;
        if (*p == '-') ++p; else break;
    }
    return RGB(v[0], v[1], v[2]);
}
std::string format_rgb_dash(COLORREF c) {
    char buf[32]; snprintf(buf, sizeof buf, "%d-%d-%d", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

// Reserved pvar keys the Overrides tab edits. Hidden from the freeform Variables grid.
const char* kFontFaceKey = "_prefs_font_face";
const char* kFontSizeKey = "_prefs_font_size";
const char* kAccentKey   = "_prefs_accent_color";
bool is_reserved_key(const std::string& k) { return k.rfind("_prefs_", 0) == 0; }

// Control IDs.
enum {
    // General
    kIdRootEdit = 1001, kIdRootBrowse = 1002, kIdSkinCombo = 1003, kIdMainCombo = 1004,
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

static const wchar_t* kPageClass = L"foo_ui_panels_prefs_page";
static const wchar_t* kRootClass = L"foo_ui_panels_prefs";

// One tab page: a plain child window hosting a fixed set of controls, shown/hidden by the
// owning PrefsInstance on tab switch. Kept dumb — PrefsInstance owns all state and logic.
struct Page {
    HWND wnd = nullptr;
};

class PrefsInstance : public preferences_page_instance {
public:
    PrefsInstance(HWND parent, preferences_page_callback::ptr callback) : m_callback(callback) {
        register_classes();
        m_pvars = load_all_pvars();
        m_appliedPvars = serialize_pvars(m_pvars);
        m_wnd = CreateWindowExW(0, kRootClass, L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 0, 0, parent, nullptr, core_api::get_my_instance(), this);
    }

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable;
        if (has_changed()) s |= preferences_state::changed;
        if (to_utf8(get_text(m_rootEdit)) != m_appliedRoot || combo_selected_skin() != m_appliedActive ||
            combo_selected_main() != m_appliedMain)
            s |= preferences_state::needs_restart;
        return s;
    }
    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        std::string root = to_utf8(get_text(m_rootEdit));
        std::string active = combo_selected_skin();
        std::string mainName = combo_selected_main();
        set_skins_root(root);
        set_active_skin(active);
        set_main_script_override(mainName);
        std::string script = to_utf8(get_text(m_scriptEdit));
        if (!m_scriptPath.empty() && script != m_appliedScript) write_file(m_scriptPath, script);
        save_all_pvars(m_pvars);

        m_appliedRoot = root; m_appliedActive = active; m_appliedMain = mainName; m_appliedScript = script;
        m_appliedPvars = serialize_pvars(m_pvars);
        m_callback->on_state_changed();
    }

    void reset() override { // back to defaults: no root override, bundled skin, no overrides
        set_text(m_rootEdit, "");
        refresh_skin_combo();
        refresh_main_combo(""); // automatic
        load_script_for_active();
        m_pvars = load_all_pvars();
        for (const char* k : { kFontFaceKey, kFontSizeKey, kAccentKey }) m_pvars.erase(k);
        refresh_overrides_ui();
        refresh_vars_list();
        m_callback->on_state_changed();
    }

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

private:
    // --- construction -------------------------------------------------------
    void create_controls(HWND wnd) {
        HINSTANCE inst = core_api::get_my_instance();
        m_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_monoFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

        m_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, wnd, (HMENU)kIdTab, inst, nullptr);
        SendMessageW(m_tab, WM_SETFONT, (WPARAM)m_font, TRUE);
        for (const wchar_t* t : { L"General", L"Script", L"Variables", L"Overrides" }) {
            TCITEMW ti = {}; ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)t;
            TabCtrl_InsertItem(m_tab, TabCtrl_GetItemCount(m_tab), &ti);
        }

        m_pageGeneral   = make_page(wnd, inst);
        m_pageScript    = make_page(wnd, inst);
        m_pageVariables = make_page(wnd, inst);
        m_pageOverrides = make_page(wnd, inst);

        create_general_controls(inst);
        create_script_controls(inst);
        create_variables_controls(inst);
        create_overrides_controls(inst);

        ShowWindow(m_pageGeneral, SW_SHOW);
        ShowWindow(m_pageScript, SW_HIDE);
        ShowWindow(m_pageVariables, SW_HIDE);
        ShowWindow(m_pageOverrides, SW_HIDE);

        set_text(m_rootEdit, skins_root());
        refresh_skin_combo();
        refresh_main_combo(main_script_override());
        m_appliedMain = combo_selected_main();
        load_script_for_active();
        refresh_vars_list();
        refresh_overrides_ui();

        m_appliedRoot = skins_root();
        m_appliedActive = combo_selected_skin();
    }

    HWND make_page(HWND parent, HINSTANCE inst) {
        return CreateWindowExW(0, kPageClass, L"", WS_CHILD, 0, 0, 0, 0, parent, nullptr, inst, this);
    }

    void create_general_controls(HINSTANCE inst) {
        HWND p = m_pageGeneral;
        m_rootLabel = CreateWindowExW(0, L"STATIC",
            L"Skins root folder (one subfolder per skin — leave empty to use the single bundled skin):",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_rootEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0,0,0,0, p, (HMENU)kIdRootEdit, inst, nullptr);
        m_rootBrowseBtn = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, p, (HMENU)kIdRootBrowse, inst, nullptr);
        m_skinLabel = CreateWindowExW(0, L"STATIC", L"Active skin:",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_skinCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0,0,0,0, p, (HMENU)kIdSkinCombo, inst, nullptr);
        m_mainLabel = CreateWindowExW(0, L"STATIC", L"Main script:",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_mainCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0,0,0,0, p, (HMENU)kIdMainCombo, inst, nullptr);
        m_skinWarning = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        for (HWND c : { m_rootLabel, m_rootEdit, m_rootBrowseBtn, m_skinLabel, m_skinCombo, m_mainLabel,
                        m_mainCombo, m_skinWarning })
            SendMessageW(c, WM_SETFONT, (WPARAM)m_font, TRUE);
    }

    void create_script_controls(HINSTANCE inst) {
        HWND p = m_pageScript;
        m_scriptLabel = CreateWindowExW(0, L"STATIC", L"Active skin's main script:",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_scriptEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
            ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL, 0,0,0,0, p, (HMENU)kIdScriptEdit, inst, nullptr);
        // Plain EDIT controls silently cap input at 30,000 chars without this — real scripts run ~36KB.
        SendMessageW(m_scriptEdit, EM_SETLIMITTEXT, 0, 0);
        SendMessageW(m_scriptLabel, WM_SETFONT, (WPARAM)m_font, TRUE);
        SendMessageW(m_scriptEdit, WM_SETFONT, (WPARAM)m_monoFont, TRUE);
    }

    void create_variables_controls(HINSTANCE inst) {
        HWND p = m_pageVariables;
        m_varsNote = CreateWindowExW(0, L"STATIC",
            L"Persistent variables ($getpvar/$setpvar) found in the active script. Double-click a value to edit.",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_varsList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0,0,0,0, p, (HMENU)kIdVarsList, inst, nullptr);
        ListView_SetExtendedListViewStyle(m_varsList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 220; col.pszText = (LPWSTR)L"Variable"; ListView_InsertColumn(m_varsList, 0, &col);
        col.cx = 300; col.pszText = (LPWSTR)L"Value";    ListView_InsertColumn(m_varsList, 1, &col);
        m_varsRescanBtn = CreateWindowExW(0, L"BUTTON", L"Rescan script for variables",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, p, (HMENU)kIdVarsRescan, inst, nullptr);
        SendMessageW(m_varsNote, WM_SETFONT, (WPARAM)m_font, TRUE);
        SendMessageW(m_varsList, WM_SETFONT, (WPARAM)m_font, TRUE);
        SendMessageW(m_varsRescanBtn, WM_SETFONT, (WPARAM)m_font, TRUE);
    }

    void create_overrides_controls(HINSTANCE inst) {
        HWND p = m_pageOverrides;
        m_ovNote = CreateWindowExW(0, L"STATIC",
            L"Fallbacks used when the active skin doesn't set its own font / accent colour. Leave blank to defer to the skin.",
            WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_fontFaceLabel = CreateWindowExW(0, L"STATIC", L"Font face:", WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_fontFaceEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0,0,0,0, p, (HMENU)kIdFontFaceEdit, inst, nullptr);
        m_fontSizeLabel = CreateWindowExW(0, L"STATIC", L"Size:", WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_fontSizeEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 0,0,0,0, p, (HMENU)kIdFontSizeEdit, inst, nullptr);
        m_fontPickBtn = CreateWindowExW(0, L"BUTTON", L"Pick font...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, p, (HMENU)kIdFontPickBtn, inst, nullptr);
        m_accentLabel = CreateWindowExW(0, L"STATIC", L"Accent colour:", WS_CHILD | WS_VISIBLE, 0,0,0,0, p, nullptr, inst, nullptr);
        m_accentSwatch = CreateWindowExW(WS_EX_CLIENTEDGE, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,0,0,0, p, (HMENU)kIdAccentSwatch, inst, nullptr);
        m_accentPickBtn = CreateWindowExW(0, L"BUTTON", L"Pick colour...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, p, (HMENU)kIdAccentPickBtn, inst, nullptr);
        m_accentClearBtn = CreateWindowExW(0, L"BUTTON", L"Clear overrides",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,0,0,0, p, (HMENU)kIdAccentClearBtn, inst, nullptr);
        for (HWND c : { m_ovNote, m_fontFaceLabel, m_fontFaceEdit, m_fontSizeLabel, m_fontSizeEdit,
                         m_fontPickBtn, m_accentLabel, m_accentPickBtn, m_accentClearBtn })
            SendMessageW(c, WM_SETFONT, (WPARAM)m_font, TRUE);
    }

    // --- data <-> UI ----------------------------------------------------------
    std::string combo_selected_skin() const { // "" = bundled default (first combo entry)
        int i = (int)SendMessageW(m_skinCombo, CB_GETCURSEL, 0, 0);
        if (i <= 0) return {};
        return m_skinChoices[i - 1];
    }

    void refresh_skin_combo() {
        std::wstring root = get_text(m_rootEdit);
        m_skinChoices = list_skin_folders(root);
        SendMessageW(m_skinCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(m_skinCombo, CB_ADDSTRING, 0, (LPARAM)L"(bundled default)");
        for (auto& s : m_skinChoices) SendMessageW(m_skinCombo, CB_ADDSTRING, 0, (LPARAM)to_wide(s).c_str());
        std::string a = active_skin();
        int sel = 0;
        for (size_t i = 0; i < m_skinChoices.size(); ++i) if (m_skinChoices[i] == a) sel = (int)i + 1;
        SendMessageW(m_skinCombo, CB_SETCURSEL, sel, 0);
        update_skin_warning();
    }

    // "(automatic)" + the folder's *.txt files; selects `name` if listed, else automatic.
    void refresh_main_combo(const std::string& name) {
        m_mainChoices = main_script_candidates(resolve_skin_dir_for_ui());
        SendMessageW(m_mainCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(m_mainCombo, CB_ADDSTRING, 0, (LPARAM)L"(automatic)");
        int sel = 0;
        for (size_t i = 0; i < m_mainChoices.size(); ++i) {
            SendMessageW(m_mainCombo, CB_ADDSTRING, 0, (LPARAM)to_wide(m_mainChoices[i]).c_str());
            if (m_mainChoices[i] == name) sel = (int)i + 1;
        }
        SendMessageW(m_mainCombo, CB_SETCURSEL, sel, 0);
    }
    std::string combo_selected_main() const { // "" = automatic
        int i = (int)SendMessageW(m_mainCombo, CB_GETCURSEL, 0, 0);
        return i <= 0 || i > (int)m_mainChoices.size() ? std::string() : m_mainChoices[i - 1];
    }

    // The main script the page's pending choices resolve to ("" = none), and why if unclear.
    std::string resolve_main_for_ui(std::string* why = nullptr) {
        std::string dir = resolve_skin_dir_for_ui();
        SkinConfig cfg; cfg.load(dir);
        return resolve_main_script_with(dir, cfg, combo_selected_main(), why);
    }

    void update_skin_warning() {
        std::string why;
        std::string main = resolve_main_for_ui(&why);
        std::wstring msg;
        if (main.empty()) msg = L"⚠ " + to_wide(why.empty() ? "no main script found in this folder." : why);
        else if (!why.empty()) msg = L"⚠ " + to_wide(why);
        SetWindowTextW(m_skinWarning, msg.c_str());
    }

    // Same resolution resolve_skin_dir() does, but against the page's *unsaved* edits.
    std::string resolve_skin_dir_for_ui() {
        return resolve_skin_dir_for(to_utf8(get_text(m_rootEdit)), combo_selected_skin());
    }

    void load_script_for_active() {
        m_scriptPath = resolve_main_for_ui();
        std::string script = m_scriptPath.empty() ? std::string() : read_file(m_scriptPath);
        set_text(m_scriptEdit, script);
        m_appliedScript = script;
        update_skin_warning();
    }

    void refresh_vars_list() {
        ListView_DeleteAllItems(m_varsList);
        int row = 0;
        for (auto& kv : m_pvars) {
            if (is_reserved_key(kv.first)) continue;
            LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = row; it.iSubItem = 0;
            std::wstring name = to_wide(kv.first);
            it.pszText = (LPWSTR)name.c_str();
            ListView_InsertItem(m_varsList, &it);
            std::wstring val = to_wide(kv.second);
            ListView_SetItemText(m_varsList, row, 1, (LPWSTR)val.c_str());
            ++row;
        }
    }

    void rescan_vars() {
        std::string script = to_utf8(get_text(m_scriptEdit));
        for (auto& name : scan_pvar_names(script))
            if (m_pvars.find(name) == m_pvars.end()) m_pvars[name] = "";
        refresh_vars_list();
    }

    void refresh_overrides_ui() {
        auto get = [&](const char* k) { auto it = m_pvars.find(k); return it != m_pvars.end() ? it->second : std::string(); };
        set_text(m_fontFaceEdit, get(kFontFaceKey));
        set_text(m_fontSizeEdit, get(kFontSizeKey));
        std::string acc = get(kAccentKey);
        m_accentColor = acc.empty() ? RGB(0, 140, 220) : parse_rgb_dash(acc);
        InvalidateRect(m_accentSwatch, nullptr, TRUE);
    }

    bool has_changed() const {
        return to_utf8(get_text(m_rootEdit)) != m_appliedRoot ||
               combo_selected_skin() != m_appliedActive ||
               combo_selected_main() != m_appliedMain ||
               to_utf8(get_text(m_scriptEdit)) != m_appliedScript ||
               serialize_pvars(m_pvars) != m_appliedPvars;
    }
    void notify_changed() { m_callback->on_state_changed(); }

    // --- variables grid inline edit --------------------------------------------
    void begin_edit_value(int row) {
        if (row < 0) return;
        RECT rc;
        if (!ListView_GetSubItemRect(m_varsList, row, 1, LVIR_BOUNDS, &rc)) return;
        wchar_t nameBuf[256] = {};
        ListView_GetItemText(m_varsList, row, 0, nameBuf, 256);
        wchar_t valBuf[1024] = {};
        ListView_GetItemText(m_varsList, row, 1, valBuf, 1024);
        m_editingRow = row;
        m_varsEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", valBuf,
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
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
            std::string name = to_utf8(nameBuf);
            m_pvars[name] = to_utf8(wval);
            ListView_SetItemText(m_varsList, m_editingRow, 1, (LPWSTR)wval.c_str());
            notify_changed();
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

    // --- font / colour pickers --------------------------------------------------
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
            set_text(m_fontFaceEdit, to_utf8(lf.lfFaceName));
            int pt = -MulDiv(lf.lfHeight, 72, GetDeviceCaps(dc, LOGPIXELSY));
            char buf[16]; snprintf(buf, sizeof buf, "%d", pt < 1 ? 1 : pt);
            set_text(m_fontSizeEdit, buf);
            commit_overrides_from_ui();
        }
        ReleaseDC(m_wnd, dc);
    }
    void pick_color() {
        static COLORREF custom[16] = {};
        CHOOSECOLORW cc = { sizeof(cc) };
        cc.hwndOwner = m_wnd; cc.rgbResult = m_accentColor; cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            m_accentColor = cc.rgbResult;
            m_pvars[kAccentKey] = format_rgb_dash(m_accentColor);
            InvalidateRect(m_accentSwatch, nullptr, TRUE);
            notify_changed();
        }
    }
    void commit_overrides_from_ui() {
        m_pvars[kFontFaceKey] = to_utf8(get_text(m_fontFaceEdit));
        m_pvars[kFontSizeKey] = to_utf8(get_text(m_fontSizeEdit));
        notify_changed();
    }
    void clear_overrides() {
        for (const char* k : { kFontFaceKey, kFontSizeKey, kAccentKey }) m_pvars.erase(k);
        refresh_overrides_ui();
        notify_changed();
    }

    // --- layout -----------------------------------------------------------------
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
        const int pad = 8, labelH = 18, editH = 22, btnW = 90;
        int y = pad;
        MoveWindow(m_rootLabel, pad, y, rc.right - pad * 2, labelH, TRUE); y += labelH + 2;
        MoveWindow(m_rootEdit, pad, y, rc.right - pad * 3 - btnW, editH, TRUE);
        MoveWindow(m_rootBrowseBtn, rc.right - pad - btnW, y, btnW, editH, TRUE); y += editH + 10;
        MoveWindow(m_skinLabel, pad, y, 100, labelH, TRUE);
        MoveWindow(m_skinCombo, pad + 104, y - 2, 260, editH, TRUE); y += editH + 6;
        MoveWindow(m_mainLabel, pad, y, 100, labelH, TRUE);
        MoveWindow(m_mainCombo, pad + 104, y - 2, 260, editH, TRUE); y += editH + 4;
        MoveWindow(m_skinWarning, pad, y, rc.right - pad * 2, labelH, TRUE);
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
        MoveWindow(m_varsList, pad, pad + labelH + 2, rc.right - pad * 2,
                   rc.bottom - pad * 3 - labelH - btnH, TRUE);
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

    void browse_root() {
        wchar_t path[MAX_PATH] = {};
        BROWSEINFOW bi = {}; bi.hwndOwner = m_wnd; bi.lpszTitle = L"Skins root folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            if (SHGetPathFromIDListW(pidl, path)) {
                SetWindowTextW(m_rootEdit, path);
                refresh_skin_combo(); load_script_for_active(); rescan_vars();
            }
            CoTaskMemFree(pidl);
        }
    }

    // --- WndProc ------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        PrefsInstance* self = reinterpret_cast<PrefsInstance*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<PrefsInstance*>(cs->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        // Only the root page (holding the tab control) creates the child controls.
        wchar_t cls[64] = {}; GetClassNameW(wnd, cls, 64);
        bool isRoot = wcscmp(cls, kRootClass) == 0;

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
                HBRUSH b = CreateSolidBrush(self->m_accentColor);
                FillRect(di->hDC, &di->rcItem, b); DeleteObject(b);
                FrameRect(di->hDC, &di->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (self) {
                WORD id = LOWORD(wp), code = HIWORD(wp);
                if (id == kIdRootBrowse && code == BN_CLICKED) self->browse_root();
                if (id == kIdSkinCombo && code == CBN_SELCHANGE) {
                    self->refresh_main_combo(self->combo_selected_main());
                    self->update_skin_warning(); self->load_script_for_active();
                    self->rescan_vars(); self->notify_changed();
                }
                if (id == kIdMainCombo && code == CBN_SELCHANGE) {
                    self->update_skin_warning(); self->load_script_for_active();
                    self->rescan_vars(); self->notify_changed();
                }
                if (id == kIdScriptEdit && code == EN_CHANGE) self->notify_changed();
                if (id == kIdVarsRescan && code == BN_CLICKED) self->rescan_vars();
                if (id == kIdFontPickBtn && code == BN_CLICKED) self->pick_font();
                if ((id == kIdFontFaceEdit || id == kIdFontSizeEdit) && code == EN_CHANGE)
                    self->commit_overrides_from_ui();
                if (id == kIdAccentPickBtn && code == BN_CLICKED) self->pick_color();
                if (id == kIdAccentClearBtn && code == BN_CLICKED) self->clear_overrides();
            }
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

    // Root + tab control
    HWND m_wnd = nullptr, m_tab = nullptr;
    HWND m_pageGeneral = nullptr, m_pageScript = nullptr, m_pageVariables = nullptr, m_pageOverrides = nullptr;
    // General
    HWND m_rootLabel = nullptr, m_rootEdit = nullptr, m_rootBrowseBtn = nullptr,
         m_skinLabel = nullptr, m_skinCombo = nullptr, m_skinWarning = nullptr,
         m_mainLabel = nullptr, m_mainCombo = nullptr;
    std::vector<std::string> m_skinChoices, m_mainChoices;
    std::string m_scriptPath; // the file the Script tab edits ("" = none resolved)
    // Script
    HWND m_scriptLabel = nullptr, m_scriptEdit = nullptr;
    // Variables
    HWND m_varsNote = nullptr, m_varsList = nullptr, m_varsRescanBtn = nullptr, m_varsEdit = nullptr;
    int m_editingRow = -1; WNDPROC m_origEditProc = nullptr;
    // Overrides
    HWND m_ovNote = nullptr, m_fontFaceLabel = nullptr, m_fontFaceEdit = nullptr,
         m_fontSizeLabel = nullptr, m_fontSizeEdit = nullptr, m_fontPickBtn = nullptr,
         m_accentLabel = nullptr, m_accentSwatch = nullptr, m_accentPickBtn = nullptr, m_accentClearBtn = nullptr;
    COLORREF m_accentColor = RGB(0, 140, 220);

    HFONT m_font = nullptr, m_monoFont = nullptr;
    PvarMap m_pvars; // shared by Variables + Overrides tabs
    std::string m_appliedRoot, m_appliedActive, m_appliedMain, m_appliedScript, m_appliedPvars; // has_changed() baseline
    const preferences_page_callback::ptr m_callback;
};

class PrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Panels UI (reborn)"; }
    GUID get_guid() override { return g_prefs_page_guid; }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        return new service_impl_t<PrefsInstance>(parent, callback);
    }
};

static preferences_page_factory_t<PrefsPage> g_prefs_page_factory;

} // namespace
} // namespace pui
