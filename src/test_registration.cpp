#include "LegacyPropertyPages.h"
#include <cstdio>
#include <stdexcept>

static void Require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

int main()
{
    GUID unique;
    if (FAILED(CoCreateGuid(&unique)))
        return 1;
    wchar_t guid[40];
    StringFromGUID2(unique, guid, 40);
    const auto path = std::wstring(L"Software\\FrankyTests\\Registration-") + guid;
    HKEY root = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr))
        return 1;
    int result = 0;
    try
    {
        const wchar_t* own = L"C:\\Renderer\\FrankyRenderer.ax";
        const wchar_t* foreign = L"C:\\OtherInstallation\\FrankyRenderer.ax";
        for (int i = 0; i < 9; ++i)
        {
            StringFromGUID2(LegacyPropertyPages::Ids[i], guid, 40);
            const auto key = std::wstring(L"CLSID\\") + guid + L"\\InprocServer32";
            const auto value = i == 1 ? foreign : own;
            Require(RegSetKeyValueW(
                        root, key.c_str(), nullptr, REG_SZ, value, DWORD((wcslen(value) + 1) * sizeof(wchar_t))) ==
                        ERROR_SUCCESS,
                    "create fixture");
        }
        Require(SUCCEEDED(LegacyPropertyPages::RemoveOwned(root, L"c:\\renderer\\frankyrenderer.ax")),
                "remove owned registrations case insensitively");
        Require(SUCCEEDED(LegacyPropertyPages::RemoveOwned(root, own)), "idempotent cleanup");
        for (int i = 0; i < 9; ++i)
        {
            StringFromGUID2(LegacyPropertyPages::Ids[i], guid, 40);
            const auto key = std::wstring(L"CLSID\\") + guid;
            HKEY found = nullptr;
            const auto status = RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &found);
            if (found)
                RegCloseKey(found);
            Require((status == ERROR_SUCCESS) == (i == 1), "retain foreign installation, remove only own old pages");
        }
        puts(
            "PASS obsolete registration migration: ownership check, case insensitive paths, all nine pages, repeat cleanup");
    }
    catch (const std::exception& error)
    {
        printf("FAIL registration migration: %s\n", error.what());
        result = 1;
    }
    RegCloseKey(root);
    RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    return result;
}
