#pragma once
#include "recording_canvas.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace t {

class MiniTf : public FakeEnv {
public:
    std::map<std::string, std::string> fields;
    std::map<std::string, std::string> vars;

    MiniTf(std::vector<std::string>& log) : FakeEnv(log) {}

    void render(pui::ScriptRuntime& rt, const std::string& script) {
        m_rt = &rt;
        std::string buf;
        rt.set_text_buffer(&buf);
        eval(script, buf);
        rt.finish();
        m_rt = nullptr;
    }

    std::string eval(const std::string& s) override { std::string o; eval(s, o); return o; }
    void replay(pui::ScriptOut& out, const std::string& s) override { out.write(eval(s)); }
    void run_subscript(pui::ScriptRuntime& rt, const std::string& s) override {
        std::string buf;
        rt.set_text_buffer(&buf);
        eval(s, buf);
        rt.set_text_buffer(nullptr);
    }

private:
    pui::ScriptRuntime* m_rt = nullptr;

    bool eval(std::string_view code, std::string& out) {
        bool truth = false;
        for (size_t i = 0; i < code.size();) {
            const char c = code[i];
            if (c == '\r' || c == '\n') { ++i; continue; }
            if (c == '/' && code.compare(i, 2, "//") == 0 && (i == 0 || code[i - 1] == '\n' || code[i - 1] == '\r')) {
                const size_t e = code.find('\n', i);
                i = e == std::string_view::npos ? code.size() : e + 1;
                continue;
            }
            if (c == '\'') {
                const size_t e = code.find('\'', i + 1);
                if (e == std::string_view::npos) { out.append(code.substr(i + 1)); break; }
                if (e == i + 1) out += '\''; else out.append(code.substr(i + 1, e - i - 1));
                i = e + 1;
            } else if (c == '%') {
                const size_t e = code.find('%', i + 1);
                if (e == std::string_view::npos) { out.append(code.substr(i)); break; }
                std::string v;
                if (field(std::string(code.substr(i + 1, e - i - 1)), v)) truth = true; else v = "?";
                out += v;
                i = e + 1;
            } else if (c == '[') {
                const size_t e = match(code, i, '[', ']');
                std::string inner;
                if (eval(code.substr(i + 1, e - i - 1), inner)) { out += inner; truth = true; }
                i = e + 1;
            } else if (c == '$' && i + 1 < code.size() && is_name(code[i + 1])) {
                size_t n = i + 1;
                while (n < code.size() && is_name(code[n])) ++n;
                if (n >= code.size() || code[n] != '(') { out.append(code.substr(i, n - i)); i = n; continue; }
                const size_t e = match(code, n, '(', ')');
                if (call(std::string(code.substr(i + 1, n - i - 1)), split(code.substr(n + 1, e - n - 1)), out)) truth = true;
                i = e + 1;
            } else {
                out += c;
                ++i;
            }
        }
        return truth;
    }

    static bool is_name(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

    static size_t match(std::string_view s, size_t at, char open, char close) {
        int depth = 0;
        for (size_t i = at; i < s.size(); ++i) {
            if (s[i] == '\'') { const size_t e = s.find('\'', i + 1); if (e == std::string_view::npos) break; i = e; continue; }
            if (s[i] == open) ++depth;
            else if (s[i] == close && --depth == 0) return i;
        }
        return s.size();
    }
    static std::vector<std::string_view> split(std::string_view s) {
        std::vector<std::string_view> a;
        if (s.empty()) return a;
        int depth = 0; size_t from = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '\'') { const size_t e = s.find('\'', i + 1); if (e == std::string_view::npos) break; i = e; }
            else if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
            else if (c == ',' && depth == 0) { a.push_back(s.substr(from, i - from)); from = i + 1; }
        }
        a.push_back(s.substr(from));
        return a;
    }

    bool field(const std::string& name, std::string& v) {
        if (m_rt && (name == "_width" || name == "el_width")) { v = std::to_string(m_rt->width()); return true; }
        if (m_rt && (name == "_height" || name == "el_height")) { v = std::to_string(m_rt->height()); return true; }
        auto it = fields.find(name);
        if (it == fields.end()) return false;
        v = it->second;
        return true;
    }

    struct Arg { std::string s; bool truth; };
    Arg arg(std::string_view code) { Arg a; a.truth = eval(code, a.s); return a; }
    static long num(const std::string& s) { return std::strtol(s.c_str(), nullptr, 10); }

    bool call(const std::string& name, const std::vector<std::string_view>& raw, std::string& out) {
        auto A = [&](size_t i) { return i < raw.size() ? arg(raw[i]) : Arg{}; };
        auto branch = [&](size_t i) { return i < raw.size() ? eval(raw[i], out) : false; };
        auto put = [&](long v) { out += std::to_string(v); return true; };
        const size_t n = raw.size();
        if (name == "if")  return A(0).truth ? branch(1) : branch(2);
        if (name == "if2") { Arg a = A(0); if (a.truth) { out += a.s; return true; } return branch(1); }
        if (name == "if3") { for (size_t i = 0; i + 1 < n; ++i) { Arg a = A(i); if (a.truth) { out += a.s; return true; } } return branch(n - 1); }
        if (name == "ifequal")   return num(A(0).s) == num(A(1).s) ? branch(2) : branch(3);
        if (name == "ifgreater") return num(A(0).s) > num(A(1).s) ? branch(2) : branch(3);
        if (name == "greater")   return num(A(0).s) > num(A(1).s);
        if (name == "strcmp")    return A(0).s == A(1).s;
        if (name == "not")       return !A(0).truth;
        if (name == "and")       { for (size_t i = 0; i < n; ++i) if (!A(i).truth) return false; return true; }
        if (name == "or")        { for (size_t i = 0; i < n; ++i) if (A(i).truth) return true; return false; }
        if (name == "add" || name == "sub" || name == "mul" || name == "div" || name == "mod" || name == "max" || name == "min") {
            long v = num(A(0).s);
            for (size_t i = 1; i < n; ++i) {
                const long b = num(A(i).s);
                if (name == "add") v += b; else if (name == "sub") v -= b; else if (name == "mul") v *= b;
                else if (name == "div") v = b ? v / b : 0; else if (name == "mod") v = b ? v % b : 0;
                else if (name == "max") v = b > v ? b : v; else v = b < v ? b : v;
            }
            return put(v);
        }
        if (name == "len")   return put((long)A(0).s.size());
        if (name == "left")  { std::string s = A(0).s; out += s.substr(0, (size_t)std::max(0L, num(A(1).s))); return true; }
        if (name == "upper") { std::string s = A(0).s; for (auto& c : s) c = (char)toupper((unsigned char)c); out += s; return true; }
        if (name == "lower") { std::string s = A(0).s; for (auto& c : s) c = (char)tolower((unsigned char)c); out += s; return true; }
        if (name == "put")   { Arg v = A(1); vars[A(0).s] = v.s; out += v.s; return v.truth; }
        if (name == "puts")  { vars[A(0).s] = A(1).s; return true; }
        if (name == "get")   { auto it = vars.find(A(0).s); if (it == vars.end()) return false; out += it->second; return true; }
        if (m_rt && pui::find_script_function(name)) {
            pui::ScriptArgs a;
            for (auto& r : raw) a.v.push_back(arg(r).s);
            StringOut o;
            m_rt->call(name, a, o);
            out += o.text;
            return !o.text.empty();
        }
        out += "[UNKNOWN FUNCTION]";
        return false;
    }
};

}
