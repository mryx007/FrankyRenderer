#define NOMINMAX
#include "Renderer/RenderProbe.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

int main()
{
    RenderProbe probe;
    RenderProbe::Sample a;
    a.time = 1000;
    a.total = 6.46;
    a.ms[RenderProbe::StatisticsInfo] = 5.9;
    a.ms[RenderProbe::Draw] = .3;
    probe.Add(a);
    auto b = a;
    b.time = 2000;
    b.total = 4;
    b.ms[RenderProbe::StatisticsInfo] = .1;
    b.ms[RenderProbe::Draw] = 3.8;
    probe.Add(b);
    const auto* peak = probe.Peak(3000);
    Check(peak && peak->total == 6.46 && peak->ms[RenderProbe::StatisticsInfo] == 5.9 &&
              peak->ms[RenderProbe::Draw] == .3,
          "stages belong to the highest-total frame, not independent maxima");
    peak = probe.Peak(6001);
    Check(peak && peak->total == 4, "peak expires after five seconds");
    Check(!probe.Peak(7001), "no expired peak is shown");
    probe.Reset();
    {
        RenderProbe::Scope scope(probe, false);
        scope.Next(RenderProbe::Draw);
    }
    probe.Record(8000, 9);
    Check(!probe.Peak(8000), "no samples collected with statistics disabled");
    {
        RenderProbe::Scope scope(probe, true);
        scope.Next(RenderProbe::StatisticsInfo);
        scope.Next(RenderProbe::Draw);
    }
    probe.Record(9000, 2);
    Check(probe.Peak(9000) && probe.Peak(9000)->total == 2, "enabled render scope is committed");
    probe.Reset();
    Check(!probe.Peak(9000), "reset clears diagnostics");
    puts("PASS render peak attribution, five-second expiration, disabled capture and reset");
}
