// Canonical Windows + foobar2000 SDK include block. Order matters for the
// clang-cl cross-build: winsock2 before windows.h (WS1/WS2 clash), objbase for
// the COM `interface` macro the SDK uses, mmsystem for timeGetTime (pfc/timers.h).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <foobar2000/SDK/foobar2000.h> // SDK_ROOT is on the include path (CMakeLists.txt)
