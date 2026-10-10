#include "builtin_skin.h"

namespace pui {
const char* builtin_test_skin() {
    return
        "$drawrect(0,0,%_width%,%_height%,brushcolor-22-24-30 pencolor-null)"
        "$gradientrect(0,0,%_width%,4,0-120-215,0-90-170)"
        "$font(Segoe UI,20,bold,240-240-240)"
        "$alignabs(28,22,$sub(%_width%,56),34,left,top)Welcome to Panels UI"
        "$font(Segoe UI,10)"
        "$drawstring('No skin to run yet. Start from a ready-made layout, or choose the folder that holds a skin (its "
        "main script is the .txt file at the top level) or a folder with one skin per subfolder.',28,62,$sub(%_width%,56),52,175-180-190,wrap)"
        "$font(Segoe UI,9,,120-125-135)"
        "$alignabs(28,122,$sub(%_width%,56),18,left,top)Looked in: %pui_skin_folder%"
        "$drawroundrect(28,152,180,32,6,6,0-120-215)"
        "$drawroundrect(220,152,200,32,6,6,58-62-72)"
        "$drawroundrect(432,152,150,32,6,6,58-62-72)"
        "$font(Segoe UI,10,bold,255-255-255)"
        "$textbutton(28,152,180,32,Choose a layout...,,WIZARD:OPEN,TOOLTIP,Start from a ready-made layout)"
        "$textbutton(220,152,200,32,Choose a skin folder...,,SKIN:CHOOSE_FOLDER,TOOLTIP,Load a skin from a folder)"
        "$textbutton(432,152,150,32,Preferences...,,PREFERENCES)"
        "$panel(welcome.playlist,Single Column Playlist,28,204,$sub(%_width%,56),$sub(%_height%,232),)";
}
}
