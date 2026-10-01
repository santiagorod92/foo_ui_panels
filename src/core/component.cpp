// Component registration shared by every platform build.
#include "../fb2k.h"

#ifndef PUI_VERSION
#define PUI_VERSION "0.0.0-dev"
#endif

DECLARE_COMPONENT_VERSION(
    "Panels UI (reborn)",
    PUI_VERSION,
    "Reimplementation of the discontinued Panels UI (foo_ui_panels, by Terrestrial) for foobar2000 v2,\n"
    "revived to run the fooAvA skin by dawxxx666 again.\n"
    "https://github.com/santiagorod92/foo_ui_panels\n");

namespace pui {
// Built-in skin used when the skin folder has no main script: background fill + title text + a
// hosted track display and playlist. Proves the draw engine and panel hosting together.
const char* builtin_test_skin() {
    return
        "$drawrect(0,0,%_width%,%_height%,brushcolor-30-30-60 pencolor-30-30-60)"
        "$gradientrect(0,0,%_width%,40,60-60-110,30-30-60)"
        "$font(Segoe UI,22,b)$drawstring(Panels UI \xe2\x80\x94 reborn,16,6,600,30,255-220-80,vcenter)"
        "$imageabs(16,48,160,120,bg.jpg,)"
        "$panel(np,Track Display,190,48,$eval({%_width%}-202),70,)"
        "$panel(pl,Playlist View,190,124,$eval({%_width%}-202),$eval({%_height%}-136),)";
}
} // namespace pui
