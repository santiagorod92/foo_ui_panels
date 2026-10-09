// Images built into the component (res/**, embedded at configure time by cmake/embed_images.cmake
// into <build>/generated/builtin_images.cpp) — e.g. the layout wizard's previews. A script draws
// one as "builtin:<path under res/>" ($imageabs(…,builtin:wizard/library.png)); image_cache
// decodes it from memory. SDK-free.
#pragma once
#include <cstddef>
#include <string>

namespace pui {

constexpr const char* kBuiltinImagePrefix = "builtin:";

struct BuiltinImage { const char* name; const unsigned char* data; size_t size; };

// The embedded file at `name` (path under res/, '/'-separated), or nullptr.
const BuiltinImage* find_builtin_image(const std::string& name);

} // namespace pui
