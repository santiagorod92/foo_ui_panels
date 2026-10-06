// Persistent skin variables ($setpvar/$getpvar, PVAR:SET buttons) — the map type and its
// serialized form. No foobar2000 SDK here: the runtime, its tests and the Preferences pages all
// share it; where the blob is stored (a cfg_var) lives in skin_engine.cpp.
#pragma once
#include <map>
#include <string>

namespace pui {

// Panels UI matched pvar names without regard to case: fooAvA's settings popup writes
// PVAR:SET:hidetitlebar while its master script reads $getpvar(Hidetitlebar), and the toggle
// only works if those are the same variable. ASCII letters fold; other bytes compare as-is (pvar
// names are identifiers).
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

// The stored form: one "key=value" per line (a value may contain '=', not a newline).
PvarMap parse_pvars(const std::string& blob);
std::string serialize_pvars(const PvarMap& pvars);

} // namespace pui
