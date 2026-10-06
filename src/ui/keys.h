// Portable input codes shared by ui::View and the SDK-free panel logic (list_logic) — kept out of
// view.h so the unit tests can use them without the foobar2000 SDK.
#pragma once

namespace pui::ui {

enum Mods : unsigned { kShift = 1u << 0, kCtrl = 1u << 1, kAlt = 1u << 2 };
enum class MouseButton { Left, Right, Middle };
enum class Cursor { Arrow, Hand, IBeam };

// Portable key codes: printable keys use their upper-case ASCII value ('A', '5', ' ').
enum Key : int {
    kKeyUnknown = 0,
    kKeyEnter = 0x100, kKeyEscape, kKeyTab, kKeyBackspace, kKeyDelete,
    kKeyLeft, kKeyRight, kKeyUp, kKeyDown, kKeyPageUp, kKeyPageDown, kKeyHome, kKeyEnd,
    kKeyF1, kKeyF2, kKeyF3, kKeyF4, kKeyF5,
};

} // namespace pui::ui
