#pragma once
#include <map>
#include <string>

namespace pui {

struct PvarNameLess {
    bool operator()(const std::string& a, const std::string& b) const {
        const size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; ++i) {
            unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
            if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
            if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    }
};
using PvarMap = std::map<std::string, std::string, PvarNameLess>;

PvarMap parse_pvars(const std::string& blob);
std::string serialize_pvars(const PvarMap& pvars);

}
