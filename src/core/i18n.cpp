#include "i18n.h"

#include <atomic>
#include <cctype>
#include <string_view>
#include <unordered_map>

namespace pui::i18n {

namespace {

constexpr int kUnresolved = -1;

std::atomic<int>      g_language{ kUnresolved };
std::atomic<Resolver> g_resolver{ nullptr };

const std::unordered_map<std::string_view, const Translation*>& translation_map() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const Translation*> m;
        for (const auto& row : translation_table()) m.emplace(row.english, &row);
        return m;
    }();
    return map;
}

const LanguageInfo* find_language(std::string_view code) {
    for (const auto& info : kLanguages)
        if (code == info.code) return &info;
    return nullptr;
}

}

Language parse_language_code(const std::string& code) {
    std::string primary;
    for (char c : code) {
        if (c == '-' || c == '_' || c == '.' || c == '@') break;
        primary.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    const LanguageInfo* info = find_language(primary);
    return info ? info->lang : Language::English;
}

const char* language_code(Language lang) {
    return kLanguages[static_cast<std::size_t>(lang)].code;
}

Language resolve_language(const std::string& setting, const std::string& systemCode) {
    if (const LanguageInfo* info = find_language(setting)) return info->lang;
    return parse_language_code(systemCode);
}

void set_resolver(Resolver resolver) { g_resolver.store(resolver); }

void set_language(Language lang) { g_language.store(static_cast<int>(lang)); }

Language language() {
    int lang = g_language.load();
    if (lang == kUnresolved) {
        const Resolver r = g_resolver.load();
        lang = static_cast<int>(r ? r() : Language::English);
        g_language.store(lang);
    }
    return static_cast<Language>(lang);
}

const char* translate(const char* english, Language lang) {
    if (!english || lang == Language::English) return english;
    const auto& map = translation_map();
    const auto it = map.find(english);
    if (it == map.end()) return english;
    const char* text = it->second->text[static_cast<std::size_t>(lang) - 1];
    return text && *text ? text : english;
}

const char* tr(const char* english) { return translate(english, language()); }

std::string tr(const std::string& english) { return tr(english.c_str()); }

std::string substitute(const std::string& pattern, std::initializer_list<std::string> args) {
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}'
            && std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (n < args.size()) {
                out += *(args.begin() + n);
                i += 2;
                continue;
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

const std::vector<LanguageOption>& language_options() {
    static const std::vector<LanguageOption> k = [] {
        std::vector<LanguageOption> v{ { N_("Automatic (system language)"), "" } };
        for (const auto& info : kLanguages) v.push_back({ info.native_name, info.code });
        return v;
    }();
    return k;
}

int language_option_index(const std::string& value) {
    const auto& opts = language_options();
    for (std::size_t i = 0; i < opts.size(); ++i)
        if (value == opts[i].value) return static_cast<int>(i);
    return 0;
}

std::string trf(const char* english, std::initializer_list<std::string> args) {
    return substitute(tr(english), args);
}

}
