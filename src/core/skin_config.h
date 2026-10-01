// Per-skin settings: foo_ui_panels.ini in the skin folder. The engine and the native panels are
// skin-agnostic; whatever one skin needs beyond its scripts — which file is the main script, where
// its art lives, which of its pvars hold the theme, layout touches for the native panels — is
// declared here, and a skin without the file gets neutral defaults. Keys: README.md, "Skin
// configuration". Format: `key = value` lines, `#` or `;` starts a comment line.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace pui {

class SkinConfig {
public:
    static constexpr const char* kFileName = "foo_ui_panels.ini";

    // Parses <dir>/foo_ui_panels.ini; a missing file leaves every key unset.
    void load(const std::string& dir);
    bool empty() const { return m_values.empty(); }

    std::string str(const std::string& key, const std::string& def = {}) const;
    int num(const std::string& key, int def) const;
    // Whitespace-separated numbers ("57 8 494 474"); empty if unset or malformed.
    std::vector<int> nums(const std::string& key) const;
    // Every key starting with `prefix`, with the prefix removed ("panel.remap." -> {name: value}).
    std::map<std::string, std::string> with_prefix(const std::string& prefix) const;

private:
    std::map<std::string, std::string> m_values;
};

// The skin's main script, as a path inside `dir` ("" = none found): the Preferences override
// if that file exists, else the config's `script`, else the only *.txt in the folder's root.
// `why` (optional) gets a one-line note for the console when the choice wasn't obvious.
std::string resolve_main_script(const std::string& dir, const SkinConfig& cfg, std::string* why = nullptr);
// Same, with an explicit override instead of the saved one (the Preferences page's pending edit).
std::string resolve_main_script_with(const std::string& dir, const SkinConfig& cfg,
                                     const std::string& over, std::string* why = nullptr);
// The *.txt files in the skin folder's root (main script candidates), sorted.
std::vector<std::string> main_script_candidates(const std::string& dir);

// Preferences override for the main script: a file name inside the skin folder, "" = automatic.
// Persisted, so these and resolve_main_script() live in skin_paths.cpp (skin_config.cpp has no
// SDK dependency — it's compiled into the unit tests).
std::string main_script_override();
void set_main_script_override(const std::string& name);

} // namespace pui
