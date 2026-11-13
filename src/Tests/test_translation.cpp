// Menu translation catalogue reader: the .po subset Weblate writes.
#include "UI/Translation.h"
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* label)
{
    if (!ok)
    {
        printf("FAIL %s\n", label);
        exit(1);
    }
}

int main()
{
    const std::string po = "# Translator comment\r\n"
                           "msgid \"\"\r\n"
                           "msgstr \"\"\r\n"
                           "\"Content-Type: text/plain; charset=UTF-8\\n\"\r\n"
                           "\r\n"
                           "#. Setting name\r\n"
                           "msgid \"General\"\r\n"
                           "msgstr \"Allgemein\"\r\n"
                           "\r\n"
                           "msgid \"Output Texture Format\"\r\n"
                           "msgstr \"Format der \"\r\n"
                           "\"Ausgabetextur\"\r\n"
                           "\r\n"
                           "msgid \"Quote \\\"%s\\\"\\nnext\"\r\n"
                           "msgstr \"Zitat \\\"%s\\\"\\nweiter\"\r\n"
                           "\r\n"
                           "msgctxt \"context\"\r\n"
                           "msgid \"Background\"\r\n"
                           "msgstr \"Hintergrund\"\r\n"
                           "\r\n"
                           "#, fuzzy\r\n"
                           "msgid \"Reset Page\"\r\n"
                           "msgstr \"Ungeprüft\"\r\n"
                           "\r\n"
                           "msgid \"Untranslated\"\r\n"
                           "msgstr \"\"\r\n"
                           "\r\n"
                           "msgid \"One\"\r\n"
                           "msgid_plural \"Many\"\r\n"
                           "msgstr[0] \"Eins\"\r\n"
                           "msgstr[1] \"Viele\"\r\n"
                           "\r\n"
                           "msgid \"Grün\"\n"
                           "msgstr \"Vert\"";
    const auto text = MenuTranslation::Parse(po);
    Require(text.at(L"General") == L"Allgemein", "plain entry");
    Require(text.at(L"Output Texture Format") == L"Format der Ausgabetextur", "continuation lines");
    Require(text.at(L"Quote \"%s\"\nnext") == L"Zitat \"%s\"\nweiter", "escapes");
    Require(text.at(L"Background") == L"Hintergrund", "context lines are ignored");
    Require(!text.count(L"Reset Page"), "fuzzy entries are skipped");
    Require(!text.count(L"Untranslated") && !text.count(L""), "empty translations and the header are skipped");
    Require(!text.count(L"One"), "plural entries are skipped");
    Require(text.at(L"Grün") == L"Vert", "UTF-8, LF endings and a final entry without newline");
    Require(Tr(L"General") == L"General", "no embedded catalogue falls back to English");
    // Issue #17: CJK text is laid out with the menu language, one font for all characters.
    auto locale = [](const wchar_t* language)
    {
        g_menuLanguage = language;
        ++g_menuLanguageVersion;
        return MenuLocale();
    };
    Require(locale(L"en") == L"en-us" && !wcscmp(MenuGdiFont(), L"Segoe UI"), "English keeps Segoe UI");
    Require(locale(L"zh_Hans") == L"zh-Hans" && !wcscmp(MenuGdiFont(), L"Microsoft YaHei UI"),
            "Simplified Chinese uses zh-Hans and Microsoft YaHei UI");
    Require(locale(L"zh_Hant") == L"zh-Hant" && !wcscmp(MenuGdiFont(), L"Microsoft JhengHei UI"),
            "Traditional Chinese uses its own font");
    Require(locale(L"pt_BR") == L"pt-BR" && locale(L"de") == L"de", "region and plain language tags");
    puts("PASS translation catalogue: entries, continuation, escapes, context, fuzzy, empty, plural, UTF-8, fallback, "
         "menu locale and CJK fonts");
}
