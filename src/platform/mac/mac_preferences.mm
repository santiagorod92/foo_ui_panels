#import <Cocoa/Cocoa.h>
#include "../../fb2k.h"
#include "../../core/prefs_model.h"
#include "../../core/prefs_store.h"
#include "../../core/skin_paths.h"
#include "../../core/diagnostics.h"
#include "../../ui/view.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
NSString* ns(const char* s) { return [NSString stringWithUTF8String:s] ?: @""; }
namespace text = pui::prefs_text;

void fill_popup(NSPopUpButton* p, const std::vector<std::string>& labels, int sel) {
    [p removeAllItems];
    for (auto& l : labels) [p addItemWithTitle:ns(l)];
    if (sel >= 0 && sel < p.numberOfItems) [p selectItemAtIndex:sel];
}
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

}

@interface FooUIPanelsPrefsController : NSViewController <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@end

@implementation FooUIPanelsPrefsController {
    std::unique_ptr<pui::PrefsModel> _model;
    std::vector<std::pair<std::string, std::string>> _vars;
    NSTextField* _rootField;
    NSPopUpButton* _skinPopup;
    NSPopUpButton* _mainPopup;
    NSTextField* _warning;
    NSImageView* _preview;
    NSTextField* _previewNote;
    NSPopUpButton* _zoomPopup;
    NSButton* _onTopCheck;
    NSTextField* _rootNote;
    NSTextView* _scriptView;
    NSTextField* _scriptPathLabel;
    NSTableView* _varsTable;
    NSTextField* _fontFace;
    NSTextField* _fontSize;
    NSColorWell* _accentWell;
    NSButton* _verboseCheck;
    NSTextField* _diagStatus;
    NSTextField* _logPath;
}

- (void)loadView {
    _model = std::make_unique<pui::PrefsModel>(pui::prefs_backend());
    _model->load();

    NSTabView* tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 640, 440)];
    for (NSArray* t in @[ @[ ns(text::kTabs[0]), [self generalTab] ], @[ ns(text::kTabs[1]), [self scriptTab] ],
                          @[ ns(text::kTabs[2]), [self variablesTab] ], @[ ns(text::kTabs[3]), [self overridesTab] ],
                          @[ ns(text::kTabs[4]), [self diagnosticsTab] ] ]) {
        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:t[0]];
        item.label = t[0];
        item.view = t[1];
        [tabs addTabViewItem:item];
    }
    NSTextField* credit = [NSTextField labelWithString:
        [NSString stringWithFormat:@"%@\n%@", ns(text::kAuthor), ns(text::kSourceUrl)]];
    credit.textColor = [NSColor tertiaryLabelColor];
    credit.font = [NSFont systemFontOfSize:10];
    credit.selectable = YES;

    NSView* root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 480)];
    for (NSView* v in @[ tabs, credit ]) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [tabs.topAnchor constraintEqualToAnchor:root.topAnchor],
        [tabs.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [tabs.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [credit.topAnchor constraintEqualToAnchor:tabs.bottomAnchor constant:6],
        [credit.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:16],
        [credit.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-8],
    ]];
    self.view = root;
    [self refresh];
}

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
    fill_popup(_zoomPopup, pui::PrefsModel::zoom_labels(text::kZoomAuto100), 0);
    _onTopCheck = [NSButton checkboxWithTitle:ns(text::kOnTop) target:self action:@selector(onTopChanged:)];
    NSButton* wizardRow = [NSButton buttonWithTitle:ns(text::kWizard) target:self action:@selector(openWizard:)];
    wizardRow.toolTip = ns(text::kWizardNote);

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ label(ns(text::kRoot)), rootRow ],
        @[ label(ns(text::kSkin)), _skinPopup ],
        @[ label(ns(text::kMain)), _mainPopup ],
        @[ [NSGridCell emptyContentView], _warning ],
        @[ label(ns(text::kZoom)), _zoomPopup ],
        @[ [NSGridCell emptyContentView], _onTopCheck ],
        @[ [NSGridCell emptyContentView], wizardRow ],
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
    _previewNote = note(ns(text::kNoPreview));
    NSStackView* previewCol = [NSStackView stackViewWithViews:@[ _preview, _previewNote ]];
    previewCol.orientation = NSUserInterfaceLayoutOrientationVertical;
    previewCol.alignment = NSLayoutAttributeLeading;
    [_previewNote.widthAnchor constraintEqualToConstant:240].active = YES;

    NSStackView* top = [NSStackView stackViewWithViews:@[ grid, previewCol ]];
    top.alignment = NSLayoutAttributeTop;
    top.spacing = 16;
    _rootNote = note(@"");
    return column(@[ top, _rootNote ]);
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
        c.title = ns([name isEqualToString:@"Value"] ? text::kValue : text::kVariable);
        c.width = [name isEqualToString:@"Value"] ? 300 : 200;
        [_varsTable addTableColumn:c];
    }
    _varsTable.dataSource = self;
    _varsTable.delegate = self;
    _varsTable.usesAlternatingRowBackgroundColors = YES;
    scroll.documentView = _varsTable;
    scroll.hasVerticalScroller = YES;
    [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:300].active = YES;
    NSButton* find = [NSButton buttonWithTitle:ns(text::kVarsFind) target:self action:@selector(rescanVars:)];
    NSStackView* page = column(@[ note(ns(text::kVarsNote)), scroll, find ]);
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
    NSStackView* fontRow = [NSStackView stackViewWithViews:@[ _fontFace, [NSTextField labelWithString:ns(text::kFontSize)], _fontSize ]];

    _accentWell = [[NSColorWell alloc] initWithFrame:NSMakeRect(0, 0, 44, 24)];
    _accentWell.target = self; _accentWell.action = @selector(accentChanged:);
    NSButton* clearAccent = [NSButton buttonWithTitle:@"Use the skin's" target:self action:@selector(clearAccent:)];
    NSStackView* accentRow = [NSStackView stackViewWithViews:@[ _accentWell, clearAccent ]];

    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[ label(ns(text::kFontFace)), fontRow ],
        @[ label(ns(text::kAccent)), accentRow ],
    ]];
    grid.rowSpacing = 10;
    grid.columnSpacing = 8;
    NSButton* clear = [NSButton buttonWithTitle:ns(text::kClearOverrides) target:self action:@selector(clearOverrides:)];
    return column(@[ note(ns(text::kOverridesNote)), grid, clear ]);
}

- (NSView*)diagnosticsTab {
    _verboseCheck = [NSButton checkboxWithTitle:ns(text::kVerbose) target:self action:@selector(verboseChanged:)];
    NSButton* copy = [NSButton buttonWithTitle:ns(text::kCopyDiagnostics) target:self action:@selector(copyDiagnostics:)];
    NSButton* show = [NSButton buttonWithTitle:ns(text::kShowLog) target:self action:@selector(showLog:)];
    NSButton* report = [NSButton buttonWithTitle:ns(text::kReportIssue) target:self action:@selector(reportIssue:)];
    NSStackView* buttons = [NSStackView stackViewWithViews:@[ copy, show, report ]];
    _diagStatus = note(@"");
    _logPath = note(@"");
    _logPath.selectable = YES;
    return column(@[ note(ns(text::kDiagNote)), _verboseCheck, note(ns(text::kVerboseNote)), buttons, _diagStatus, _logPath ]);
}

- (void)refresh {
    const pui::PrefsSettings& s = _model->pending();
    _rootField.stringValue = s.root.empty() ? @"(none: the component's own folder)" : ns(s.root);

    fill_popup(_skinPopup, _model->skin_labels(), _model->skin_index());
    fill_popup(_mainPopup, _model->main_labels(), _model->main_index());
    _warning.stringValue = ns(_model->skin_warning());
    _rootNote.stringValue = ns(_model->root_note());

    const std::string preview = _model->preview_image();
    NSImage* img = preview.empty() ? nil : [[NSImage alloc] initWithContentsOfFile:ns(preview)];
    _preview.image = img ?: [NSImage new];
    _previewNote.hidden = img != nil;

    [_zoomPopup selectItemAtIndex:_model->zoom_index()];
    _onTopCheck.state = s.onTop ? NSControlStateValueOn : NSControlStateValueOff;
    _verboseCheck.state = s.verbose ? NSControlStateValueOn : NSControlStateValueOff;
    const std::string log = _model->log_path();
    _logPath.stringValue = ns(std::string(text::kLogFile) + " " + (log.empty() ? text::kNoLog : log));

    _scriptView.string = ns(_model->script());
    _scriptPathLabel.stringValue = ns(_model->script_path());

    _vars = _model->variables();
    [_varsTable reloadData];

    _fontFace.stringValue = ns(_model->font_face());
    _fontSize.stringValue = ns(_model->font_size());
    pui::gfx::Color c(0, 140, 220);
    _model->accent(c);
    _accentWell.color = [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:1];
}

- (void)commit {
    _model->apply();
    [self refresh];
}

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
- (void)skinChanged:(id)sender { _model->set_skin_index((int)_skinPopup.indexOfSelectedItem); [self commit]; }
- (void)mainChanged:(id)sender { _model->set_main_index((int)_mainPopup.indexOfSelectedItem); [self commit]; }
- (void)zoomChanged:(id)sender { _model->set_zoom_index((int)_zoomPopup.indexOfSelectedItem); [self commit]; }
- (void)onTopChanged:(id)sender { _model->set_on_top(_onTopCheck.state == NSControlStateValueOn); [self commit]; }
- (void)openWizard:(id)sender {
    if (pui::open_layout_wizard()) return;
    NSAlert* a = [NSAlert new];
    a.messageText = @"Panels UI";
    a.informativeText = ns(text::kWizardUnavailable);
    [a runModal];
}

- (void)verboseChanged:(id)sender { _model->set_verbose(_verboseCheck.state == NSControlStateValueOn); [self commit]; }
- (void)copyDiagnostics:(id)sender {
    const bool ok = pui::ui::copy_to_clipboard(_model->diagnostics());
    _diagStatus.stringValue = ns(ok ? text::kCopied : text::kCopyFailed);
}
- (void)showLog:(id)sender {
    const std::string log = _model->log_path();
    if (!log.empty()) pui::ui::reveal_in_file_manager(log);
}
- (void)reportIssue:(id)sender { pui::ui::open_url(pui::new_issue_url()); }

- (void)saveScript:(id)sender { _model->set_script(utf8(_scriptView.string)); [self commit]; }
- (void)revertScript:(id)sender { _scriptView.string = ns(_model->script()); }

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
    _model->apply();
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

}
