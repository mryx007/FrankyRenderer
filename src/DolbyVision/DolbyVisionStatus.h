#pragma once
#include <ostream>
#include <libplacebo/colorspace.h>

struct DolbyVisionSource
{
    enum class Layer
    {
        Unknown,
        FEL,
        MEL
    };
    bool present = false, hdr10Base = false;
    int profile = 0;
    Layer layer = Layer::Unknown;
};

inline void AppendDolbyVisionStatus(std::wostream& text,
                                    const DolbyVisionSource& source,
                                    bool metadataActive,
                                    bool layeredActive,
                                    const pl_color_space& output)
{
    if (!source.present && !metadataActive)
        return;
    text << L"\nSource: Dolby Vision";
    if (source.profile > 0)
        text << L" Profile " << source.profile;
    else
        text << L" (profile unknown)";
    if (source.layer != DolbyVisionSource::Layer::Unknown)
        text << (source.layer == DolbyVisionSource::Layer::FEL ? L" (FEL)" : L" (MEL)");
    if (source.hdr10Base)
        text << L" / HDR10";
    text << L"\nDolby Vision: ";
    if (metadataActive && layeredActive && source.layer == DolbyVisionSource::Layer::FEL)
        text << L"FEL processing active";
    else if (metadataActive && source.layer == DolbyVisionSource::Layer::FEL)
        text << L"metadata processing active; FEL not processed";
    else if (metadataActive && source.layer == DolbyVisionSource::Layer::MEL)
        text << L"MEL processing active";
    else if (metadataActive)
        text << L"metadata processing active";
    else
        text << (source.hdr10Base ? L"not processed - HDR10 fallback" : L"not processed");
    text << L"\nOutput: "
         << (pl_color_space_is_hdr(&output) ? (output.transfer == PL_COLOR_TRC_HLG ? L"HDR/HLG" : L"HDR/PQ") : L"SDR");
}
