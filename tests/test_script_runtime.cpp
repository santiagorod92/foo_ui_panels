#include "test.h"
#include "recording_canvas.h"
#include "core/script_util.h"
#include <fstream>
#include <sstream>

using namespace pui;
using t::Rig;

static std::string ops_str(const std::vector<std::string>& ops) {
    std::string s;
    for (auto& o : ops) { s += o; s += '\n'; }
    return s;
}
static bool has_op(Rig& r, const std::string& prefix) {
    for (auto& o : r.ops()) if (o.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

TEST("runtime: table lookup, unknown names and too few arguments") {
    CHECK(find_script_function("drawrect") != nullptr);
    CHECK(find_script_function("if") == nullptr); // the core's own
    CHECK(find_script_function("scplsetlayout")->status == ScriptFunction::Status::Stub);
    Rig r;
    bool found = true;
    r.call("no_such", {}, &found);
    CHECK(!found);
    r.call("drawrect", { "1", "2", "3" }, &found); // needs 5
    CHECK(!found);
    CHECK(r.ops().empty());
    r.call("scplsetlayout", { "x" }, &found);
    CHECK(found);
}

TEST("runtime: every table entry is unique and documented") {
    std::set<std::string> names;
    for (auto& f : script_functions()) {
        CHECK(names.insert(f.name).second);
        CHECK(std::string(f.signature).compare(0, std::string(f.name).size() + 1, "$" + std::string(f.name)) == 0);
        CHECK(*f.summary != 0);
    }
}

TEST("runtime: docs/SCRIPT_FUNCTIONS.md is generated from the table (make docs)") {
    std::ifstream in("docs/SCRIPT_FUNCTIONS.md", std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    CHECK(ss.str() == script_functions_markdown());
}

TEST("runtime: script_calls skips quoted text and repeats") {
    auto calls = script_calls("$if(a,$drawrect(1),'$notme(')$font(x)$drawrect(2)$ no$(");
    CHECK_EQ(calls.size(), (size_t)3);
    CHECK_EQ(calls[0], std::string("if"));
    CHECK_EQ(calls[1], std::string("drawrect"));
    CHECK_EQ(calls[2], std::string("font"));
}

TEST("runtime: $panel records placements, or into the capture list") {
    Rig r;
    r.call("panel", { "pl", "Single Column Playlist", "1", "2", "30", "40" });
    CHECK_EQ(r.st.placements.size(), (size_t)1);
    CHECK_EQ(r.st.placements[0].type, std::string("Single Column Playlist"));
    CHECK_EQ(r.st.placements[0].h, 40);
    std::vector<Placement> cap;
    r.st.capturePlacements = &cap;
    r.call("panel", { "x", "y", "0", "0", "1", "1" });
    CHECK_EQ(cap.size(), (size_t)1);
    CHECK_EQ(r.st.placements.size(), (size_t)1);
}

TEST("runtime: variables — $puts/$get, pvars, $eval with nested $get") {
    Rig r;
    r.call("setpvar", { "Colour", "3" });
    CHECK_EQ(r.call("getpvar", { "colour" }), std::string("3")); // names ignore case
    r.call("puts", { "w", "120" });
    CHECK_EQ(r.call("get", { "w" }), std::string("<120>"));       // replayed through the env
    CHECK_EQ(r.call("get", { "COLOUR" }), std::string("3"));      // falls back to the pvar
    CHECK_EQ(r.call("get", { "nothing" }), std::string(""));
    CHECK_EQ(r.call("eval", { "$get(w)/2+$getpvar(colour)" }), std::string("63"));
    r.call("puts", { "cmd", "$font(a,1)" }); // a draw command is kept verbatim
    CHECK_EQ(r.st.tfvars["cmd"], std::string("$font(a,1)"));
    CHECK_EQ(r.call("greater", { "3", "2" }), std::string("1"));
    CHECK_EQ(r.call("greater", { "2", "2" }), std::string(""));
}

TEST("runtime: colour helpers") {
    Rig r;
    CHECK_EQ(r.call("calculate_blend_target", { "ffffff" }), hex_colorref(0));
    CHECK_EQ(r.call("calculate_blend_target", { "000000" }), hex_colorref(0xffffff));
    CHECK_EQ(r.call("offset_colour", { "000000", "ffffff", "0" }), hex_colorref(0));
    CHECK_EQ(r.call("offset_colour", { "000000", "0000ff", "255" }), hex_colorref(0x0000ff));
}

TEST("runtime: $drawrect brush, pen, alpha and null") {
    Rig r;
    r.call("drawrect", { "1", "2", "3", "4", "brushcolor-255-0-0 pencolor-0-0-255" });
    r.call("drawrect", { "1", "2", "3", "4", "brushcolor-0-255-0 pencolor-null alpha-100" });
    CHECK_EQ(ops_str(r.ops()), std::string(
        "fill_rect 1,2,3,4 #ff0000\n"
        "frame_rect 1,2,3,4 #0000ff\n"
        "fill_rect_alpha 1,2,3,4 #00ff00 a100\n"));
}

TEST("runtime: shapes and GDI+ brush/pen") {
    Rig r;
    r.call("drawroundrect", { "0", "0", "10", "10", "3", "4", "1-2-3" });
    r.call("gradientrect", { "0", "0", "5", "6", "0-0-0", "255-255-255" });
    r.call("gp_set_brush", { "128-10-20-30" });
    r.call("gp_fill_rectangle", { "1", "1", "2", "2" });
    r.call("gp_set_pen", { "0-0-0-0", "2" });          // invisible pen: draws nothing
    r.call("gp_draw_rectangle", { "0", "0", "10", "10" });
    r.call("gp_set_pen", { "255-9-9-9", "2" });
    r.call("gp_draw_rectangle", { "10", "10", "10", "10" });
    CHECK_EQ(ops_str(r.ops()), std::string(
        "round_rect 0,0,10,10 3,4 #010203\n"
        "gradient 0,0,5,6 #000000>#ffffff\n"
        "fill_rect_alpha 1,1,2,2 #0a141e a128\n"
        "fill_rect 9,9,12,2 #090909\n"   // top band
        "fill_rect 9,19,12,2 #090909\n"  // bottom
        "fill_rect 9,11,2,8 #090909\n"   // left
        "fill_rect 19,11,2,8 #090909\n"));
}

TEST("runtime: $font selects face/size/style, colour arg, empty colour keeps the current") {
    Rig r;
    r.call("font", { "Arial", "12", "bold underline", "10-20-30" });
    r.call("drawstring", { "hi", "0", "0", "50", "10" }); // own colour: black default
    r.call("alignabs", { "0", "0", "100", "50" });
    r.buf += "A";
    r.call("font", { "Arial", "9", "", "" }); // flushes "A" in 10-20-30, keeps the colour
    r.buf += "B";
    r.rt.finish();
    const std::string log = ops_str(r.ops());
    CHECK(log.find("font Arial 12 bold underline\n") != std::string::npos);
    CHECK(log.find("text \"hi\" 0,0,50,10") != std::string::npos);
    CHECK(log.find("text \"A\" 0,0,100,50 f64 #0a141e") != std::string::npos);
    CHECK(log.find("text \"B\" 0,0,100,50 f64 #0a141e") != std::string::npos);
}

TEST("runtime: font fallbacks — face pvar, Preferences override, default, alias") {
    Rig r;
    r.st.cfg.set("font.face_pvar", "MyFace");
    r.st.pvars["myface"] = "Verdana";
    r.call("font", { " ", "10" });
    r.st.pvars.erase("myface");
    r.st.pvars["_prefs_font_face"] = "Segoe";
    r.call("font", { "", "0" }); // size 0: Preferences size, else 9
    r.st.pvars.erase("_prefs_font_face");
    r.st.cfg.set("font.alias.Fancy", "Plain");
    r.cv.fontAvailable = false;
    r.call("font", { "fancy", "8" });
    const std::string log = ops_str(r.ops());
    CHECK(log.find("font Verdana 10\n") != std::string::npos);
    CHECK(log.find("font Segoe 9\n") != std::string::npos);
    CHECK(log.find("font fancy 8\nfont Plain 8\n") != std::string::npos);
}

TEST("runtime: glow draws a halo under the text") {
    Rig r;
    r.call("font", { "Arial", "10", "glow-255-0-0 glowexpand-2" });
    r.call("drawstring", { "x", "10", "10", "20", "10" });
    CHECK(has_op(r, "glow "));
    Rig plain;
    plain.call("font", { "Arial", "10", "glow-255-0-0glowexpand-0" }); // glued, expand 0 = none
    plain.call("drawstring", { "x", "10", "10", "20", "10" });
    CHECK(!has_op(plain, "glow "));
}

TEST("runtime: $alignabs boxes — width form, to-the-edge, short caption") {
    Rig r;
    r.call("alignabs", { "10", "31", "0", "14", "center", "middle" });
    r.buf += "Title";
    r.call("alignabs", { "5", "5", "50", "200", "right" });
    r.buf += "Body";
    r.rt.finish();
    const unsigned caption = gfx::kAlignCenter | gfx::kNoClip | gfx::kSingleLine;
    const unsigned body = gfx::kAlignRight | gfx::kWordEllipsis;
    CHECK_EQ(r.ops()[0], "text \"Title\" 10,31,390,14 f" + std::to_string(caption) + " #ffffff");
    CHECK_EQ(r.ops()[1], "text \"Body\" 5,5,45,195 f" + std::to_string(body) + " #ffffff");
}

TEST("runtime: text after a truncation is read from the live buffer") {
    Rig r;
    r.call("alignabs", { "0", "0", "100", "100" });
    r.buf += "1";      // an $if condition's value...
    r.buf.resize(0);   // ...truncated away again by titleformat
    r.buf += "shown\x01";
    r.rt.finish();
    CHECK_EQ(r.ops().size(), (size_t)1);
    CHECK(r.ops()[0].compare(0, 12, "text \"shown\"") == 0); // control chars dropped
}

TEST("runtime: images resolve against the skin folder and report missing ones once") {
    Rig r;
    r.st.base = "/skin";
    r.env.images["/skin/img/a.png"] = { 10, 10 };
    r.call("imageabs", { "1", "2", "3", "4", ".\\img\\a.png" });
    r.call("imageabs", { "1", "2", "3", "4", "img/missing.png" });
    r.call("imageabs", { "1", "2", "3", "4", "img/missing.png" });
    r.call("imageabs", { "1", "2", "3", "4", "C:/covers/*.jpg" }); // outside the skin: not checked
    CHECK_EQ(r.ops()[0], std::string("img /skin/img/a.png 1,2,3,4"));
    CHECK_EQ(r.env.reports.size(), (size_t)1);
    CHECK_EQ(r.call("fileexists", { "img/a.png" }), std::string("1"));
    CHECK_EQ(r.call("cwb_fileexists", { "img/b.png" }), std::string(""));
}

TEST("runtime: $imageabs2 — dummy state passthrough, crop, cap, plain draw") {
    Rig r;
    r.st.base = "/s";
    r.env.images["/s/v.png"] = { 56, 3 };
    // All-zero: handed back as a snippet for the button to replay.
    CHECK_EQ(r.call("imageabs2", { "0", "0", "0", "0", "0", "0", "0", "0", "b.png" }),
             std::string("$imageabs2(0,0,0,0,0,0,0,0,b.png)"));
    // The volume fill: height 12 is a cap, not a target; first 20 px of the 56x3 bar.
    r.call("imageabs2", { "0", "12", "0", "0", "20", "0", "5", "6", "v.png" });
    CHECK_EQ(r.ops().back(), std::string("part /s/v.png 5,6,20,3 <- 0,0,20,3"));
    // No crop: scaled draw with options.
    r.call("imageabs2", { "100", "50", "0", "0", "0", "0", "1", "1", "w.jpg", "alpha-200ROTATEFLIP-6" });
    CHECK_EQ(r.ops().back(), std::string("img /s/w.jpg 1,1,100,50 a200 flip6"));
}

TEST("runtime: $button — image states, hover, hit box from the image, action and tooltip") {
    Rig r(12, 12);
    r.st.base = "/s";
    r.env.images["/s/n.png"] = { 55, 20 };
    r.env.images["/s/h.png"] = { 55, 20 };
    r.call("button", { "0", "0", "", "", "0", "0", "n.png", "h.png", "'Playback/Stop'", "TOOLTIP:\"Stop it\"" });
    CHECK_EQ(r.ops()[0], std::string("img /s/h.png 0,0,0,0")); // hovered (12,12 inside 55x20)
    CHECK_EQ(r.st.buttons.size(), (size_t)1);
    const Button& b = r.st.buttons[0];
    CHECK_EQ(b.w, 55); CHECK_EQ(b.h, 20);
    CHECK_EQ(b.action, std::string("Playback/Stop"));
    CHECK_EQ(b.tooltip, std::string("Stop it"));
}

TEST("runtime: $button2 runs a draw-command state at the button origin") {
    Rig r;
    r.call("button2", { "30", "40", "", "", "10", "10", "'$imageabs2(0,0,0,0,0,0,0,0,x.png)'", "$x()", "PVAR:SET:showPanel:2" });
    CHECK_EQ(r.env.subscripts.size(), (size_t)1);
    CHECK_EQ(r.env.subscripts[0], std::string("$imageabs2(0,0,0,0,0,0,0,0,x.png)"));
    CHECK_EQ(r.st.buttons[0].pvarKey, std::string("showPanel"));
    CHECK_EQ(r.st.buttons[0].pvarValue, std::string("2"));
}

TEST("runtime: text buttons draw the label with the state's own font") {
    Rig r;
    r.call("font", { "Normal", "9" });
    r.call("font", { "Hover", "9" }); // both states' $font ran during argument evaluation
    r.call("button2", { "0", "0", "", "", "40", "20", "INFO", "INFO", "MENU" });
    const std::string log = ops_str(r.ops());
    CHECK(log.find("font Normal 9\nfont Hover 9\nfont Normal 9\ntext \"INFO\" 0,0,40,20") != std::string::npos);
}

TEST("runtime: $textbutton / $imagebutton record clickable regions") {
    Rig r;
    r.call("textbutton", { "1", "2", "0", "0", "EQ", "EQ", "'View/Equalizer'", "TOOLTIP", "Equalizer" });
    r.call("imagebutton", { "5", "5", "s0.png", "s1.png", "TAG:SET:rating:3" });
    CHECK_EQ(r.st.buttons.size(), (size_t)2);
    CHECK_EQ(r.st.buttons[0].w, 240); // 0 = default box
    CHECK_EQ(r.st.buttons[0].tooltip, std::string("Equalizer"));
    CHECK_EQ(r.st.buttons[1].w, 11);
    std::vector<Button> cap;
    r.st.capture = &cap;
    r.call("textbutton", { "1", "2", "3", "4", "x", "x", "A" });
    CHECK_EQ(cap.size(), (size_t)1);
}

TEST("runtime: selected radio-group button is relit, toggles are not") {
    Rig r;
    r.st.pvars["showPanel"] = "2";
    r.call("button", { "0", "0", "", "", "10", "10", "a.png", "a_lit.png", "PVAR:SET:showPanel:1" });
    r.call("button", { "20", "0", "", "", "10", "10", "b.png", "b_lit.png", "PVAR:SET:showPanel:2" });
    r.call("button", { "40", "0", "", "", "10", "10", "c.png", "c_lit.png", "PVAR:SET:onepanel:1" });
    r.ops().clear();
    r.rt.apply_selected_buttons(r.st.buttons, true, gfx::Color(255, 0, 0));
    CHECK(!r.st.buttons[0].selected);
    CHECK(r.st.buttons[1].selected);
    CHECK(!r.st.buttons[2].selected);
    CHECK_EQ(r.ops()[0], std::string("img b_lit.png 20,0,10,10"));
    CHECK_EQ(r.ops().back(), std::string("frame_rect 20,0,10,10 #ff0000"));
}

TEST("runtime: window functions go to the env") {
    Rig r;
    r.call("windowstyle", { "hidetitlebar" });
    CHECK(!r.env.titlebar);
    r.call("settray", {});
    CHECK_EQ(r.env.tray, std::string("foobar2000"));
    r.call("settitle", { "a - b" });
    CHECK_EQ(r.env.title, std::string("a - b"));
}

TEST("pvars: serialize / parse round trip, case-insensitive names") {
    PvarMap m;
    m["b"] = "x=y";
    m["A"] = "1";
    const std::string s = serialize_pvars(m);
    CHECK_EQ(s, std::string("A=1\nb=x=y\n"));
    PvarMap back = parse_pvars(s + "junk\n");
    CHECK_EQ(back.size(), (size_t)2);
    CHECK_EQ(back["a"], std::string("1"));
    CHECK_EQ(back["B"], std::string("x=y"));
}
