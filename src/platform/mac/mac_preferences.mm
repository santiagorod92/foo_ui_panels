// Preferences page (Display > Panels UI (reborn)), macOS view. All logic is PrefsModel
// (src/core/prefs_model.h), shared with the Windows page — the same four tabs: General (skins root,
// active skin with a preview, main script, zoom, always on top), Script (the main script, saved
// with its button), Variables (the skin's persistent variables) and Overrides (font / accent
// fallbacks). macOS preferences apply as you change them (there is no Apply button).
#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "../../core/prefs_model.h"
#include "../../core/prefs_store.h"
#include "../../core/skin_paths.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {


NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string utf8(NSString* s) { return s.UTF8String ? s.UTF8String : ""; }

NSTextField* label(NSString* text) {
    NSTextField* l = [NSTextField labelWithString:text];
    l.alignment = NSTextAlignmentRight;
    return l;
}

NSTextField* note(NSString* text) {
    NSTextField* n = [NSTextField wrappingLabelWithString:text];
    n.textColor = NSColor.secondaryLabelColor;
    n.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    n.preferredMaxLayoutWidth = 520;
    [n setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                 forOrientation:NSLayoutConstraintOrientationHorizontal];
    return n;
}

NSStackView* column(NSArray<NSView*>* views) {
    NSStackView* s = [NSStackView stackViewWithViews:views];
    s.orientation = NSUserInterfaceLayoutOrientationVertical;
    s.alignment = NSLayoutAttributeLeading;
    s.spacing = 12;
    s.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    return s;
}

} // namespace

@interface FooUIPanelsPrefsController : NSViewController <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@end

@implementation FooUIPanelsPrefsController {
    std::unique_ptr<pui::PrefsModel> _model;
    std::vector<std::pair<std::string, std::string>> _vars; // the table's rows
    // General
    NSTextField* _rootField;
    NSPopUpButton* _skinPopup;
    NSPopUpButton* _mainPopup;
    NSTextField* _warning;
    NSImageView* _preview;
    NSTextField* _previewNote;
    NSPopUpButton* _zoomPopup;
    NSButton* _onTopCheck;
    // Script
    NSTextView* _scriptView;
    NSTextField* _scriptPathLabel;
    // Variables
    NSTableView* _varsTable;
    // Overrides
    NSTextField* _fontFace;
    NSTextField* _fontSize;
    NSColorWell* _accentWell;
}

- (void)loadView {
    _model = std::make_unique<pui::PrefsModel>(pui::prefs_backend());
    _model->load();

    NSTabView* tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 640, 440)];
    for (NSArray* t in @[ @[ @"General", [self generalTab] ], @[ @"Script", [self scriptTab] ],
                          @[ @"Variables", [self variablesTab] ], @[ @"Overrides", [self overridesTab] ] ]) {
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:t[0]];
        item.label = t[0];
        item.view = t[1];
        [tabs addTabViewItem:item];
    }
    self.view = tabs;
    [self refresh];
}

// --- tabs ------------------------------------------------------------------------------------
- (NSView*)generalTab {
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
    _warning = note(@"");
    _warning.textColor = NSColor.systemOrangeColor;

    _zoomPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _zoomPopup.target = self; _zoomPopup.action = @selector(zoomChanged:);
    [_zoomPopup addItemWithTitle:@"Automatic (100%)"];
    for (int z : pui::PrefsModel::zoom_choices()) [_zoomPopup addItemWithTitle:[NSString stringWithFormat:@"%d%%", z]];
    _onTopCheck = [NSButton checkboxWithTitle:@"Keep the player window on top of other windows"
                                       target:self action:@selector(onTopChanged:)];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ label(@"Skins root folder:"), rootRow ],
        @[ label(@"Active skin:"), _skinPopup ],
        @[ label(@"Main script:"), _mainPopup ],
        @[ [NSGridCell emptyContentView], _warning ],
        @[ label(@"Zoom:"), _zoomPopup ],
        @[ [NSGridCell emptyContentView], _onTopCheck ],
    ]];
    grid.rowSpacing = 10;
    grid.columnSpacing = 8;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    for (NSInteger r = 0; r < grid.numberOfRows; ++r) [grid rowAtIndex:r].yPlacement = NSGridCellPlacementCenter;

    _preview = [NSImageView imageViewWithImage:[NSImage new]];
    _preview.imageScaling = NSImageScaleProportionallyUpOrDown;
    _preview.wantsLayer = YES;
    _preview.layer.backgroundColor = [NSColor colorWithWhite:0.12 alpha:1].CGColor;
    [_preview.widthAnchor constraintEqualToConstant:240].active = YES;
    [_preview.heightAnchor constraintEqualToConstant:180].active = YES;
    _previewNote = note(@"No preview yet: a skin gets one the first time it is shown.");
    NSStackView* previewCol = [NSStackView stackViewWithViews:@[ _preview, _previewNote ]];
    previewCol.orientation = NSUserInterfaceLayoutOrientationVertical;
    previewCol.alignment = NSLayoutAttributeLeading;
    [_previewNote.widthAnchor constraintEqualToConstant:240].active = YES;

    NSStackView* top = [NSStackView stackViewWithViews:@[ grid, previewCol ]];
    top.alignment = NSLayoutAttributeTop;
    top.spacing = 16;
    return column(@[ top, note(@"The skins root folder holds one subfolder per skin. Leave it empty to use the single "
                                @"skin in ~/Library/foobar2000-v2/foo_ui_panels. Main script: the skin's top-level "
                                @".txt to run (automatic when there is only one).") ]);
}

- (NSView*)scriptTab {
    NSScrollView* scroll = [NSTextView scrollableTextView];
    _scriptView = (NSTextView*)scroll.documentView;
    _scriptView.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    _scriptView.automaticQuoteSubstitutionEnabled = NO;
    _scriptView.automaticDashSubstitutionEnabled = NO;
    _scriptView.automaticTextReplacementEnabled = NO;
    _scriptView.richText = NO;
    _scriptView.allowsUndo = YES;
    [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    _scriptPathLabel = note(@"");
    NSButton* save = [NSButton buttonWithTitle:@"Save" target:self action:@selector(saveScript:)];
    save.keyEquivalent = @"s";
    save.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    NSButton* revert = [NSButton buttonWithTitle:@"Revert" target:self action:@selector(revertScript:)];
    NSStackView* buttons = [NSStackView stackViewWithViews:@[ save, revert, _scriptPathLabel ]];
    NSStackView* page = column(@[ scroll, buttons ]);
    [scroll.widthAnchor constraintEqualToAnchor:page.widthAnchor constant:-32].active = YES;
    return page;
}

- (NSView*)variablesTab {
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _varsTable = [[NSTableView alloc] initWithFrame:NSZeroRect];
    for (NSString* name in @[ @"Variable", @"Value" ]) {
        NSTableColumn* c = [[NSTableColumn alloc] initWithIdentifier:name];
        c.title = name;
        c.width = [name isEqualToString:@"Value"] ? 300 : 200;
        [_varsTable addTableColumn:c];
    }
    _varsTable.dataSource = self;
    _varsTable.delegate = self;
    _varsTable.usesAlternatingRowBackgroundColors = YES;
    scroll.documentView = _varsTable;
    scroll.hasVerticalScroller = YES;
    [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    NSButton* find = [NSButton buttonWithTitle:@"Find the skin's variables" target:self action:@selector(rescanVars:)];
    NSStackView* page = column(@[ note(@"Persistent variables ($getpvar/$setpvar) the skin uses. Double-click a value to edit it."),
                                  scroll, find ]);
    [scroll.widthAnchor constraintEqualToAnchor:page.widthAnchor constant:-32].active = YES;
    return page;
}

- (NSView*)overridesTab {
    _fontFace = [NSTextField textFieldWithString:@""];
    _fontFace.placeholderString = @"the skin's";
    _fontFace.delegate = self;
    [_fontFace.widthAnchor constraintEqualToConstant:200].active = YES;
    _fontSize = [NSTextField textFieldWithString:@""];
    _fontSize.delegate = self;
    [_fontSize.widthAnchor constraintEqualToConstant:50].active = YES;
    NSStackView* fontRow = [NSStackView stackViewWithViews:@[ _fontFace, [NSTextField labelWithString:@"Size:"], _fontSize ]];

    _accentWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 44, 24)];
    _accentWell.target = self; _accentWell.action = @selector(accentChanged:);
    NSButton* clearAccent = [NSButton buttonWithTitle:@"Use the skin's" target:self action:@selector(clearAccent:)];
    NSStackView* accentRow = [NSStackView stackViewWithViews:@[ _accentWell, clearAccent ]];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ label(@"Font face:"), fontRow ],
        @[ label(@"Accent colour:"), accentRow ],
    ]];
    grid.rowSpacing = 10;
    grid.columnSpacing = 8;
    NSButton* clear = [NSButton buttonWithTitle:@"Clear overrides" target:self action:@selector(clearOverrides:)];
    return column(@[ note(@"Fallbacks used when the active skin doesn't set its own font / accent colour. "
                          @"Leave blank to defer to the skin."), grid, clear ]);
}

// --- model -> UI -------------------------------------------------------------------------------
- (void)refresh {
    const pui::PrefsSettings& s = _model->pending();
    _rootField.stringValue = s.root.empty() ? @"(none: the component's own folder)" : ns(s.root);

    [_skinPopup removeAllItems];
    for (auto& name : _model->skins()) [_skinPopup addItemWithTitle:ns(name)];
    if (!s.active.empty()) [_skinPopup selectItemWithTitle:ns(s.active)];
    _skinPopup.enabled = !_model->skins().empty();

    [_mainPopup removeAllItems];
    [_mainPopup addItemWithTitle:@"Automatic"];
    for (auto& c : _model->main_choices()) [_mainPopup addItemWithTitle:ns(c)];
    if (s.main.empty() || ![_mainPopup itemWithTitle:ns(s.main)]) [_mainPopup selectItemAtIndex:0];
    else [_mainPopup selectItemWithTitle:ns(s.main)];
    _warning.stringValue = ns(_model->skin_warning());

    const std::string preview = _model->preview_image();
    NSImage* img = preview.empty() ? nil : [[NSImage alloc] initWithContentsOfFile:ns(preview)];
    _preview.image = img ?: [NSImage new];
    _previewNote.hidden = img != nil;

    [_zoomPopup selectItemAtIndex:_model->zoom_index()];
    _onTopCheck.state = s.onTop ? NSControlStateValueOn : NSControlStateValueOff;

    _scriptView.string = ns(_model->script());
    _scriptPathLabel.stringValue = ns(_model->script_path());

    _vars = _model->variables();
    [_varsTable reloadData];

    _fontFace.stringValue = ns(_model->font_face());
    _fontSize.stringValue = ns(_model->font_size());
    pui::gfx::Color c(0, 140, 220); // the engine's default accent
    _model->accent(c);
    _accentWell.color = [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:1];
}

// Every change applies right away.
- (void)commit {
    _model->apply();
    [self refresh];
}

// --- General -----------------------------------------------------------------------------------
- (void)chooseRoot:(id)sender {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.canChooseDirectories = YES;
    p.canChooseFiles = NO;
    p.allowsMultipleSelection = NO;
    p.message = @"Folder with one subfolder per skin";
    const std::string cur = _model->pending().root;
    if (!cur.empty()) p.directoryURL = [NSURL fileURLWithPath:ns(cur)];
    [p beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse res) {
        if (res != NSModalResponseOK || !p.URL.fileSystemRepresentation) return;
        self->_model->set_root(p.URL.fileSystemRepresentation);
        [self commit];
    }];
}

- (void)clearRoot:(id)sender { _model->set_root(""); [self commit]; }
- (void)skinChanged:(id)sender { _model->set_active(utf8(_skinPopup.titleOfSelectedItem)); [self commit]; }
- (void)mainChanged:(id)sender {
    _model->set_main(_mainPopup.indexOfSelectedItem <= 0 ? std::string() : utf8(_mainPopup.titleOfSelectedItem));
    [self commit];
}
- (void)zoomChanged:(id)sender { _model->set_zoom_index((int)_zoomPopup.indexOfSelectedItem); [self commit]; }
- (void)onTopChanged:(id)sender { _model->set_on_top(_onTopCheck.state == NSControlStateValueOn); [self commit]; }

// --- Script ------------------------------------------------------------------------------------
- (void)saveScript:(id)sender { _model->set_script(utf8(_scriptView.string)); [self commit]; }
- (void)revertScript:(id)sender { _scriptView.string = ns(_model->script()); }

// --- Variables ---------------------------------------------------------------------------------
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tv { return (NSInteger)_vars.size(); }

- (id)tableView:(NSTableView*)tv objectValueForTableColumn:(NSTableColumn*)col row:(NSInteger)row {
    if (row < 0 || (size_t)row >= _vars.size()) return nil;
    return ns([col.identifier isEqualToString:@"Value"] ? _vars[(size_t)row].second : _vars[(size_t)row].first);
}

- (BOOL)tableView:(NSTableView*)tv shouldEditTableColumn:(NSTableColumn*)col row:(NSInteger)row {
    return [col.identifier isEqualToString:@"Value"];
}

- (void)tableView:(NSTableView*)tv setObjectValue:(id)value forTableColumn:(NSTableColumn*)col row:(NSInteger)row {
    if (row < 0 || (size_t)row >= _vars.size() || ![value isKindOfClass:NSString.class]) return;
    _model->set_variable(_vars[(size_t)row].first, utf8(value));
    [self commit];
}

- (void)rescanVars:(id)sender { _model->rescan_variables(); [self commit]; }

// --- Overrides ---------------------------------------------------------------------------------
- (void)controlTextDidEndEditing:(NSNotification*)n {
    if (n.object != _fontFace && n.object != _fontSize) return;
    _model->set_font(utf8(_fontFace.stringValue), utf8(_fontSize.stringValue));
    [self commit];
}

- (void)accentChanged:(id)sender {
    NSColor* c = [_accentWell.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!c) return;
    _model->set_accent(pui::gfx::Color((int)std::lround(c.redComponent * 255), (int)std::lround(c.greenComponent * 255),
                                       (int)std::lround(c.blueComponent * 255)));
    _model->apply(); // no refresh: the well is still being dragged
}

- (void)clearAccent:(id)sender { _model->clear_accent(); [self commit]; }
- (void)clearOverrides:(id)sender { _model->clear_overrides(); [self commit]; }

@end

namespace {

class preferences_page_panels : public preferences_page {
public:
    service_ptr instantiate() override { return fb2k::wrapNSObject([FooUIPanelsPrefsController new]); }
    const char* get_name() override { return "Panels UI (reborn)"; }
    GUID get_guid() override { return pui::prefs_page_guid(); }
    GUID get_parent_guid() override { return preferences_page::guid_display; }
};

FB2K_SERVICE_FACTORY(preferences_page_panels);

} // namespace
