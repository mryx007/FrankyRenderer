#pragma once
// Menu translations. The English text is the source and the lookup key
// (gettext style); translations/<language>.po files from Weblate are embedded
// in the renderer as "PO" resources named by language (de, pt_BR, ...).
// Missing or empty translations fall back to English.
#include <windows.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include "UI/MenuTheme.h"

// Empty: follow the Windows display language. Otherwise a resource name.
inline std::wstring g_menuLanguage;
// Bumped on every change so open views rebuild their texts.
inline int g_menuLanguageVersion = 0;

namespace MenuTranslation
{
inline HMODULE Module()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Module),
                       &module);
    return module;
}

inline std::wstring Upper(std::wstring text)
{
    CharUpperBuffW(text.data(), DWORD(text.size()));
    return text;
}

// gettext tag (pt_BR) to Windows locale name (pt-BR).
inline std::wstring LocaleName(std::wstring tag)
{
    std::replace(tag.begin(), tag.end(), L'_', L'-');
    return tag;
}

inline std::wstring Utf8(const std::string& text)
{
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(0, count)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count);
    return out;
}

// Minimal .po reader: msgid/msgstr with continuation lines and C escapes.
// Context lines are ignored; fuzzy, plural and empty translations are skipped.
inline std::unordered_map<std::wstring, std::wstring> Parse(const std::string& po)
{
    std::unordered_map<std::wstring, std::wstring> out;
    std::string id, str, ignored;
    std::string* target = nullptr;
    bool fuzzyNext = false, fuzzy = false, plural = false;
    auto flush = [&]
    {
        if (!id.empty() && !str.empty() && !fuzzy && !plural)
            out[Utf8(id)] = Utf8(str);
        id.clear();
        str.clear();
        target = nullptr;
        fuzzy = plural = false;
    };
    auto unquote = [](const std::string& line)
    {
        std::string value;
        const auto first = line.find('"'), last = line.rfind('"');
        if (first == std::string::npos || last <= first)
            return value;
        for (size_t i = first + 1; i < last; ++i)
        {
            char c = line[i];
            if (c == '\\' && i + 1 < last)
            {
                c = line[++i];
                c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
            }
            value += c;
        }
        return value;
    };
    size_t at = 0;
    while (at < po.size())
    {
        auto end = po.find('\n', at);
        if (end == std::string::npos)
            end = po.size();
        std::string line = po.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
        {
            if (target)
                flush();
        }
        else if (line[0] == '#')
        {
            if (target)
                flush();
            if (line.rfind("#,", 0) == 0 && line.find("fuzzy") != std::string::npos)
                fuzzyNext = true;
        }
        else if (line.rfind("msgctxt", 0) == 0)
        {
            if (target)
                flush();
            target = &ignored;
        }
        else if (line.rfind("msgid_plural", 0) == 0 || line.rfind("msgstr[", 0) == 0)
        {
            plural = true;
            target = &ignored;
        }
        else if (line.rfind("msgid ", 0) == 0)
        {
            if (target && target != &ignored)
                flush();
            fuzzy = fuzzyNext;
            fuzzyNext = false;
            id = unquote(line);
            target = &id;
        }
        else if (line.rfind("msgstr ", 0) == 0)
        {
            str = unquote(line);
            target = &str;
        }
        else if (line[0] == '"' && target)
            *target += unquote(line);
    }
    flush();
    return out;
}

inline std::string Resource(const std::wstring& name)
{
    const HMODULE module = Module();
    HRSRC found = FindResourceW(module, name.c_str(), L"PO");
    HGLOBAL data = found ? LoadResource(module, found) : nullptr;
    const auto bytes = data ? static_cast<const char*>(LockResource(data)) : nullptr;
    return bytes ? std::string(bytes, SizeofResource(module, found)) : std::string();
}

// Embedded languages as resource names (upper case, as the resource compiler stores them).
inline std::vector<std::wstring> Embedded()
{
    std::vector<std::wstring> names;
    EnumResourceNamesW(
        Module(),
        L"PO",
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR param) -> BOOL
        {
            if (!IS_INTRESOURCE(name))
                reinterpret_cast<std::vector<std::wstring>*>(param)->push_back(name);
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&names));
    std::sort(names.begin(), names.end());
    return names;
}

// Resource for a Windows locale ("de-DE"): exact match, then language and
// script ("zh-CN" lists "Hani;Hans;" -> zh_Hans, as Weblate names Chinese),
// then the base language.
inline std::wstring Match(std::wstring locale)
{
    wchar_t scripts[64] = {};
    const bool scripted = GetLocaleInfoEx(locale.c_str(), LOCALE_SSCRIPTS, scripts, 64) > 1;
    std::replace(locale.begin(), locale.end(), L'-', L'_');
    const auto base = locale.substr(0, locale.find(L'_'));
    std::vector<std::wstring> candidates{Upper(locale)};
    for (std::wstring rest = scripted ? scripts : L""; !rest.empty();)
    {
        const auto end = rest.find(L';');
        candidates.push_back(Upper(base + L"_" + rest.substr(0, end)));
        rest = end == std::wstring::npos ? L"" : rest.substr(end + 1);
    }
    candidates.push_back(Upper(base));
    const auto names = Embedded();
    for (const auto& candidate : candidates)
        if (std::find(names.begin(), names.end(), candidate) != names.end())
            return candidate;
    return {};
}

inline std::wstring WindowsLanguage()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0))
        return {};
    return Match(locale);
}

// Language name in that language ("Deutsch"), for the language list.
inline std::wstring NativeName(const std::wstring& resource)
{
    wchar_t name[128] = {};
    if (GetLocaleInfoEx(LocaleName(resource).c_str(), LOCALE_SNATIVELANGUAGENAME, name, 128) > 1)
    {
        CharUpperBuffW(name, 1);
        return name;
    }
    return resource;
}

struct Catalog
{
    int version = -1;
    std::wstring language;
    std::unordered_map<std::wstring, std::wstring> text;
};

inline Catalog& Active()
{
    static Catalog catalog;
    if (catalog.version != g_menuLanguageVersion)
    {
        catalog.version = g_menuLanguageVersion;
        catalog.language = g_menuLanguage == L"en"  ? std::wstring()
                           : g_menuLanguage.empty() ? WindowsLanguage()
                                                    : Upper(g_menuLanguage);
        catalog.text = catalog.language.empty() ? decltype(catalog.text){} : Parse(Resource(catalog.language));
    }
    return catalog;
}
} // namespace MenuTranslation

// BCP 47 name of the menu language ("zh-Hans", "pt-BR"), "en-us" for English.
// Text is laid out with it so CJK characters fall back to one matching font
// instead of a Japanese or Traditional Chinese font per character.
inline std::wstring MenuLocale()
{
    auto tag = MenuTranslation::Active().language;
    if (tag.empty())
        return L"en-us";
    std::wstring out;
    size_t start = 0;
    for (int part = 0; start <= tag.size(); ++part)
    {
        const auto end = std::min(tag.find(L'_', start), tag.size());
        auto piece = tag.substr(start, end - start);
        for (size_t i = 0; i < piece.size(); ++i)
            piece[i] =
                (part == 0 || (piece.size() == 4 && i > 0)) ? wchar_t(towlower(piece[i])) : wchar_t(towupper(piece[i]));
        out += (part ? L"-" : L"") + piece;
        start = end + 1;
    }
    return out;
}

// GDI font for menu controls drawn without DirectWrite (lists, edit fields):
// GDI font linking would also mix fonts for CJK text.
inline const wchar_t* MenuGdiFont()
{
    const auto locale = MenuLocale();
    if (locale.rfind(L"zh-Hant", 0) == 0 || locale == L"zh-TW" || locale == L"zh-HK")
        return L"Microsoft JhengHei UI";
    if (locale.rfind(L"zh", 0) == 0)
        return L"Microsoft YaHei UI";
    if (locale.rfind(L"ja", 0) == 0)
        return L"Yu Gothic UI";
    if (locale.rfind(L"ko", 0) == 0)
        return L"Malgun Gothic";
    return L"Segoe UI";
}

// Translated menu text; English when no translation exists.
inline std::wstring Tr(const std::wstring& english)
{
    if (english.empty())
        return english;
    const auto& text = MenuTranslation::Active().text;
    const auto found = text.find(english);
    return found == text.end() ? english : found->second;
}

inline void LoadMenuLanguage()
{
    wchar_t value[32] = {};
    DWORD size = sizeof(value);
    const std::wstring language =
        RegGetValueW(
            HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"Language", RRF_RT_REG_SZ, nullptr, value, &size) ==
                ERROR_SUCCESS
            ? value
            : L"";
    if (language != g_menuLanguage)
    {
        g_menuLanguage = language;
        ++g_menuLanguageVersion;
    }
}

// "" follows Windows, "en" forces English, otherwise an embedded language.
inline void SetMenuLanguage(const std::wstring& language)
{
    if (language == g_menuLanguage)
        return;
    g_menuLanguage = language;
    ++g_menuLanguageVersion;
    RegSetKeyValueW(HKEY_CURRENT_USER,
                    MenuAppearanceKey().c_str(),
                    L"Language",
                    REG_SZ,
                    language.c_str(),
                    DWORD((language.size() + 1) * sizeof(wchar_t)));
}
