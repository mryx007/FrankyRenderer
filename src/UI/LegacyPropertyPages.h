#pragma once
#include <windows.h>
#include <objbase.h>
#include <string>
#include <vector>

// Retired page IDs remain only for registration migration. Never remove a
// registration belonging to another renderer installation.
namespace LegacyPropertyPages
{
inline constexpr CLSID Ids[] = {{0x6b35ef45, 0x9c5e, 0x40b4, {0x8e, 0xac, 0xde, 0x2c, 0x51, 0x61, 0x17, 0x4d}},
                                {0x7d274b8b, 0x7c98, 0x43d7, {0xb4, 0x48, 0x78, 0x2c, 0xba, 0x0c, 0x6a, 0x33}},
                                {0x5f6faac4, 0x5b83, 0x4dbe, {0xab, 0x84, 0xc0, 0x4a, 0x1c, 0x57, 0x53, 0xa6}},
                                {0x72eb4d7a, 0x7d02, 0x4080, {0xa8, 0x5e, 0x6e, 0x65, 0xcb, 0x42, 0x23, 0xd1}},
                                {0xaf0d638a, 0x478b, 0x43b0, {0x81, 0xd0, 0xe7, 0xb0, 0xfa, 0x46, 0xa3, 0xe1}},
                                {0x60585d47, 0x776f, 0x4b48, {0xa3, 0x57, 0x24, 0x2d, 0x8b, 0x8a, 0x3f, 0x42}},
                                {0x10dd7ade, 0xf6d7, 0x4a4d, {0xa1, 0xe9, 0x5d, 0x88, 0x61, 0x3a, 0x4e, 0xf2}},
                                {0x4c64b3b7, 0x5485, 0x4230, {0xae, 0x73, 0x84, 0xc7, 0x90, 0x7e, 0x4f, 0x3b}},
                                {0xc5b9232e, 0xdd14, 0x43bb, {0xb8, 0xd9, 0x91, 0x75, 0x52, 0xc0, 0xa7, 0x68}}};

inline HRESULT RemoveOwned(HKEY classes, const wchar_t* module)
{
    for (const auto& id : Ids)
    {
        wchar_t guid[40];
        if (!StringFromGUID2(id, guid, int(std::size(guid))))
            return E_FAIL;
        const auto key = std::wstring(L"CLSID\\") + guid;
        const auto server = key + L"\\InprocServer32";
        DWORD size = 0;
        auto result = RegGetValueW(classes, server.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
            continue;
        if (result != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(result);
        if (size > 65536 || size < sizeof(wchar_t))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::vector<wchar_t> owner(size / sizeof(wchar_t) + 1, 0);
        result = RegGetValueW(classes, server.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, owner.data(), &size);
        if (result != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(result);
        if (_wcsicmp(owner.data(), module))
            continue;
        result = RegDeleteTreeW(classes, key.c_str());
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
            return HRESULT_FROM_WIN32(result);
    }
    return S_OK;
}
}
