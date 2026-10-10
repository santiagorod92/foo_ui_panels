#pragma once
#include <map>
#include <string>
#include <vector>

namespace pui {

class SkinConfig {
public:
    static constexpr const char* kFileName = "foo_ui_panels.ini";

    void load(const std::string& dir);
    bool empty() const { return m_values.empty(); }

    std::string str(const std::string& key, const std::string& def = {}) const;
    int num(const std::string& key, int def) const;
    std::vector<int> nums(const std::string& key) const;
    std::map<std::string, std::string> with_prefix(const std::string& prefix) const;
    void set(const std::string& key, const std::string& value) { m_values[key] = value; }

private:
    std::map<std::string, std::string> m_values;
};

std::string resolve_main_script(const std::string& dir, const SkinConfig& cfg, std::string* why = nullptr);
std::string resolve_main_script_with(const std::string& dir, const SkinConfig& cfg,
                                     const std::string& over, std::string* why = nullptr);
std::vector<std::string> main_script_candidates(const std::string& dir);

std::string main_script_override();
void set_main_script_override(const std::string& name);

}
