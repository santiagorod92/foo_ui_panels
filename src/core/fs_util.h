// UTF-8 <-> std::filesystem::path, and small whole-file helpers. foobar2000 strings are UTF-8;
// on Windows a path built from a narrow std::string would be read in the ANSI code page, so
// always go through fs_path().
#pragma once
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace pui {

#ifdef _WIN32
constexpr char kPathSep = '\\';
#else
constexpr char kPathSep = '/';
#endif

// Milliseconds on a monotonic clock (for "hold until" style timeouts).
inline unsigned long long tick_ms() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline std::filesystem::path fs_path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

inline std::string fs_utf8(const std::filesystem::path& p) {
    std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

inline FILE* fs_open(const std::string& utf8, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + strlen(mode));
    return _wfopen(fs_path(utf8).c_str(), m.c_str());
#else
    return fopen(utf8.c_str(), mode);
#endif
}

// Whole file as bytes; empty string if it can't be read.
inline std::string read_file(const std::string& utf8) {
    FILE* f = fs_open(utf8, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) s.resize(fread(&s[0], 1, (size_t)n, f));
    fclose(f);
    return s;
}

inline bool write_file(const std::string& utf8, const std::string& data) {
    FILE* f = fs_open(utf8, "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

} // namespace pui
