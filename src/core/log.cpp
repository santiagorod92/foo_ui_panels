#include "log.h"
#include "fs_util.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

namespace pui::log {

const char* level_name(Level l) {
    switch (l) {
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
    case Level::Note: return "NOTE";
    }
    return "?";
}

LocalTime local_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    const int ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    return { lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, ms };
}

std::string format_date(const LocalTime& t) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", t.year, t.month, t.day);
    return buf;
}

std::string format_line(const LocalTime& t, Level level, const std::string& tag, unsigned thread,
                        const std::string& msg) {
    char head[80];
    std::snprintf(head, sizeof head, "%02d:%02d:%02d.%03d  %-5s  %-8s  [t%04u] ", t.hour, t.minute, t.second, t.ms,
                  level_name(level), tag.c_str(), thread % 10000u);
    return head + msg;
}

void LineRing::push(std::string line) {
    if (m_cap == 0) return;
    if (m_lines.size() == m_cap) m_lines.pop_front();
    m_lines.push_back(std::move(line));
}

namespace {
unsigned thread_tag() {
    static thread_local unsigned id = (unsigned)(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000u);
    return id;
}
}

Logger& Logger::get() {
    static Logger inst;
    return inst;
}

void Logger::configure(const std::string& path, long maxBytes) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_path = path;
    m_maxBytes = maxBytes > 0 ? maxBytes : kMaxBytes;
    rotate_if_needed(true);
    for (const auto& l : m_pending) append(l);
    m_pending.clear();
}

void Logger::set_verbose(bool on) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_verbose = on;
}

bool Logger::verbose() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_verbose;
}

void Logger::set_console(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_console = std::move(sink);
}

std::string Logger::path() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_path;
}

std::vector<std::string> Logger::recent() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_ring.lines();
}

void Logger::write(Level level, const std::string& tag, const std::string& msg, bool console) {
    std::function<void(const std::string&)> sink;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        const LocalTime now = local_now();
        const std::string date = format_date(now);
        const bool toFile = level != Level::Info || m_verbose;
        if (m_date.empty()) {
            char banner[96];
            std::snprintf(banner, sizeof banner, "==== foo_ui_panels session %s %02d:%02d:%02d ====", date.c_str(),
                          now.hour, now.minute, now.second);
            emit(std::string(), false);
            emit(banner, true);
        } else if (date != m_date) {
            emit("==== " + date + " ====", true);
        }
        m_date = date;
        const std::string line = format_line(now, level, tag, thread_tag(), msg);
        if (toFile) emit(line, true);
        else m_ring.push(line);
        if (console) sink = m_console;
    }
    if (sink) sink(msg);
}

void Logger::reset_for_tests() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_path.clear();
    m_maxBytes = kMaxBytes;
    m_verbose = false;
    m_console = nullptr;
    m_date.clear();
    m_ring = LineRing(kRingLines);
    m_pending.clear();
}

void Logger::emit(const std::string& text, bool toRing) {
    if (toRing) m_ring.push(text);
    if (m_path.empty()) {
        m_pending.push_back(text);
        if (m_pending.size() > kRingLines) m_pending.pop_front();
        return;
    }
    append(text);
}

void Logger::append(const std::string& text) {
    FILE* f = fs_open(m_path, "ab");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fputc('\n', f);
    const long size = std::ftell(f);
    std::fclose(f);
    if (size > m_maxBytes) rotate_if_needed(false);
}

void Logger::rotate_if_needed(bool startup) {
    if (m_path.empty()) return;
    std::error_code ec;
    const auto p = fs_path(m_path);
    if (startup) {
        const auto size = std::filesystem::file_size(p, ec);
        if (ec || (long long)size <= m_maxBytes) return;
    }
    const auto old = fs_path(m_path + ".1");
    std::filesystem::remove(old, ec);
    std::filesystem::rename(p, old, ec);
}

}
