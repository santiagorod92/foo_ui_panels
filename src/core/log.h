#pragma once
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pui::log {

enum class Level { Info, Warn, Error, Note };

const char* level_name(Level l);

struct LocalTime { int year, month, day, hour, minute, second, ms; };
LocalTime local_now();
std::string format_date(const LocalTime& t);
std::string format_line(const LocalTime& t, Level level, const std::string& tag, unsigned thread,
                        const std::string& msg);

class LineRing {
public:
    explicit LineRing(std::size_t capacity) : m_cap(capacity) {}
    void push(std::string line);
    std::vector<std::string> lines() const { return { m_lines.begin(), m_lines.end() }; }
private:
    std::size_t m_cap;
    std::deque<std::string> m_lines;
};

class Logger {
public:
    static constexpr std::size_t kRingLines = 400;
    static constexpr long kMaxBytes = 2L * 1024 * 1024;

    static Logger& get();

    void configure(const std::string& path, long maxBytes = kMaxBytes);
    void set_verbose(bool on);
    bool verbose() const;
    void set_console(std::function<void(const std::string&)> sink);
    std::string path() const;
    std::vector<std::string> recent() const;

    void write(Level level, const std::string& tag, const std::string& msg, bool console = false);

    void reset_for_tests();

private:
    Logger() = default;
    void emit(const std::string& text, bool toRing);
    void append(const std::string& text);
    void rotate_if_needed(bool startup);

    mutable std::mutex m_mu;
    std::string m_path;
    long m_maxBytes = kMaxBytes;
    bool m_verbose = false;
    std::function<void(const std::string&)> m_console;
    std::string m_date;
    LineRing m_ring{ kRingLines };
    std::deque<std::string> m_pending;
};

inline void info(const std::string& tag, const std::string& msg) { Logger::get().write(Level::Info, tag, msg); }
inline void warn(const std::string& tag, const std::string& msg) { Logger::get().write(Level::Warn, tag, msg); }
inline void error(const std::string& tag, const std::string& msg) { Logger::get().write(Level::Error, tag, msg); }
inline void note(const std::string& tag, const std::string& msg) { Logger::get().write(Level::Note, tag, msg); }
inline void console(Level level, const std::string& tag, const std::string& msg) {
    Logger::get().write(level, tag, msg, true);
}

template <class Fn>
bool guarded(const std::string& tag, const std::string& what, Fn&& fn) {
    try {
        std::forward<Fn>(fn)();
        return true;
    } catch (const std::exception& e) {
        error(tag, what + " failed: " + e.what());
    } catch (...) {
        error(tag, what + " failed: unknown exception");
    }
    return false;
}

}
