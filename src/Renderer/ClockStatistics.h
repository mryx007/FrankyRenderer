#pragma once
// The "Clock" statistics row, like madVR's: how far the reference clock (the
// audio clock) deviates from real time and how often that, together with the
// display's real refresh rate, makes a frame repeat or drop.
#include <cmath>
#include <cstdio>
#include <string>

namespace ClockStatistics
{
// Rate of one clock against real time (QPC seconds), by a least-squares line
// through every reading since the last reset: an audio clock advances in steps
// of several milliseconds, a single pair of readings would be far too coarse.
class RateFit
{
    double origin = 0, base = 0, n = 0, sx = 0, sz = 0, sxx = 0, sxz = 0, last = 0;

  public:
    void Reset()
    {
        n = sx = sz = sxx = sxz = 0;
    }

    // seconds: real time; value: the other clock in seconds.
    void Add(double seconds, double value)
    {
        if (!std::isfinite(seconds) || !std::isfinite(value))
            return;
        if (n > 0 && (seconds < last || std::abs((value - base) - (seconds - origin)) > 1 + 0.01 * (seconds - origin)))
            Reset(); // a jump: another clock, a restart or a broken reading
        if (n == 0)
        {
            origin = seconds;
            base = value;
        }
        last = seconds;
        // The difference between both clocks against time; its slope is the deviation.
        const double x = seconds - origin, z = (value - base) - x;
        n += 1;
        sx += x;
        sz += z;
        sxx += x * x;
        sxz += x * z;
    }

    // Seconds the readings cover.
    double Span() const
    {
        return n > 1 ? last - origin : 0;
    }

    // Rate minus one, e.g. 0.00001 for a clock 0.001 % fast.
    double Deviation() const
    {
        const double denominator = n * sxx - sx * sx;
        return n > 1 && denominator > 0 ? (n * sxz - sx * sz) / denominator : 0;
    }
};

// The display's real refresh rate from DXGI's vsync times (exact, so the first
// and the latest reading suffice).
class RefreshFit
{
    double firstSeconds = 0, lastSeconds = 0;
    long long firstCount = 0, lastCount = 0;
    bool started = false;

  public:
    void Reset()
    {
        started = false;
    }

    void Add(double seconds, long long count, double nominalHz)
    {
        if (started && (count < lastCount || seconds < lastSeconds))
            started = false;
        if (!started)
        {
            firstSeconds = lastSeconds = seconds;
            firstCount = lastCount = count;
            started = true;
            return;
        }
        lastSeconds = seconds;
        lastCount = count;
        // A rate far from the nominal one is a new output, not drift.
        if (nominalHz > 0 && Span() > 2 && std::abs(Hz() / nominalHz - 1) > 0.01)
        {
            firstSeconds = seconds;
            firstCount = count;
        }
    }

    double Span() const
    {
        return started ? lastSeconds - firstSeconds : 0;
    }

    double Hz() const
    {
        return Span() > 0 ? double(lastCount - firstCount) / Span() : 0;
    }
};

inline constexpr double MeasuringSeconds = 30;

// " · 1 drop every 27.2 s": a drop when the display has fewer refreshes than
// the clock asks for (mismatch < 0), a repeat when it has more.
inline std::wstring Event(double mismatch)
{
    const wchar_t* what = mismatch > 0 ? L"repeat" : L"drop";
    const double seconds = 1 / std::abs(mismatch);
    wchar_t text[64];
    if (seconds >= 36000)
        swprintf_s(text, L" · less than 1 %ls in 10 h", what);
    else if (seconds >= 3600)
        swprintf_s(text, L" · 1 %ls every %.1f h", what, seconds / 3600);
    else if (seconds >= 60)
        swprintf_s(text, L" · 1 %ls every %.1f min", what, seconds / 60);
    else
        swprintf_s(text, L" · 1 %ls every %.1f s", what, seconds);
    return text;
}

struct Input
{
    double deviation = 0, clockSpan = 0; // reference clock against real time
    double displayHz = 0;                // the display's real refresh rate
    double nominalHz = 0;                // the rate Smooth Motion plans with
    double fps = 0;                      // the video's frame rate (stream time)
    bool smoothMotion = false;
};

// e.g. "+0.0012 % · 1 repeat every 14.2 min"; with Smooth Motion a drop or
// repeat is one of its pictures, otherwise a video frame.
inline std::wstring Text(const Input& in)
{
    if (in.clockSpan < MeasuringSeconds || !(in.displayHz > 0))
        return L"measuring…";
    wchar_t deviation[48];
    swprintf_s(deviation, L"%+.4f %%", in.deviation * 100);
    const double rate = 1 + in.deviation; // reference seconds per real second
    std::wstring text = deviation;
    if (in.smoothMotion)
    {
        // One picture per planned refresh of the reference clock; the display
        // shows its own number of refreshes.
        const double mismatch = in.displayHz - (in.nominalHz > 0 ? in.nominalHz : in.displayHz) * rate;
        return mismatch != 0 ? text + Event(mismatch) : text;
    }
    if (!(in.fps > 0))
        return text;
    // Refreshes per frame: whole or half (3:2) cadences repeat exactly.
    const double ratio = in.displayHz / (in.fps * rate), cadence = std::round(ratio * 2) / 2;
    if (cadence <= 0 || std::abs(ratio - cadence) > 0.01)
        return text + L" · uneven cadence";
    const double mismatch = in.displayHz - cadence * in.fps * rate;
    return mismatch != 0 ? text + Event(mismatch) : text;
}
} // namespace ClockStatistics
