#pragma once

namespace pui::ui {

enum Mods : unsigned { kShift = 1u << 0, kCtrl = 1u << 1, kAlt = 1u << 2 };
enum class MouseButton { Left, Right, Middle };
enum class Cursor { Arrow, Hand, IBeam };

enum Key : int {
    kKeyUnknown = 0,
    kKeyEnter = 0x100, kKeyEscape, kKeyTab, kKeyBackspace, kKeyDelete,
    kKeyLeft, kKeyRight, kKeyUp, kKeyDown, kKeyPageUp, kKeyPageDown, kKeyHome, kKeyEnd,
    kKeyF1, kKeyF2, kKeyF3, kKeyF4, kKeyF5,
};

}
