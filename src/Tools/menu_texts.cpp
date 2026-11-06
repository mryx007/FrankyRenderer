// Writes every settings menu text for the translation template, one per line
// as UTF-8 "note<TAB>text" with backslash, tab and line breaks escaped.
// Used by Tools/translations.py template.
#include "UI/Direct2DSettingsHost.h"
#include <cstdio>
#include <string>

static std::string Escaped(const std::wstring& text)
{
    std::wstring escaped;
    for (wchar_t c : text)
        escaped += c == L'\\'   ? std::wstring(L"\\\\")
                   : c == L'\t' ? std::wstring(L"\\t")
                   : c == L'\n' ? std::wstring(L"\\n")
                                : std::wstring(1, c);
    const int count =
        WideCharToMultiByte(CP_UTF8, 0, escaped.c_str(), int(escaped.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, escaped.c_str(), int(escaped.size()), out.data(), count, nullptr, nullptr);
    return out;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
        return 2;
    FILE* file = nullptr;
    if (_wfopen_s(&file, argv[1], L"wb") || !file)
        return 1;
    for (const auto& [text, note] : Direct2DSettingsHost::TranslatableTexts())
        fprintf(file, "%s\t%s\n", Escaped(note).c_str(), Escaped(text).c_str());
    fclose(file);
    return 0;
}
