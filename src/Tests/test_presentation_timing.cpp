#include "Renderer/PresentationMeasurement.h"
#define NOMINMAX
#include "Renderer/MotionTiming.h"
#include <algorithm>
#include <cstdio>
#include <utility>
#include <cstdlib>

static void Require(bool ok, const char* message)
{
    if (!ok)
    {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

int main()
{
    DurationMeasurement duration;
    Require(duration.RecentAverage(1000) == 0, "empty recent render average");
    duration.Add(1000, 12);
    for (uint64_t i = 1; i <= 5; ++i)
        duration.Add(1000 + i, .6);
    Require(std::abs(duration.RecentAverage(1005) - 2.5) < 1e-9,
            "one expensive source picture and five cache outputs form their mean, not their peak");
    duration.Add(6001, 1);
    Require(std::abs(duration.RecentAverage(6001) - (5 * .6 + 1) / 6) < 1e-9,
            "recent render average excludes samples older than five seconds");
    Require(duration.RecentAverage(11001) == 1, "five-second boundary remains included");
    Require(duration.RecentAverage(11002) == 0, "paused measurements expire without another sample");
    Require(std::abs(duration.Average() - 16. / 7) < 1e-9, "lifetime diagnostic average is retained");
    duration.Reset();
    Require(duration.RecentAverage(11002) == 0, "reset clears recent render average");
    puts("PASS rolling five-second render average, cached refreshes, expiration and reset");

    constexpr int64_t period = 100000, epoch = 1000000000;
    PresentationMeasurement original, fixed;
    original.Period(period);
    fixed.Period(period);
    for (uint32_t id = 1; id <= 10000; ++id)
    {
        const int64_t timeline = id * period;
        const int64_t clockNoise = id % 31 == 0 ? -2 * period : id % 7 == 0 ? period : 0;
        const int64_t due = epoch + timeline + clockNoise;
        original.Submit(id, due, true);
        fixed.Submit(id, due, true, timeline);
        original.Observe(id, id + 100, true, true, epoch + timeline + period);
        fixed.Observe(id, id + 100, true, true, epoch + timeline + period);
    }
    Require(original.glitches > 0, "noisy clock conversion reproduces false errors in continuous scanout");
    Require(fixed.glitches == 0 && fixed.repeatedRefreshes == 0, "stable media timeline rejects false repeats");
    printf("PASS noisy clock reproduction: old=%llu glitches, fixed=0\n", original.glitches);

    for (double fps : {23.976, 24., 25., 30., 50., 59.94, 60., 120.})
    {
        PresentationMeasurement cadence;
        cadence.Period(period);
        for (uint32_t id = 1; id <= 10000; ++id)
        {
            const int64_t timeline = static_cast<int64_t>(std::llround(id * 120. / fps * period));
            const auto refresh = static_cast<uint32_t>(std::llround(double(timeline) / period)) + 100;
            cadence.Submit(id, epoch + timeline, true, timeline);
            cadence.Observe(id, refresh, true, true, epoch + refresh * period);
        }
        Require(cadence.glitches == 0, "ordinary fractional/integer video cadence has no false errors");
    }
    puts("PASS ordinary 23.976/24/25/30/50/59.94/60/120 fps cadence");

    PresentationMeasurement stall;
    stall.Period(period);
    for (uint32_t id = 1; id <= 1000; ++id)
    {
        const int64_t timeline = id * period;
        const uint32_t extra = id >= 100 ? 3 : 0;
        stall.Submit(id, epoch + timeline, true, timeline);
        stall.Observe(id, id + 100 + extra, true, true, epoch + timeline + (1 + extra) * period);
    }
    Require(stall.glitches == 1 && stall.repeatedRefreshes == 3, "actual three-refresh stall remains visible");
    stall.Reset(false, true);
    stall.Submit(1001, epoch + 1001 * period, true, 0);
    stall.Observe(1001, 1200, true, true, epoch + 1001 * period + 7 * period);
    Require(stall.glitches == 2, "late first picture after restart remains visible");
    puts("PASS real missed refreshes and late restart are still counted");

    DropMeasurement drops;
    drops.Observe(14);
    drops.RestartSource();
    drops.Observe(2);
    Require(drops.total == 16, "real scheduler drops remain independent of display timing");
    puts("PASS independent real drop counts preserved");

    unsigned combinations = 0, injectedStalls = 0;
    constexpr double frequency = 10000000.;
    for (double fps : {15.,
                       23.976,
                       24000. / 1001,
                       24.,
                       25.,
                       29.97,
                       30000. / 1001,
                       30.,
                       48.,
                       50.,
                       59.94,
                       60000. / 1001,
                       60.,
                       100.,
                       119.88,
                       120000. / 1001,
                       120.})
        for (double hz :
             {24000. / 1001, 24., 25., 30., 48., 50., 60000. / 1001, 60., 100., 120000. / 1001, 120., 144., 165., 240.})
        {
            if (fps > hz)
                continue; // An overloaded cadence cannot display every source image.
            const double displayPeriod = frequency / hz;
            for (bool roundedMilliseconds : {false, true})
            {
                PresentationMeasurement cadence, delayed;
                cadence.Period(displayPeriod);
                delayed.Period(displayPeriod);
                for (uint32_t id = 1; id <= 4096; ++id)
                {
                    const int64_t timeline = std::llround(id * (roundedMilliseconds ? 1000. : frequency) / fps) *
                                             (roundedMilliseconds ? 10000 : 1);
                    const uint32_t refresh = static_cast<uint32_t>(std::ceil(timeline / displayPeriod)) + 100;
                    const int64_t noisyDue = epoch + timeline +
                                             static_cast<int64_t>((id % 23 == 0   ? -2
                                                                   : id % 11 == 0 ? 1
                                                                                  : 0) *
                                                                  displayPeriod);
                    const int64_t shown = epoch + std::llround((refresh - 100) * displayPeriod);
                    cadence.Submit(id, noisyDue, true, timeline);
                    cadence.Observe(id, refresh, true, true, shown);
                    const uint32_t extra = id >= 2048 ? 3 : 0;
                    delayed.Submit(id, noisyDue, true, timeline);
                    delayed.Observe(id,
                                    refresh + extra,
                                    true,
                                    true,
                                    shown + static_cast<int64_t>(std::llround(extra * displayPeriod)));
                }
                if (cadence.glitches || delayed.glitches == 0 || delayed.repeatedRefreshes < 2)
                {
                    printf("FAIL fps=%.9f hz=%.9f rounded=%d false=%llu real=%llu/%llu\n",
                           fps,
                           hz,
                           roundedMilliseconds,
                           cadence.glitches,
                           delayed.repeatedRefreshes,
                           delayed.glitches);
                    return 1;
                }
                ++combinations;
                ++injectedStalls;
            }
            // Smooth Motion outputs must remain on one unique display grid,
            // independently of the source's integer or fractional frame rate.
            PresentationMeasurement mixed;
            mixed.Period(displayPeriod);
            uint32_t id = 0;
            for (int frame = 0; frame < 512; ++frame)
            {
                const auto start = std::llround(frame * 1000. / fps) * 10000;
                const auto end = std::llround((frame + 1) * 1000. / fps) * 10000;
                for (const auto& tick : MotionTicks(start, end, displayPeriod))
                {
                    ++id;
                    const auto refresh = static_cast<uint32_t>(std::llround(tick.start / displayPeriod)) + 100;
                    mixed.Submit(id, epoch + tick.start, true, tick.start);
                    mixed.Observe(id, refresh, true, true, epoch + tick.start);
                }
            }
            Require(mixed.glitches == 0 && id > 0, "mixed integer/fractional frame rates keep accurate counters");
        }
    printf("PASS %u integer/fractional cadence and timestamp combinations, %u real stalls detected\n",
           combinations,
           injectedStalls);

    PresentationMeasurement variable;
    variable.Period(period);
    int64_t timeline = 0;
    uint32_t refresh = 100;
    for (uint32_t id = 1; id < 10000; ++id)
    {
        const auto step = id % 3 == 0 ? 5 : id % 3 == 1 ? 4 : 2;
        timeline += step * period;
        refresh += step;
        variable.Submit(id, epoch + timeline + (id % 7 == 0 ? -2 * period : 0), true, timeline);
        variable.Observe(id, refresh, true, true, epoch + timeline);
    }
    Require(variable.glitches == 0, "variable frame rate follows actual timestamps rather than nominal fps");
    puts("PASS variable frame rate with changing image intervals");

    PresentationMeasurement longRun;
    const double fractionalPeriod = frequency / (120000. / 1001);
    longRun.Period(fractionalPeriod);
    for (uint32_t id = 1; id <= 2000000; ++id)
    {
        const auto timeline = static_cast<int64_t>(std::llround(id * fractionalPeriod));
        longRun.Submit(id, epoch + timeline, true, timeline);
        longRun.Observe(id, id + 100, true, true, epoch + timeline);
    }
    Require(longRun.glitches == 0, "fractional refresh remains accurate over millions of presentations");
    puts("PASS 2,000,000 fractional-refresh presentations without accumulated rounding errors");

    // Reference clock 100 ppm fast against a 120 Hz scanout for one hour. Windowed
    // flip presentation normally reports one picture ahead of scanout before a
    // submit (measured); DXGI blocks Present once three are waiting.
    auto simulate = [](bool resync, uint64_t& skipped)
    {
        PresentBacklog backlog;
        uint32_t queued = 1, deepest = 0;
        uint64_t blocked = 0;
        double clock = 0;
        for (uint32_t refresh = 0; refresh < 120 * 3600; ++refresh)
        {
            clock += 1.0001;
            int ticks = 0;
            while (clock >= 1)
            {
                clock -= 1;
                ++ticks;
            }
            for (int t = 0; t < ticks; ++t)
            {
                if (resync && backlog.Skip(queued))
                    continue;
                if (queued >= 3)
                    ++blocked;
                else
                    ++queued;
            }
            deepest = std::max(deepest, queued);
            if (queued > 1)
                --queued;
        }
        skipped = backlog.skipped;
        return std::pair(deepest, blocked);
    };
    uint64_t skipped = 0;
    const auto drifting = simulate(false, skipped);
    Require(drifting.first == 3 && drifting.second > 0, "uncorrected clock drift fills the swapchain queue");
    const auto corrected = simulate(true, skipped);
    // Jitter may still meet a fresh surplus before the backlog is confirmed;
    // without resync every later jitter blocks for the rest of playback.
    Require(corrected.second * 20 < drifting.second, "resync drains the backlog instead of blocking indefinitely");
    Require(skipped >= 40 && skipped <= 60, "only the surplus pictures are left out");
    printf("PASS clock drift: %llu blocked presents uncorrected, %llu with %llu ticks left out per hour\n",
           static_cast<unsigned long long>(drifting.second),
           static_cast<unsigned long long>(corrected.second),
           static_cast<unsigned long long>(skipped));

    PresentBacklog jitter;
    for (int tick = 0; tick < 120 * 3600; ++tick)
        jitter.Skip(tick % 37 == 0 || tick % 53 < 20 ? 2 : 1);
    Require(!jitter.skipped, "ordinary queue jitter never leaves out a tick");
    // Refresh-locked presents land exactly on a vsync and read one or two
    // ahead by chance, even two for a full second; only three is a backlog.
    PresentBacklog locked;
    for (int tick = 0; tick < 120 * 600; ++tick)
        locked.Skip(tick % 600 < 300 ? 2 : 1 + ((static_cast<unsigned>(tick) * 2654435761u) >> 31), 3);
    Require(!locked.skipped, "refresh-locked presents on a vsync boundary are no backlog");
    for (unsigned tick = 0; tick < PresentBacklog::Ticks; ++tick)
        locked.Skip(3, 3);
    Require(locked.skipped == 1, "a lasting third queued picture still is");
    puts("PASS queue jitter without a lasting backlog leaves every tick in place");

    // Scanout shows one picture per refresh, never before it was submitted.
    // Ticks come from a reference clock off by `ppm`; their reported due time
    // carries the clock conversion noise of the first test.
    struct Scanout
    {
        uint64_t skipped = 0;
        double worst = 0;
    };

    auto scanout = [&](double ppm, bool resync)
    {
        PresentationMeasurement measurement;
        measurement.Period(period);
        Scanout result;
        int64_t slot = 0;
        uint32_t id = 0;
        for (int64_t tick = 1; tick <= 120 * 3600; ++tick)
        {
            if (resync && measurement.drift.TakeSurplus())
            {
                ++result.skipped;
                continue;
            }
            const int64_t timeline = tick * period;
            const auto due = static_cast<int64_t>(std::llround(timeline / (1 + ppm * 1e-6)));
            slot = std::max(slot + 1, (due + period - 1) / period);
            const int64_t actual = slot * period;
            if (tick > 600)
                result.worst = std::max(result.worst, double(actual - due) / period);
            const int64_t noise = tick % 31 == 0 ? -2 * period : tick % 7 == 0 ? period : 0;
            measurement.Submit(++id, epoch + due + noise, true, timeline);
            measurement.Observe(id, static_cast<uint32_t>(slot + 100), true, true, epoch + actual);
        }
        return result;
    };
    const auto ahead = scanout(100, false), resynced = scanout(100, true);
    Require(ahead.worst > 40, "a fast reference clock keeps adding scanout delay");
    Require(resynced.worst < 1.5 && resynced.skipped >= 40 && resynced.skipped <= 44,
            "latency drift leaves out exactly the surplus ticks");
    const auto steady = scanout(0, true), behind = scanout(-100, true);
    Require(!steady.skipped && !behind.skipped, "noise and a slow clock leave every tick in place");
    printf("PASS reference clock 100 ppm fast: delay %.1f refreshes uncorrected, %.1f with %llu ticks left out\n",
           ahead.worst,
           resynced.worst,
           static_cast<unsigned long long>(resynced.skipped));
}
