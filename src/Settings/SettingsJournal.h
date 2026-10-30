#pragma once
#include "Settings/SettingsBackup.h"
#include <mutex>

// Live preview persists every change, and Cancel restores the opening state
// when the dialog closes. If the player dies while the dialog is open, that
// restore never runs. Each process records the profiles as they were when its
// first dialog opened; a later renderer restores records whose owner is gone.
namespace SettingsJournal
{
inline constexpr wchar_t Prefix[] = L"PendingSession.";

inline uint64_t CreationTime(HANDLE process)
{
    FILETIME created = {}, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return 0;
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

inline std::wstring ValueName(DWORD pid, uint64_t created)
{
    wchar_t name[64];
    swprintf_s(name, L"%ls%08lX.%016llX", Prefix, pid, static_cast<unsigned long long>(created));
    return name;
}

inline std::wstring CurrentName()
{
    return ValueName(GetCurrentProcessId(), CreationTime(GetCurrentProcess()));
}

inline bool OwnerAlive(const std::wstring& name)
{
    unsigned long pid = 0;
    unsigned long long created = 0;
    if (swscanf_s(name.c_str() + wcslen(Prefix), L"%8lX.%16llX", &pid, &created) != 2)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE && CreationTime(process) == created;
    CloseHandle(process);
    return alive;
}

struct State
{
    std::mutex lock;
    int sessions = 0;
};

inline State& Shared()
{
    static State state;
    return state;
}

inline void Begin()
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    if (state.sessions++)
        return;
    try
    {
        const auto text = SettingsBackup::Serialize(SettingsBackup::Capture(PlaceboSettings{}));
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
        RegSetValueExW(
            key, CurrentName().c_str(), 0, REG_BINARY, reinterpret_cast<const BYTE*>(text.data()), DWORD(text.size()));
        RegCloseKey(key);
    }
    catch (const std::exception&)
    {
        // No display profiles yet: Cancel still works, only crash recovery is unavailable.
    }
}

inline void End()
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    if (state.sessions <= 0 || --state.sessions)
        return;
    RegDeleteKeyValueW(HKEY_CURRENT_USER, DisplayProfiles::Root().c_str(), CurrentName().c_str());
}

inline void Recover()
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
        if (result == ERROR_SUCCESS && !wcsncmp(name, Prefix, wcslen(Prefix)) && !OwnerAlive(name))
            abandoned.emplace_back(name);
    }
    for (const auto& name : abandoned)
    {
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            type == REG_BINARY && bytes <= 16 * 1024 * 1024)
        {
            std::string text(bytes, '\0');
            if (RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &bytes) ==
                ERROR_SUCCESS)
                try
                {
                    text.resize(bytes);
                    SettingsBackup::Apply(SettingsBackup::Parse(text), nullptr);
                }
                catch (const std::exception&)
                {
                }
        }
        RegDeleteValueW(key, name.c_str());
    }
    RegCloseKey(key);
}
}
