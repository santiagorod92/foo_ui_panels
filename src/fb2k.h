// The one foobar2000 SDK include for platform-free code. On Windows the SDK needs <windows.h>
// in a specific order first (win_sdk.h); elsewhere it is self-contained. Platform-free files
// include this — never win_sdk.h / Cocoa headers — so they keep building on every platform.
#pragma once
#ifdef _WIN32
#include "platform/win/win_sdk.h"
#else
#include <foobar2000/SDK/foobar2000.h>
#endif

// An abort_callback_impl owned through a smart pointer (std::make_shared<shared_abort>()):
// abort_callback_impl has virtual functions but a non-virtual destructor, so deleting it
// directly warns (-Wdelete-non-abstract-non-virtual-dtor); a final subclass is safe to delete.
struct shared_abort final : abort_callback_impl {};
