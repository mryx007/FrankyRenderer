#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>
#include "Settings/DisplayProfiles.h"

// madVR-style display mode switching. The list names the modes a display may
// use, e.g. "2160p23, 2160p24, 2160p50, 2160p60" or just "23, 24, 60". Rates
// follow the Windows/madVR convention: 23 means 23.976 Hz, 24 means 24.000 Hz.
namespace DisplayModes
{
struct Mode
{
    int height = 0; // 0 keeps the current resolution
    int code = 0;   // integer rate as Windows lists it
    double hz = 0;  // exact rate

    bool operator==(const Mode& o) const
    {
        return height == o.height && code == o.code;
    }
};

// 23, 29, 47, 59, 119 ... stand for the NTSC rates just below 24, 30, 48, 60, 120.
inline double RateOf(int code)
{
    const int next = code + 1;
    return next % 24 == 0 || next % 30 == 0 ? next * 1000.0 / 1001.0 : double(code);
}

inline std::wstring Name(const Mode& mode)
{
    return (mode.height ? std::to_wstring(mode.height) + L"p" : std::wstring()) + std::to_wstring(mode.code);
}

// Parses the list; false for any malformed entry. An empty list is valid.
inline bool Parse(const wchar_t* text, std::vector<Mode>& out)
{
    out.clear();
    std::wstring token;
    const auto flush = [&]
    {
        if (token.empty())
            return true;
        Mode mode;
        size_t at = 0;
        const auto number = [&](int& value)
        {
            const size_t begin = at;
            value = 0;
            while (at < token.size() && iswdigit(token[at]) && at - begin < 5)
                value = value * 10 + (token[at++] - L'0');
            return at > begin;
        };
        int first = 0;
        if (!number(first))
            return false;
        if (at < token.size() && (token[at] == L'p' || token[at] == L'P'))
        {
            ++at;
            mode.height = first;
            if (!number(mode.code))
                return false;
        }
        else
            mode.code = first;
        if (at != token.size() || (mode.height && (mode.height < 240 || mode.height > 4320)) || mode.code < 23 ||
            mode.code > 240)
            return false;
        mode.hz = RateOf(mode.code);
        if (std::find(out.begin(), out.end(), mode) == out.end())
            out.push_back(mode);
        token.clear();
        return true;
    };
    for (const wchar_t* p = text; p && *p; ++p)
    {
        if (*p == L',' || *p == L';' || iswspace(*p))
        {
            if (!flush())
                return false;
        }
        else
            token.push_back(*p);
    }
    return flush() && out.size() <= 32;
}

// The list entry whose rate is a whole multiple of the content rate; the
// lowest multiple wins and list order breaks ties. -1 when none fits.
inline int Choose(const std::vector<Mode>& list, double contentHz)
{
    if (!(contentHz > 1))
        return -1;
    int best = -1;
    double bestMultiple = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const double ratio = list[i].hz / contentHz, multiple = std::round(ratio);
        if (multiple < 1 || std::abs(ratio - multiple) > multiple * 0.0005)
            continue;
        if (best < 0 || multiple < bestMultiple)
        {
            best = int(i);
            bestMultiple = multiple;
        }
    }
    return best;
}

// Distinct modes the display offers at its current colour depth, for the menu.
inline std::vector<Mode> Available(const wchar_t* device)
{
    std::vector<Mode> modes;
    DEVMODEW current = {};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &current))
        return modes;
    DEVMODEW candidate = {};
    candidate.dmSize = sizeof(candidate);
    for (DWORD i = 0; EnumDisplaySettingsExW(device, i, &candidate, 0); ++i)
    {
        if (candidate.dmBitsPerPel != current.dmBitsPerPel || candidate.dmDisplayFrequency < 23 ||
            candidate.dmDisplayFrequency > 240)
            continue;
        Mode mode{int(candidate.dmPelsHeight), int(candidate.dmDisplayFrequency), RateOf(candidate.dmDisplayFrequency)};
        if (std::find(modes.begin(), modes.end(), mode) == modes.end())
            modes.push_back(mode);
    }
    std::sort(modes.begin(),
              modes.end(),
              [](const Mode& a, const Mode& b)
              { return a.height != b.height ? a.height > b.height : a.code < b.code; });
    return modes;
}

// A short summary of Available() for the menu: the rates at the current
// resolution, then the other resolutions from 720p up.
inline std::wstring Describe(const wchar_t* device)
{
    DEVMODEW current = {};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &current))
        return {};
    const int height = int(current.dmPelsHeight);
    std::wstring rates, others;
    bool film = false;
    for (const auto& mode : Available(device))
    {
        if (mode.height == height)
        {
            rates += (rates.empty() ? L"" : L", ") + std::to_wstring(mode.code);
            film |= mode.code == 23 || mode.code == 24;
        }
        else if (mode.height >= 720 && others.find(std::to_wstring(mode.height) + L"p") == std::wstring::npos)
            others += (others.empty() ? L"" : L", ") + std::to_wstring(mode.height) + L"p";
    }
    if (rates.empty())
        return {};
    std::wstring text = L"At " + std::to_wstring(height) + L"p this display offers " + rates + L" Hz.";
    if (!film)
        text += L" No 23 or 24 Hz mode, so films cannot switch here.";
    if (!others.empty())
        text += L"\nOther resolutions: " + others + L".";
    return text;
}

// Crash safety: a temporary mode survives a crashed player. Record each switch
// so a later renderer restores displays whose switching process is gone.
inline constexpr wchar_t PendingPrefix[] = L"PendingDisplayMode.";

inline uint64_t ProcessCreation(HANDLE process)
{
    FILETIME created = {}, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return 0;
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

inline std::wstring PendingName(const wchar_t* device)
{
    wchar_t name[160];
    swprintf_s(name,
               L"%ls%08lX.%016llX.%ls",
               PendingPrefix,
               GetCurrentProcessId(),
               static_cast<unsigned long long>(ProcessCreation(GetCurrentProcess())),
               device);
    return name;
}

inline void RecoverAbandoned()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, DisplayProfiles::Root().c_str(), 0, KEY_READ | KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS)
        return;
    std::vector<std::wstring> abandoned;
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256];
        DWORD length = DWORD(std::size(name));
        const auto result = RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS || wcsncmp(name, PendingPrefix, wcslen(PendingPrefix)))
            continue;
        unsigned long pid = 0;
        unsigned long long created = 0;
        wchar_t device[64] = {};
        if (swscanf_s(name + wcslen(PendingPrefix),
                      L"%8lX.%16llX.%63ls",
                      &pid,
                      &created,
                      device,
                      unsigned(std::size(device))) != 3)
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        bool alive = !process && GetLastError() == ERROR_ACCESS_DENIED;
        if (process)
        {
            DWORD code = 0;
            alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE && ProcessCreation(process) == created;
            CloseHandle(process);
        }
        if (!alive)
        {
            // Reload the display's registry (desktop) mode.
            ChangeDisplaySettingsExW(device, nullptr, nullptr, 0, nullptr);
            abandoned.emplace_back(name);
        }
    }
    for (const auto& name : abandoned)
        RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
}

// Switches one display temporarily (CDS_FULLSCREEN never touches the saved
// desktop mode) and restores it. Use from a single thread without renderer locks:
// a mode change broadcasts WM_DISPLAYCHANGE synchronously.
class Switcher
{
    std::wstring device;
    Mode active;

    void Record(bool pending)
    {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            DisplayProfiles::Root().c_str(),
                            0,
                            nullptr,
                            0,
                            KEY_SET_VALUE,
                            nullptr,
                            &key,
                            nullptr) != ERROR_SUCCESS)
            return;
        const auto name = PendingName(device.c_str());
        if (pending)
            RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
        else
            RegDeleteValueW(key, name.c_str());
        RegCloseKey(key);
    }

  public:
    ~Switcher()
    {
        Restore();
    }

    bool Active() const
    {
        return !device.empty();
    }

    const std::wstring& Device() const
    {
        return device;
    }

    const Mode& Current() const
    {
        return active;
    }

    // True when the display already runs this mode.
    static bool Matches(const wchar_t* display, const Mode& mode)
    {
        DEVMODEW current = {};
        current.dmSize = sizeof(current);
        return EnumDisplaySettingsW(display, ENUM_CURRENT_SETTINGS, &current) &&
               int(current.dmDisplayFrequency) == mode.code &&
               (!mode.height || int(current.dmPelsHeight) == mode.height);
    }

    // Changes the display to mode; false if the display offers no such mode.
    bool Switch(const wchar_t* display, const Mode& mode)
    {
        if (Active() && device != display)
            Restore();
        DEVMODEW current = {};
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsW(display, ENUM_CURRENT_SETTINGS, &current))
            return false;
        const int height = mode.height ? mode.height : int(current.dmPelsHeight);
        DEVMODEW chosen = {}, candidate = {};
        candidate.dmSize = sizeof(candidate);
        bool found = false;
        for (DWORD i = 0; EnumDisplaySettingsExW(display, i, &candidate, 0); ++i)
        {
            if (int(candidate.dmPelsHeight) != height || int(candidate.dmDisplayFrequency) != mode.code ||
                candidate.dmBitsPerPel != current.dmBitsPerPel)
                continue;
            // Keep the current width when the display offers it, else the widest.
            const bool better =
                !found || candidate.dmPelsWidth == current.dmPelsWidth ||
                (chosen.dmPelsWidth != current.dmPelsWidth && candidate.dmPelsWidth > chosen.dmPelsWidth);
            if (better)
            {
                chosen = candidate;
                found = true;
            }
        }
        if (!found)
            return false;
        chosen.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        const bool first = !Active();
        device = display;
        if (first)
            Record(true);
        if (ChangeDisplaySettingsExW(display, &chosen, nullptr, CDS_FULLSCREEN, nullptr) != DISP_CHANGE_SUCCESSFUL)
        {
            Restore();
            return false;
        }
        active = mode;
        return true;
    }

    void Restore()
    {
        if (!Active())
            return;
        ChangeDisplaySettingsExW(device.c_str(), nullptr, nullptr, 0, nullptr);
        Record(false);
        device.clear();
        active = {};
    }
};
}
