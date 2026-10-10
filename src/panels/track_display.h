#pragma once
#include "../ui/view.h"
#include "../core/button.h"
#include <vector>
#include <set>
#include <string>

namespace pui {

class SkinEngine;

class TrackDisplay : public ui::View {
public:
    TrackDisplay(SkinEngine* engine, std::string name);
    void set_script(const char* spec);

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
    void on_rclick(int x, int y);
    void host_children();
    void open_code_editor();
    bool apply_code(const std::string& utf8);
    bool update_hover(int x, int y);

    SkinEngine* m_engine = nullptr;
    std::string m_name;
    service_ptr_t<titleformat_object> m_script;
    std::vector<Button> m_buttons;
    int m_hoverX = -1, m_hoverY = -1;
    std::vector<Placement> m_childPlacements;
    std::set<std::string> m_shownChildren;
    gfx::Rect m_problemMarker;
};

}
