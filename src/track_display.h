// Native "Track Display" panel: a child window that paints now-playing info by
// running a titleformat script through the shared draw engine each repaint.
// Replaces the legacy fooAvA "Track Display" uie panel (which had no DUI equivalent).
#pragma once
#include "win_sdk.h"
#include "button.h"
#include <vector>
#include <set>
#include <string>

namespace pui {

class SkinEngine; // shares pvars / base dir / draw funcs

class TrackDisplay {
public:
    HWND create(HWND parent, SkinEngine* engine);
    HWND wnd() const { return m_wnd; }
    void set_script(const char* spec); // compiles; falls back to a default now-playing script
    void set_name(const std::string& n) { m_name = n; } // this $panel()'s name — for "edit code"

    static void register_class();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void paint();
    void on_click(int x, int y);
    void on_rclick(int x, int y); // right-click -> "Edit code..." (panels/<name>.txt)
    void host_children(); // host/hide the $panel()s this panel's own script requested
    void open_code_editor();       // panels/<name>.txt in a small editor window (Apply/OK/Cancel)
    bool apply_code(const std::string& utf8); // save + recompile + repaint (hot reload)
    static LRESULT CALLBACK EditorProc(HWND, UINT, WPARAM, LPARAM);
    bool update_hover(int x, int y); // returns true if the hovered button changed

    HWND m_wnd = nullptr;
    SkinEngine* m_engine = nullptr;
    std::string m_name; // this $panel()'s name, set once at creation by dispatch_placement
    service_ptr_t<titleformat_object> m_script;
    std::vector<Button> m_buttons; // clickable regions captured during the last paint
    int m_hoverX = -1, m_hoverY = -1; // panel-local mouse pos, for $button/$button2 hover state
    // $panel() calls this panel's OWN script makes (e.g. Display.txt's mini.playlist/miniinfo
    // cover overlay, cycled by the CD-case-edge button2) — render()'s own placement loop never
    // sees these since it only walks the master canvas script; see SkinEngine::host_child_panel.
    std::vector<Placement> m_childPlacements;
    std::set<std::string> m_shownChildren; // names hosted last paint, to hide ones that drop out
    HWND m_editor = nullptr; // open "Edit code" window, if any (one per panel)
};

} // namespace pui
