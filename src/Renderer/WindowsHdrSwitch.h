#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Renderer/DisplayModes.h"
#include "Renderer/Trace.h"

// Turns the Windows HDR switch of a display on or off for playback and puts it
// back afterwards. With Windows HDR on, the desktop composes HDR itself, so the
// picture stays correct in a window and under overlapping controls.
namespace WindowsHdr
{
enum Toggle
{
    Disabled,
    OnFullscreen,
    On,
    OnOffFullscreen,
    OnOff
};

enum class Action
{
    Keep,
    TurnOn,
    TurnOff
};

struct Situation
{
    int toggle = Disabled;
    bool hdrSource = false, rtxHdr = false, fullscreen = false, enabled = false, enabledAtStart = false;
};

inline Action Decide(const Situation& s)
{
    if (s.toggle == Disabled)
        return Action::Keep;
    const bool followsFullscreen = s.toggle == OnOffFullscreen;
    if (s.hdrSource)
    {
        if (!s.enabled)
            return s.toggle == On || s.toggle == OnOff || s.fullscreen ? Action::TurnOn : Action::Keep;
        return followsFullscreen && !s.fullscreen && !s.enabledAtStart ? Action::TurnOff : Action::Keep;
    }
    if (s.enabled)
    {
        // RTX Video HDR for SDR video only works with Windows HDR on.
        if (s.rtxHdr)
            return Action::Keep;
        return !s.enabledAtStart || s.toggle == OnOff || (followsFullscreen && s.fullscreen) ? Action::TurnOff
                                                                                             : Action::Keep;
    }
    return followsFullscreen && !s.fullscreen && s.enabledAtStart ? Action::TurnOn : Action::Keep;
}

struct Display
{
    LUID adapter = {};
    UINT32 target = 0;
    bool supported = false, enabled = false;
    // Windows 11 24H2 separates HDR from other advanced colour modes.
    bool separateHdrState = false;
};

// Declared here because the SDK hides them behind its newest NTDDI versions.
inline constexpr int GetColorInfo2 = 15, SetHdrState = 16, SetAdvancedColor = 10;

struct ColorInfo2
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 flags;
    DISPLAYCONFIG_COLOR_ENCODING encoding;
    UINT32 bits;
    UINT32 activeMode;
};

struct SetState
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 enable;
};

inline std::optional<Display> Query(const wchar_t* device)
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_INSUFFICIENT_BUFFER;
    while (result == ERROR_INSUFFICIENT_BUFFER)
    {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return {};
        paths.resize(pathCount);
        modes.resize(modeCount);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        paths.resize(pathCount);
    }
    if (result != ERROR_SUCCESS)
        return {};
    for (const auto& path : paths)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name = {};
        name.header = {
            DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(name), path.sourceInfo.adapterId, path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || wcscmp(name.viewGdiDeviceName, device))
            continue;
        Display display;
        display.adapter = path.targetInfo.adapterId;
        display.target = path.targetInfo.id;
        ColorInfo2 modern = {};
        modern.header = {static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(GetColorInfo2),
                         sizeof(modern),
                         display.adapter,
                         display.target};
        if (DisplayConfigGetDeviceInfo(&modern.header) == ERROR_SUCCESS)
        {
            display.separateHdrState = true;
            display.supported = modern.flags & (1u << 4);
            display.enabled = modern.activeMode == 2;
            return display;
        }
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color = {};
        color.header = {
            DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color), display.adapter, display.target};
        if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS)
            return {};
        display.supported = color.advancedColorSupported;
        display.enabled = color.advancedColorEnabled && !color.wideColorEnforced;
        return display;
    }
    return {};
}

inline bool Set(const wchar_t* device, const Display& display, bool enable)
{
    // Private test graphs must never switch a physical display.
    if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
        return false;
    DEVMODEW before = {};
    before.dmSize = sizeof(before);
    const bool known = EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &before);
    SetState state = {};
    state.header = {
        static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(display.separateHdrState ? SetHdrState : SetAdvancedColor),
        sizeof(state),
        display.adapter,
        display.target};
    state.enable = enable;
    auto result = DisplayConfigSetDeviceInfo(&state.header);
    // Older Windows 11 versions report the new state but only take the old request.
    if (result != ERROR_SUCCESS && display.separateHdrState)
    {
        state.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(SetAdvancedColor);
        result = DisplayConfigSetDeviceInfo(&state.header);
    }
    if (result != ERROR_SUCCESS)
        return false;
    // Switching can change the display mode; the user's mode stays.
    DEVMODEW after = {};
    after.dmSize = sizeof(after);
    if (known && EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &after) &&
        (before.dmPelsWidth != after.dmPelsWidth || before.dmPelsHeight != after.dmPelsHeight ||
         before.dmBitsPerPel != after.dmBitsPerPel || before.dmDisplayFrequency != after.dmDisplayFrequency))
        ChangeDisplaySettingsExW(device, &before, nullptr, CDS_FULLSCREEN, nullptr);
    return true;
}

// Crash safety, like display modes: the prefix holds the state to go back to.
inline constexpr wchar_t PendingOn[] = L"PendingWindowsHdrOn.", PendingOff[] = L"PendingWindowsHdrOff.";

inline void RecoverAbandoned()
{
    const auto recover = [](const wchar_t* device, bool enable)
    {
        const auto display = Query(device);
        if (display && display->supported && display->enabled != enable)
            Set(device, *display, enable);
    };
    DisplayModes::ForEachAbandoned(PendingOn, [&](const wchar_t* device) { recover(device, true); });
    DisplayModes::ForEachAbandoned(PendingOff, [&](const wchar_t* device) { recover(device, false); });
}

// Use from one thread without renderer locks: a switch notifies every top-level
// window synchronously.
class Switcher
{
  public:
    using QueryFunction = std::function<std::optional<Display>(const wchar_t*)>;
    using SetFunction = std::function<bool(const wchar_t*, const Display&, bool)>;

    struct Result
    {
        bool known = false, enabled = false, switched = false;
    };

  private:
    QueryFunction query;
    SetFunction set;
    bool persist;
    std::map<std::wstring, bool> atStart, saved;

    void Record(const std::wstring& device, bool state, bool pending) const
    {
        if (!persist)
            return;
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
        const auto name = DisplayModes::PendingName(state ? PendingOn : PendingOff, device.c_str());
        if (pending)
            RegSetValueExW(key, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
        else
            RegDeleteValueW(key, name.c_str());
        RegCloseKey(key);
    }

    // Windows reports the new state a moment after accepting the request.
    bool Settled(const wchar_t* device, bool wanted) const
    {
        for (int i = 0; i < 6; ++i)
        {
            Sleep(100);
            const auto now = query(device);
            if (now && now->enabled == wanted)
                return true;
        }
        return false;
    }

  public:
    Switcher() : query(WindowsHdr::Query), set(WindowsHdr::Set), persist(true) {}

    Switcher(QueryFunction queryDisplay, SetFunction setDisplay)
        : query(std::move(queryDisplay)), set(std::move(setDisplay)), persist(false)
    {
    }

    ~Switcher()
    {
        Restore();
    }

    bool Active() const
    {
        return !saved.empty();
    }

    Result Update(const wchar_t* device, int toggle, bool sourceKnown, bool hdrSource, bool rtxHdr, bool fullscreen)
    {
        const auto display = query(device);
        if (!display || !display->supported)
            return {};
        const bool startState = atStart.try_emplace(device, display->enabled).first->second;
        Result result{true, display->enabled, false};
        if (!sourceKnown)
            return result;
        const auto action = Decide({toggle, hdrSource, rtxHdr, fullscreen, display->enabled, startState});
        if (action == Action::Keep)
            return result;
        const bool enable = action == Action::TurnOn, first = !saved.count(device);
        if (first)
            Record(device, display->enabled, true);
        if (!set(device, *display, enable))
        {
            if (first)
                Record(device, display->enabled, false);
            Trace("Windows HDR switch to %d failed on %ls", enable, device);
            return result;
        }
        saved.try_emplace(device, display->enabled);
        result.switched = true;
        result.enabled = Settled(device, enable) ? enable : display->enabled;
        Trace("Windows HDR %d on %ls (reported %d)", enable, device, result.enabled);
        return result;
    }

    void Restore()
    {
        for (const auto& [device, state] : saved)
        {
            const auto display = query(device.c_str());
            if (display && display->supported && display->enabled != state)
                set(device.c_str(), *display, state);
            Record(device, state, false);
        }
        saved.clear();
        atStart.clear();
    }
};
}
