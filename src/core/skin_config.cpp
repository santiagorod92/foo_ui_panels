#include "skin_config.h"
#include "fs_util.h"
#include <algorithm>
#include <cstdlib>

namespace pui {

namespace {
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool is_file(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(fs_path(path), ec);
}
} // namespace

void SkinConfig::load(const std::string& dir) {
    m_values.clear();
    std::string text = read_file(dir + "/" + kFileName);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3); // UTF-8 BOM
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (!key.empty()) m_values[key] = trim(line.substr(eq + 1));
    }
}

std::string SkinConfig::str(const std::string& key, const std::string& def) const {
    auto it = m_values.find(key);
    return it == m_values.end() ? def : it->second;
}

int SkinConfig::num(const std::string& key, int def) const {
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second.empty()) return def;
    char* end = nullptr;
    long v = strtol(it->second.c_str(), &end, 10);
    return end == it->second.c_str() ? def : (int)v;
}

std::vector<int> SkinConfig::nums(const std::string& key) const {
    std::vector<int> out;
    const std::string s = str(key);
    const char* p = s.c_str();
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) return {};
        out.push_back((int)v);
        p = end;
    }
    return out;
}

std::map<std::string, std::string> SkinConfig::with_prefix(const std::string& prefix) const {
    std::map<std::string, std::string> out;
    for (auto it = m_values.lower_bound(prefix); it != m_values.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        out[it->first.substr(prefix.size())] = it->second;
    }
    return out;
}

std::vector<std::string> main_script_candidates(const std::string& dir) {
    std::vector<std::string> found;
    std::error_code ec;
    std::filesystem::directory_iterator it(fs_path(dir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && it->path().extension() == ".txt")
            found.push_back(fs_utf8(it->path().filename()));
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string resolve_main_script_with(const std::string& dir, const SkinConfig& cfg,
                                     const std::string& over, std::string* why) {
    if (!over.empty()) {
        if (is_file(dir + "/" + over)) return dir + "/" + over;
        if (why) *why = "main script '" + over + "' (Preferences) not found in the skin folder";
    }
    const std::string named = cfg.str("script");
    if (!named.empty()) {
        if (is_file(dir + "/" + named)) return dir + "/" + named;
        if (why) *why = "main script '" + named + "' (" + SkinConfig::kFileName + ") not found";
    }
    // Automatic: the only .txt in the folder's root (per-panel scripts live in a subfolder).
    const std::vector<std::string> found = main_script_candidates(dir);
    if (found.size() == 1) return dir + "/" + found[0];
    if (why && why->empty()) {
        if (found.empty()) *why = "no main script (*.txt) in the skin folder";
        else *why = std::to_string(found.size()) + " *.txt files in the skin folder; pick the main one in "
                    "Preferences or with `script =` in " + SkinConfig::kFileName;
    }
    return {};
}

} // namespace pui
