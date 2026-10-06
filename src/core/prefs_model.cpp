#include "prefs_model.h"
#include "fs_util.h"
#include "script_util.h"
#include "skin_config.h"
#include <algorithm>
#include <set>

namespace pui {

void PrefsModel::load() {
    m_now = m_applied = m_b.load();
    refresh_skins();
    load_script();
}

bool PrefsModel::changed() const {
    return !(m_now == m_applied) || m_script != m_appliedScript;
}

void PrefsModel::apply() {
    if (!m_scriptPath.empty() && m_script != m_appliedScript) write_file(m_scriptPath, m_script);
    m_appliedScript = m_script;
    const bool skinChanged = m_now.root != m_applied.root || m_now.active != m_applied.active || m_now.main != m_applied.main;
    m_b.store(m_now, m_applied);
    m_applied = m_now;
    if (skinChanged) load_script(); // the file the Script tab edits may be another one now
}

void PrefsModel::reset() {
    m_now.root.clear();
    m_now.active.clear();
    m_now.main.clear();
    m_now.zoom = 0;
    m_now.onTop = false;
    clear_overrides();
    refresh_skins();
    load_script();
}

// --- skin --------------------------------------------------------------------------------
void PrefsModel::refresh_skins() { m_skins = m_b.list_skins(m_now.root); }

void PrefsModel::set_root(const std::string& root) {
    m_now.root = root;
    refresh_skins();
    if (std::find(m_skins.begin(), m_skins.end(), m_now.active) == m_skins.end())
        m_now.active = m_skins.empty() ? std::string() : m_skins.front();
    set_main("");
}

void PrefsModel::set_active(const std::string& skin) {
    m_now.active = skin;
    set_main(""); // file names belong to the previous skin
}

void PrefsModel::set_main(const std::string& file) {
    m_now.main = file;
    load_script();
}

void PrefsModel::choose_skin_folder(const std::string& folderIn) {
    std::string folder = folderIn;
    while (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\')) folder.pop_back();
    std::error_code ec;
    const bool isSkin = !main_script_candidates(folder).empty() ||
                        std::filesystem::is_regular_file(fs_path(folder + "/" + SkinConfig::kFileName), ec);
    if (!isSkin) { set_root(folder); return; }
    const std::filesystem::path p = fs_path(folder);
    m_now.root = fs_utf8(p.parent_path());
    refresh_skins();
    set_active(fs_utf8(p.filename()));
}

std::string PrefsModel::skin_dir() const { return m_b.skin_dir(m_now.root, m_now.active); }

std::vector<std::string> PrefsModel::main_choices() const { return main_script_candidates(skin_dir()); }

std::string PrefsModel::root_note() const {
    return "The skins root folder holds one subfolder per skin. Leave it empty to use the single skin in " +
           m_b.skin_dir("", "") + ". Main script: the skin's top-level .txt to run (automatic when there is only one).";
}

// Index i of a picker whose entry 0 means "" and entries 1.. are `values`.
static int index_in(const std::vector<std::string>& values, const std::string& v) {
    if (v.empty()) return 0;
    auto it = std::find(values.begin(), values.end(), v);
    return it == values.end() ? 0 : (int)(it - values.begin()) + 1;
}
static std::string value_at(const std::vector<std::string>& values, int i) {
    return i <= 0 || i > (int)values.size() ? std::string() : values[(size_t)i - 1];
}
static std::vector<std::string> with_first(const char* first, const std::vector<std::string>& rest) {
    std::vector<std::string> v{ first };
    v.insert(v.end(), rest.begin(), rest.end());
    return v;
}

std::vector<std::string> PrefsModel::skin_labels() const { return with_first(prefs_text::kOwnFolder, m_skins); }
int PrefsModel::skin_index() const { return index_in(m_skins, m_now.active); }
void PrefsModel::set_skin_index(int i) { set_active(value_at(m_skins, i)); }
std::vector<std::string> PrefsModel::main_labels() const { return with_first(prefs_text::kAutomatic, main_choices()); }
int PrefsModel::main_index() const { return index_in(main_choices(), m_now.main); }
void PrefsModel::set_main_index(int i) { set_main(value_at(main_choices(), i)); }

std::string PrefsModel::main_script(std::string* why) const {
    const std::string dir = skin_dir();
    SkinConfig cfg;
    cfg.load(dir);
    return resolve_main_script_with(dir, cfg, m_now.main, why);
}

std::string PrefsModel::skin_warning() const {
    std::string why;
    if (main_script(&why).empty()) return why.empty() ? "No main script found in this folder." : why;
    return why;
}

std::string PrefsModel::skin_preview_file(const std::string& dir) {
    std::error_code ec;
    auto exists = [&](const std::string& p) { return std::filesystem::is_regular_file(fs_path(p), ec); };
    SkinConfig cfg;
    cfg.load(dir);
    std::string own = cfg.str("preview");
    for (auto& c : own) if (c == '\\') c = '/';
    if (!own.empty() && exists(dir + "/" + own)) return dir + "/" + own;
    for (const char* name : { "preview.png", "preview.jpg", "screenshot.png", "screenshot.jpg" })
        if (exists(dir + "/" + name)) return dir + "/" + name;
    return {};
}

std::string PrefsModel::preview_image() const {
    const std::string dir = skin_dir();
    std::string own = skin_preview_file(dir);
    return own.empty() ? m_b.cached_preview(dir) : own;
}

void PrefsModel::load_script() {
    m_scriptPath = main_script();
    m_script = m_appliedScript = m_scriptPath.empty() ? std::string() : read_file(m_scriptPath);
}

// --- window ------------------------------------------------------------------------------
const std::vector<int>& PrefsModel::zoom_choices() {
    static const std::vector<int> z = { 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };
    return z;
}

std::vector<std::string> PrefsModel::zoom_labels(const std::string& automatic) {
    std::vector<std::string> v{ automatic };
    for (int z : zoom_choices()) v.push_back(std::to_string(z) + "%");
    return v;
}

int PrefsModel::zoom_index() const {
    int sel = 0;
    const auto& z = zoom_choices();
    for (size_t i = 0; m_now.zoom > 0 && i < z.size(); ++i) if (z[i] <= m_now.zoom) sel = (int)i + 1;
    return sel;
}

void PrefsModel::set_zoom_index(int i) {
    const auto& z = zoom_choices();
    m_now.zoom = (i <= 0 || i > (int)z.size()) ? 0 : z[(size_t)i - 1];
}

// --- overrides ---------------------------------------------------------------------------
void PrefsModel::set_font(const std::string& face, const std::string& size) {
    auto put = [&](const char* k, const std::string& v) { if (v.empty()) m_now.pvars.erase(k); else m_now.pvars[k] = v; };
    put(kFontFaceKey, face);
    put(kFontSizeKey, size);
}

bool PrefsModel::accent(gfx::Color& out) const {
    const std::string v = get(kAccentKey);
    if (v.empty()) return false;
    out = parse_rgb(v.c_str());
    return true;
}

void PrefsModel::set_accent(gfx::Color c) {
    m_now.pvars[kAccentKey] = std::to_string(c.r) + "-" + std::to_string(c.g) + "-" + std::to_string(c.b);
}

void PrefsModel::clear_accent() { m_now.pvars.erase(kAccentKey); }

void PrefsModel::clear_overrides() {
    for (const char* k : { kFontFaceKey, kFontSizeKey, kAccentKey }) m_now.pvars.erase(k);
}

// --- variables ---------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> PrefsModel::variables() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& [k, v] : m_now.pvars)
        if (!k.empty() && k[0] != '_') out.emplace_back(k, v); // "_…": the component's own bookkeeping
    return out;
}

void PrefsModel::set_variable(const std::string& key, const std::string& value) {
    if (!key.empty() && !is_reserved(key)) m_now.pvars[key] = value;
}

std::vector<std::string> PrefsModel::scan_pvar_names(const std::string& script) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const char* fn : { "$getpvar(", "$setpvar(", "PVAR:SET:", "PVAR:TOGGLE:" }) {
        const std::string needle = fn;
        const bool action = needle[0] == 'P';
        for (size_t pos = 0; (pos = script.find(needle, pos)) != std::string::npos;) {
            size_t start = pos + needle.size(), end = start;
            while (end < script.size() && script[end] != ',' && script[end] != ')' &&
                   !(action && (script[end] == ':' || script[end] == '\''))) ++end;
            std::string name = script.substr(start, end - start);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\'')) name.erase(name.begin());
            while (!name.empty() && (name.back() == ' ' || name.back() == '\'')) name.pop_back();
            if (!name.empty() && name.find_first_of("$%") == std::string::npos && seen.insert(name).second)
                names.push_back(name);
            pos = end;
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void PrefsModel::rescan_variables() {
    std::string all = m_script;
    const std::string dir = skin_dir();
    SkinConfig cfg;
    cfg.load(dir);
    std::error_code ec;
    std::filesystem::directory_iterator it(fs_path(dir + "/" + cfg.str("panels", "panels")), ec), end;
    for (; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".txt") all += read_file(fs_utf8(it->path()));
    for (const std::string& name : scan_pvar_names(all))
        if (!is_reserved(name) && !m_now.pvars.count(name)) m_now.pvars[name] = "";
}

} // namespace pui
