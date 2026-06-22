// Phase 3 — Panels UI skin interpreter.
// The skin is a titleformat-style script re-evaluated on every resize. We reuse
// foobar2000's titleformat compiler (gets $if/$sub/$add/… for free) and supply the
// Panels-UI-specific functions/fields via a titleformat_hook:
//   %_width% %_height%      -> current canvas size
//   $panel(name,type,x,y,w,h) -> records a panel placement (rect already evaluated)
//   $eval(expr)             -> integer arithmetic over { } expressions
//   $getpvar/$setpvar       -> persistent variables (setup panel state)
// Drawing functions ($drawrect/$font/$imageabs2/$button…) are stubbed for now.
#pragma once
#include "win_sdk.h"
#include "panel_host.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace pui {

struct Placement {
    std::string name;   // unique instance name
    std::string type;   // legacy panel type (mapped to a DUI element name)
    int x = 0, y = 0, w = 0, h = 0;
};

class SkinEngine {
public:
    void set_parent(HWND parent) { m_parent = parent; }
    // Compile a skin script. Returns false on compile error (logged to console).
    bool load(const char* script);
    // Re-evaluate at the given canvas size: recomputes placements, then creates/positions
    // the hosted panel windows accordingly.
    void layout(int width, int height);

private:
    friend class SkinHook;
    HWND m_parent = nullptr;
    service_ptr_t<titleformat_object> m_script;
    std::map<std::string, std::string> m_pvars;
    std::vector<Placement> m_placements;
    std::map<std::string, std::unique_ptr<PanelHost>> m_hosts; // by placement name
};

} // namespace pui
