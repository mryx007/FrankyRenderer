#define NOMINMAX
#include "Renderer/MotionTiming.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

int main()
{
    // Captured PTS values: a 41.7083 ms nominal duration overlaps the next
    // frame, which starts only 41 ms later. Both formerly emitted tick 44009.
    const LONGLONG a = 3667000000, b = 3667410000, duration = 417083;
    const double period = 1e7 / 120;
    const auto old = MotionTicks(a, a + duration, period), next = MotionTicks(b, b + duration, period);
    bool duplicate = false;
    for (auto& x : old)
        for (auto& y : next)
            if (x.start == y.start)
                duplicate = true;
    Check(duplicate, "captured timestamp pair reproduces duplicate output");
    const auto fixed = MotionTicks(a, MotionIntervalEnd(a, a + duration, b), period);
    Check(!fixed.empty() && !next.empty() && fixed.back().start < next.front().start,
          "captured pair has unique output times");
    for (double fps : {23.976, 24., 25., 29.97, 50., 59.94, 60., 120.})
        for (double hz : {59.94, 60., 120., 144.})
        {
            const double p = 1e7 / hz;
            const auto nominal = static_cast<LONGLONG>(std::llround(1e7 / fps));
            if (!MotionCadenceSupported(nominal, p))
            {
                Check(fps > hz, "only faster streams bypass the display grid");
                continue;
            }
            LONGLONG previous = -1;
            size_t count = 0;
            LONGLONG boundary = 0;
            for (int i = 0; i < 2000; i++)
            {
                const auto start = static_cast<LONGLONG>(std::llround(i * 1000. / fps)) * 10000;
                boundary = static_cast<LONGLONG>(std::llround((i + 1) * 1000. / fps)) * 10000;
                const auto stop = start + static_cast<LONGLONG>(std::llround(1e7 / fps));
                for (const auto& tick : MotionTicks(start, MotionIntervalEnd(start, stop, boundary), p))
                {
                    Check(tick.start > previous, "strictly increasing output times across rounded PTS boundaries");
                    if (previous >= 0)
                        Check(std::abs(double(tick.start - previous) - p) <= 1, "no missing output refresh");
                    Check(tick.start >= start && tick.start < boundary && tick.position >= 0 && tick.position < 1,
                          "timestamp and mix position stay inside frame");
                    previous = tick.start;
                    ++count;
                }
            }
            size_t expected = 0;
            while (std::llround(expected * p) < boundary)
                ++expected;
            Check(count == expected, "exact output count over 2000 source frames");
        }
    Check(MotionTicks(170000, 330000, 1e7 / 60).empty() && MotionIntervalValid(170000, 330000, 1e7 / 60) &&
              MotionCadenceSupported(166667, 1e7 / 60),
          "valid rounded interval without a display tick does not trigger fallback");
    Check(!MotionCadenceSupported(83333, 1e7 / 60), "120 fps on 60 Hz keeps ordinary delivery");
    Check(!MotionCadenceSupported(0, 1e7 / 60) && !MotionCadenceSupported(166667, 0),
          "unknown source or display cadence keeps ordinary delivery");
    Check(!MotionIntervalValid(10, 10, 1e7 / 60) && !MotionIntervalValid(10, 9, 1e7 / 60) &&
              !MotionIntervalValid(0, 10000001, 1e7 / 60),
          "invalid intervals are rejected");
    Check(MotionIntervalEnd(100000, 517083, 0) == 517083, "EOS retains nominal stop");
    Check(MotionIntervalEnd(100000, 517083, 800000) == 517083, "large discontinuity is not bridged");
    Check(MotionIntervalEnd(100000, 517083, 90000) == 517083, "backward seek is not bridged");
    Check(MotionIntervalEnd(100000, 90000, 520000) == 90000, "invalid interval remains invalid");

    // One hour at 120 Hz with the reference clock 100 ppm fast, exact and
    // 100 ppm slow, plus clock conversion jitter of up to 0.4 refresh and
    // readings two refreshes early.
    struct Run
    {
        int surplus = 0, repeats = 0, widest = 0;
    };

    auto run = [](double ppm)
    {
        VsyncSlots slots;
        Run result;
        LONGLONG previous = 0;
        bool first = true;
        for (int tick = 0; tick < 120 * 3600; ++tick)
        {
            const double jitter = tick % 31 == 0 ? -2 : (static_cast<LONGLONG>(tick) * 7919 % 9 - 4) * .1;
            LONGLONG slot = 0;
            if (!slots.Assign(5000.3 + tick / (1 + ppm * 1e-6) + jitter, slot))
            {
                ++result.surplus;
                continue;
            }
            // The first second settles an anchor taken from a single reading.
            if (!first && tick >= 120)
            {
                result.repeats += int(slot - previous - 1);
                result.widest = std::max(result.widest, int(slot - previous));
            }
            first = false;
            previous = slot;
        }
        return result;
    };
    const auto fast = run(100), exact = run(0), slow = run(-100);
    Check(fast.surplus >= 42 && fast.surplus <= 44 && !fast.repeats, "fast clock leaves out only surplus ticks");
    Check(!exact.surplus && !exact.repeats, "jitter alone never leaves out or repeats a refresh");
    Check(!slow.surplus && slow.repeats >= 42 && slow.repeats <= 44 && slow.widest == 2,
          "slow clock repeats single refreshes only");
    VsyncSlots slots;
    LONGLONG slot = 0;
    Check(slots.Assign(9000.2, slot) && slot == 9000 && slots.Assign(9001.1, slot) && slot == 9001,
          "consecutive refreshes");
    Check(slots.Assign(9000.0, slot) && slot == 9002 && slots.Assign(9005.1, slot) && slot == 9003,
          "single readings refreshes off do not leave out or repeat");
    Check(slots.Assign(9004.9, slot) && slot == 9004, "a few ticks shown outside the grid continue it");
    Check(slots.Assign(9009.2, slot) && slot == 9009, "ticks dropped outside the grid anchor again");
    Check(slots.Assign(4000.4, slot) && slot == 4000, "a backward seek anchors again");
    Check(slots.Assign(4500.6, slot) && slot == 4501, "a forward jump anchors again");
    printf("PASS display-synchronous refreshes: %d surplus (fast), %d repeats (slow), none with jitter alone\n",
           fast.surplus,
           slow.repeats);
    puts(
        "PASS captured overlap, rounded film/50/59.94/60/120 fps, exact display grid, empty intervals, faster-source fallback, EOS and discontinuities");
}
