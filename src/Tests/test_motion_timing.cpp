#define NOMINMAX
#include "Renderer/MotionTiming.h"
#include <algorithm>
#include <cmath>
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
    Check(MotionIntervalEnd(100000, 517083, 1300000) == 517083, "large discontinuity is not bridged");
    Check(MotionIntervalEnd(100000, 517083, 934166) == 934166, "one missing frame is bridged");
    // PotPlayer with VC-1 after a seek: stops miss the next start by 31 ms.
    Check(MotionTimelineContinuous(0, 417083, 730000, 417083) && MotionTimelineContinuous(0, 417083, 104000, 417083),
          "irregular stops after a seek keep the timeline");
    Check(!MotionTimelineContinuous(0, 417083, 0, 417083) && !MotionTimelineContinuous(417083, 834166, 0, 417083),
          "duplicate and backward starts are discontinuities");
    Check(MotionTimelineContinuous(0, 417083, 417083, 166833) && MotionTimelineContinuous(0, 417083, 730000, 166833),
          "a wrong nominal duration does not matter while pictures carry their own");
    Check(MotionTimelineContinuous(0, 0, 417083, 417083) && !MotionTimelineContinuous(0, 0, 417083, 0),
          "the nominal duration stands in for a picture without one");
    // The previous rule: a next start within 2 ms of the previous stop.
    for (LONGLONG length : {166833LL, 333667LL, 400000LL, 417083LL, 1000000LL})
        for (LONGLONG offset : {-20000LL, -1LL, 0LL, 1LL, 20000LL})
            Check(MotionTimelineContinuous(5000000, 5000000 + length, 5000000 + length + offset),
                  "every start the previous rule accepted still continues");
    Check(!MotionTimelineContinuous(0, 417083, 2000000, 417083), "a gap of several frames is a discontinuity");
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

    // Presenting in advance: a tick every 8 ms, 125 per second.
    PresentLead lead;
    lead.Configure(0, 2);
    ULONGLONG now = 1000;
    for (int i = 0; i < 125 * 60; ++i, now += 8)
        lead.OnTime(now);
    Check(lead.Current() == 1, "automatic presents one refresh ahead while every tick is on time");
    lead.Restart(now);
    for (int i = 0; i < 300; ++i, now += 8)
        lead.Tight(now);
    Check(lead.Current() == 1, "tight ticks while playback starts or seeks do not add latency");
    for (int i = 0; i < 125 * 60; ++i, now += 8)
        lead.OnTime(now);
    for (int i = 0; i < 600; ++i, now += 8)
        if (i % 300 == 0)
            lead.Tight(now);
        else
            lead.OnTime(now);
    Check(lead.Current() == 1, "isolated tight ticks do not add latency");
    for (int i = 0; i < 3; ++i, now += 100)
        lead.Tight(now);
    Check(lead.Current() == 2, "a burst of tight ticks adds one refresh");
    for (int i = 0; i < 20; ++i, now += 8)
        lead.Tight(now);
    Check(lead.Current() == 2, "ticks tight while the queue fills do not raise again");
    for (int i = 0; i < 5; ++i, now += 300)
        lead.Tight(now);
    Check(lead.Current() == 2, "the frame latency limit caps automatic");
    for (int i = 0; i < 125 * 59; ++i, now += 8)
        lead.OnTime(now);
    Check(lead.Current() == 2, "less than a quiet minute keeps the lead");
    for (int i = 0; i < 125 * 2; ++i, now += 8)
        lead.OnTime(now);
    Check(lead.Current() == 1, "a quiet minute gives the refresh back");
    lead.Configure(3, 2);
    Check(lead.Current() == 2, "a fixed value never exceeds what the queue allows");
    lead.Configure(3, 4);
    for (int i = 0; i < 10; ++i, now += 100)
        lead.Tight(now);
    Check(lead.Current() == 3, "a fixed value ignores tight ticks");
    lead.Configure(0, 4);
    Check(lead.Current() == 1, "automatic resumes where it was");
    puts("PASS present in advance: automatic adds a refresh after bursts of tight ticks, gives it back after a quiet "
         "minute, respects the frame latency limit and fixed values");
    Check(WholeMilliseconds(0, 15900, 1000000) == 15, "a wait of 15.9 ms takes out 15 ms, never more");
    Check(WholeMilliseconds(0, 999, 1000000) == 0, "a wait under 1 ms takes out nothing");
    Check(WholeMilliseconds(500, 400, 1000000) == 0 && WholeMilliseconds(0, 100, 0) == 0,
          "no negative time and no division by zero");
    Check(WholeMilliseconds(0, 41708000, 1000000000) == 41, "a whole frame of waiting at 23.976 fps");
    puts("PASS deliberate waits leave the render time in whole milliseconds, rounded down");
    {
        // 23.976 fps on 144.001 Hz with millisecond time stamps and jitter:
        // due times near a refresh boundary. Every frame keeps 6 refreshes,
        // one in about 7 s gets 7, never 5.
        FrameSlots slots;
        const double refreshes = 144.001 / (24000.0 / 1001);
        unsigned seed = 1;
        auto noise = [&]
        {
            seed = seed * 1103515245 + 12345;
            return double((seed >> 8) % 2001) / 1000 - 1;
        };
        LONGLONG previous = 0;
        int sevens = 0, others = 0;
        for (int i = 0; i < 24 * 600; ++i)
        {
            const double ms = std::floor(i * 1001.0 / 24 + 0.5);
            const LONGLONG slot = slots.Assign(1000.0 + ms * 0.144001 + 0.2 * noise(), refreshes);
            if (i)
            {
                sevens += slot - previous == 7;
                others += slot - previous != 6 && slot - previous != 7;
            }
            previous = slot;
        }
        Check(others == 0, "frames without Smooth Motion keep 6 or 7 refreshes");
        Check(sevens >= 80 && sevens <= 90, "one extra refresh about every 6.9 s, no more");
        FrameSlots pulldown;
        previous = 0;
        int twos = 0, threes = 0;
        for (int i = 0; i < 2400; ++i)
        {
            const LONGLONG slot = pulldown.Assign(1000.0 + i * 2.5 + 0.1 * noise(), 2.5);
            if (i)
            {
                twos += slot - previous == 2;
                threes += slot - previous == 3;
            }
            previous = slot;
        }
        Check(twos + threes == 2399 && std::abs(twos - threes) <= 1, "3:2 cadence alternates 2 and 3 refreshes");
        Check(pulldown.Assign(1000.0 + 50000.5, 2.5) == 51001, "a jump anchors on the next refresh after the due time");
    }
    {
        FrameSlots fast;
        const double advance = 59.973 / 60.0; // refreshes per chosen frame
        LONGLONG previous = 0;
        int zeros = 0, ones = 0, others = 0;
        for (int i = 0; i < 60 * 600; ++i)
        {
            const LONGLONG slot = fast.Assign(1000.0 + i * advance, 1.0);
            if (i)
            {
                zeros += slot == previous;
                ones += slot == previous + 1;
                others += slot != previous && slot != previous + 1;
            }
            previous = slot;
        }
        Check(others == 0, "a faster clock never skips a refresh");
        Check(zeros >= 14 && zeros <= 18, "60 fps on 59.973 Hz leaves out one frame about every 37 s");
    }
    puts(
        "PASS frames without Smooth Motion: 6 or 7 refreshes at 23.976 fps on 144 Hz despite jitter, 3:2 alternates, seeks anchor anew");
    puts(
        "PASS captured overlap, rounded film/50/59.94/60/120 fps, exact display grid, empty intervals, faster-source fallback, EOS and discontinuities");
}
