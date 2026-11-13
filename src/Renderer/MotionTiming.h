#pragma once
#include <windows.h>
#include <algorithm>
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

// Whether the next picture continues the previous one's timeline: it starts
// after the previous start and at most two of the previous picture's own
// durations (plus 2 ms) later. That includes a next start exactly at the
// previous stop, tolerates stops that miss the next start for a while
// (PotPlayer with VC-1 after a seek) and does not depend on the stream's
// nominal frame rate, which only stands in when a picture has no duration.
// Backward steps and larger gaps are discontinuities.
inline bool MotionTimelineContinuous(LONGLONG previousStart,
                                     LONGLONG previousStop,
                                     LONGLONG nextStart,
                                     LONGLONG nominalDuration = 0)
{
    const double own = double(previousStop) - double(previousStart);
    const double duration = own > 0 && own <= 10000000                           ? own
                            : nominalDuration > 0 && nominalDuration <= 10000000 ? double(nominalDuration)
                                                                                 : 0;
    const double step = double(nextStart) - double(previousStart);
    return duration > 0 && step > 0 && step <= 2 * duration + 20000;
}

// A nominal sample duration can overlap the next millisecond-rounded PTS.
// Use the already buffered next frame to partition the output timeline; keep
// genuine discontinuities and the final frame's nominal end unchanged.
inline LONGLONG MotionIntervalEnd(LONGLONG start, LONGLONG stop, LONGLONG nextStart, LONGLONG nominalDuration = 0)
{
    return stop > start && MotionTimelineContinuous(start, stop, nextStart, nominalDuration) ? nextStart : stop;
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

// Display-synchronous Smooth Motion: every tick is bound to one display
// refresh (absolute DXGI refresh count) and at most one tick per refresh is
// presented, scheduled one refresh ahead of it. The reference clock and the
// display drift apart; a tick landing on an occupied refresh is surplus (fast
// clock) and an unclaimed refresh simply repeats (slow clock), so no picture
// ever queues up. A single clock reading can be off by refreshes (audio clock
// to QPC conversion), so ticks stay on consecutive refreshes and only the
// smoothed phase decides: once it is three quarters of a refresh off, one
// tick is left out or one refresh repeats, which leaves it a quarter off.
class VsyncSlots
{
    LONGLONG last = 0;
    double phase = 0;
    bool anchored = false;

  public:
    void Reset()
    {
        anchored = false;
    }

    double Phase() const
    {
        return phase;
    }

    // position: wanted refresh as a fractional refresh count. False: surplus.
    bool Assign(double position, LONGLONG& slot)
    {
        if (!std::isfinite(position))
            return false;
        const double offset = position - double(last + 1);
        // Seeks, restarts, display changes and ticks shown or dropped outside
        // the grid are not drift: anchor again.
        if (!anchored || offset < -4 || offset > 3)
        {
            last = slot = static_cast<LONGLONG>(std::llround(position));
            phase = position - double(last);
            anchored = true;
            return true;
        }
        if (std::abs(offset) <= 3)
            phase += (offset - phase) * .05;
        if (phase < -.75)
        {
            phase += 1;
            return false;
        }
        if (phase > .75)
        {
            phase -= 1;
            ++last;
        }
        slot = ++last;
        return true;
    }
};

// How many refreshes ahead refresh-locked ticks are presented (like madVR's
// "present several frames in advance"). One keeps latency lowest; more lets
// the display queue absorb render peaks. Automatic starts at one, adds a
// refresh after three tight ticks (left out because they missed their
// refresh) within two seconds and gives one back after a quiet minute.
// Ticks right after a raise, a start or a seek arrive tight until the queue
// has filled up; they must not raise it.
class PresentLead
{
    int fixed = 0, limit = 4, automatic = 1, tight = 0;
    ULONGLONG firstTight = 0, quietSince = 0, settled = 0;

  public:
    static constexpr int Maximum = 4;

    // setting: 0 automatic, 1-4 fixed; ceiling: highest lead the queue allows.
    void Configure(int setting, int ceiling)
    {
        fixed = (std::clamp)(setting, 0, Maximum);
        limit = (std::clamp)(ceiling, 1, Maximum);
        automatic = (std::min)(automatic, limit);
    }

    // Playback (re)starts: the queue is empty and refills.
    void Restart(ULONGLONG now)
    {
        settled = now + 3000;
        tight = 0;
    }

    int Current() const
    {
        return fixed ? (std::min)(fixed, limit) : automatic;
    }

    void Tight(ULONGLONG now)
    {
        if (fixed)
            return;
        quietSince = now;
        if (now < settled)
            return;
        if (!tight || now - firstTight > 2000)
        {
            firstTight = now;
            tight = 0;
        }
        if (++tight < 3)
            return;
        tight = 0;
        if (automatic < limit)
        {
            ++automatic;
            settled = now + 1000;
        }
    }

    void OnTime(ULONGLONG now)
    {
        if (fixed)
            return;
        if (!quietSince)
            quietSince = now;
        if (automatic > 1 && now - quietSince >= 60000)
        {
            --automatic;
            quietSince = now;
        }
    }
};

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
