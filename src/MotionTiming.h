#pragma once
#include <windows.h>
#include <cmath>
#include <vector>

// Display cadence only. Media timestamps continue to use the DirectShow clock.
inline double MonitorPeriod(HWND window)
{
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        return 0;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        UINT32 count = 0, modesCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &count, &modesCount) != ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modesCount);
        LONG result =
            QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &count, paths.data(), &modesCount, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (result != ERROR_SUCCESS)
            break;
        for (UINT32 i = 0; i < count; i++)
        {
            auto& p = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name = {};
            name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            name.header.size = sizeof(name);
            name.header.adapterId = p.sourceInfo.adapterId;
            name.header.id = p.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS ||
                _wcsicmp(name.viewGdiDeviceName, info.szDevice))
                continue;
            auto rate = p.targetInfo.refreshRate;
            if (rate.Numerator && rate.Denominator)
            {
                double hz = double(rate.Numerator) / rate.Denominator;
                if (hz >= 20 && hz <= 1000)
                    return 1e7 / hz;
            }
        }
        break;
    }
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency >= 20 &&
        mode.dmDisplayFrequency <= 1000)
        return 1e7 / mode.dmDisplayFrequency;
    return 0;
}

struct MotionTick
{
    LONGLONG start, stop;
    float position, duration;
};

// A nominal sample duration can overlap the next millisecond-rounded PTS.
// Use the already buffered next frame to partition the output timeline; keep
// genuine discontinuities and the final frame's nominal end unchanged.
inline LONGLONG MotionIntervalEnd(LONGLONG start, LONGLONG stop, LONGLONG nextStart)
{
    return stop > start && nextStart > start && std::abs(double(nextStart) - double(stop)) <= 20000 ? nextStart : stop;
}

// Decide from the stream's nominal cadence, not each rounded PTS interval.
// A valid interval may contain no display tick; that is not a delivery failure.
inline bool MotionCadenceSupported(LONGLONG nominalDuration, double period)
{
    return nominalDuration > 0 && nominalDuration <= 10000000 && std::isfinite(period) && period >= 10000 &&
           period <= 500000 && nominalDuration >= period * .98;
}

inline bool MotionIntervalValid(LONGLONG start, LONGLONG stop, double period)
{
    return stop > start && double(stop) - double(start) <= 10000000 && std::isfinite(period) && period >= 10000 &&
           period <= 500000;
}

inline std::vector<MotionTick> MotionTicks(LONGLONG start, LONGLONG stop, double period)
{
    std::vector<MotionTick> result;
    if (!MotionIntervalValid(start, stop, period))
        return result;
    const auto span = stop - start;
    auto index = static_cast<LONGLONG>(std::ceil((start - .5) / period));
    for (int n = 0; n < 512; n++, index++)
    {
        double pts = index * period;
        auto a = static_cast<LONGLONG>(std::llround(pts));
        if (a >= stop)
            break;
        if (a < start)
            continue;
        auto b = static_cast<LONGLONG>(std::llround(pts + period));
        result.push_back(
            {a, b, static_cast<float>(double(a - start) / span), static_cast<float>(double(b - a) / span)});
    }
    return result;
}
