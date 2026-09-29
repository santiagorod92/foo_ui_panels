#include "library_tree.h"
#include "skin_engine.h"
#include "navidrome_library_api.h"
#include <commctrl.h>
#include <algorithm>
#include <functional>
#include <map>

#pragma comment(lib, "comctl32.lib")

namespace pui {

static const wchar_t* kClass = L"foo_ui_panels_library_tree";
enum { kTreeId = 201 };

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string lower_str(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static navidrome::navidrome_library_api::ptr navidrome_api() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

static pfc::string8 fmt(const metadb_handle_ptr& h, const char* script) {
    service_ptr_t<titleformat_object> obj;
    titleformat_compiler::get()->compile_safe(obj, script);
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, obj, nullptr);
    return o;
}

void LibraryTree::register_class() {
    static bool done = false; if (done) return; done = true;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
}

HWND LibraryTree::create(HWND parent, SkinEngine* engine) {
    register_class();
    m_engine = engine;
    m_wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), this);
    return m_wnd;
}

LibraryTree::Artist& LibraryTree::artist_for(const std::string& name) {
    const std::string k = lower_str(name);
    for (auto& a : m_artists) if (lower_str(a.name) == k) return a;
    m_artists.push_back({ name, "", {} });
    return m_artists.back();
}

void LibraryTree::load_local() {
    m_artists.clear();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);
    for (t_size i = 0; i < all.get_count(); ++i) {
        std::string artist = fmt(all[i], "[%album artist%]").c_str();
        if (artist.empty()) artist = fmt(all[i], "[%artist%]").c_str();
        if (artist.empty()) artist = "Unknown artist";
        std::string album = fmt(all[i], "[%album%]").c_str();
        if (album.empty()) album = "(no album)";
        Artist& ar = artist_for(artist);
        Album* found = nullptr;
        for (auto& al : ar.albums) if (!al.remote && lower_str(al.name) == lower_str(album)) { found = &al; break; }
        if (!found) { ar.albums.push_back({ album, "", false, {} }); found = &ar.albums.back(); }
        found->items.push_back(all[i]);
    }
}

void LibraryTree::start_remote_load() {
    ++m_gen;
    m_remote_err.clear(); m_remote_loading = false;
    auto api = navidrome_api();
    if (api.is_empty()) return;
    if (!api->is_configured()) return;
    m_remote_loading = true;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; LibraryTree* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_album_sink {
            std::vector<RemoteAlbum> all;
            void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char*, int, int) override {
                all.push_back({ artistName, albumName, albumId, artistId });
            }
        } sink;
        pfc::string8 err; bool ok = false;
        try { ok = api->list_albums(sink, *abort, err); } catch (...) { err = "Navidrome library request failed."; }
        auto shared = std::make_shared<std::vector<RemoteAlbum>>(std::move(sink.all));
        std::string errs = ok ? std::string() : std::string(err.c_str());
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->m_remote_loading = false; self->m_remote_err = errs;
            self->merge_remote(std::move(*shared));
        });
    });
}

void LibraryTree::merge_remote(std::vector<RemoteAlbum>&& add) {
    for (auto& r : add) {
        Artist& ar = artist_for(r.artist);
        if (ar.remote_id.empty()) ar.remote_id = r.artist_id;
        bool have = false;
        for (auto& al : ar.albums) if (lower_str(al.name) == lower_str(r.album)) { have = true; break; }
        if (!have) ar.albums.push_back({ r.album, r.id, true, {} });
    }
    finish_data();
}

void LibraryTree::finish_data() {
    auto byName = [](const std::string& a, const std::string& b) { return lower_str(a) < lower_str(b); };
    for (auto& ar : m_artists)
        std::stable_sort(ar.albums.begin(), ar.albums.end(), [&](const Album& a, const Album& b) { return byName(a.name, b.name); });
    std::stable_sort(m_artists.begin(), m_artists.end(), [&](const Artist& a, const Artist& b) { return byName(a.name, b.name); });
    refresh_tree();
}

void LibraryTree::refresh_tree() {
    if (!m_tree) return;
    SendMessageW(m_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(m_tree);
    m_nodes.clear();
    m_nodes.reserve(m_artists.size() * 8 + 64); // lParam holds indices, not pointers; reserve is just a hint

    auto add = [&](HTREEITEM parent, const std::wstring& text, Node node, bool children) {
        m_nodes.push_back(node);
        TVINSERTSTRUCTW is = {};
        is.hParent = parent; is.hInsertAfter = TVI_LAST;
        is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        is.item.pszText = const_cast<wchar_t*>(text.c_str());
        is.item.lParam = (LPARAM)(m_nodes.size() - 1);
        is.item.cChildren = children ? 1 : 0;
        return TreeView_InsertItem(m_tree, &is);
    };

    auto pm = playlist_manager::get();
    const t_size np = pm->get_playlist_count();
    HTREEITEM pls = add(TVI_ROOT, L"Playlists", { kPlaylists }, np > 0);
    for (t_size i = 0; i < np; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        std::wstring label = widen(nm.c_str()) + L"  (" + std::to_wstring(pm->playlist_get_item_count(i)) + L")";
        add(pls, label, { kPlaylist, (int)i }, false);
    }
    if (m_remote_loading) add(TVI_ROOT, L"Loading Navidrome library...", { kLoading }, false);
    else if (!m_remote_err.empty()) add(TVI_ROOT, widen(m_remote_err), { kLoading }, false);
    if (m_artists.empty() && !m_remote_loading)
        add(TVI_ROOT, L"No music found (Media Library empty, Navidrome not available)", { kLoading }, false);

    for (size_t a = 0; a < m_artists.size(); ++a) {
        const Artist& ar = m_artists[a];
        HTREEITEM ai = add(TVI_ROOT, widen(ar.name) + L"  (" + std::to_wstring(ar.albums.size()) + L")",
                           { kArtist, (int)a }, !ar.albums.empty());
        for (size_t b = 0; b < ar.albums.size(); ++b)
            add(ai, widen(ar.albums[b].name), { kAlbum, (int)a, (int)b }, false);
    }
    SendMessageW(m_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_tree, nullptr, TRUE);
}

LibraryTree::Node* LibraryTree::node_of(HTREEITEM h) {
    if (!h) return nullptr;
    TVITEMW it = {}; it.mask = TVIF_PARAM; it.hItem = h;
    if (!TreeView_GetItem(m_tree, &it)) return nullptr;
    return (it.lParam >= 0 && it.lParam < (LPARAM)m_nodes.size()) ? &m_nodes[it.lParam] : nullptr;
}

void LibraryTree::play_album(const Album& al, bool replace) {
    if (al.remote) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_album(al.id.c_str(), replace, replace);
        return;
    }
    metadb_handle_list list; for (auto& h : al.items) list.add_item(h);
    auto pm = playlist_manager::get();
    if (replace) {
        static const char* kName = "Album Browser";
        t_size n = pm->get_playlist_count(), target = pfc_infinite;
        for (t_size i = 0; i < n; ++i) { pfc::string8 nm; pm->playlist_get_name(i, nm); if (nm == kName) { target = i; break; } }
        if (target == pfc_infinite) target = pm->create_playlist(kName, strlen(kName), pfc_infinite);
        if (target == pfc_infinite) return;
        pm->playlist_remove_items(target, bit_array_true());
        pm->playlist_insert_items(target, 0, list, bit_array_true());
        pm->set_active_playlist(target);
        pm->playlist_execute_default_action(target, 0);
    } else {
        pm->activeplaylist_add_items(list, bit_array_false());
    }
}

// Enter on an artist: clear the active playlist, add everything by them, start playing —
// the same thing Enter does in foo_navidrome's own browser.
void LibraryTree::play_artist(const Artist& ar, bool replace) {
    if (!ar.remote_id.empty()) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_artist(ar.remote_id.c_str(), replace, replace);
        return;
    }
    metadb_handle_list list;
    for (auto& al : ar.albums) for (auto& h : al.items) list.add_item(h);
    if (!list.get_count()) return;
    auto pm = playlist_manager::get();
    if (replace) {
        pm->activeplaylist_clear();
        pm->activeplaylist_add_items(list, bit_array_false());
        pm->playlist_execute_default_action(pm->get_active_playlist(), 0);
    } else pm->activeplaylist_add_items(list, bit_array_false());
}

void LibraryTree::activate(HTREEITEM h) {
    Node* n = node_of(h);
    if (!n) return;
    if (n->kind == kAlbum) play_album(m_artists[n->a].albums[n->b], true);
    else if (n->kind == kArtist) play_artist(m_artists[n->a], true);
    else if (n->kind == kPlaylist) {
        playlist_manager::get()->set_active_playlist((t_size)n->a);
        if (m_engine) m_engine->repaint_all();
    }
}

void LibraryTree::context_menu(HTREEITEM h, POINT pt) {
    Node* n = node_of(h);
    if (!n || (n->kind != kAlbum && n->kind != kPlaylist && n->kind != kArtist)) return;
    TreeView_SelectItem(m_tree, h);
    HMENU menu = CreatePopupMenu();
    if (n->kind == kArtist) {
        AppendMenuW(menu, MF_STRING, 4, L"Play artist");
        AppendMenuW(menu, MF_STRING, 5, L"Add artist to current playlist");
    } else if (n->kind == kAlbum) {
        AppendMenuW(menu, MF_STRING, 1, L"Play album");
        AppendMenuW(menu, MF_STRING, 2, L"Add to current playlist");
    } else AppendMenuW(menu, MF_STRING, 3, L"Switch to this playlist");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_wnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 1 || cmd == 2) play_album(m_artists[n->a].albums[n->b], cmd == 1);
    else if (cmd == 3) activate(h);
    else if (cmd == 4 || cmd == 5) play_artist(m_artists[n->a], cmd == 4);
}

LRESULT CALLBACK LibraryTree::WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    LibraryTree* self = reinterpret_cast<LibraryTree*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<LibraryTree*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(wnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        self->m_wnd = wnd;
        self->m_tree = CreateWindowExW(0, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, wnd, (HMENU)(INT_PTR)kTreeId, GetModuleHandleW(nullptr), nullptr);
        self->m_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SendMessageW(self->m_tree, WM_SETFONT, (WPARAM)self->m_font, TRUE);
        TreeView_SetBkColor(self->m_tree, RGB(20, 21, 26));
        TreeView_SetTextColor(self->m_tree, RGB(228, 231, 240));
        TreeView_SetLineColor(self->m_tree, RGB(80, 85, 100));
        TreeView_SetItemHeight(self->m_tree, 20);
        self->load_local();
        self->start_remote_load();
        self->finish_data();
        self->m_loaded = true;
        return 0;
    case WM_SIZE:
        if (self->m_tree) MoveWindow(self->m_tree, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SHOWWINDOW:
        if (wp && self->m_loaded) self->refresh_tree(); // playlists may have changed while hidden
        return 0;
    case WM_SETFOCUS: SetFocus(self->m_tree); return 0;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->idFrom != kTreeId) break;
        if (nm->code == NM_DBLCLK || nm->code == NM_RETURN) {
            HTREEITEM h = nullptr;
            if (nm->code == NM_DBLCLK) {
                TVHITTESTINFO ht = {}; GetCursorPos(&ht.pt); ScreenToClient(self->m_tree, &ht.pt);
                h = TreeView_HitTest(self->m_tree, &ht);
            } else h = TreeView_GetSelection(self->m_tree);
            Node* n = self->node_of(h);
            if (n && (n->kind == kAlbum || n->kind == kPlaylist)) { self->activate(h); return 1; } // skip expand toggle
            if (n && n->kind == kArtist && nm->code == NM_RETURN) { self->activate(h); return 1; } // Enter plays the artist
            return 0;
        }
        if (nm->code == NM_RCLICK) {
            TVHITTESTINFO ht = {}; GetCursorPos(&ht.pt);
            POINT screen = ht.pt; ScreenToClient(self->m_tree, &ht.pt);
            HTREEITEM h = TreeView_HitTest(self->m_tree, &ht);
            if (h) self->context_menu(h, screen);
            return 1;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wp == VK_F5) {
            self->load_local(); self->start_remote_load(); self->finish_data();
            return 0;
        }
        break;
    case WM_DESTROY:
        self->m_alive->store(false); self->m_abort->abort();
        if (self->m_font) DeleteObject(self->m_font);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace pui
