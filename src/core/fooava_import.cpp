#include "fooava_import.h"
#include "pui_import.h"
#include "fs_util.h"
#include "../../third_party/bzip2/bzlib.h"
#include "../../third_party/miniz/miniz.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>

extern "C" void bz_internal_error(int) {}

namespace pui {

namespace {

using namespace pui_text;

const std::string kSkinConfig = R"(# foo_ui_panels skin configuration for fooAvA 1.05 (written by Panels UI's fooAvA import).
# Keys: README.md, "Skin configuration".
script = main.txt
images = images/fooAVA

# Theme: accent colour "r-g-b" and theme number (1 black, 2 blue, 3 red, 4 green).
theme.accent_pvar = colour
theme.index_pvar = colour.b
theme.index_default = 2

# Fonts: the face is set through $setpvar(fontAVA,...); stand-ins for faces rarely installed.
font.face_pvar = fontAVA
font.alias.Swis721 Cn BT D-Type = Nimbus Sans Narrow
font.alias.Calibri = Carlito
font.alias.HandelGotD = Nimbus Sans Narrow

# Wallpaper (shared with the native panels): drawn below the 24px title bar.
background.enabled_pvar = backgroundd
background.image_pvar = background
background.alpha_pvar = alpha.bgr
background.alpha = 195
background.top = 24

# Art the native panels draw (relative to images; {theme} = theme number, {n} = stars).
asset.bar_fill = bar/v{theme}.png
asset.volume_knob = bar/vol{theme}.png
asset.nocover = nocoverb.png
asset.cd_case = cdcaseck3.png
asset.cd_case_window = 57 8 494 474
asset.rating_stars = rating_stars24/{n}s1.png

# The analyser strips sit on the cover: the 20px one is the reflection; lift the pair a bit.
spectrum.mirror_below = 30
spectrum.grow = 8
spectrum.raise = 26

# Settings popup: its layout needs 360px (theme swatches left of the font column).
popup.size = 360 500

# Mini player (View > Panels UI > Mini mode): the size fooAvA's own MiniPlayer button uses, the
# top-right corner kept in place, and the same variables its restore button reads.
mini.size = 430 172
mini.anchor = RIGHT TOP
mini.saved_w_pvar = savedW
mini.saved_h_pvar = savedH

# View > Panels UI > Skin commands (assignable to keyboard shortcuts).
command.Toggle title bar = PVAR:TOGGLE:hidetitlebar

# Cover lookup the skin did through a dead plugin.
cover.pvar = MyCoverPath
cover.pattern = *folder*.*

# The cover's mini playlist overlay becomes the Lyric Show panel; the CD-case edge then
# toggles cover <-> lyrics (state 3 folds back to 1).
panel.remap.mini.playlist = mini.lyrics|Lyric Show
action.remap.PVAR:SET:mini.panels:3 = PVAR:SET:mini.panels:1

# First-run flags cleared when a settings popup opens (first.cf=1 would show the page asking
# to install foo_chronflow instead of the native cover flow).
onboarding.pvars = first.* !first.cf

# Cover flow needed foo_chronflow (dead); the native panel provides it: turn it on once.
pvar.once.cfbutton = 1
pvar.once.first.cf = 0
)";

const std::string kSettingsBackground =
    "$ifequal($getpvar(backgroundd),1,$imageabs2(%_width%,%_height%,,,,,0,0,/images\\fooAVA\\$getpvar(background),"
    "NOKEEPASPECT alpha-$getpvar(alpha.bgr)),$drawrect(0,0,%_width%,%_height%,brushcolor-0-0-0 pencolor-0-0-0))";
const std::string kLyricsCdFlag = "$puts(lyrcd,$ifequal($getpvar(showPanel),3,$getpvar(lyricshow),0))";
const std::string kVolumeFillOld = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,42),";
const std::string kVolumeFillNew = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,38),";
const char* const kAuthorPaths[] = { "G:\\Dawid\\Muza\\Anberlin\\Cities\\", "F:\\Program Files\\foobar2000_VISTA\\\\Artists\\",
                                     "F:\\Program Files\\foobar2000_VISTA\\Artists\\" };

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
    return s;
}

std::string cut(const std::string& s, size_t from, size_t to) {
    return (from == std::string::npos ? s.substr(0, s.empty() ? 0 : s.size() - 1) : s.substr(0, from)) + s.substr(to);
}

bool inflate(const char* in, size_t n, bool zlib, size_t size, std::string& out) {
    out.assign(size, '\0');
    tinfl_decompressor d;
    tinfl_init(&d);
    size_t in_n = n, out_n = size;
    auto* buf = reinterpret_cast<mz_uint8*>(out.data());
    const tinfl_status s = tinfl_decompress(&d, reinterpret_cast<const mz_uint8*>(in), &in_n, buf, buf, &out_n,
                                            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (zlib ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0));
    if (s != TINFL_STATUS_DONE && s != TINFL_STATUS_HAS_MORE_OUTPUT) return false;
    out.resize(out_n);
    return true;
}

bool bunzip2(const char* in, size_t n, size_t size, std::string& out) {
    bz_stream s{};
    if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) return false;
    out.assign(size, '\0');
    s.next_in = const_cast<char*>(in);
    s.avail_in = (unsigned)std::min<size_t>(n, 0xFFFFFFFFu);
    s.next_out = out.data();
    s.avail_out = (unsigned)size;
    const int r = BZ2_bzDecompress(&s);
    BZ2_bzDecompressEnd(&s);
    if (r != BZ_STREAM_END && !(r == BZ_OK && s.avail_out == 0)) return false;
    out.resize(size - s.avail_out);
    return true;
}

bool unpack(const std::string& d, size_t p, bool sized, size_t size, std::string& out) {
    if (!sized) {
        if (p + 4 > d.size()) return false;
        size = u32(d, p);
        p += 4;
    }
    if (p >= d.size()) return false;
    const unsigned char method = (unsigned char)d[p++];
    const char* in = d.data() + p;
    const size_t n = d.size() - p;
    if (method == 0) { out = d.substr(p, size); return true; }
    if (n >= 2 && (unsigned char)in[0] == 0x78) return inflate(in + 2, n - 2, false, size, out);
    if (n >= 3 && d.compare(p, 3, "BZh") == 0) return bunzip2(in, n, size, out);
    return inflate(in, n, true, size, out);
}

std::string scrub_author_paths(std::string text) {
    for (const char* p : kAuthorPaths) text = replace_all(text, p, "");
    return text;
}

std::string keep_search_open(const std::string& text) {
    return replace_all(text, "$panel(search,Quick Search Toolbar,60,1,190,20,)$setpvar(showsr,0))",
                       "$panel(search,Quick Search Toolbar,60,1,190,20,))");
}

std::string drop_frame(std::string seg, const std::string& tag) {
    const size_t k = seg.find(tag);
    if (k == std::string::npos) return seg;
    return cut(seg, seg.rfind("$drawrect(", k), k + tag.size());
}

std::string tidy_collection(const std::string& text) {
    const std::string tails[] = { "COLLECTION$if($strcmp(", "COLLECTION$alignabs(0,0,1,1,left,top)$if($strcmp(" };
    std::string out;
    size_t pos = 0, search = 0;
    for (;;) {
        size_t i = std::string::npos;
        for (const auto& t : tails) i = std::min(i, text.find(t, search));
        if (i == std::string::npos) break;
        const size_t start = std::max(pos, i >= 900 ? i - 900 : 0);
        std::string seg = text.substr(start, i - start);
        for (const char* tag : { "brushcolor-0-0-0 pencolor-255-255-255 alpha-102)", "brushcolor-0-0-0 pencolor-null alpha-102)" })
            seg = drop_frame(seg, tag);
        seg = drop_frame(seg, "brushcolor-null pencolor-150-150-150)");
        seg = replace_all(seg, ",240-240-240)$alignabs(", ",$getpvar(colour))$alignabs(");
        out += text.substr(pos, start - pos) + seg;
        pos = i;
        search = i + 10;
    }
    out += text.substr(pos);
    return replace_all(out, tails[0], "COLLECTION$alignabs(0,0,1,1,left,top)$if($strcmp(");
}

std::string tidy_coverflow(const std::string& text) {
    const std::string mark = "COVERFLOW MODE";
    std::string out;
    size_t pos = 0, search = 0;
    for (;;) {
        const size_t i = text.find(mark, search);
        if (i == std::string::npos) break;
        const size_t start = std::max(pos, i >= 600 ? i - 600 : 0);
        std::string seg = replace_all(text.substr(start, i - start), "brushcolor-0-0-0 pencolor-255-255-255 alpha-102)",
                                      "brushcolor-0-0-0 pencolor-null alpha-102)");
        seg = drop_frame(seg, "brushcolor-null pencolor-150-150-150)");
        out += text.substr(pos, start - pos) + seg;
        pos = i;
        search = i + mark.size();
    }
    return out + text.substr(pos);
}

std::string remember_window_size(std::string text) {
    for (const char* lit : { "'371':'527'", "'736':'527'" })
        text = replace_all(text, std::string("WINDOWSIZE:") + lit + ":RIGHT:TOP", "WINDOWSIZE:$getpvar(savedW):$getpvar(savedH):RIGHT:TOP");
    const std::string anchor = "WINDOWSIZE:$getpvar(savedW):$getpvar(savedH):RIGHT:TOP,TOOLTIP:\"\"))";
    std::string out;
    size_t pos = 0;
    for (int group = 0;; ++group) {
        const size_t i = text.find(anchor, pos);
        if (i == std::string::npos) break;
        size_t j = i + anchor.size();
        if (j < text.size() && text[j] == ',') ++j;
        const std::string x = group == 0 ? "33" : "72";
        out += text.substr(pos, j - pos);
        out += "$button($sub(%_width%," + x + "),3,0,0,0,0,,,PVAR:SET:savedW:$eval(%_width%),TOOLTIP:)"
               "$button($sub(%_width%," + x + "),3,0,0,0,0,,,PVAR:SET:savedH:$eval(%_height%),TOOLTIP:)";
        pos = j;
    }
    return out + text.substr(pos);
}

std::string left_lyrics_in_display(std::string text) {
    text = replace_all(text, "$panel(Lyrics,Lyric Show,1,24,$sub(%_width%,5),$sub(%_height%,$ifequal($getpvar(spectrum.off),1,90,137)),)",
                       "$panel(Display,Track Display,1,24,$sub(%_width%,4),$sub(%_height%,90),)");
    return replace_all(text, "$panel(Lyrics,Lyric Show,1,24,$eval({%_width%}/2+3),$sub(%_height%,$ifequal($getpvar(spectrum.off),1,90,137)),)",
                       "$panel(Display,Track Display,1,24,$eval({%_width%}/2+3),$sub(%_height%,90),)");
}

std::string tidy_settings_popup(std::string text) {
    const size_t i = text.find(kSettingsBackground);
    if (i != std::string::npos && i > 0) {
        const size_t j = text.find("$alignabs(", i);
        if (j != std::string::npos && j > i) text = text.substr(i, j - i) + text.substr(0, i) + text.substr(j);
    }
    return replace_all(text, "$alignabs(-45,79,%_width%,%_height%,right,top)", "$alignabs($sub(%_width%,117),79,%_width%,%_height%,left,top)");
}

std::string tidy_display(std::string text) {
    text = replace_all(text, "$alignabs(,$sub(%_width%,45),", "$alignabs(,$sub(%_width%,43),");
    text = replace_all(text, "PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,20),", "PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,13),");
    return replace_all(text, "']']),2)),$sub(%_width%,2),", "']']),2)),$add($sub(%_width%,13),18),");
}

std::string lyrics_in_cd_case(std::string text) {
    const std::string head = "$panel(Album,Album Art,";
    const size_t i = text.find(head);
    if (i == std::string::npos) return text;
    size_t j = i;
    for (int depth = 0; j < text.size(); ++j) {
        if (text[j] == '(') ++depth;
        else if (text[j] == ')' && --depth == 0) break;
    }
    const std::string args = text.substr(i + head.size(), j - i - head.size());
    const std::string lyrics = "$ifequal($get(lyrcd),1,$panel(LyricsCD,Lyric Show," + args + "),)";
    const std::string art = "$ifequal($get(lyrcd),1,,$panel(Album,Album Art," + args + "))";
    size_t k = text.rfind("$ifequal($getpvar(albumart),1,", i);
    if (k == std::string::npos) k = i;
    text = text.substr(0, i) + art + (j + 1 < text.size() ? text.substr(j + 1) : std::string());
    return kLyricsCdFlag + text.substr(0, k) + lyrics + text.substr(k);
}

std::vector<std::pair<size_t, size_t>> printable_runs(const std::string& data) {
    std::vector<std::pair<size_t, size_t>> runs;
    size_t cur = 0, start = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        const unsigned char b = (unsigned char)data[i];
        if (b == 9 || b == 10 || b == 13 || (b >= 32 && b < 127)) {
            if (!cur) start = i;
            ++cur;
        } else {
            if (cur >= 40) runs.push_back({ start, cur });
            cur = 0;
        }
    }
    if (cur >= 40) runs.push_back({ start, cur });
    return runs;
}

std::vector<std::string> runs_of(const std::string& seg) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char b : seg) {
        if (b >= 9 && b < 127) cur += (char)b;
        else {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Converted {
    std::string script;
    FooavaFiles panels;
    bool main = false;
};

Converted convert_config(const std::string& data, const std::map<std::string, std::string>& index) {
    Converted c;
    size_t o = 56;
    std::vector<std::pair<std::string, std::string>> pvars;
    while (o + 4 <= data.size()) {
        const uint32_t nlen = u32(data, o);
        if (nlen < 1 || nlen > 100) break;
        o += 4;
        std::string name = o < data.size() ? data.substr(o, nlen) : std::string();
        o += nlen;
        if (o + 4 > data.size()) break;
        const uint32_t vlen = u32(data, o);
        o += 4;
        if (vlen > data.size() - o) break;
        pvars.push_back({ name, data.substr(o, vlen) });
        o += vlen;
    }
    for (auto& [k, v] : pvars) {
        if (k == "MyCoverPath" || k == "artistFolderPath" || k == "MyArtistPath") v.clear();
        if (k == "backgroundd") v = "1";
    }

    const auto runs = printable_runs(data);
    if (runs.empty()) return c;
    auto master = runs.front();
    for (const auto& r : runs) if (r.second > master.second) master = r;
    std::string globals;
    for (const auto& r : runs) {
        if (r.first > o && r != master && r.second >= 100) {
            const std::string cand = pystrip(data.substr(r.first, r.second));
            if (cand.rfind("$if", 0) == 0 || cand.rfind("$puts", 0) == 0) {
                globals = cand;
                break;
            }
        }
    }

    const std::string body = tidy_settings_popup(left_lyrics_in_display(remember_window_size(replace_all(
        tidy_coverflow(tidy_collection(keep_search_open(scrub_author_paths(preprocess(data.substr(master.first, master.second)))))),
        kVolumeFillOld, kVolumeFillNew))));
    std::string prefix;
    for (const auto& [k, v] : pvars) prefix += "$if($strcmp($getpvar(" + k + "),),$setpvar(" + k + "," + v + "),)";
    c.script = fix_image_case(prefix + (globals.empty() ? std::string() : preprocess(globals)) + body, index);

    const size_t region = data.find("///END");
    if (region == std::string::npos) return c;
    c.main = true;

    struct Rec { size_t off, len; std::string name; };
    std::vector<Rec> recs;
    for (size_t r = region; r + 8 < data.size();) {
        const uint32_t ln = u32(data, r);
        bool ok = ln >= 2 && ln <= 40 && r + 4 < data.size() && data[r + 4] != ' ';
        for (size_t k = r + 4; ok && k < std::min(data.size(), r + 4 + ln); ++k)
            ok = (unsigned char)data[k] >= 32 && (unsigned char)data[k] < 127;
        if (ok) {
            recs.push_back({ r, ln, data.substr(r + 4, ln) });
            r += 4 + ln;
        } else {
            ++r;
        }
    }
    const std::set<std::string> referenced = referenced_panels(data);
    std::vector<std::pair<std::string, std::string>> texts;
    for (size_t idx = 0; idx < recs.size(); ++idx) {
        const Rec& rec = recs[idx];
        if (!referenced.count(rec.name) || !safe_panel_name(rec.name)) continue;
        const size_t from = std::min(data.size(), rec.off + 4 + rec.len);
        const size_t next = idx + 1 < recs.size() ? recs[idx + 1].off : data.size();
        std::string best;
        for (auto& s : runs_of(data.substr(from, next > from ? next - from : 0))) if (s.size() > best.size()) best = s;
        if (best.size() <= 40) {
            size_t nxt = data.size();
            for (size_t k = idx + 1; k < recs.size(); ++k)
                if (referenced.count(recs[k].name)) { nxt = recs[k].off; break; }
            best.clear();
            for (auto& s : runs_of(data.substr(from, nxt > from ? nxt - from : 0)))
                if (s.size() > 40) { best = s; break; }
        }
        if (best.size() <= 40) continue;
        auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) { return t.first == rec.name; });
        if (it != texts.end()) it->second = best;
        else texts.push_back({ rec.name, best });
    }
    std::set<std::string> used;
    std::vector<std::string> pending{ c.script };
    while (!pending.empty()) {
        const std::string text = pending.back();
        pending.pop_back();
        for (const auto& [name, body] : texts) {
            if (!used.count(name) && text.find("$panel(" + name + ",") != std::string::npos) {
                used.insert(name);
                pending.push_back(body);
            }
        }
    }
    for (const auto& name : used) {
        const auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) { return t.first == name; });
        std::string text = fix_image_case(scrub_author_paths(preprocess(it->second)), index);
        if (name == "Display") text = lyrics_in_cd_case(tidy_display(text));
        c.panels.push_back({ "panels/" + name + ".txt", text });
    }
    return c;
}

std::string images_root(const FooavaFiles& files) {
    std::string best;
    size_t best_depth = SIZE_MAX;
    for (const auto& [path, data] : files) {
        size_t start = 0, depth = 0;
        for (size_t slash = path.find('/'); slash != std::string::npos; start = slash + 1, slash = path.find('/', start), ++depth) {
            if (lower(path.substr(start, slash - start)) != "images") continue;
            const size_t next = path.find('/', slash + 1);
            if (lower(path.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1)) != "fooava") continue;
            const std::string root = path.substr(0, slash);
            if (depth < best_depth || (depth == best_depth && root < best)) { best = root; best_depth = depth; }
            break;
        }
    }
    return best;
}

}

bool unpack_fooava_installer(const std::string& d, FooavaFiles& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const size_t sig = d.find("\x77\x77\x67\x54\x29\x48");
    if (sig == std::string::npos) return fail("not a ClickTeam installer (no data signature found)");
    size_t data_start = std::string::npos;
    std::string man;
    bool have_list = false;
    for (size_t p = sig + 6; p + 8 <= d.size();) {
        const uint32_t id = u16(d, p);
        const size_t next = p + 8 + u32(d, p + 4);
        if (id == 0x7F7F) data_start = p + 8;
        else if (id == 0x143A) have_list = unpack(d, p + 8, false, 0, man) || have_list;
        if (next <= p) break;
        p = next;
    }
    if (!have_list || data_start == std::string::npos) return fail("the installer's file list or data is missing");

    const std::string prefix = "PanelsUI\\AvA 1.05\\";
    const size_t count = u16(man, 0);
    for (size_t i = 0, p = 4; i < count; ++i) {
        if (p + 4 > man.size()) break;
        const size_t size = u16(man, p), type = u16(man, p + 2), end = p + size;
        if (end > man.size() || size < 4) break;
        if (type != 0) { p = end; continue; }
        const size_t q = p + 6;
        if (q + 62 > end) break;
        const uint32_t offset = u32(man, q), unpacked = u32(man, q + 12);
        std::string path = man.substr(q + 62, end - q - 62);
        path = path.substr(0, path.find('\0'));
        p = end;
        const std::string ext = extension(path);
        if (ext.empty() || ext == "dll" || ext == "exe") continue;
        for (size_t at = path.find(prefix); at != std::string::npos; at = path.find(prefix, at)) path.erase(at, prefix.size());
        std::replace(path.begin(), path.end(), '\\', '/');
        path.erase(0, path.find_first_not_of('/'));
        std::string data;
        if (path.empty() || !unpack(d, data_start + offset + 4, true, unpacked, data)) continue;
        out.push_back({ latin1_to_utf8(path), std::move(data) });
    }
    if (out.empty()) return fail("no files could be unpacked from the installer");
    return true;
}

FooavaFiles read_fooava_folder(const std::string& dir) {
    namespace fs = std::filesystem;
    FooavaFiles out;
    std::error_code ec;
    const fs::path root = fs_path(dir);
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = fs_utf8(it->path().lexically_relative(root));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const std::string ext = extension(rel);
        if (ext == "dll" || ext == "exe") continue;
        out.push_back({ rel, read_file(fs_utf8(it->path())) });
    }
    std::sort(out.begin(), out.end());
    return out;
}

FooavaSkin convert_fooava(const FooavaFiles& files, std::string* error) {
    FooavaSkin skin;
    const std::string images = images_root(files);
    static const std::set<std::string> kBinary = { "png", "jpg", "jpeg", "bmp", "ico", "dll", "exe", "ttf" };
    const FooavaFiles::value_type* main = nullptr;
    std::vector<const FooavaFiles::value_type*> popups;
    for (const auto& f : files) {
        if (kBinary.count(extension(f.first))) continue;
        const bool ava = ends_with(lower(f.first), ".ava");
        if (f.second.find("$panel(") == std::string::npos && !ava) continue;
        if (f.second.find("///END") != std::string::npos) {
            if (!main || f.second.size() > main->second.size()) main = &f;
        } else if (ava) {
            popups.push_back(&f);
        }
    }
    if (images.empty() || !main) {
        if (error) *error = "no fooAvA images folder or main configuration found";
        return skin;
    }
    std::map<std::string, std::string> index;
    for (const auto& [path, data] : files) {
        if (path.compare(0, images.size() + 1, images + "/") != 0) continue;
        const std::string rel = "images" + path.substr(images.size());
        index[lower(rel)] = rel;
        skin.files.push_back({ rel, data });
        ++skin.images;
    }
    Converted c = convert_config(main->second, index);
    skin.main = c.script;
    skin.ini = kSkinConfig;
    skin.panels = (int)c.panels.size();
    for (auto& p : c.panels) skin.files.push_back(std::move(p));
    std::sort(popups.begin(), popups.end(), [](auto* a, auto* b) { return a->first < b->first; });
    for (const auto* p : popups) {
        const size_t slash = p->first.rfind('/');
        const std::string name = slash == std::string::npos ? p->first : p->first.substr(slash + 1);
        skin.files.push_back({ "panels/" + name + ".txt", convert_config(p->second, index).script });
        ++skin.popups;
    }
    return skin;
}

}
