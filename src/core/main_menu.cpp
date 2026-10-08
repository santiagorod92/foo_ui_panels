// View > Panels UI: player-level commands as foobar2000 main-menu commands, so every one of them
// can get a keyboard shortcut (Preferences > Keyboard Shortcuts) and shows up in the skin's own
// MENU button. Platform-free: the commands act on the live engines (SkinEngine::live()).
//
//   View > Panels UI > Mini mode / Zoom in / Zoom out / Reset zoom / Reload skin / Layout wizard
//                      (+ Always on top on macOS; on Windows that's the core's View > Always on Top)
//                    > Skin > <one entry per skin folder under the skins root>
//                    > Skin commands > <the active skin's `command.<label>` config keys>
#include "skin_engine.h"
#include "skin_paths.h"
#include "ui_logic.h"
#include "ui_settings.h"

namespace pui {

namespace {

// {B2D74E19-5C3A-4F86-9E17-0A6C8D2F4B53}
const GUID g_group_panels = { 0xb2d74e19, 0x5c3a, 0x4f86, { 0x9e, 0x17, 0x0a, 0x6c, 0x8d, 0x2f, 0x4b, 0x53 } };
// {C7A15F3E-2D84-4B69-8F01-5E3B9A7C6D24}
const GUID g_group_skins = { 0xc7a15f3e, 0x2d84, 0x4b69, { 0x8f, 0x01, 0x5e, 0x3b, 0x9a, 0x7c, 0x6d, 0x24 } };
// {D4E82A61-7B9F-4C35-A6D0-1F8E3C5B9A72}
const GUID g_group_skin_cmds = { 0xd4e82a61, 0x7b9f, 0x4c35, { 0xa6, 0xd0, 0x1f, 0x8e, 0x3c, 0x5b, 0x9a, 0x72 } };

FB2K_DECLARE_MAINMENU_GROUP_POPUP(g_group_panels, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare, "Panels UI");
FB2K_DECLARE_MAINMENU_GROUP_POPUP(g_group_skins, g_group_panels, mainmenu_commands::sort_priority_base + 1, "Skin");
FB2K_DECLARE_MAINMENU_GROUP_POPUP(g_group_skin_cmds, g_group_panels, mainmenu_commands::sort_priority_base + 2, "Skin commands");

// The engine of the player window (the first one: one per window, and there's one player).
SkinEngine* engine() { return SkinEngine::live().empty() ? nullptr : SkinEngine::live().front(); }

// GUID from a name, for commands whose list comes from the disk/config (a shortcut bound to one
// keeps working as long as it keeps its name).
GUID guid_for(const std::string& kind, const std::string& name) {
    uint8_t h[16];
    name_hash128("foo_ui_panels/" + kind + "/" + name, h);
    GUID g;
    g.Data1 = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    g.Data2 = (uint16_t)(h[4] | h[5] << 8);
    g.Data3 = (uint16_t)(h[6] | h[7] << 8);
    for (int i = 0; i < 8; ++i) g.Data4[i] = h[8 + i];
    return g;
}

// --- View > Panels UI ------------------------------------------------------------------------
class panels_commands : public mainmenu_commands {
    enum Cmd { kMini, kZoomIn, kZoomOut, kZoomReset, kReload, kProblems, kWizard, kOnTop };
    struct Entry { Cmd cmd; GUID id; const char* name; const char* desc; };
    static const std::vector<Entry>& entries() {
        // {E1A4…} family: fixed GUIDs, one per command.
        static const std::vector<Entry> e = {
            { kMini, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x02 } }, "Mini mode",
              "Switches between the skin's compact layout and the full window (skins that declare one)." },
            { kZoomIn, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x03 } }, "Zoom in",
              "Makes the whole skin bigger." },
            { kZoomOut, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x04 } }, "Zoom out",
              "Makes the whole skin smaller." },
            { kZoomReset, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x05 } }, "Reset zoom",
              "Back to automatic zoom (the system's display scaling)." },
            { kReload, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x06 } }, "Reload skin",
              "Reloads the active skin from disk." },
            { kProblems, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x07 } }, "Show script problems",
              "Marks panels whose script doesn't compile, is missing or uses unknown functions." },
            { kWizard, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x08 } }, "Layout wizard...",
              "Start from a ready-made layout, or pick a skin folder." },
#ifdef __APPLE__
            // The Windows core has its own View > Always on Top (same setting); the macOS one doesn't.
            { kOnTop, { 0xe1a4c2b0, 0x3f51, 0x4d7a, { 0x9b, 0x26, 0x4c, 0x8e, 0x1d, 0x6f, 0x3a, 0x01 } }, "Always on top",
              "Keeps the player window above other windows." },
#endif
        };
        return e;
    }
public:
    t_uint32 get_command_count() override { return (t_uint32)entries().size(); }
    GUID get_command(t_uint32 i) override { return i < entries().size() ? entries()[i].id : pfc::guid_null; }
    void get_name(t_uint32 i, pfc::string_base& out) override { out = i < entries().size() ? entries()[i].name : ""; }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i >= entries().size()) return false;
        out = entries()[i].desc;
        return true;
    }
    GUID get_parent() override { return g_group_panels; }
    t_uint32 get_sort_priority() override { return mainmenu_commands::sort_priority_base; }
    bool get_display(t_uint32 i, pfc::string_base& text, t_uint32& flags) override {
        flags = 0;
        get_name(i, text);
        if (i >= entries().size()) return true;
        SkinEngine* e = engine();
        switch (entries()[i].cmd) {
        case kMini:
            if (!e || !e->mini_mode_available()) flags |= flag_disabled;
            else if (e->in_mini_mode()) flags |= flag_checked;
            break;
        case kZoomReset: {
            const int pct = effective_zoom_percent();
            pfc::string8 t; t << "Reset zoom (now " << pct << "%" << (zoom_setting() == 0 ? ", automatic)" : ")");
            text = t;
            if (zoom_setting() == 0) flags |= flag_disabled;
            break;
        }
        case kReload: case kWizard: if (!e) flags |= flag_disabled; break;
        case kOnTop: if (always_on_top()) flags |= flag_checked; break;
        case kProblems: if (show_script_problems()) flags |= flag_checked; break;
        default: break;
        }
        return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i >= entries().size()) return;
        SkinEngine* e = engine();
        switch (entries()[i].cmd) {
        case kMini: if (e) e->toggle_mini_mode(); break;
        case kZoomIn: step_zoom(+1); break;
        case kZoomOut: step_zoom(-1); break;
        case kZoomReset: step_zoom(0); break;
        case kReload: SkinEngine::reload_all(); break;
        case kWizard: if (e) e->show_layout_wizard(); break;
        case kOnTop: set_always_on_top(!always_on_top()); break;
        case kProblems: set_show_script_problems(!show_script_problems()); break;
        }
    }
};
FB2K_SERVICE_FACTORY(panels_commands);

// --- View > Panels UI > Skin -----------------------------------------------------------------
// One radio item per subfolder of the skins root (Preferences > Display > Panels UI). Picking
// one makes it the active skin and loads it right away.
class skin_list_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { refresh(); return (t_uint32)m_skins.size(); }
    GUID get_command(t_uint32 i) override { return i < m_skins.size() ? guid_for("skin", m_skins[i]) : pfc::guid_null; }
    void get_name(t_uint32 i, pfc::string_base& out) override { out = i < m_skins.size() ? m_skins[i].c_str() : ""; }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i >= m_skins.size()) return false;
        out = "Switches Panels UI to this skin.";
        return true;
    }
    GUID get_parent() override { return g_group_skins; }
    bool get_display(t_uint32 i, pfc::string_base& text, t_uint32& flags) override {
        get_name(i, text);
        flags = (i < m_skins.size() && m_skins[i] == active_skin()) ? flag_radiochecked : 0;
        return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i >= m_skins.size()) return;
        if (m_skins[i] != active_skin()) {
            set_active_skin(m_skins[i]);
            set_main_script_override(""); // script names belong to the previous skin
        }
        SkinEngine::reload_all();
    }
private:
    void refresh() { m_skins = list_skins(skins_root()); }
    std::vector<std::string> m_skins;
};
FB2K_SERVICE_FACTORY(skin_list_commands);

// --- View > Panels UI > Skin commands --------------------------------------------------------
class skin_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        SkinEngine* e = engine();
        m_cmds = e ? e->skin_commands() : decltype(m_cmds)();
        return (t_uint32)m_cmds.size();
    }
    GUID get_command(t_uint32 i) override { return i < m_cmds.size() ? guid_for("command", m_cmds[i].first) : pfc::guid_null; }
    void get_name(t_uint32 i, pfc::string_base& out) override { out = i < m_cmds.size() ? m_cmds[i].first.c_str() : ""; }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i >= m_cmds.size()) return false;
        out = "Skin command: "; out += m_cmds[i].second.c_str();
        return true;
    }
    GUID get_parent() override { return g_group_skin_cmds; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        SkinEngine* e = engine();
        if (e && i < m_cmds.size()) e->run_actions(m_cmds[i].second);
    }
private:
    std::vector<std::pair<std::string, std::string>> m_cmds;
};
FB2K_SERVICE_FACTORY(skin_commands);

} // namespace
} // namespace pui
