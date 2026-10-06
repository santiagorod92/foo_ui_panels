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
// Built-in welcome screen, shown while the skin folder has no main script (a fresh install: the
// component ships no skin). Says where it looked and offers the ways forward — pick a skin
// folder, the Preferences page, the setup guide — with a playlist below so the player is usable
// meanwhile. Plain Panels UI script: the engine's own functions and actions only.
const char* builtin_test_skin() {
    return
        "$drawrect(0,0,%_width%,%_height%,brushcolor-22-24-30 pencolor-null)"
        "$gradientrect(0,0,%_width%,4,0-120-215,0-90-170)"
        "$font(Segoe UI,20,bold,240-240-240)"
        "$alignabs(28,22,$sub(%_width%,56),34,left,top)Welcome to Panels UI"
        "$font(Segoe UI,10)"
        "$drawstring('No skin is set up yet. Panels UI runs skins made for the original Panels UI, such as fooAvA. "
        "Choose the folder that holds a skin, or a folder with one skin per subfolder.',28,62,$sub(%_width%,56),52,175-180-190,wrap)"
        "$font(Segoe UI,9,,120-125-135)"
        "$alignabs(28,122,$sub(%_width%,56),18,left,top)Looked in: %pui_skin_folder%"
        "$drawroundrect(28,152,200,32,6,6,0-120-215)"
        "$drawroundrect(240,152,150,32,6,6,58-62-72)"
        "$drawroundrect(402,152,190,32,6,6,58-62-72)"
        "$font(Segoe UI,10,bold,255-255-255)"
        "$textbutton(28,152,200,32,Choose a skin folder...,,SKIN:CHOOSE_FOLDER,TOOLTIP,Load a skin from a folder)"
        "$textbutton(240,152,150,32,Preferences...,,PREFERENCES)"
        "$textbutton(402,152,190,32,Setting up fooAvA...,,'URL:https://github.com/santiagorod92/foo_ui_panels#setting-up-fooava')"
        "$panel(welcome.playlist,Single Column Playlist,28,204,$sub(%_width%,56),$sub(%_height%,232),)";
}
} // namespace pui
