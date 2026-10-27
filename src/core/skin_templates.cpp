#include "skin_templates.h"
#include "fs_util.h"
#include "i18n.h"
#include "script_util.h"
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

std::string toolbar() {
    return i18n::substitute(R"PUI(
// ---- Toolbar: transport buttons, seek bar, volume, main menu -----------------------------------
$drawrect(0,0,%_width%,40,brushcolor-36-38-46 pencolor-null)
$font(Segoe UI Symbol,13,,225-228-235)
$textbutton(6,4,32,32,⏮,,Previous,TOOLTIP,{0})
$textbutton(38,4,32,32,$if(%isplaying%,$if(%ispaused%,▶,⏸),▶),,Play,TOOLTIP,{1})
$textbutton(70,4,32,32,⏹,,Stop,TOOLTIP,{2})
$textbutton(102,4,32,32,⏭,,Next,TOOLTIP,{3})
// The seek and volume bars are drawn here; the Seekbar/Volume panels over them take the clicks.
$drawrect(148,18,$eval(%_width% - 300),4,brushcolor-52-55-64 pencolor-null)
$if(%isplaying%,$ifgreater(%length_seconds%,0,$drawrect(148,18,$eval({%_width% - 300} * %playback_time_seconds% / %length_seconds%),4,brushcolor-0-120-215 pencolor-null),))
$drawrect($eval(%_width% - 136),18,90,4,brushcolor-52-55-64 pencolor-null)
$drawrect($eval(%_width% - 136),18,$eval(90 * %panel_volume% / 1000),4,brushcolor-160-165-175 pencolor-null)
$panel(Seekbar,Seekbar,148,12,$eval(%_width% - 300),16)
$panel(Volume,Volume,$eval(%_width% - 136),12,90,16)
$textbutton($eval(%_width% - 38),4,32,32,☰,,MENU,TOOLTIP,{4})
)PUI", { tf_quote(tr("Previous")), tf_quote(tr("Play / pause")), tf_quote(tr("Stop")), tf_quote(tr("Next")), tf_quote(tr("Menu")) });
}

std::string status() {
    return i18n::substitute(R"PUI(
// ---- Status bar --------------------------------------------------------------------------------
$drawrect(0,$eval(%_height% - 22),%_width%,22,brushcolor-36-38-46 pencolor-null)
$font(Segoe UI,9)
$drawstring($if(%isplaying%,$if(%ispaused%,{0},{1})   %codec%[   %bitrate% kbps]   %playback_time%[ / %length%],{2}),10,$eval(%_height% - 19),$eval(%_width% - 20),18,150-155-165)
)PUI", { tf_quote(tr("Paused")), tf_quote(tr("Playing")), tf_quote(tr("Stopped")) });
}

std::string now_playing(const std::string& x, const std::string& y, const std::string& w) {
    auto at = [&](int dy) { return "$eval(" + y + " + " + std::to_string(dy) + ")"; };
    return "\n// ---- What's playing ----------------------------------------------------------------------------\n"
           "$font(Segoe UI,12,bold)\n"
           "$drawstring($if(%isplaying%,%title%," + tf_quote(tr("Nothing playing")) + ")," + x + "," + y + "," + w + ",22,235-238-245)\n"
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
playlist.header = 1
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

const char* now_playing_js() {
    return R"JSNOW(include('lib/palette.js');

const fonts = {
    title: gdi.Font('Segoe UI', 14, 1),
    info: gdi.Font('Segoe UI', 12, 0),
    small: gdi.Font('Segoe UI', 11, 0),
    note: gdi.Font('Segoe UI', 20, 0),
};
const tf = {
    title: fb.TitleFormat('%title%'),
    info: fb.TitleFormat('[%artist%][  ·  %album%][ (%date%)]'),
    tech: fb.TitleFormat('%codec%[  ·  %bitrate% kbps][  ·  %samplerate% Hz]'),
};
let hover = false;
let cover = null;
let coverFor = null;

function format_time(s) {
    s = Math.max(0, Math.floor(s));
    const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = String(s % 60).padStart(2, '0');
    return h ? h + ':' + String(m).padStart(2, '0') + ':' + sec : m + ':' + sec;
}

function request_cover(handle) {
    cover = null;
    coverFor = handle;
    if (handle) utils.GetAlbumArtAsync(window.ID, handle, 0);
    window.Repaint();
}

function on_get_album_art_done(handle, artId, image) {
    if (!coverFor || !handle || !handle.Compare(coverFor)) return;
    cover = image;
    window.Repaint();
}

function draw_cover(gr, x, y, size) {
    gr.FillSolidRect(x, y, size, size, palette.track);
    if (!cover || !cover.Width || !cover.Height) {
        gr.GdiDrawText('♪', fonts.note, palette.dim, x, y, size, size, DT_CENTER | LINE);
        return;
    }
    const k = Math.min(size / cover.Width, size / cover.Height);
    const w = Math.round(cover.Width * k), h = Math.round(cover.Height * k);
    gr.DrawImage(cover, x + Math.floor((size - w) / 2), y + Math.floor((size - h) / 2), w, h, 0, 0, cover.Width, cover.Height);
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillSolidRect(0, 0, w, h, hover ? palette.hover : palette.bar);
    let left = 12;
    const textW = Math.max(120, Math.floor(w * 0.45));

    if (!fb.IsPlaying) {
        const right = left + textW + 24, barW = Math.max(60, w - right - 12);
        gr.GdiDrawText('Stopped', fonts.title, palette.secondary, left, 8, textW, 22, LINE);
        gr.GdiDrawText('Click to play  ·  right-click: JS Panel menu', fonts.info, palette.dim, left, 30, textW, 18, LINE);
        gr.FillRoundRect(right, 26, barW, 4, 2, 2, palette.track);
        return;
    }

    const size = Math.max(16, h - 12);
    draw_cover(gr, 8, 6, size);
    left = 8 + size + 10;
    const right = left + textW + 24, barW = Math.max(60, w - right - 12);
    gr.GdiDrawText(tf.title.Eval(), fonts.title, palette.text, left, 8, textW, 22, LINE);
    gr.GdiDrawText(tf.info.Eval(), fonts.info, palette.secondary, left, 30, textW, 18, LINE);

    const pos = fb.PlaybackTime, len = fb.PlaybackLength;
    const state = fb.IsPaused ? 'Paused' : 'Playing';
    gr.GdiDrawText(state + '  ·  ' + tf.tech.Eval(), fonts.small, palette.dim, right, 6, barW, 16, LINE);
    gr.FillRoundRect(right, 26, barW, 4, 2, 2, palette.track);
    if (len > 0) gr.FillRoundRect(right, 26, Math.max(4, Math.round(barW * pos / len)), 4, 2, 2, palette.accent);
    gr.GdiDrawText(format_time(pos), fonts.small, palette.secondary, right, 34, barW, 16, DT_LEFT | LINE);
    gr.GdiDrawText(len > 0 ? format_time(len) : 'live', fonts.small, palette.secondary, right, 34, barW, 16, DT_RIGHT | LINE);
}

function on_mouse_move() { if (!hover) { hover = true; window.Repaint(); } }
function on_mouse_leave() { hover = false; window.Repaint(); }
function on_mouse_lbtn_up() { fb.PlayOrPause(); }
function on_mouse_wheel(step) { if (step > 0) fb.Prev(); else fb.Next(); }

function on_playback_new_track(handle) { request_cover(handle); }
function on_playback_stop(reason) {
    if (reason !== 2) {
        cover = null;
        coverFor = null;
    }
    window.Repaint();
}
function on_playback_pause() { window.Repaint(); }
function on_playback_time() { window.Repaint(); }

if (fb.IsPlaying) request_cover(fb.GetNowPlaying());
console.log('now_playing.js loaded (' + window.Width + 'x' + window.Height + ')');
)JSNOW";
}

const char* palette_js() {
    return R"JSPAL(function RGB(r, g, b) { return (0xff000000 | (r << 16) | (g << 8) | b) >>> 0; }

const palette = {
    bar: RGB(36, 38, 46),
    panel: RGB(28, 30, 36),
    hover: RGB(40, 43, 52),
    selected: RGB(0, 84, 150),
    text: RGB(225, 228, 235),
    secondary: RGB(160, 165, 175),
    dim: RGB(120, 125, 135),
    track: RGB(52, 55, 64),
    separator: RGB(45, 48, 56),
    accent: RGB(0, 120, 215),
};

const DT_LEFT = 0x0, DT_CENTER = 0x1, DT_RIGHT = 0x2, DT_VCENTER = 0x4, DT_SINGLELINE = 0x20,
      DT_END_ELLIPSIS = 0x8000;
const LINE = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
const MK_CONTROL = 0x8;
const VK_RETURN = 0x0D, VK_CONTROL = 0x11, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24,
      VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28;

function list_key(vk, current, count, page) {
    switch (vk) {
    case VK_UP: return current < 0 ? 0 : current - 1;
    case VK_DOWN: return current + 1;
    case VK_PRIOR: return current - page;
    case VK_NEXT: return current < 0 ? page - 1 : current + page;
    case VK_HOME: return 0;
    case VK_END: return count - 1;
    default: return null;
    }
}

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

function lower(s) { return String(s).toLowerCase(); }

function by_name(a, b) {
    const x = lower(a), y = lower(b);
    return x < y ? -1 : x > y ? 1 : 0;
}
)JSPAL";
}

const char* library_tree_js() {
    return R"JSLIB(include('lib/palette.js');

const fonts = {
    head: gdi.Font('Segoe UI', 12, 1),
    artist: gdi.Font('Segoe UI', 13, 1),
    album: gdi.Font('Segoe UI', 12, 0),
    small: gdi.Font('Segoe UI', 11, 0),
};
const ROW = 24, HEAD = 30, PLAYLIST = 'Library selection';
const tfGroup = fb.TitleFormat('$if2(%album artist%,Unknown artist)^^$if2(%album%,Unknown album)^^%date%');
const tfSort = fb.TitleFormat('%album artist%|%date%|%album%|$num(%discnumber%,2)|$num(%tracknumber%,3)|%title%');
let artists = [];
let rows = [];
let scroll = 0, hoverRow = -1, selected = null;
let status = 'Loading the library…';
let rebuildTimer = 0;

function build() {
    const wasOpen = new Set(artists.filter(a => a.open).map(a => a.name));
    const items = fb.GetLibraryItems();
    const handles = items.Convert();
    const keys = tfGroup.EvalWithMetadbs(items);
    const sorts = tfSort.EvalWithMetadbs(items);
    const byArtist = new Map();
    for (let i = 0; i < handles.length; i++) {
        const [artist, album, date] = keys[i].split('^^');
        let a = byArtist.get(artist);
        if (!a) {
            a = { name: artist, albums: new Map(), open: wasOpen.has(artist) };
            byArtist.set(artist, a);
        }
        let al = a.albums.get(album);
        if (!al) {
            al = { name: album, date: date || '', tracks: [] };
            a.albums.set(album, al);
        }
        al.tracks.push({ h: handles[i], key: sorts[i] });
    }
    artists = [...byArtist.values()].sort((x, y) => by_name(x.name, y.name));
    for (const a of artists) a.albums = [...a.albums.values()].sort((x, y) => by_name(x.date + x.name, y.date + y.name));
    status = artists.length ? '' : 'The Media Library is empty: add music folders in Preferences \u203a Media Library';
    console.log('library_tree.js: ' + handles.length + ' tracks, ' + artists.length + ' artists');
    selected = null;
    flatten();
    window.Repaint();
}

function flatten() {
    rows = [];
    for (const a of artists) {
        rows.push({ artist: a, album: null });
        if (a.open) for (const al of a.albums) rows.push({ artist: a, album: al });
    }
    scroll = clamp(scroll, 0, Math.max(0, rows.length - visible_rows()));
}

function visible_rows() { return Math.max(1, Math.floor((window.Height - HEAD) / ROW)); }

function row_at(y) {
    if (y < HEAD) return -1;
    const i = scroll + Math.floor((y - HEAD) / ROW);
    return i < rows.length ? i : -1;
}

function tracks_of(row) {
    const list = [];
    for (const al of row.album ? [row.album] : row.artist.albums) list.push(...al.tracks);
    list.sort((x, y) => by_name(x.key, y.key));
    const handles = fb.CreateHandleList();
    for (const t of list) handles.Add(t.h);
    return handles;
}

function play(row, add) {
    const handles = tracks_of(row);
    if (!handles.Count) return;
    const p = plman.FindOrCreatePlaylist(PLAYLIST, true);
    if (p < 0) return;
    if (!add) plman.ClearPlaylist(p);
    plman.InsertPlaylistItems(p, add ? 1 << 30 : 0, handles);
    plman.ActivePlaylist = p;
    if (!add) plman.ExecutePlaylistDefaultAction(p, 0);
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillSolidRect(0, 0, w, h, palette.panel);
    gr.FillSolidRect(0, 0, w, HEAD, palette.bar);
    gr.GdiDrawText('Library', fonts.head, palette.text, 10, 0, w - 20, HEAD, LINE);
    if (artists.length) gr.GdiDrawText(artists.length + ' artists', fonts.small, palette.dim, 10, 0, w - 20, HEAD, DT_RIGHT | LINE);
    if (status) {
        gr.GdiDrawText(status, fonts.small, palette.dim, 10, HEAD + 6, w - 20, ROW, LINE);
        return;
    }
    const n = visible_rows();
    for (let i = 0; i < n && scroll + i < rows.length; i++) {
        const r = rows[scroll + i], y = HEAD + i * ROW;
        const isSel = selected && selected.artist === r.artist && selected.album === r.album;
        if (isSel) gr.FillSolidRect(0, y, w, ROW, palette.selected);
        else if (scroll + i === hoverRow) gr.FillSolidRect(0, y, w, ROW, palette.hover);
        if (!r.album) {
            gr.GdiDrawText(r.artist.open ? '▾' : '▸', fonts.small, palette.secondary, 8, y, 14, ROW, LINE);
            gr.GdiDrawText(r.artist.name, fonts.artist, palette.text, 24, y, w - 76, ROW, LINE);
            gr.GdiDrawText(String(r.artist.albums.length), fonts.small, palette.dim, w - 50, y, 40, ROW, DT_RIGHT | LINE);
        } else {
            const label = r.album.name + (r.album.date ? '  (' + r.album.date + ')' : '');
            gr.GdiDrawText(label, fonts.album, isSel ? palette.text : palette.secondary, 36, y, w - 88, ROW, LINE);
            gr.GdiDrawText(String(r.album.tracks.length), fonts.small, palette.dim, w - 50, y, 40, ROW, DT_RIGHT | LINE);
        }
    }
    if (rows.length > n) {
        const trackH = h - HEAD, thumb = Math.max(20, Math.floor(trackH * n / rows.length));
        const top = HEAD + Math.floor((trackH - thumb) * scroll / Math.max(1, rows.length - n));
        gr.FillSolidRect(w - 4, top, 3, thumb, palette.track);
    }
}

function on_size() { flatten(); }

function on_mouse_lbtn_down(x, y) {
    const i = row_at(y);
    if (i < 0) return;
    const r = rows[i];
    selected = r;
    if (!r.album) {
        r.artist.open = !r.artist.open;
        flatten();
    }
    window.Repaint();
}

function on_mouse_lbtn_dblclk(x, y, mask) {
    const i = row_at(y);
    if (i >= 0) play(rows[i], (mask & MK_CONTROL) !== 0);
}

function selected_index() {
    if (!selected) return -1;
    return rows.findIndex(r => r.artist === selected.artist && r.album === selected.album);
}

function select_index(i) {
    if (!rows.length) return;
    i = clamp(i, 0, rows.length - 1);
    selected = rows[i];
    const n = visible_rows();
    if (i < scroll) scroll = i;
    else if (i >= scroll + n) scroll = i - n + 1;
    window.Repaint();
}

function on_key_down(vk) {
    if (!rows.length) return false;
    const i = selected_index();
    const r = i >= 0 ? rows[i] : null;
    if (vk === VK_RETURN) {
        if (r) play(r, utils.IsKeyPressed(VK_CONTROL));
        return true;
    }
    if (vk === VK_RIGHT && r) {
        if (!r.album && !r.artist.open) {
            r.artist.open = true;
            flatten();
            window.Repaint();
        } else if (!r.album) {
            select_index(i + 1);
        }
        return true;
    }
    if (vk === VK_LEFT && r) {
        if (r.album) {
            select_index(rows.findIndex(x => x.artist === r.artist && !x.album));
        } else if (r.artist.open) {
            r.artist.open = false;
            flatten();
            window.Repaint();
        }
        return true;
    }
    const to = list_key(vk, i, rows.length, visible_rows());
    if (to === null) return false;
    select_index(to);
    return true;
}

function on_mouse_move(x, y) {
    const i = row_at(y);
    if (i !== hoverRow) {
        hoverRow = i;
        window.Repaint();
    }
}

function on_mouse_leave() {
    hoverRow = -1;
    window.Repaint();
}

function on_mouse_wheel(step) {
    scroll = clamp(scroll - step * 3, 0, Math.max(0, rows.length - visible_rows()));
    window.Repaint();
}

function schedule_build() {
    if (rebuildTimer) window.ClearTimeout(rebuildTimer);
    rebuildTimer = window.SetTimeout(() => {
        rebuildTimer = 0;
        build();
    }, 300);
}

function on_library_items_added() { schedule_build(); }
function on_library_items_removed() { schedule_build(); }
function on_library_items_changed() { schedule_build(); }

window.SetTimeout(build, 10);
)JSLIB";
}

const char* navidrome_artists_js() {
    return R"JSNDA(include('lib/palette.js');

const fonts = {
    head: gdi.Font('Segoe UI', 12, 1),
    name: gdi.Font('Segoe UI', 13, 0),
    small: gdi.Font('Segoe UI', 11, 0),
};
const ROW = 24, HEAD = 30;
let artists = [];
let scroll = 0, hoverRow = -1, selected = -1;
let status = '';

function load() {
    if (!navidrome.IsAvailable) {
        status = 'foo_navidrome isn’t installed';
        return;
    }
    if (!navidrome.IsConfigured) {
        status = 'Set up a server in foo_navidrome first';
        return;
    }
    status = 'Loading artists…';
    navidrome.GetArtistsAsync().then(list => {
        artists = list.filter(a => a.Id).map(a => ({ id: a.Id, name: a.Name.trim() || 'Unknown artist', albums: a.AlbumCount }));
        artists.sort((x, y) => by_name(x.name, y.name));
        status = artists.length ? '' : 'No artists on the server';
        window.Repaint();
    }, err => {
        status = 'Navidrome: ' + err.message;
        window.Repaint();
    });
}

function visible_rows() { return Math.max(1, Math.floor((window.Height - HEAD) / ROW)); }

function row_at(y) {
    if (y < HEAD) return -1;
    const i = scroll + Math.floor((y - HEAD) / ROW);
    return i < artists.length ? i : -1;
}

function on_paint(gr) {
    const w = window.Width, h = window.Height;
    gr.FillSolidRect(0, 0, w, h, palette.panel);
    gr.FillSolidRect(0, 0, w, HEAD, palette.bar);
    gr.GdiDrawText('Navidrome artists', fonts.head, palette.text, 10, 0, w - 20, HEAD, LINE);
    if (artists.length) gr.GdiDrawText(String(artists.length), fonts.small, palette.dim, 10, 0, w - 20, HEAD, DT_RIGHT | LINE);
    if (status) {
        gr.GdiDrawText(status, fonts.small, palette.dim, 10, HEAD + 6, w - 20, ROW, LINE);
        return;
    }
    const n = visible_rows();
    for (let i = 0; i < n && scroll + i < artists.length; i++) {
        const a = artists[scroll + i], y = HEAD + i * ROW;
        if (scroll + i === selected) gr.FillSolidRect(0, y, w, ROW, palette.selected);
        else if (scroll + i === hoverRow) gr.FillSolidRect(0, y, w, ROW, palette.hover);
        gr.GdiDrawText(a.name, fonts.name, palette.text, 12, y, w - 76, ROW, LINE);
        gr.GdiDrawText(a.albums + (a.albums === 1 ? ' album' : ' albums'), fonts.small, palette.dim, w - 74, y, 64, ROW, DT_RIGHT | LINE);
    }
    if (artists.length > n) {
        const trackH = h - HEAD, thumb = Math.max(20, Math.floor(trackH * n / artists.length));
        const top = HEAD + Math.floor((trackH - thumb) * scroll / Math.max(1, artists.length - n));
        gr.FillSolidRect(w - 4, top, 3, thumb, palette.track);
    }
}

function on_mouse_lbtn_down(x, y) {
    const i = row_at(y);
    if (i < 0) return;
    selected = i;
    window.Repaint();
}

function activate(i, add) {
    if (i < 0 || i >= artists.length) return;
    if (add) navidrome.AddArtist(artists[i].id);
    else navidrome.PlayArtist(artists[i].id);
}

function on_mouse_lbtn_dblclk(x, y, mask) { activate(row_at(y), (mask & MK_CONTROL) !== 0); }

function on_key_down(vk) {
    if (!artists.length) return false;
    if (vk === VK_RETURN) {
        activate(selected, utils.IsKeyPressed(VK_CONTROL));
        return true;
    }
    const to = list_key(vk, selected, artists.length, visible_rows());
    if (to === null) return false;
    selected = clamp(to, 0, artists.length - 1);
    const n = visible_rows();
    if (selected < scroll) scroll = selected;
    else if (selected >= scroll + n) scroll = selected - n + 1;
    window.Repaint();
    return true;
}

function on_mouse_move(x, y) {
    const i = row_at(y);
    if (i !== hoverRow) {
        hoverRow = i;
        window.Repaint();
    }
}

function on_mouse_leave() {
    hoverRow = -1;
    window.Repaint();
}

function on_mouse_wheel(step) {
    scroll = clamp(scroll - step * 3, 0, Math.max(0, artists.length - visible_rows()));
    window.Repaint();
}

load();
)JSNDA";
}

std::string js_now_playing_bar() {
    return "\n// ---- Now playing: a JS Panel running js/now_playing.js (foo_ui_panels.ini: panel.NowPlaying.script).\n"
           "// Right-click it › Edit script… to change it.\n"
           "$drawrect(0,$eval(%_height% - 57),%_width%,1,brushcolor-45-48-56 pencolor-null)\n"
           "$panel(NowPlaying,JS Panel,0,$eval(%_height% - 56),%_width%,56)\n";
}

std::vector<SkinTemplate> build() {
    std::vector<SkinTemplate> t;

    t.push_back({ "playlist-cover", N_("Playlist and cover"), N_("Album art and track info beside the playlist."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 2, 12, 26, 34, Part::Cover }, { 30, 9, 70, 86, Part::Playlist },
          { 0, 95, 100, 5, Part::Status } },
        std::string(header("Playlist and cover")) + toolbar() +
            "\n// ---- Left: album art; right: the playlist ---------------------------------------------------\n"
            "$panel(Cover,Album Art,12,52,256,256)\n" +
            now_playing("12", "318", "256") +
            separator("280") +
            "$panel(Playlist,Single Column Playlist,281,40,$eval(%_width% - 281),$eval(%_height% - 62))\n" + status(),
        ini("Playlist and cover"), false, {} });

    t.push_back({ "library", N_("Library"), N_("Playlists on the left, the playlist, album art on the right."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 22, 86, Part::Tree }, { 22, 9, 52, 86, Part::Playlist },
          { 76, 12, 22, 30, Part::Cover }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Library")) + toolbar() +
            "\n// ---- Left: playlists; middle: the playlist; right: album art ----------------------------------\n"
            "$panel(Playlists,Playlist switcher,0,40,220,$eval(%_height% - 62))\n" +
            separator("220") +
            "$panel(Playlist,Single Column Playlist,221,40,$eval(%_width% - 482),$eval(%_height% - 62))\n" +
            separator("$eval(%_width% - 261)") +
            "$panel(Cover,Album Art,$eval(%_width% - 249),52,238,238)\n" +
            now_playing("$eval(%_width% - 249)", "300", "238") + status(),
        ini("Library"), false, {} });

    t.push_back({ "albums", N_("Album browser"), N_("Your albums as a cover grid, the playlist beside it."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 60, 86, Part::Albums }, { 60, 9, 40, 86, Part::Playlist },
          { 0, 95, 100, 5, Part::Status } },
        std::string(header("Album browser")) + toolbar() +
            "\n// ---- Left: the album grid (right-click it for cover flow and sorting); right: the playlist ----\n"
            "$panel(Albums,Album list,0,40,$eval(%_width% * 3 / 5),$eval(%_height% - 62))\n" +
            separator("$eval(%_width% * 3 / 5)") +
            "$panel(Playlist,Single Column Playlist,$eval(%_width% * 3 / 5 + 1),40,$eval(%_width% - %_width% * 3 / 5 - 1),$eval(%_height% - 62))\n" +
            status(),
        ini("Album browser"), false, {} });

    t.push_back({ "playlist", N_("Playlist"), N_("Just the playlist, with the controls on top."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 100, 86, Part::Playlist }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Playlist")) + toolbar() +
            "\n// ---- The playlist ------------------------------------------------------------------------------\n"
            "$panel(Playlist,Single Column Playlist,0,40,%_width%,$eval(%_height% - 62))\n" + status(),
        ini("Playlist"), false, {} });

    const std::string S = "$min($eval(%_height% - 230),$eval(%_width% / 2 - 24))";
    t.push_back({ "now-playing", N_("Now playing"), N_("A large cover, synced lyrics and a spectrum."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 2, 12, 40, 56, Part::Cover }, { 45, 10, 53, 64, Part::Lyrics },
          { 0, 78, 100, 17, Part::Spectrum }, { 0, 95, 100, 5, Part::Status } },
        std::string(header("Now playing")) + toolbar() +
            "\n// ---- Album art as large as the window allows, lyrics beside it, a spectrum below -------------\n"
            "$panel(Cover,Album Art,12,52," + S + "," + S + ")\n" +
            now_playing("12", "$eval(" + S + " + 60)", S) +
            "$panel(Lyrics,Lyric Show,$eval(" + S + " + 24),40,$eval(%_width% - " + S + " - 24),$eval(%_height% - 132))\n"
            "$panel(Spectrum,Spectrum,0,$eval(%_height% - 88),%_width%,64)\n" + status(),
        ini("Now playing"), false, {} });

    const auto jsFiles = [](std::vector<std::pair<std::string, std::string>> extra) {
        std::vector<std::pair<std::string, std::string>> f = { { "js/now_playing.js", now_playing_js() },
                                                               { "js/lib/palette.js", palette_js() } };
        f.insert(f.end(), extra.begin(), extra.end());
        return f;
    };
    const std::string barIni = "panel.NowPlaying.script = js/now_playing.js\n";

    t.push_back({ "albums-js", N_("Albums + JS"),
        N_("The album browser with an editable JS now-playing bar."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 60, 80, Part::Albums }, { 60, 9, 40, 80, Part::Playlist },
          { 0, 89, 100, 11, Part::Script } },
        std::string(header("Album browser + JS Panel")) + toolbar() +
            "\n// ---- Left: the album grid (right-click it for cover flow and sorting); right: the playlist ----\n"
            "$panel(Albums,Album list,0,40,$eval(%_width% * 3 / 5),$eval(%_height% - 97))\n"
            "$drawrect($eval(%_width% * 3 / 5),40,1,$eval(%_height% - 97),brushcolor-45-48-56 pencolor-null)\n"
            "$panel(Playlist,Single Column Playlist,$eval(%_width% * 3 / 5 + 1),40,$eval(%_width% - %_width% * 3 / 5 - 1),$eval(%_height% - 97))\n" +
            js_now_playing_bar(),
        ini("Album browser + JS Panel") + barIni, true, jsFiles({}) });

    t.push_back({ "playlist-js", N_("Playlist + JS"),
        N_("The playlist with a JS now-playing bar and the album cover."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 100, 80, Part::Playlist }, { 0, 89, 100, 11, Part::Script } },
        std::string(header("Playlist + JS Panel")) + toolbar() +
            "\n// ---- The playlist: double-click or Enter plays, Delete removes, drop files to add -------------\n"
            "$panel(Playlist,Single Column Playlist,0,40,%_width%,$eval(%_height% - 97))\n" + js_now_playing_bar(),
        ini("Playlist + JS Panel") + barIni, true, jsFiles({}) });

    t.push_back({ "library-js", N_("Library + Navidrome"),
        N_("Library and Navidrome artists in two JS Panels, beside the playlist."),
        { { 0, 0, 100, 9, Part::Toolbar }, { 0, 9, 24, 44, Part::Script }, { 0, 54, 24, 35, Part::Script },
          { 24, 9, 76, 80, Part::Playlist }, { 0, 89, 100, 11, Part::Script } },
        std::string(header("Library + Navidrome (JS)")) + toolbar() +
            "\n// ---- Left: two JS Panels, the Media Library (js/library_tree.js) and the Navidrome artists\n"
            "// (js/navidrome_artists.js); double-click plays, Ctrl+double-click adds. Right: the playlist. ----\n"
            "$panel(Library,JS Panel,0,40,280,$eval({%_height% - 97} * 11 / 20))\n"
            "$drawrect(0,$eval(40 + {%_height% - 97} * 11 / 20),280,1,brushcolor-45-48-56 pencolor-null)\n"
            "$panel(Artists,JS Panel,0,$eval(41 + {%_height% - 97} * 11 / 20),280,$eval(%_height% - 97 - 1 - {%_height% - 97} * 11 / 20))\n"
            "$drawrect(280,40,1,$eval(%_height% - 97),brushcolor-45-48-56 pencolor-null)\n"
            "$panel(Playlist,Single Column Playlist,281,40,$eval(%_width% - 281),$eval(%_height% - 97))\n" + js_now_playing_bar(),
        ini("Library + Navidrome (JS)") + barIni + "panel.Library.script = js/library_tree.js\n" +
            "panel.Artists.script = js/navidrome_artists.js\n",
        true, jsFiles({ { "js/library_tree.js", library_tree_js() }, { "js/navidrome_artists.js", navidrome_artists_js() } }) });

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
    case Part::Script:   return "214-160-0";
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

std::vector<const SkinTemplate*> wizard_templates(bool jsPanel) {
    std::vector<const SkinTemplate*> out;
    for (const auto& t : skin_templates())
        if (jsPanel || !t.needs_js_panel) out.push_back(&t);
    return out;
}

std::string layout_wizard_script(bool jsPanel) {
    const auto ts = wizard_templates(jsPanel);
    const size_t cols = ts.size() > 6 ? 4 : 3, rows = (ts.size() + cols - 1) / cols;
    const std::string C = std::to_string(cols), R = std::to_string(rows);
    const std::string byWidth = "$eval({%_width% - " + std::to_string(56 + 16 * (cols - 1)) + "} / " + C + ")";
    auto byHeight = [&](int text) {
        return "$eval(2 * {{%_height% - " + std::to_string(166 + 16 * (rows - 1)) + "} / " + R + " - " + std::to_string(text) + "})";
    };
    const std::string full = "$min(" + byWidth + "," + byHeight(88) + ")";
    const std::string TEXT = "$ifgreater(" + full + ",179,1,0)";
    const std::string CW = "$ifgreater(" + full + ",179," + full + ",$max(120,$min(" + byWidth + "," + byHeight(40) + ")))";
    const std::string PH = "{" + CW + " / 2}", CH = "{" + PH + " + $ifgreater(" + full + ",179,88,40)}";
    const std::string X0 = "$max(28,$eval({%_width% - {" + C + " * " + CW + " + " + std::to_string(16 * (cols - 1)) + "}} / 2))";
    std::string s = R"PUI(
$drawrect(0,0,%_width%,%_height%,brushcolor-22-24-30 pencolor-null)
$gradientrect(0,0,%_width%,4,0-120-215,0-90-170)
$font(Segoe UI,20,bold,240-240-240)
$alignabs(28,22,$eval(%_width% - 56),34,left,top){0}
$font(Segoe UI,10)
$drawstring({1},28,60,$eval(%_width% - 56),36,175-180-190,wrap)
$drawroundrect(28,100,210,32,6,6,0-120-215)
$drawroundrect(250,100,110,32,6,6,58-62-72)
$font(Segoe UI,10,bold,255-255-255)
$textbutton(28,100,210,32,{2},,SKIN:CHOOSE_FOLDER,TOOLTIP,{3})
$textbutton(250,100,110,32,{4},,WIZARD:CLOSE)
)PUI";
    s = i18n::substitute(s, { tf_quote(tr("Choose a layout")),
                              tf_quote(tr("Pick a starting point. It becomes a skin folder of its own, so anything in it can be "
                                          "changed later. Or use a skin you already have.")),
                              tf_quote(tr("Use my own skin folder...")), tf_quote(tr("Load a skin from a folder")),
                              tf_quote(tr("Close")) });
    for (size_t i = 0; i < ts.size(); ++i) {
        const SkinTemplate& t = *ts[i];
        const std::string col = std::to_string(i % cols), row = std::to_string(i / cols);
        const std::string X = "{" + X0 + " + " + col + " * {" + CW + " + 16}}", Y = "{150 + " + row + " * {" + CH + " + 16}}";
        auto e = [](const std::string& expr) { return "$eval(" + expr + ")"; };
        s += "\n// " + std::string(t.name) + "\n";
        s += "$button2(" + e(X) + "," + e(Y) + ",0,0," + e(CW) + "," + e(CH) +
             ",'$drawroundrect(0,0," + e(CW) + "," + e(CH) + ",8,8,34-37-45)'" +
             ",'$drawroundrect(0,0," + e(CW) + "," + e(CH) + ",8,8,46-50-62)'" +
             ",SKIN:TEMPLATE:" + t.id + "," + tf_quote(std::string("TOOLTIP:") + tr(t.summary)) + ")\n";
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
        if (t.needs_js_panel) {
            s += "$drawroundrect(" + e(TX + " + " + TW + " - 34") + "," + e(TY + " + 6") + ",28,16,4,4,214-160-0)\n";
            s += "$font(Segoe UI,8,bold)\n";
            s += "$drawstring(JS," + e(TX + " + " + TW + " - 34") + "," + e(TY + " + 7") + ",28,14,24-25-30,center)\n";
        }
        s += "$font(Segoe UI,11,bold)\n";
        s += "$drawstring(" + tf_quote(tr(t.name)) + "," + e(TX) + "," + e(Y + " + " + PH + " + 12") + "," + e(TW) + ",20,235-238-245)\n";
        s += "$ifgreater(" + TEXT + ",0,$font(Segoe UI,9)$drawstring(" + tf_quote(tr(t.summary)) + "," + e(TX) + "," +
             e(Y + " + " + PH + " + 34") + "," + e(TW) + ",50,150-155-165,wrap),)\n";
    }
    return s;
}

std::string install_skin_template(const SkinTemplate& t, const std::string& parent, std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(fs_path(parent), ec);
    for (int n = 1; n < 1000; ++n) {
        const std::string dir = parent + kPathSep + tr(t.name) + (n > 1 ? " " + std::to_string(n) : std::string());
        if (std::filesystem::exists(fs_path(dir), ec)) continue;
        if (!std::filesystem::create_directory(fs_path(dir), ec) ||
            !write_file(dir + kPathSep + "main.txt", t.script) || !write_file(dir + kPathSep + SkinConfig::kFileName, t.ini)) {
            if (err) *err = trf("Couldn't write the layout to {0}", { dir });
            return {};
        }
        for (const auto& [rel, text] : t.files) {
            const auto path = fs_path(dir) / fs_path(rel);
            std::filesystem::create_directories(path.parent_path(), ec);
            if (!write_file(fs_utf8(path), text)) {
                if (err) *err = trf("Couldn't write the layout to {0}", { dir });
                return {};
            }
        }
        return dir;
    }
    if (err) *err = trf("No free folder name under {0}", { parent });
    return {};
}

}
