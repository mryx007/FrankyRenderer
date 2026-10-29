#define NOMINMAX
#include "Renderer/MotionTiming.h"
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
    puts(
        "PASS captured overlap, rounded film/50/59.94/60/120 fps, exact display grid, empty intervals, faster-source fallback, EOS and discontinuities");
}
