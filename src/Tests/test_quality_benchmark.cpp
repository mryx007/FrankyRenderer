// The quality measurement renders on the display's GPU and keeps exactly the
// upgrades that fit its budget: none for an impossible budget, all for a
// generous one.
#include "Renderer/QualityBenchmark.h"
#include <cstdio>
#include <stdexcept>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        BenchmarkTarget display;
        Require(FindBenchmarkTarget(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), display), "display target");
        Require(display.width > 0 && display.height > 0 && display.refresh > 0, "display mode");

        std::atomic<float> progress = 0;
        BenchmarkResult result;
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(PlaceboSettings{}, display, &progress);
        }
        Require(result.ok && progress == 1.f, "measurement completes");
        Require(result.milliseconds <= result.budget || !result.upgrades, "chosen settings fit the budget");
        printf("Display %dx%d @ %.0f Hz: %ls, %.2f of %.2f ms\n",
               display.width,
               display.height,
               display.refresh,
               DescribeMeasuredQuality(result.upgrades).c_str(),
               result.milliseconds,
               result.budget);

        auto impossible = display;
        impossible.refresh = 1e6; // 0.0005 ms
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(PlaceboSettings{}, impossible);
        }
        Require(result.ok && !result.upgrades, "nothing fits an impossible budget");

        auto generous = display;
        generous.refresh = .25; // 2 s
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(PlaceboSettings{}, generous);
        }
        Require(result.ok && result.upgrades == 127, "every upgrade fits a generous budget");
        PlaceboSettings applied;
        ApplyMeasuredQuality(applied, result.upgrades);
        Require(MatchQualityPreset(applied) == 2, "all upgrades equal the High Quality preset");

        // HDR output (passthrough) renders into a 10-bit target.
        PlaceboSettings hdr;
        hdr.hdrMode = 1;
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(hdr, display);
        }
        Require(result.ok, "measurement with HDR output");

        // The real profile, whatever its other options are.
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(LoadSettings(), display);
        }
        Require(result.ok, "measurement with the stored settings");

        std::atomic<bool> cancel = true;
        {
            QualityBenchmark benchmark;
            result = benchmark.Run(PlaceboSettings{}, display, nullptr, &cancel);
        }
        Require(!result.ok, "a cancelled measurement reports no result");
        puts("PASS quality measurement: display GPU and mode, budget kept, none/all upgrades at the limits, cancel");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL %s\n", e.what());
        return 1;
    }
}
