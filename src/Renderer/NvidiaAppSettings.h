#pragma once
#include <windows.h>

// The RTX Video switches of the NVIDIA app. The app keeps them as driver
// values of the display adapter; they are not part of any public API, so a
// missing value means "unknown" and the renderer behaves as if they were on.
class NvidiaAppSettings
{
  public:
    enum class State
    {
        Unknown,
        Off,
        On
    };

    // Super Resolution and RTX Video HDR, read again at most every two
    // seconds so a change in the app applies while a video plays.
    State SuperResolution()
    {
        Refresh();
        return superResolution;
    }

    State TrueHdr()
    {
        Refresh();
        return trueHdr;
    }

  private:
    State superResolution = State::Unknown, trueHdr = State::Unknown;
    ULONGLONG readAt = 0;
    bool read = false;

    static State Value(HKEY key, const wchar_t* name)
    {
        DWORD value = 0, size = sizeof(value), type = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
            type != REG_DWORD)
            return State::Unknown;
        return value ? State::On : State::Off;
    }

    void Refresh()
    {
        const ULONGLONG now = GetTickCount64();
        if (read && now - readAt < 2000)
            return;
        read = true;
        readAt = now;
        superResolution = trueHdr = State::Unknown;
        // Display adapters: the first one that carries the NVIDIA app values.
        HKEY adapters = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                          0,
                          KEY_READ,
                          &adapters) != ERROR_SUCCESS)
            return;
        wchar_t name[64];
        for (DWORD i = 0; superResolution == State::Unknown && trueHdr == State::Unknown; ++i)
        {
            DWORD length = ARRAYSIZE(name);
            if (RegEnumKeyExW(adapters, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            HKEY adapter = nullptr;
            if (RegOpenKeyExW(adapters, name, 0, KEY_QUERY_VALUE, &adapter) != ERROR_SUCCESS)
                continue;
            superResolution = Value(adapter, L"_User_Global_VAL_SuperResolution");
            trueHdr = Value(adapter, L"_User_Global_VAL_TrueHDR");
            RegCloseKey(adapter);
        }
        RegCloseKey(adapters);
    }
};
