#pragma once
#ifdef _WIN32
#include "platform/win/win_sdk.h"
#else
#include <foobar2000/SDK/foobar2000.h>
#endif

struct shared_abort final : abort_callback_impl {};
