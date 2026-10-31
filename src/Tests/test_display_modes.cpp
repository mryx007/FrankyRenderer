#define NOMINMAX
#include "Renderer/DisplayModes.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void Check(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

static int Pick(const wchar_t* list, double fps)
{
    std::vector<DisplayModes::Mode> modes;
    Check(DisplayModes::Parse(list, modes), "list parses");
    return DisplayModes::Choose(modes, fps);
}

int main()
{
    try
    {
        using DisplayModes::RateOf;
        Check(std::abs(RateOf(23) - 24000. / 1001) < 1e-9, "23 is 23.976");
        Check(std::abs(RateOf(59) - 60000. / 1001) < 1e-9, "59 is 59.94");
        Check(std::abs(RateOf(119) - 120000. / 1001) < 1e-9, "119 is 119.88");
        Check(RateOf(24) == 24 && RateOf(50) == 50 && RateOf(60) == 60 && RateOf(25) == 25, "integer rates");

        std::vector<DisplayModes::Mode> modes;
        Check(DisplayModes::Parse(L"2160p23, 2160p24;2160p50 2160p60", modes) && modes.size() == 4, "madVR list");
        Check(modes[0].height == 2160 && modes[0].code == 23, "first entry");
        Check(DisplayModes::Parse(L"", modes) && modes.empty(), "empty list");
        Check(DisplayModes::Parse(L"  23 , 24 ,, 60  ", modes) && modes.size() == 3 && !modes[0].height, "rates only");
        Check(DisplayModes::Parse(L"1080P50, 1080p50", modes) && modes.size() == 1, "duplicate removed");
        for (const auto* bad : {L"2160", L"p24", L"2160p", L"2160p24x", L"100p24", L"2160p10", L"2160p500", L"24Hz",
                                L"2160i50", L"-24", L"2160p23.976"})
            Check(!DisplayModes::Parse(bad, modes), "malformed entry rejected");

        const auto film = 24000. / 1001, pal = 25., ntscVideo = 60000. / 1001;
        const wchar_t* tv = L"2160p23, 2160p24, 2160p50, 2160p60";
        Check(Pick(tv, film) == 0, "23.976 fps -> 2160p23");
        Check(Pick(tv, 24) == 1, "24 fps -> 2160p24");
        Check(Pick(tv, pal) == 2, "25 fps -> 2160p50");
        Check(Pick(tv, 50) == 2, "50 fps -> 2160p50");
        Check(Pick(tv, 60) == 3, "60 fps -> 2160p60");
        Check(Pick(tv, ntscVideo) == -1, "59.94 fps has no exact mode");
        Check(Pick(L"2160p59, 2160p60", ntscVideo) == 0, "59.94 fps -> 2160p59");
        Check(Pick(L"2160p60", film) == -1, "23.976 is no multiple of 60");
        Check(Pick(L"2160p60, 2160p119", film) == 1, "119.88 is five times 23.976");
        Check(Pick(L"2160p119, 2160p23", film) == 1, "lowest multiple wins");
        Check(Pick(L"2160p120, 2160p24", 24) == 1, "lowest multiple wins over list order");
        Check(Pick(L"1080p24, 2160p24", 24) == 0, "list order breaks ties");
        Check(Pick(L"2160p24", film) == -1, "24 Hz is not 23.976");
        Check(Pick(tv, 0) == -1, "unknown frame rate");
        // A 25 fps AvgTimePerFrame of 400000 and a 23.976 one of 417083 or 417084.
        Check(Pick(tv, 1e7 / 417083) == 0 && Pick(tv, 1e7 / 417084) == 0, "rounded 23.976 durations");
        Check(Pick(tv, 1e7 / 400000) == 2, "rounded 25 fps duration");
        puts("PASS display mode list parsing and selection");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL %s\n", e.what());
        return 1;
    }
}
