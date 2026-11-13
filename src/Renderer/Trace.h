#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace Diagnostics
{
inline std::wstring SettingsKey()
{
    wchar_t test[512];
    const auto size = GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", test, 512);
    return size && size < 512 ? test : L"Software\\PlaceboRenderer";
}

inline constexpr wchar_t ArmedValue[] = L"DiagnosticLogNextStart";

inline bool Armed()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(
               HKEY_CURRENT_USER, SettingsKey().c_str(), ArmedValue, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
               ERROR_SUCCESS &&
           value == 1;
}

inline void SetArmed(bool armed)
{
    HKEY key;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, SettingsKey().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    const DWORD value = 1;
    if (armed)
        RegSetValueExW(key, ArmedValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    else
        RegDeleteValueW(key, ArmedValue);
    RegCloseKey(key);
}

// One switch-on covers one start of the player, also if it crashes.
inline bool ConsumeArmed()
{
    if (!Armed())
        return false;
    SetArmed(false);
    return true;
}

inline std::wstring LogFile(const wchar_t* name = L"FrankyRenderer.log")
{
    wchar_t folder[MAX_PATH] = {};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, MAX_PATH);
    if (!count || count >= MAX_PATH)
        return {};
    std::wstring path = std::wstring(folder) + L"\\FrankyRenderer";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\" + name;
}

// What the playing renderer reports for the bug report.
inline std::mutex& LiveLock()
{
    static std::mutex lock;
    return lock;
}

inline std::function<std::wstring()>& LiveSource()
{
    static std::function<std::wstring()> source;
    return source;
}

inline void SetLive(std::function<std::wstring()> source)
{
    std::lock_guard guard(LiveLock());
    LiveSource() = std::move(source);
}

inline std::wstring LiveText()
{
    std::lock_guard guard(LiveLock());
    return LiveSource() ? LiveSource()() : std::wstring();
}
}

// Explicit opt-in only: nothing is recorded unless the user switched on
// Diagnostic Log for this start, or PLACEBO_RENDERER_DEBUG_LOG=1 is set.
// No frame polling or network activity.
inline bool TraceEnabled()
{
    static const bool enabled = []()
    {
        if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
            return false;
        wchar_t value[2] = {};
        if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_DEBUG_LOG", value, 2) == 1 && value[0] == L'1')
            return true;
        if (!Diagnostics::ConsumeArmed())
            return false;
        // A report describes this session alone.
        DeleteFileW(Diagnostics::LogFile().c_str());
        DeleteFileW(Diagnostics::LogFile(L"FrankyRenderer.old.log").c_str());
        return true;
    }();
    return enabled;
}

inline void Trace(const char* format, ...)
{
    if (!TraceEnabled())
        return;
    static std::mutex lock;
    std::lock_guard guard(lock);
    const auto path = Diagnostics::LogFile();
    if (path.empty())
        return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"ab") || !file)
        return;
    // Two files of 4 MB at most, so a long session cannot fill the disk.
    _fseeki64(file, 0, SEEK_END);
    if (_ftelli64(file) > 4 * 1024 * 1024)
    {
        fclose(file);
        MoveFileExW(path.c_str(), Diagnostics::LogFile(L"FrankyRenderer.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") || !file)
            return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(file,
            "%04u-%02u-%02u %02u:%02u:%02u PID=%lu ",
            now.wYear,
            now.wMonth,
            now.wDay,
            now.wHour,
            now.wMinute,
            now.wSecond,
            GetCurrentProcessId());
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}

inline void TraceInterface(const char* object, REFIID id)
{
    if (!TraceEnabled())
        return;

    struct Seen
    {
        std::string object;
        GUID id;
    };

    static std::mutex mutex;
    static std::vector<Seen> seen;
    bool fresh = false;
    {
        std::lock_guard guard(mutex);
        bool found = false;
        for (const auto& entry : seen)
            if (entry.object == object && entry.id == id)
            {
                found = true;
                break;
            }
        if (!found && seen.size() < 80)
        {
            seen.push_back({object, id});
            fresh = true;
        }
    }
    if (fresh)
    {
        wchar_t text[40] = {};
        StringFromGUID2(id, text, 40);
        Trace("Interface query on %s: %ls", object, text);
    }
}
