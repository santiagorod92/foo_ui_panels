#include "skin_templates.h"
#include "fs_util.h"
#include "skin_config.h"
#include <filesystem>

namespace pui {

namespace {

using Part = SkinTemplate::Part;

std::string header(const char* name) {
    return std::string("// ") + name + R"PUI( — a Panels UI skin made by the layout wizard (View › Panels UI › Layout wizard…).
// It is yours to change. This file lays out the window and is run again on every repaint; saved
// edits show up right away. %_width% and %_height% are the window's size; panel(name,type,x,y,w,h)
// places a built-in panel (playlist, album art, seek bar…), textbutton(x,y,w,h,label,,action) a
// button. Colours of the built-in panels are in foo_ui_panels.ini next to this file.

$drawrect(0,0,%_width%,%_height%,brushcolor-24-25-30 pencolor-null)
)PUI";
}

const char* kToolbar = R"PUI(
// ---- Toolbar: transport buttons, seek bar, volume, main menu -----------------------------------
$drawrect(0,0,%_width%,40,brushcolor-36-38-46 pencolor-null)
$font(Segoe UI Symbol,13,,225-228-235)
$textbutton(6,4,32,32,⏮,,Previous,TOOLTIP,Previous)
$textbutton(38,4,32,32,$if(%isplaying%,$if(%ispaused%,▶,⏸),▶),,Play,TOOLTIP,Play / pause)
$textbutton(70,4,32,32,⏹,,Stop,TOOLTIP,Stop)
$textbutton(102,4,32,32,⏭,,Next,TOOLTIP,Next)
// The seek and volume bars are drawn here; the Seekbar/Volume panels over them take the clicks.
$drawrect(148,18,$eval(%_width% - 300),4,brushcolor-52-55-64 pencolor-null)
$if(%isplaying%,$ifgreater(%length_seconds%,0,$drawrect(148,18,$eval({%_width% - 300} * %playback_time_seconds% / %length_seconds%),4,brushcolor-0-120-215 pencolor-null),))
$drawrect($eval(%_width% - 136),18,90,4,brushcolor-52-55-64 pencolor-null)
$drawrect($eval(%_width% - 136),18,$eval(90 * %panel_volume% / 1000),4,brushcolor-160-165-175 pencolor-null)
$panel(Seekbar,Seekbar,148,12,$eval(%_width% - 300),16)
$panel(Volume,Volume,$eval(%_width% - 136),12,90,16)
$textbutton($eval(%_width% - 38),4,32,32,☰,,MENU,TOOLTIP,Menu)
)PUI";

const char* kStatus = R"PUI(
// ---- Status bar --------------------------------------------------------------------------------
$drawrect(0,$eval(%_height% - 22),%_width%,22,brushcolor-36-38-46 pencolor-null)
$font(Segoe UI,9)
$drawstring($if(%isplaying%,$if(%ispaused%,Paused,Playing)   %codec%[   %bitrate% kbps]   %playback_time%[ / %length%],Stopped),10,$eval(%_height% - 19),$eval(%_width% - 20),18,150-155-165)
)PUI";

std::string now_playing(const std::string& x, const std::string& y, const std::string& w) {
    auto at = [&](int dy) { return "$eval(" + y + " + " + std::to_string(dy) + ")"; };
    return "\n// ---- What's playing ----------------------------------------------------------------------------\n"
           "$font(Segoe UI,12,bold)\n"
           "$drawstring($if(%isplaying%,%title%,Nothing playing)," + x + "," + y + "," + w + ",22,235-238-245)\n"
           "$font(Segoe UI,10)\n"
           "$drawstring($if(%isplaying%,%artist%)," + x + "," + at(22) + "," + w + ",18,160-165-175)\n"
           "$drawstring($if(%isplaying%,%album%[ '('%date%')'])," + x + "," + at(40) + "," + w + ",18,120-125-135)\n";
}

std::string separator(const std::string& x) {
    return "$drawrect(" + x + ",40,1,$eval(%_height% - 62),brushcolor-45-48-56 pencolor-null)\n";
}

std::string ini(const char* name) {
    return std::string("# ") + name + R"PUI( — made by the layout wizard. Settings for the built-in panels; the keys are
# listed in the Panels UI README ("Skin configuration"). Colours: r g b.
script = main.txt
color.background = 24 25 30
color.text = 225 228 235
color.text_secondary = 160 165 175
color.text_dim = 120 125 135
color.separator = 45 48 56
color.hover = 40 43 52
color.seekbar.background = 52 55 64
color.volume.background = 52 55 64
)PUI";
}

std::vector<SkinTemplate> build() {
    std::vector<SkinTemplate> t;

    t.push_back({ "playlist-cover", "Playlist and cover", "Album art and track info beside the playlist.",
        { { 0, 0, 100, 9, Part::Toolbar }, { 2, 12, 26, 34, Part::Cover }, { 30, 9, 70, 86, Part::Playlist },
          { 0, 95, 100, 5, Part::Status } },
        std::string(header("Playlist and cover")) + kToolbar +
            "\n// ---- Left: album art; right: the playlist ---------------------------------------------------\n"
            "$panel(Cover,Album Art,12,52,256,256)\n" +
            now_playing("12", "318", "256") +
            separator("280") +
            "$panel(Playlist,Single Column Playlist,281,40,$eval(%_width% - 281),$eval(%_height% - 62))\n" + kStatus,
        ini("Playlist and cover") });

    t.push_back({ "library", "Library", "Playlists on the left, the playlist, album art on the right.",
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 22, 86, Part::Tree }, { 22, 9, 52, 86, Part::Playlist },
          { 76, 12, 22, 30, Part::Cover }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Library")) + kToolbar +
            "\n// ---- Left: playlists; middle: the playlist; right: album art ----------------------------------\n"
            "$panel(Playlists,Playlist switcher,0,40,220,$eval(%_height% - 62))\n" +
            separator("220") +
            "$panel(Playlist,Single Column Playlist,221,40,$eval(%_width% - 482),$eval(%_height% - 62))\n" +
            separator("$eval(%_width% - 261)") +
            "$panel(Cover,Album Art,$eval(%_width% - 249),52,238,238)\n" +
            now_playing("$eval(%_width% - 249)", "300", "238") + kStatus,
        ini("Library") });

    t.push_back({ "albums", "Album browser", "Your albums as a cover grid, the playlist beside it.",
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 60, 86, Part::Albums }, { 60, 9, 40, 86, Part::Playlist },
          { 0, 95, 100, 5, Part::Status } },
        std::string(header("Album browser")) + kToolbar +
            "\n// ---- Left: the album grid (right-click it for cover flow and sorting); right: the playlist ----\n"
            "$panel(Albums,Album list,0,40,$eval(%_width% * 3 / 5),$eval(%_height% - 62))\n" +
            separator("$eval(%_width% * 3 / 5)") +
            "$panel(Playlist,Single Column Playlist,$eval(%_width% * 3 / 5 + 1),40,$eval(%_width% - %_width% * 3 / 5 - 1),$eval(%_height% - 62))\n" +
            kStatus,
        ini("Album browser") });

    t.push_back({ "playlist", "Playlist", "Just the playlist, with the controls on top.",
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 100, 86, Part::Playlist }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Playlist")) + kToolbar +
            "\n// ---- The playlist ------------------------------------------------------------------------------\n"
            "$panel(Playlist,Single Column Playlist,0,40,%_width%,$eval(%_height% - 62))\n" + kStatus,
        ini("Playlist") });

    const std::string S = "$min($eval(%_height% - 230),$eval(%_width% / 2 - 24))";
    t.push_back({ "now-playing", "Now playing", "A large cover, synced lyrics and a spectrum.",
        { { 0, 0, 100, 9, Part::Toolbar }, { 2, 12, 40, 56, Part::Cover }, { 45, 10, 53, 64, Part::Lyrics },
          { 0, 78, 100, 17, Part::Spectrum }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Now playing")) + kToolbar +
            "\n// ---- Album art as large as the window allows, lyrics beside it, a spectrum below -------------\n"
            "$panel(Cover,Album Art,12,52," + S + "," + S + ")\n" +
            now_playing("12", "$eval(" + S + " + 60)", S) +
            "$panel(Lyrics,Lyric Show,$eval(" + S + " + 24),40,$eval(%_width% - " + S + " - 24),$eval(%_height% - 132))\n"
            "$panel(Spectrum,Spectrum,0,$eval(%_height% - 88),%_width%,64)\n" + kStatus,
        ini("Now playing") });

    return t;
}

const char* part_color(Part p) {
    switch (p) {
    case Part::Toolbar:  return "58-62-72";
    case Part::Status:   return "48-51-60";
    case Part::Playlist: return "38-41-50";
    case Part::Cover:    return "0-120-215";
    case Part::Tree:     return "46-50-62";
    case Part::Albums:   return "0-95-170";
    case Part::Lyrics:   return "70-62-110";
    case Part::Spectrum: return "0-150-136";
    }
    return "60-60-60";
}

}

const std::vector<SkinTemplate>& skin_templates() {
    static const std::vector<SkinTemplate> t = build();
    return t;
}

const SkinTemplate* find_skin_template(const std::string& id) {
    for (auto& t : skin_templates()) if (id == t.id) return &t;
    return nullptr;
}

std::string layout_wizard_script() {
    const std::string CW = "{{%_width% - 88} / 3}", PH = "{" + CW + " / 2}", CH = "{" + PH + " + 72}";
    std::string s = R"PUI(
$drawrect(0,0,%_width%,%_height%,brushcolor-22-24-30 pencolor-null)
$gradientrect(0,0,%_width%,4,0-120-215,0-90-170)
$font(Segoe UI,20,bold,240-240-240)
$alignabs(28,22,$eval(%_width% - 56),34,left,top)Choose a layout
$font(Segoe UI,10)
$drawstring('Pick a starting point. It becomes a skin folder of its own, so anything in it can be changed later. Or use a skin you already have.',28,60,$eval(%_width% - 56),36,175-180-190,wrap)
$drawroundrect(28,100,210,32,6,6,0-120-215)
$drawroundrect(250,100,110,32,6,6,58-62-72)
$font(Segoe UI,10,bold,255-255-255)
$textbutton(28,100,210,32,Use my own skin folder...,,SKIN:CHOOSE_FOLDER,TOOLTIP,Load a skin from a folder)
$textbutton(250,100,110,32,Close,,WIZARD:CLOSE)
)PUI";
    const auto& ts = skin_templates();
    for (size_t i = 0; i < ts.size(); ++i) {
        const SkinTemplate& t = ts[i];
        const std::string col = std::to_string(i % 3), row = std::to_string(i / 3);
        const std::string X = "{28 + " + col + " * {" + CW + " + 16}}", Y = "{150 + " + row + " * {" + CH + " + 16}}";
        auto e = [](const std::string& expr) { return "$eval(" + expr + ")"; };
        s += "\n// " + std::string(t.name) + "\n";
        s += "$button2(" + e(X) + "," + e(Y) + ",0,0," + e(CW) + "," + e(CH) +
             ",'$drawroundrect(0,0," + e(CW) + "," + e(CH) + ",8,8,34-37-45)'" +
             ",'$drawroundrect(0,0," + e(CW) + "," + e(CH) + ",8,8,46-50-62)'" +
             ",SKIN:TEMPLATE:" + t.id + ",'TOOLTIP:" + t.summary + "')\n";
        const std::string TX = "{" + X + " + 12}", TY = "{" + Y + " + 12}", TW = "{" + CW + " - 24}", TH = "{" + PH + " - 8}";
        s += "$drawrect(" + e(TX) + "," + e(TY) + "," + e(TW) + "," + e(TH) + ",brushcolor-24-25-30 pencolor-70-74-86)\n";
        for (const auto& b : t.thumbnail) {
            s += "$drawrect(" + e(TX + " + " + std::to_string(b.x) + " * " + TW + " / 100") + "," +
                 e(TY + " + " + std::to_string(b.y) + " * " + TH + " / 100") + "," +
                 e(std::to_string(b.w) + " * " + TW + " / 100") + "," + e(std::to_string(b.h) + " * " + TH + " / 100") +
                 ",brushcolor-" + part_color(b.part) + " pencolor-null)\n";
        }
        s += "$imageabs(" + e(TX) + "," + e(TY) + "," + e(TW) + "," + e(TH) + ",builtin:wizard/" + t.id + ".png)\n";
        s += "$drawrect(" + e(TX) + "," + e(TY) + "," + e(TW) + "," + e(TH) + ",brushcolor-null pencolor-70-74-86)\n";
        s += "$font(Segoe UI,11,bold)\n";
        s += "$drawstring(" + std::string(t.name) + "," + e(TX) + "," + e(Y + " + " + PH + " + 12") + "," + e(TW) + ",20,235-238-245)\n";
        s += "$font(Segoe UI,9)\n";
        s += "$drawstring('" + std::string(t.summary) + "'," + e(TX) + "," + e(Y + " + " + PH + " + 34") + "," + e(TW) +
             ",34,150-155-165,wrap)\n";
    }
    return s;
}

std::string install_skin_template(const SkinTemplate& t, const std::string& parent, std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(fs_path(parent), ec);
    for (int n = 1; n < 1000; ++n) {
        const std::string dir = parent + kPathSep + t.name + (n > 1 ? " " + std::to_string(n) : std::string());
        if (std::filesystem::exists(fs_path(dir), ec)) continue;
        if (!std::filesystem::create_directory(fs_path(dir), ec) ||
            !write_file(dir + kPathSep + "main.txt", t.script) || !write_file(dir + kPathSep + SkinConfig::kFileName, t.ini)) {
            if (err) *err = "Couldn't write the layout to " + dir;
            return {};
        }
        return dir;
    }
    if (err) *err = "No free folder name under " + parent;
    return {};
}

}
