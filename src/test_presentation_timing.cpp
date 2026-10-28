#include "PresentationMeasurement.h"
#define NOMINMAX
#include "MotionTiming.h"
#include <cstdio>
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
}
