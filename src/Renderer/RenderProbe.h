#pragma once
#include <windows.h>
#include <array>
#include <deque>
#include <cmath>
#include <cstdint>

// CPU wall time only: attribute one measured frame, never combine peaks
// from different frames. Enabled only while the statistics are visible.
class RenderProbe
{
  public:
    enum Stage
    {
        Bars,
        Setup,
        Rtx,
        Hooks,
        PlayerOverlay,
        StatisticsInfo,
        StatisticsUpload,
        Analysis,
        Draw,
        BarsMap,
        BarsScan,
        BarsRender,
        BarsFlush,
        BarsCopy,
        Count
    };

    inline static constexpr const wchar_t* Names[] = {L"Black bars",
                                                      L"Setup",
                                                      L"RTX",
                                                      L"Shader setup",
                                                      L"Player OSD",
                                                      L"Stats query",
                                                      L"Stats upload",
                                                      L"Analysis OSD",
                                                      L"libplacebo",
                                                      L"Bars readback",
                                                      L"Bars CPU scan",
                                                      L"Bars GPU submit",
                                                      L"Bars flush",
                                                      L"Bars copy"};

    struct Sample
    {
        uint64_t time = 0;
        double total = 0;
        std::array<double, Count> ms = {};
    };

  private:
    Sample current;
    std::deque<Sample> recent;
    bool enabled = false;

  public:
    class Scope
    {
        RenderProbe& owner;
        Stage stage = Bars;
        LARGE_INTEGER last = {}, frequency = {};

      public:
        Scope(RenderProbe& probe, bool active) : owner(probe)
        {
            owner.enabled = active;
            owner.current = {};
            if (active)
            {
                QueryPerformanceFrequency(&frequency);
                QueryPerformanceCounter(&last);
            }
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        void Next(Stage next)
        {
            if (!owner.enabled)
                return;
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            owner.current.ms[stage] += 1000. * (now.QuadPart - last.QuadPart) / frequency.QuadPart;
            last = now;
            stage = next;
        }

        ~Scope()
        {
            Next(stage);
        }
    };

    void Record(uint64_t now, double total)
    {
        if (!enabled)
            return;
        current.time = now;
        current.total = total;
        Add(current);
    }

    void Add(const Sample& sample)
    {
        if (!std::isfinite(sample.total) || sample.total < 0)
            return;
        recent.push_back(sample);
        while (!recent.empty() && (sample.time - recent.front().time > 5000 || recent.size() > 8192))
            recent.pop_front();
    }

    const Sample* Peak(uint64_t now) const
    {
        const Sample* peak = nullptr;
        for (const auto& sample : recent)
            if (now - sample.time <= 5000 && (!peak || sample.total > peak->total))
                peak = &sample;
        return peak;
    }

    void Reset()
    {
        recent.clear();
        current = {};
        enabled = false;
    }
};
