// Preferences page (Display > Panels UI (reborn)) for macOS: the General tab of the Windows page
// (src/platform/win/preferences.cpp) — skins root folder, active skin, main script — plus its
// accent colour override. macOS preferences apply as you change them (there is no Apply button),
// so a skin or main-script change reloads the open Panels UI canvases right away.
#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "mac_view.h"
#include "../../core/skin_engine.h"
#include "../../core/skin_paths.h"
#include "../../core/skin_config.h"
#include "../../core/fs_util.h"
#include "../../core/ui_settings.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

// Same page GUID as the Windows build: one setting, whichever platform shows it.
// {9C1D9F3A-2B7E-4A6C-9F0D-7E3C5A8B1D40}
const GUID g_prefs_page_guid =
    { 0x9c1d9f3a, 0x2b7e, 0x4a6c, { 0x9f, 0x0d, 0x7e, 0x3c, 0x5a, 0x8b, 0x1d, 0x40 } };

const char* const kAccentKey = "_prefs_accent_color"; // read by SkinEngine::theme_color()

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string utf8(NSString* s) { return s.UTF8String ? s.UTF8String : ""; }

std::vector<std::string> skin_folders(const std::string& root) { return pui::list_skins(root); }

// Zoom popup: index 0 is automatic, then these percentages (same list as the Windows page).
const int kZoomChoices[] = { 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };

NSTextField* label(NSString* text) {
    NSTextField* l = [NSTextField labelWithString:text];
    l.alignment = NSTextAlignmentRight;
    return l;
}

} // namespace

@interface FooUIPanelsPrefsController : NSViewController
@end

@implementation FooUIPanelsPrefsController {
    NSTextField* _rootField;
    NSPopUpButton* _skinPopup;
    NSPopUpButton* _mainPopup;
    NSColorWell* _accentWell;
    NSPopUpButton* _zoomPopup;
    NSButton* _onTopCheck;
}

- (void)loadView {
    _rootField = [NSTextField labelWithString:@""];
    _rootField.selectable = YES;
    _rootField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [_rootField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                         forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSButton* choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(chooseRoot:)];
    NSButton* clearRoot = [NSButton buttonWithTitle:@"Clear" target:self action:@selector(clearRoot:)];
    NSStackView* rootRow = [NSStackView stackViewWithViews:@[ _rootField, choose, clearRoot ]];

    _skinPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _skinPopup.target = self; _skinPopup.action = @selector(skinChanged:);
    _mainPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _mainPopup.target = self; _mainPopup.action = @selector(mainChanged:);

    _accentWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 44, 24)];
    _accentWell.target = self; _accentWell.action = @selector(accentChanged:);
    NSButton* clearAccent = [NSButton buttonWithTitle:@"Use the skin's" target:self action:@selector(clearAccent:)];
    NSStackView* accentRow = [NSStackView stackViewWithViews:@[ _accentWell, clearAccent ]];

    _zoomPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _zoomPopup.target = self; _zoomPopup.action = @selector(zoomChanged:);
    _onTopCheck = [NSButton checkboxWithTitle:@"Keep the player window on top of other windows"
                                       target:self action:@selector(onTopChanged:)];

    NSTextField* note = [NSTextField wrappingLabelWithString:
        @"The skins root folder holds one subfolder per skin. Leave it empty to use the single skin in "
        @"~/Library/foobar2000-v2/foo_ui_panels. Main script: the skin's top-level .txt to run "
        @"(automatic when there is only one). The accent colour is used when the skin doesn't set its own."];
    note.textColor = NSColor.secondaryLabelColor;
    note.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    // Wrap at the grid's width instead of stretching everything to one long line (which pushed
    // the grid off the right edge of the page).
    note.preferredMaxLayoutWidth = 520;
    [note setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                    forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ label(@"Skins root folder:"), rootRow ],
        @[ label(@"Active skin:"), _skinPopup ],
        @[ label(@"Main script:"), _mainPopup ],
        @[ label(@"Accent colour:"), accentRow ],
        @[ label(@"Zoom:"), _zoomPopup ],
        @[ [NSGridCell emptyContentView], _onTopCheck ],
    ]];
    grid.rowSpacing = 10;
    grid.columnSpacing = 8;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    for (NSInteger r = 0; r < grid.numberOfRows; ++r) [grid rowAtIndex:r].yPlacement = NSGridCellPlacementCenter;

    NSStackView* page = [NSStackView stackViewWithViews:@[ grid, note ]];
    page.orientation = NSUserInterfaceLayoutOrientationVertical;
    page.alignment = NSLayoutAttributeLeading;
    page.spacing = 16;
    page.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    [note.widthAnchor constraintEqualToAnchor:grid.widthAnchor].active = YES;
    [grid.widthAnchor constraintGreaterThanOrEqualToConstant:520].active = YES;
    self.view = page;
    [self refresh];
}

// Everything from the stored settings (they're the source of truth: each control writes through).
- (void)refresh {
    const std::string root = pui::skins_root();
    _rootField.stringValue = root.empty() ? @"(none: the component's own folder)" : ns(root);

    [_skinPopup removeAllItems];
    const auto skins = skin_folders(root);
    for (auto& s : skins) [_skinPopup addItemWithTitle:ns(s)];
    const std::string active = pui::active_skin();
    if (!active.empty()) [_skinPopup selectItemWithTitle:ns(active)];
    _skinPopup.enabled = !skins.empty();

    [_mainPopup removeAllItems];
    [_mainPopup addItemWithTitle:@"Automatic"];
    for (auto& c : pui::main_script_candidates(pui::resolve_skin_dir())) [_mainPopup addItemWithTitle:ns(c)];
    const std::string over = pui::main_script_override();
    if (over.empty() || ![_mainPopup itemWithTitle:ns(over)]) [_mainPopup selectItemAtIndex:0];
    else [_mainPopup selectItemWithTitle:ns(over)];

    pui::PvarMap pv = pui::load_all_pvars();
    auto it = pv.find(kAccentKey);
    int r = 0, g = 140, b = 220; // the engine's default accent
    if (it != pv.end() && !it->second.empty()) sscanf(it->second.c_str(), "%d-%d-%d", &r, &g, &b);
    _accentWell.color = [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1];

    [_zoomPopup removeAllItems];
    [_zoomPopup addItemWithTitle:@"Automatic (100%)"];
    for (int z : kZoomChoices) [_zoomPopup addItemWithTitle:[NSString stringWithFormat:@"%d%%", z]];
    const int zs = pui::zoom_setting();
    NSInteger sel = 0;
    for (int i = 0; zs > 0 && i < (int)std::size(kZoomChoices); ++i) if (kZoomChoices[i] <= zs) sel = i + 1;
    [_zoomPopup selectItemAtIndex:sel];
    _onTopCheck.state = pui::always_on_top() ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)zoomChanged:(id)sender {
    const NSInteger i = _zoomPopup.indexOfSelectedItem;
    pui::set_zoom_setting(i <= 0 || i > (NSInteger)std::size(kZoomChoices) ? 0 : kZoomChoices[i - 1]);
    pui::apply_view_settings();
}

- (void)onTopChanged:(id)sender {
    pui::set_always_on_top(_onTopCheck.state == NSControlStateValueOn);
    pui::apply_view_settings();
}

- (void)applySkinChange {
    pui::mac::reload_skin_everywhere();
    [self refresh];
}

- (void)chooseRoot:(id)sender {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.canChooseDirectories = YES;
    p.canChooseFiles = NO;
    p.allowsMultipleSelection = NO;
    p.message = @"Folder with one subfolder per skin";
    const std::string cur = pui::skins_root();
    if (!cur.empty()) p.directoryURL = [NSURL fileURLWithPath:ns(cur)];
    [p beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse res) {
        if (res != NSModalResponseOK || !p.URL.fileSystemRepresentation) return;
        const std::string root = p.URL.fileSystemRepresentation;
        pui::set_skins_root(root);
        // Keep the active skin if the new root has it, else take the first one there.
        const auto skins = skin_folders(root);
        if (std::find(skins.begin(), skins.end(), pui::active_skin()) == skins.end())
            pui::set_active_skin(skins.empty() ? std::string() : skins.front());
        pui::set_main_script_override("");
        [self applySkinChange];
    }];
}

- (void)clearRoot:(id)sender {
    pui::set_skins_root("");
    pui::set_active_skin("");
    pui::set_main_script_override("");
    [self applySkinChange];
}

- (void)skinChanged:(id)sender {
    pui::set_active_skin(utf8(_skinPopup.titleOfSelectedItem));
    pui::set_main_script_override(""); // script names belong to the previous skin
    [self applySkinChange];
}

- (void)mainChanged:(id)sender {
    pui::set_main_script_override(_mainPopup.indexOfSelectedItem <= 0 ? std::string()
                                                                        : utf8(_mainPopup.titleOfSelectedItem));
    [self applySkinChange];
}

- (void)accentChanged:(id)sender {
    NSColor* c = [_accentWell.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!c) return;
    char buf[32];
    snprintf(buf, sizeof buf, "%d-%d-%d", (int)std::lround(c.redComponent * 255),
             (int)std::lround(c.greenComponent * 255), (int)std::lround(c.blueComponent * 255));
    pui::mac::set_pvar_everywhere(kAccentKey, buf);
}

- (void)clearAccent:(id)sender {
    pui::mac::set_pvar_everywhere(kAccentKey, "");
    [self refresh];
}

@end

namespace {

class preferences_page_panels : public preferences_page {
public:
    service_ptr instantiate() override { return fb2k::wrapNSObject([FooUIPanelsPrefsController new]); }
    const char* get_name() override { return "Panels UI (reborn)"; }
    GUID get_guid() override { return g_prefs_page_guid; }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
};

FB2K_SERVICE_FACTORY(preferences_page_panels);

} // namespace
