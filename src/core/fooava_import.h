#pragma once
#include <string>
#include <utility>
#include <vector>

namespace pui {

using FooavaFiles = std::vector<std::pair<std::string, std::string>>;

bool unpack_fooava_installer(const std::string& exe, FooavaFiles& out, std::string* error);
FooavaFiles read_fooava_folder(const std::string& dir);

struct FooavaSkin {
    std::string main, ini;
    FooavaFiles files;
    int panels = 0, popups = 0, images = 0;
};

FooavaSkin convert_fooava(const FooavaFiles& files, std::string* error);

}
