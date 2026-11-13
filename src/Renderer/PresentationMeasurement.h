#pragma once
#include <cstdint>
#include <deque>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

// Smooth Motion ticks follow the DirectShow reference clock, scanout follows
// the display. A slightly fast clock adds one surplus picture every few
// minutes; it waits in DXGI's queue and its delay persists. Track the scanout
// latency of timed presents (actual minus due) against its lowest level since
// the last restart. Within one refresh that is only the phase between both
// clocks: leaving out a tick there would just repeat a picture. A full
// refresh beyond it is a queued surplus picture; then one tick is left out.
// A slow clock needs nothing: scanout repeats and the level follows it down.
class ClockDrift
{
    double reference = 0, smoothed = 0;
    unsigned seeded = 0, outliers = 0;
    bool surplus = false;

  public:
    static constexpr unsigned Seed = 60;

    void Reset()
    {
        seeded = outliers = 0;
        surplus = false;
    }

    // latency and period in the same clock units.
    void Observe(double latency, double period)
    {
        if (!std::isfinite(latency) || !(period > 0))
            return;
        if (seeded < Seed)
        {
            smoothed = seeded ? smoothed + (latency - smoothed) / (seeded + 1) : latency;
            if (++seeded == Seed)
                reference = smoothed;
            return;
        }
        // Clock conversion noise and isolated stalls are not drift; a lasting
        // jump (new output path) re-establishes the level.
        if (std::abs(latency - smoothed) > 2 * period)
        {
            if (++outliers > Seed)
                Reset();
            return;
        }
        outliers = 0;
        smoothed += (latency - smoothed) * .02;
        reference = std::min(reference, smoothed);
        if (smoothed > reference + period * 1.25)
        {
            surplus = true;
            smoothed -= period;
        }
    }

    bool TakeSurplus()
    {
        return std::exchange(surplus, false);
    }
};

// DXGI feedback is delayed. These are acknowledged presentation IDs, not a
// physical driver queue. No clock, sleep, flush or scheduling calls live here.
class PresentationMeasurement
{
    struct Ticket
    {
        uint32_t id;
        int64_t due;
        int64_t timeline;
        bool timed;
    };

    std::deque<Ticket> tickets;
    uint32_t submitted = 0, observed = 0, anchorRefresh = 0;
    int64_t anchorDue = 0;
    double period = 0;
    bool anchored = false, haveLatency = false;
    double baselineLatency = 0;

  public:
    uint64_t confirmed = 0, repeatedRefreshes = 0, glitches = 0, resets = 0, submissions = 0;
    bool feedback = false, timing = false, haveCounters = false;
    uint32_t pending = 0;
    ClockDrift drift;

    void Reset(bool counters = true, bool keepLatency = false)
    {
        tickets.clear();
        drift.Reset();
        submitted = observed = 0;
        anchored = feedback = timing = false;
        pending = 0;
        if (!keepLatency || counters)
            haveLatency = false;
        if (counters)
        {
            confirmed = repeatedRefreshes = glitches = resets = submissions = 0;
            haveCounters = false;
        }
    }

    void Disjoint()
    {
        tickets.clear();
        drift.Reset();
        anchored = feedback = timing = haveLatency = false;
        pending = 0;
        observed = 0;
        ++resets;
    }

    void Unavailable()
    {
        anchored = feedback = timing = false;
    }

    void Period(double next)
    {
        if (!std::isfinite(next) || next <= 0)
        {
            period = 0;
            anchored = timing = false;
            return;
        }
        if (period && std::abs(next / period - 1) > .001)
            Disjoint();
        period = next;
    }

    void Submit(uint32_t id, int64_t due, bool timed)
    {
        Submit(id, due, timed, due);
    }

    void Submit(uint32_t id, int64_t due, bool timed, int64_t timeline)
    {
        if (!id || id == submitted)
            return;
        if (id < submitted)
            Disjoint();
        submitted = id;
        ++submissions;
        tickets.push_back({id, due, timeline, timed && due > 0});
        if (tickets.size() > 512)
        {
            tickets.pop_front();
            anchored = timing = false;
        }
    }

    void Observe(uint32_t id, uint32_t refresh, bool valid, bool stableCadence = true, int64_t actualQpc = 0)
    {
        if (!valid || !id || !refresh || id > submitted)
        {
            Unavailable();
            return;
        }
        feedback = true;
        pending = submitted - id;
        if (id == observed)
            return;
        if (id < observed)
        {
            Disjoint();
            return;
        }
        observed = id;
        auto ticket = std::find_if(tickets.begin(), tickets.end(), [&](auto& t) { return t.id == id; });
        if (ticket == tickets.end())
        {
            anchored = timing = false;
            return;
        }
        const auto current = *ticket;
        tickets.erase(tickets.begin(), std::next(ticket));
        if (!stableCadence)
        {
            anchored = timing = haveLatency = false;
            return;
        }
        if (!current.timed || period <= 0)
        {
            anchored = timing = false;
            return;
        }
        ++confirmed;
        int64_t late = 0;
        bool assessed = false;
        if (anchored && current.timeline > anchorDue && refresh >= anchorRefresh)
        {
            const auto planned = static_cast<int64_t>(anchorRefresh) +
                                 static_cast<int64_t>(std::llround((current.timeline - anchorDue) / period));
            late = static_cast<int64_t>(refresh) - planned;
            assessed = true;
            // One refresh of tolerance for phase quantization/compositor jitter.
            // Count extra refreshes only for >1-vsync lateness; ordinary 3:2,
            // 24->144 Hz holds, fields and Smooth Motion use their media timeline.
            // A fresh audio-clock/QPC conversion can jitter or run backwards;
            // that must not report a missed refresh in a continuous scanout.
        }
        else if (anchored)
        {
            anchored = false;
            timing = false;
        }
        const bool clocked = actualQpc > 0;
        const double latency = static_cast<double>(actualQpc) - current.due;
        if (clocked)
            drift.Observe(latency, period);
        // After seek/flush retain the established output latency, not the old
        // media timestamp/refresh anchor. This can assess the first returning
        // frame instead of blindly accepting any restart delay as a new normal.
        if (!anchored && haveLatency && clocked)
        {
            late = static_cast<int64_t>(std::llround((latency - baselineLatency) / period));
            assessed = true;
        }
        if (assessed)
        {
            timing = haveCounters = true;
            if (late > 1)
            {
                repeatedRefreshes += static_cast<uint64_t>(late);
                ++glitches;
                anchored = false;
            }
            else if (late < -1)
                anchored = false;
        }
        if (clocked && (!assessed || late <= 1))
        {
            baselineLatency = haveLatency && !(assessed && late < -1) ? baselineLatency * .9 + latency * .1 : latency;
            haveLatency = true;
        }
        if (!anchored)
        {
            anchorDue = current.timeline;
            anchorRefresh = refresh;
            anchored = true;
        }
    }
};

// Smooth Motion ticks follow the DirectShow reference clock, scanout follows
// the display. When that clock runs slightly fast, one surplus picture
// accumulates every few minutes until DXGI's queue is full: Present blocks,
// rendering waits for a back buffer and DirectShow starts dropping frames.
// Pause drains it, so it only shows after long playback. Ordinary jitter
// briefly queues two deep; a drift backlog never drains on its own. Leave out
// one interpolated tick only after a full run of ticks without relief.
class PresentBacklog
{
    unsigned run = 0;

  public:
    static constexpr unsigned Ticks = 120;
    uint64_t skipped = 0;

    void Reset()
    {
        run = 0;
    }

    // queued: presents submitted but not yet scanned out, before this one.
    // depth: the lowest backlog. Refresh-locked presents land exactly on a
    // vsync and read their lead or one more by chance; only lead + 2 is one.
    bool Skip(uint32_t queued, uint32_t depth = 2)
    {
        if (queued < depth)
        {
            run = 0;
            return false;
        }
        if (++run < Ticks)
            return false;
        run = 0;
        ++skipped;
        return true;
    }
};

// DirectShow restarts its raw drop counter at OnStartStreaming. Accumulate
// increments across those source epochs instead of losing the previous total.
class DropMeasurement
{
    int previous = 0;
    bool seen = false;

  public:
    uint64_t total = 0;

    void RestartSource()
    {
        seen = false;
    }

    void Reset()
    {
        total = 0;
    }

    void Observe(int value)
    {
        if (value < 0)
            return;
        total += !seen || value < previous ? static_cast<uint64_t>(value) : static_cast<uint64_t>(value - previous);
        previous = value;
        seen = true;
    }
};

class DurationMeasurement
{
    std::deque<std::pair<uint64_t, double>> recent;

  public:
    uint64_t count = 0;
    double total = 0, last = 0;

    void Reset()
    {
        recent.clear();
        count = 0;
        total = last = 0;
    }

    void Add(uint64_t now, double ms)
    {
        if (!std::isfinite(ms) || ms < 0)
            return;
        last = ms;
        total += ms;
        ++count;
        recent.emplace_back(now, ms);
        while (!recent.empty() && (now - recent.front().first > 5000 || recent.size() > 8192))
            recent.pop_front();
    }

    double Average() const
    {
        return count ? total / count : 0;
    }

    // The visible load follows current playback, rather than all frames since
    // playback started. Ignore stale samples even when playback is paused.
    double RecentAverage(uint64_t now) const
    {
        double sum = 0;
        size_t samples = 0;
        for (const auto& sample : recent)
            if (now - sample.first <= 5000)
            {
                sum += sample.second;
                ++samples;
            }
        return samples ? sum / samples : 0;
    }

    double Maximum(uint64_t now) const
    {
        double value = 0;
        for (auto& s : recent)
            if (now - s.first <= 5000)
                value = std::max(value, s.second);
        return value;
    }
};
