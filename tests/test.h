#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({ n, std::move(f) }); } };

template <class T> std::string show(const T& v) { std::ostringstream o; o << v; return o.str(); }
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return v ? "\"" + std::string(v) + "\"" : "nullptr"; }
inline std::string show(std::nullptr_t) { return "nullptr"; }

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::fprintf(stderr, "  %s:%d: %s\n", file, line, what.c_str());
}

}

#define T_CAT2(a, b) a##b
#define T_CAT(a, b) T_CAT2(a, b)
#define TEST(name)                                                         \
    static void T_CAT(test_, __LINE__)();                                  \
    static t::Reg T_CAT(reg_, __LINE__)(name, T_CAT(test_, __LINE__));      \
    static void T_CAT(test_, __LINE__)()

#define CHECK(cond) \
    do { if (!(cond)) t::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        const auto& va_ = (a); const auto& vb_ = (b);                                      \
        if (!(va_ == vb_))                                                                 \
            t::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b "): " + t::show(va_) +      \
                                            " != " + t::show(vb_));                        \
    } while (0)
