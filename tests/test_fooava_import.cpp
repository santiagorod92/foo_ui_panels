#include "test.h"
#include "core/fooava_import.h"
#include "../third_party/bzip2/bzlib.h"
#include "../third_party/miniz/miniz.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace pui;

namespace {

std::string u16(uint16_t n) { return std::string{ (char)(n & 0xff), (char)(n >> 8) }; }
std::string u32(uint32_t n) { return std::string{ (char)(n & 0xff), (char)((n >> 8) & 0xff), (char)((n >> 16) & 0xff), (char)(n >> 24) }; }
std::string str(const std::string& s) { return u32((uint32_t)s.size()) + s; }

std::string deflated(const std::string& s) {
    size_t n = 0;
    void* p = tdefl_compress_mem_to_heap(s.data(), s.size(), &n, TDEFL_WRITE_ZLIB_HEADER | 128);
    std::string out(static_cast<const char*>(p), n);
    mz_free(p);
    return out;
}

std::string bzipped(const std::string& s) {
    std::string out(s.size() + s.size() / 100 + 600, '\0');
    unsigned n = (unsigned)out.size();
    BZ2_bzBuffToBuffCompress(out.data(), &n, const_cast<char*>(s.data()), (unsigned)s.size(), 9, 0, 0);
    out.resize(n);
    return out;
}

struct Packed { std::string path, payload; size_t size; };

std::string installer(const std::vector<Packed>& files) {
    std::string data, man = u16((uint16_t)files.size()) + u16(0);
    for (const auto& f : files) {
        const std::string node = u32((uint32_t)data.size()) + u32((uint32_t)f.payload.size()) + u32(0) + u32((uint32_t)f.size) +
                                 std::string(46, '\0') + f.path + '\0';
        man += u16((uint16_t)(6 + node.size())) + u16(0) + u16(0) + node;
        data += u32(0) + f.payload;
    }
    const std::string list = u32((uint32_t)man.size()) + std::string(1, '\0') + man;
    return std::string("MZ junk", 7) + "\x77\x77\x67\x54\x29\x48" + u16(0x143A) + u16(0) + str(list) + u16(0x7F7F) + u16(0) + str(data);
}

std::string config(const std::vector<std::pair<std::string, std::string>>& pvars, const std::string& script, const std::string& tail) {
    std::string out(56, '\0');
    for (const auto& [k, v] : pvars) out += str(k) + str(v);
    return out + u32(0) + std::string(1, '\x01') + script + std::string(1, '\x01') + tail;
}

std::string record(const std::string& name, const std::string& text) { return str(name) + std::string(1, '\0') + text + std::string(1, '\x01'); }

const std::string kMain =
    "// fooAvA\n"
    "  $panel(Display,Track Display,0,0,10,10,)  \n"
    "$imageabs2(,,,,,,0,0,/images\\FOOAVA\\A.PNG,) // the cover\n"
    "$drawrect(0,0,1,1,G:\\Dawid\\Muza\\Anberlin\\Cities\\x)))\n";
const std::string kDisplay = "$drawstring(PLAY A SONG,0,0,%_width%,20,)\n$panel(Album,Album Art,1,2,3,4,)\n$puts(a,1234567890)";
const std::string kPopup = "$drawrect(0,0,%_width%,%_height%,brushcolor-0-0-0 pencolor-0-0-0) // popup\n";

const std::string* find(const FooavaSkin& s, const std::string& path) {
    for (const auto& [p, d] : s.files) if (p == path) return &d;
    return nullptr;
}

FooavaFiles sample_files() {
    const std::string pui = config({ { "colour", "1-2-3" }, { "MyCoverPath", "G:\\covers" } }, kMain,
                                   "///END" + std::string(1, '\0') + record("Display", kDisplay) + record("Unused", kDisplay));
    const std::string ava = config({ { "colour", "1-2-3" } }, kPopup, "");
    const std::string png = "\x89PNG fake";
    return { { "images/fooAVA/a.png", png }, { "pop.ava", ava }, { "skin.pui", pui } };
}

}

TEST("fooava: installer entries come out stored, deflated or bzip2ed, DLLs skipped") {
    const auto files = sample_files();
    const std::string exe = installer({ { "PanelsUI\\AvA 1.05\\images\\fooAVA\\a.png", std::string(1, '\0') + files[0].second, files[0].second.size() },
                                        { "PanelsUI\\AvA 1.05\\pop.ava", std::string(1, '\x01') + deflated(files[1].second), files[1].second.size() },
                                        { "PanelsUI\\AvA 1.05\\skin.pui", std::string(1, '\x01') + bzipped(files[2].second), files[2].second.size() },
                                        { "foo_ui_panels.dll", std::string(1, '\0') + "MZ", 2 } });
    FooavaFiles got;
    std::string err;
    CHECK(unpack_fooava_installer(exe, got, &err));
    CHECK_EQ(err, std::string());
    CHECK_EQ(got.size(), (size_t)3);
    for (size_t i = 0; i < std::min(got.size(), files.size()); ++i) {
        CHECK_EQ(got[i].first, files[i].first);
        CHECK(got[i].second == files[i].second);
    }
    FooavaFiles none;
    CHECK(!unpack_fooava_installer("not an installer", none, &err));
    CHECK(!err.empty());
}

TEST("fooava: the configuration becomes main.txt, panel scripts and popups") {
    std::string err;
    const FooavaSkin skin = convert_fooava(sample_files(), &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(skin.images, 1);
    CHECK_EQ(skin.panels, 1);
    CHECK_EQ(skin.popups, 1);
    CHECK_EQ(skin.main, std::string("$if($strcmp($getpvar(colour),),$setpvar(colour,1-2-3),)$if($strcmp($getpvar(MyCoverPath),),$setpvar(MyCoverPath,),)"
                                    "$panel(Display,Track Display,0,0,10,10,)$imageabs2(,,,,,,0,0,/images\\fooAVA\\a.png,)$drawrect(0,0,1,1,x)"));
    CHECK(skin.ini.find("\nscript = main.txt\n") != std::string::npos);
    CHECK(find(skin, "images/fooAVA/a.png") != nullptr);
    CHECK(find(skin, "panels/Unused.txt") == nullptr);
    const std::string* display = find(skin, "panels/Display.txt");
    CHECK(display != nullptr);
    if (display) {
        CHECK_EQ(display->rfind("$puts(lyrcd,", 0), (size_t)0);
        CHECK(display->find("$ifequal($get(lyrcd),1,$panel(LyricsCD,Lyric Show,1,2,3,4,),)") != std::string::npos);
        CHECK(display->find("$ifequal($get(lyrcd),1,,$panel(Album,Album Art,1,2,3,4,))") != std::string::npos);
    }
    const std::string* popup = find(skin, "panels/pop.ava.txt");
    CHECK(popup != nullptr);
    if (popup) CHECK_EQ(*popup, std::string("$if($strcmp($getpvar(colour),),$setpvar(colour,1-2-3),)$drawrect(0,0,%_width%,%_height%,brushcolor-0-0-0 pencolor-0-0-0)"));
}

TEST("fooava: no images folder or main configuration is an error") {
    std::string err;
    auto files = sample_files();
    files.erase(files.begin());
    convert_fooava(files, &err);
    CHECK(!err.empty());
}

TEST("fooava: the real 1.05 installer (FOOAVA_INSTALLER)") {
    const char* path = std::getenv("FOOAVA_INSTALLER");
    if (!path) return;
    std::ifstream in(path, std::ios::binary);
    FooavaFiles files;
    std::string err;
    CHECK(unpack_fooava_installer(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), files, &err));
    const FooavaSkin skin = convert_fooava(files, &err);
    CHECK_EQ(err, std::string());
    CHECK_EQ(skin.images, 387);
    CHECK_EQ(skin.panels, 11);
    CHECK_EQ(skin.popups, 3);
    CHECK_EQ(skin.main.size(), (size_t)39887);
    CHECK(find(skin, "panels/ChronTIM.txt") != nullptr);
    CHECK(find(skin, "panels/FOOAvA_settings.ava.txt") != nullptr);
    CHECK(find(skin, "images/fooAVA/walls/8.jpg") != nullptr);
}
