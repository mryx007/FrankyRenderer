#pragma once
// What the settings window shows beside its name: the version of a stable
// release, or the build of a Nightly or test build (Tools/build_label.py).
#include <windows.h>
#include <string>
#include <vector>
#if __has_include("BuildLabel.h")
#include "BuildLabel.h"
#endif
#ifndef FRANKY_BUILD_LABEL
#define FRANKY_BUILD_LABEL L""
#endif
#pragma comment(lib, "version.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

// "1.2.1" from the version resource of this module; empty without one.
inline std::wstring ModuleVersion()
{
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return {};
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || !info)
        return {};
    auto version = std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
                   std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
                   std::to_wstring(HIWORD(info->dwFileVersionLS));
    if (LOWORD(info->dwFileVersionLS))
        version += L"." + std::to_wstring(LOWORD(info->dwFileVersionLS));
    return version;
}

inline const std::wstring& BuildLabel()
{
    // Built at run time: MSVC 14.51 turned the same choice written as one
    // constant expression into an empty text.
    static const std::wstring label = []
    {
        std::wstring text = FRANKY_BUILD_LABEL;
        return text.empty() ? ModuleVersion() : text;
    }();
    return label;
}
