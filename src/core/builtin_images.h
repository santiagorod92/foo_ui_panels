#pragma once
#include <cstddef>
#include <string>

namespace pui {

constexpr const char* kBuiltinImagePrefix = "builtin:";

struct BuiltinImage { const char* name; const unsigned char* data; size_t size; };

const BuiltinImage* find_builtin_image(const std::string& name);

}
