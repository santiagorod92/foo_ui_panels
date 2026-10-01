// Native "Track Display" panel: paints now-playing info by running a titleformat script through
// the shared draw engine each repaint. Replaces the legacy fooAvA "Track Display" uie panel
// (which had no DUI equivalent).
#pragma once
#include "../ui/view.h"
#include "../core/button.h"
#include <vector>
#include <set>
#include <string>

namespace pui {

class SkinEngine; // shares pvars / base dir / draw funcs

class TrackDisplay : public ui::View {
public:
    // name: this $panel()'s name — for "Edit code..." (panels/<name>.txt) and child hosting.
    TrackDisplay(SkinEngine* engine, std::string name);
    void set_script(const char* spec); // compiles; falls back to a default now-playing script

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_timer(int) override;
    void on_resize(int, int) override { invalidate(); }
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_up(const ui::MouseEvent& e) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_leave() override;
    void on_visibility(bool shown) override;
    void on_destroy() override;

private:
    void on_click(int x, int y);
    void on_rclick(int x, int y); // right-click -> "Edit code..." (panels/<name>.txt)
    void host_children(); // host/hide the $panel()s this panel's own script requested
    void open_code_editor();       // panels/<name>.txt in a small editor window (Apply/OK/Cancel)
    bool apply_code(const std::string& utf8); // save + recompile + repaint (hot reload)
    bool update_hover(int x, int y); // returns true if the hovered button changed

    SkinEngine* m_engine = nullptr;
    std::string m_name;
    service_ptr_t<titleformat_object> m_script;
    std::vector<Button> m_buttons; // clickable regions captured during the last paint
    int m_hoverX = -1, m_hoverY = -1; // panel-local mouse pos, for $button/$button2 hover state
    // $panel() calls this panel's OWN script makes (e.g. Display.txt's mini.playlist/miniinfo
    // cover overlay, cycled by the CD-case-edge button2) — render()'s own placement loop never
    // sees these since it only walks the master canvas script; see SkinEngine::host_child_panel.
    std::vector<Placement> m_childPlacements;
    std::set<std::string> m_shownChildren; // names hosted last paint, to hide ones that drop out
};

} // namespace pui
