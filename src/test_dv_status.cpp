#include "DolbyVisionStatus.h"
#include <sstream>
#include <stdexcept>
#include <cstdio>

static std::wstring Status(DolbyVisionSource source, bool metadata, bool layered, bool hdr)
{
    auto output = hdr ? pl_color_space_hdr10 : pl_color_space_bt709;
    std::wostringstream text;
    AppendDolbyVisionStatus(text, source, metadata, layered, output);
    return text.str();
}

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        using Layer = DolbyVisionSource::Layer;
        DolbyVisionSource source{true, true, 7, Layer::FEL};
        auto active = Status(source, true, true, true);
        Require(active.find(L"Profile 7 (FEL) / HDR10") != std::wstring::npos, "FEL source identity");
        Require(active.find(L"FEL processing active") != std::wstring::npos, "FEL reconstruction active");
        Require(active.find(L"Output: HDR/PQ") != std::wstring::npos, "HDR output is separate from DV source");
        auto partial = Status(source, true, false, true);
        Require(partial.find(L"FEL not processed") != std::wstring::npos, "metadata alone is not FEL reconstruction");
        Require(partial.find(L"FEL processing active") == std::wstring::npos, "no false FEL active status");
        auto fallback = Status(source, false, false, false);
        Require(fallback.find(L"not processed - HDR10 fallback") != std::wstring::npos, "HDR10 fallback");
        Require(fallback.find(L"Output: SDR") != std::wstring::npos, "tone mapped fallback output");
        source.layer = Layer::MEL;
        Require(Status(source, true, false, false).find(L"MEL processing active") != std::wstring::npos,
                "MEL needs no residual layer");
        for (int profile : {5, 8})
        {
            source = {true, profile == 8, profile, Layer::Unknown};
            auto text = Status(source, true, false, true);
            Require(text.find(L"Profile " + std::to_wstring(profile)) != std::wstring::npos, "other DV profile");
            Require(text.find(L"metadata processing active") != std::wstring::npos, "single layer DV active");
            Require(profile != 5 || text.find(L"/ HDR10") == std::wstring::npos, "P5 is not HDR10 compatible");
        }
        auto unknown = Status({}, true, false, false);
        Require(unknown.find(L"profile unknown") != std::wstring::npos, "do not invent profile from RPU profile field");
        Require(Status({}, false, false, true).empty(), "ordinary HDR10 must not claim Dolby Vision");
        puts(
            "PASS DV status: FEL/MEL, P5/P8, metadata-only, HDR10 fallback, HDR/SDR output, unknown profile and non-DV");
        return 0;
    }
    catch (const std::exception& error)
    {
        puts(error.what());
        return 1;
    }
}
