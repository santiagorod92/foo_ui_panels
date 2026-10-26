#include "test.h"
#include "core/i18n.h"
#include "core/prefs_model.h"
#include "core/script_util.h"
#include <set>

using namespace pui;
using namespace pui::i18n;

namespace {
std::multiset<std::string> placeholders(const std::string& s) {
    std::multiset<std::string> out;
    for (size_t i = 0; i + 2 < s.size(); ++i)
        if (s[i] == '{' && s[i + 2] == '}' && s[i + 1] >= '0' && s[i + 1] <= '9') out.insert(s.substr(i, 3));
    return out;
}

struct LanguageScope {
    explicit LanguageScope(Language l) { set_language(l); }
    ~LanguageScope() { set_language(Language::English); }
};
}

TEST("i18n: language codes and the setting over the system language") {
    CHECK(parse_language_code("es") == Language::Spanish);
    CHECK(parse_language_code("es-AR") == Language::Spanish);
    CHECK(parse_language_code("es_ES.UTF-8") == Language::Spanish);
    CHECK(parse_language_code("ES") == Language::Spanish);
    CHECK(parse_language_code("pt-BR") == Language::Portuguese);
    CHECK(parse_language_code("zh-Hans-CN") == Language::Chinese);
    CHECK(parse_language_code("ja-JP") == Language::Japanese);
    CHECK(parse_language_code("fr-CA") == Language::French);
    CHECK(parse_language_code("de_DE@euro") == Language::German);
    CHECK(parse_language_code("ru") == Language::Russian);
    CHECK(parse_language_code("en-US") == Language::English);
    CHECK(parse_language_code("est") == Language::English);
    CHECK(parse_language_code("it-IT") == Language::English);
    CHECK(parse_language_code("") == Language::English);
    CHECK(resolve_language("", "es-MX") == Language::Spanish);
    CHECK(resolve_language("", "ja-JP") == Language::Japanese);
    CHECK(resolve_language("en", "es-MX") == Language::English);
    CHECK(resolve_language("es", "en-GB") == Language::Spanish);
    CHECK(resolve_language("xx", "pt-BR") == Language::Portuguese);
    for (const auto& info : kLanguages) {
        CHECK_EQ(std::string(language_code(info.lang)), std::string(info.code));
        CHECK(parse_language_code(info.code) == info.lang);
    }
}

TEST("i18n: English is the key, each language its own column, missing keys fall back") {
    set_language(Language::English);
    CHECK_EQ(std::string(tr("Reload skin")), std::string("Reload skin"));
    {
        LanguageScope es(Language::Spanish);
        CHECK_EQ(std::string(tr("Reload skin")), std::string("Recargar la skin"));
        CHECK_EQ(std::string(tr("No such key")), std::string("No such key"));
        CHECK_EQ(trf("Script problems in {0}:", { "main.txt" }), std::string("Problemas de script en main.txt:"));
    }
    {
        LanguageScope ja(Language::Japanese);
        CHECK_EQ(std::string(tr("Reload skin")), std::string("スキンを再読み込み"));
    }
    CHECK_EQ(std::string(translate("Reload skin", Language::Portuguese)), std::string("Recarregar skin"));
    CHECK_EQ(std::string(translate("Reload skin", Language::Chinese)), std::string("重新加载皮肤"));
    CHECK_EQ(std::string(translate("Reload skin", Language::German)), std::string("Skin neu laden"));
    CHECK_EQ(std::string(tr("Reload skin")), std::string("Reload skin"));
    CHECK_EQ(substitute("{1} of {0}, {2}", { "a", "b" }), std::string("b of a, {2}"));
    CHECK_EQ(substitute("$eval({%_width% - 1})", {}), std::string("$eval({%_width% - 1})"));
}

TEST("i18n: the translation table has no duplicates, empty entries or mismatched placeholders") {
    std::set<std::string> seen;
    for (const auto& row : translation_table()) {
        CHECK(seen.insert(row.english).second);
        for (const char* text : row.text) {
            CHECK(text && *text);
            if (text) CHECK(placeholders(row.english) == placeholders(text));
        }
    }
}

TEST("i18n: the language choice in Preferences") {
    struct NoBackend : PrefsBackend {
        PrefsSettings load() override { return {}; }
        void store(const PrefsSettings&, const PrefsSettings&) override {}
        std::vector<std::string> list_skins(const std::string&) override { return {}; }
        std::string skin_dir(const std::string&, const std::string&) override { return {}; }
    } b;
    PrefsModel m(b);
    CHECK_EQ(PrefsModel::language_labels().size(), language_options().size());
    CHECK_EQ(PrefsModel::language_labels().size(), kLanguages.size() + 1);
    CHECK_EQ(m.language_index(), 0);
    m.set_language_index(2);
    CHECK_EQ(m.pending().language, std::string("es"));
    CHECK_EQ(m.language_index(), 2);
    m.set_language_index(0);
    CHECK_EQ(m.pending().language, std::string());
}

TEST("i18n: tf_quote keeps commas and apostrophes literal in skin scripts") {
    CHECK_EQ(tf_quote("a, b"), std::string("'a, b'"));
    CHECK_EQ(tf_quote("it's"), std::string("'it''''s'"));
}
