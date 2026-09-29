#include "track_display.h"
#include "skin_engine.h"
#include "lyrics_panel.h"

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_trackdisplay";

// Default content until per-panel scripts are wired from the skin.
static const char* kDefaultScript =
    "$font(Segoe UI,15,b)$drawstring([%title%],6,4,%el_width%,22,255-255-255,)"
    "$font(Segoe UI,11,)$drawstring([%artist%][ \xe2\x80\x94 %album%],6,28,%el_width%,18,200-200-210,)"
    "$font(Segoe UI,11,)$drawstring([%playback_time% / %length%],6,48,%el_width%,18,160-200-255,)";

void TrackDisplay::register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND TrackDisplay::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    if (m_script.is_empty()) set_script(nullptr);
    // WS_CLIPSIBLINGS: don't paint over the overlay panels hosted on top of us (the lyrics
    // view) every repaint — that made them flicker.
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), this);
    if (m_wnd) SetTimer(m_wnd, 1, 1000, nullptr); // refresh elapsed time / track changes
    return m_wnd;
}

void TrackDisplay::set_script(const char* spec) {
    titleformat_compiler::get()->compile_safe(m_script, spec && *spec ? spec : kDefaultScript);
}

void TrackDisplay::paint() {
    RECT rc; GetClientRect(m_wnd, &rc);
    HDC dc = GetDC(m_wnd);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);

    // Transparent background: copy what the parent painted behind us.
    HWND parent = GetParent(m_wnd);
    POINT org = { 0, 0 }; MapWindowPoints(m_wnd, parent, &org, 1);
    HDC pdc = GetDC(parent);
    BitBlt(mem, 0, 0, rc.right, rc.bottom, pdc, org.x, org.y, SRCCOPY);
    ReleaseDC(parent, pdc);

    metadb_handle_ptr track;
    playback_control::get()->get_now_playing(track);
    // Capture clickable regions in this panel's own coordinate space (cover-case view switch,
    // play/pause overlay, rating stars, theme buttons), plus any $panel() this script requests.
    if (m_engine) m_engine->draw_script(mem, rc.right, rc.bottom, m_script, track, &m_buttons,
                                        m_hoverX, m_hoverY, &m_childPlacements);
    host_children();

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
    ReleaseDC(m_wnd, dc);
}

// Display.txt's CD-case-edge button cycles mini.panels 1 -> 2 -> 3 -> 1, and its $select puts a
// cover-sized overlay panel on top of the cover for states 2 and 3. The legacy skin used a mini
// playlist there for state 2; ours shows the Lyric Show panel instead. State 3 is never reached
// (on_click folds it back to 1), so the edge click toggles cover <-> lyrics.
static Placement remap_child(const Placement& p) {
    Placement q = p;
    if (p.name == "mini.playlist") { q.name = "mini.lyrics"; q.type = "Lyric Show"; }
    return q;
}

void TrackDisplay::host_children() {
    if (!m_engine) return;
    HWND parent = GetParent(m_wnd);
    POINT org = { 0, 0 }; MapWindowPoints(m_wnd, parent, &org, 1);
    std::set<std::string> shown;
    for (const auto& raw : m_childPlacements) {
        Placement p = remap_child(raw);
        if (p.name == m_name) continue; // never host ourselves
        m_engine->host_child_panel(p, org.x, org.y);
        shown.insert(p.name);
    }
    for (const auto& n : m_shownChildren)
        if (!shown.count(n)) m_engine->hide_child_panel(n);
    m_shownChildren.swap(shown);
}

bool TrackDisplay::update_hover(int x, int y) {
    auto hit = [&](int hx, int hy) {
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            const auto& b = m_buttons[i];
            if (hx >= b.x && hx < b.x + b.w && hy >= b.y && hy < b.y + b.h) return (int)i;
        }
        return -1;
    };
    int before = hit(m_hoverX, m_hoverY);
    m_hoverX = x; m_hoverY = y;
    return hit(x, y) != before;
}

static const wchar_t* kEditorClass = L"foo_ui_panels_codeedit";
enum { IDC_CODE = 100, IDC_APPLY, IDC_OK, IDC_CANCEL };

static std::wstring to_wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string panel_path(SkinEngine* e, const std::string& name) {
    return e->base_dir() + "/panels/" + name + ".txt";
}

bool TrackDisplay::apply_code(const std::string& utf8) {
    if (!m_engine || m_name.empty()) return false;
    FILE* f = fopen(panel_path(m_engine, m_name).c_str(), "wb");
    if (!f) return false;
    fwrite(utf8.data(), 1, utf8.size(), f);
    fclose(f);
    // Same load path as panel creation (adds the engine's cover-path init), then repaint.
    std::string sc = m_engine->read_panel_script(m_name);
    set_script(sc.empty() ? nullptr : sc.c_str());
    InvalidateRect(m_wnd, nullptr, FALSE);
    return true;
}

LRESULT CALLBACK TrackDisplay::EditorProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    TrackDisplay* self = reinterpret_cast<TrackDisplay*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
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
        if ((id == IDC_APPLY || id == IDC_OK) && self) {
            HWND ed = GetDlgItem(wnd, IDC_CODE);
            int n = GetWindowTextLengthW(ed);
            std::wstring w(n + 1, L'\0');
            GetWindowTextW(ed, &w[0], n + 1); w.resize(n);
            pfc::stringcvt::string_utf8_from_wide u(w.c_str());
            if (!self->apply_code(u.get_ptr())) {
                MessageBoxW(wnd, L"Could not save the panel script.", L"Edit code", MB_ICONERROR);
                return 0;
            }
            if (id == IDC_OK) DestroyWindow(wnd);
            return 0;
        }
        if (id == IDC_CANCEL) { DestroyWindow(wnd); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(wnd); return 0;
    case WM_DESTROY:
        if (self) self->m_editor = nullptr;
        if (HFONT f = (HFONT)SendMessageW(GetDlgItem(wnd, IDC_CODE), WM_GETFONT, 0, 0)) DeleteObject(f);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void TrackDisplay::open_code_editor() {
    if (!m_engine || m_name.empty() || m_engine->base_dir().empty()) return;
    if (m_editor) { SetForegroundWindow(m_editor); return; }

    static bool reg = false;
    if (!reg) {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = EditorProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kEditorClass;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
    }

    std::string text;
    if (FILE* f = fopen(panel_path(m_engine, m_name).c_str(), "rb")) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0) { text.resize(n); text.resize(fread(&text[0], 1, n, f)); }
        fclose(f);
    }

    std::wstring title = L"Edit code - " + to_wide(m_name);
    HWND owner = GetAncestor(m_wnd, GA_ROOT);
    m_editor = CreateWindowExW(0, kEditorClass, title.c_str(),
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
                               owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_editor) return;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    // Multi-line, word-wrapped (the extracted scripts are mostly one very long line).
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wide(text).c_str(),
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              0, 0, 0, 0, m_editor, (HMENU)(INT_PTR)IDC_CODE, hi, nullptr);
    SendMessageW(ed, EM_SETLIMITTEXT, 0, 0); // no 32K cap
    HFONT mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    SendMessageW(ed, WM_SETFONT, (WPARAM)mono, TRUE);
    HFONT ui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    const struct { int id; const wchar_t* label; } btns[] = {
        { IDC_APPLY, L"Apply" }, { IDC_OK, L"OK" }, { IDC_CANCEL, L"Cancel" } };
    for (auto& b : btns) {
        HWND h = CreateWindowExW(0, L"BUTTON", b.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 0, 0, m_editor, (HMENU)(INT_PTR)b.id, hi, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)ui, TRUE);
    }
    RECT rc; GetClientRect(m_editor, &rc);
    SendMessageW(m_editor, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    SetFocus(ed);
}

void TrackDisplay::on_rclick(int x, int y) {
    // While the lyrics overlay is up, right-click anywhere on the CD-case frame opens the Lyric
    // Show settings (the same menu as right-clicking the lyrics themselves).
    if (m_engine && m_shownChildren.count("mini.lyrics")) {
        LyricsPanel::show_settings_menu(m_engine, m_wnd);
        return;
    }
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Edit code...");
    POINT pt = { x, y }; ClientToScreen(m_wnd, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(m);
    if (cmd == 1) open_code_editor();
}

LRESULT CALLBACK TrackDisplay::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    TrackDisplay* self = reinterpret_cast<TrackDisplay*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<TrackDisplay*>(cs->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); if (self) self->paint(); EndPaint(wnd, &ps); return 0; }
    case WM_TIMER: InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_SIZE:  InvalidateRect(wnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: if (self) self->on_click((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_SHOWWINDOW:
        // Hidden by the canvas (e.g. cover flow mode replaces the Display panel): take the
        // overlay panels our script hosted down with us — nothing repaints us to hide them.
        if (self && !wp && self->m_engine) {
            for (const auto& n : self->m_shownChildren) self->m_engine->hide_child_panel(n);
            self->m_shownChildren.clear();
        }
        break;
    case WM_RBUTTONUP:   if (self) self->on_rclick((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_MOUSEMOVE:
        if (self) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme);
            if (self->update_hover((short)LOWORD(lp), (short)HIWORD(lp))) InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self && self->update_hover(-1, -1)) InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY: KillTimer(wnd, 1); if (self && self->m_editor) DestroyWindow(self->m_editor); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void TrackDisplay::on_click(int x, int y) {
    if (!m_engine) return;
    for (const auto& b : m_buttons) {
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
            // The CD-case edge cycles mini.panels 1 -> 2 -> 3; we only use cover (1) and lyrics
            // (2) — cover flow has its own button — so the step to 3 goes back to 1 instead.
            std::string act = b.action == "PVAR:SET:mini.panels:3" ? "PVAR:SET:mini.panels:1" : b.action;
            m_engine->run_button_action(act); // repaints all panels on a pvar change
            InvalidateRect(m_wnd, nullptr, FALSE);
            return;
        }
    }
}

} // namespace pui
