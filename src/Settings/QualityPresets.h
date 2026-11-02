#pragma once
#include "Settings/Settings.h"
#include <cstdint>
#include <cstring>
#include <iterator>

// libplacebo's render presets (pl_render_fast_params, pl_render_default_params,
// pl_render_high_quality_params) expressed in the settings they differ in.
// Applying one is a starting point: HDR mode, shaders, Smooth Motion and all
// other settings stay as they are.
inline constexpr const wchar_t* QualityPresetNames[] = {L"Fast", L"Default", L"High Quality"};
inline constexpr const wchar_t* QualityPresetDescriptions[] = {
    L"For weak integrated GPUs.", L"Balanced. Fine for most PCs.", L"Sharpest picture. For dedicated GPUs."};

struct QualityPreset
{
    const char *upscaler, *downscaler;
    BOOL deband, peakDetect;
    int ditherMethod; // DitherChoices index; 3 is Off
    float peakPercentile, contrastRecovery;
};

// Measured from the libplacebo runtime; "none" is plain GPU texture sampling.
inline constexpr QualityPreset QualityPresets[] = {
    {"none", "none", FALSE, FALSE, 3, 100.f, 0.f},
    {"auto", "auto", FALSE, TRUE, 0, 100.f, 0.f},
    {"ewa_lanczossharp", "auto", TRUE, TRUE, 0, 99.995f, .3f},
};

inline void ApplyQualityPreset(PlaceboSettings& s, const QualityPreset& p)
{
    for (int role = 0; role < 4; role++)
    {
        auto& f = s.scaling.filters[role];
        f = {};
        if (role < 2)
            strcpy_s(f.filter, role ? p.downscaler : p.upscaler);
    }
    s.scaling.antiringing = -1;
    s.deband = p.deband;
    s.debandIterations = -1;
    s.debandThreshold = s.debandRadius = s.debandGrain = -1;
    s.ditherMethod = p.ditherMethod;
    s.ditherTemporal = FALSE;
    s.peakDetect = p.peakDetect;
    s.peakPercentile = p.peakPercentile;
    s.contrastRecovery = p.contrastRecovery;
}

inline void ApplyQualityPreset(PlaceboSettings& s, int index)
{
    ApplyQualityPreset(s, QualityPresets[index]);
}

// Whether the settings a preset covers equal it. Values of a disabled stage
// do not count, and explicit library defaults count as unchanged.
inline bool SameQuality(const PlaceboSettings& s, const QualityPreset& p)
{
    const auto& deband = pl_deband_default_params;
    auto unchanged = [](float value, float fallback) { return value == -1 || value == fallback; };
    bool same = s.scaling.antiringing <= 0 && s.deband == p.deband && s.ditherMethod == p.ditherMethod &&
                s.peakDetect == p.peakDetect && s.contrastRecovery == p.contrastRecovery;
    for (int role = 0; role < 4; role++)
        same = same && !strcmp(s.scaling.filters[role].filter,
                               role == 0   ? p.upscaler
                               : role == 1 ? p.downscaler
                                           : "auto");
    if (s.deband)
        same = same && unchanged(float(s.debandIterations), float(deband.iterations)) &&
               unchanged(s.debandThreshold, deband.threshold) && unchanged(s.debandRadius, deband.radius) &&
               unchanged(s.debandGrain, deband.grain);
    if (s.ditherMethod != 3)
        same = same && !s.ditherTemporal;
    if (s.peakDetect)
        same = same && s.peakPercentile == p.peakPercentile;
    return same;
}

// Index of the preset the settings currently equal, or -1 once anything it
// sets was changed by hand.
inline int MatchQualityPreset(const PlaceboSettings& s)
{
    for (int index = 0; index < int(std::size(QualityPresets)); index++)
        if (SameQuality(s, QualityPresets[index]))
            return index;
    return -1;
}

// The measurement starts from Fast and adds these upgrades in order while the
// GPU keeps enough headroom. PlaceboSettings::measuredQuality stores
// MeasuredFlag | the accepted upgrades for the display profile.
enum MeasuredUpgrade : uint32_t
{
    MeasureDither = 1,
    MeasurePeakDetection = 2,
    MeasureLanczos = 4,
    MeasureEwa = 8,
    MeasureEwaSharp = 16,
    MeasureDeband = 32,
    MeasureHdrDetail = 64,
    MeasuredFlag = 128,
};

inline constexpr MeasuredUpgrade MeasuredUpgrades[] = {
    MeasureDither, MeasurePeakDetection, MeasureLanczos, MeasureEwa, MeasureEwaSharp, MeasureDeband, MeasureHdrDetail};

inline QualityPreset MeasuredPreset(uint32_t upgrades)
{
    QualityPreset p = QualityPresets[0];
    if (upgrades & (MeasureLanczos | MeasureEwa | MeasureEwaSharp))
    {
        p.upscaler = upgrades & MeasureEwaSharp ? "ewa_lanczossharp"
                     : upgrades & MeasureEwa    ? "ewa_lanczos"
                                                : "lanczos";
        p.downscaler = "auto";
    }
    if (upgrades & MeasureDither)
        p.ditherMethod = 0;
    p.peakDetect = (upgrades & MeasurePeakDetection) != 0;
    p.deband = (upgrades & MeasureDeband) != 0;
    if (p.peakDetect && (upgrades & MeasureHdrDetail))
    {
        p.peakPercentile = 99.995f;
        p.contrastRecovery = .3f;
    }
    return p;
}

inline void ApplyMeasuredQuality(PlaceboSettings& s, uint32_t upgrades)
{
    ApplyQualityPreset(s, MeasuredPreset(upgrades));
    s.measuredQuality = int(MeasuredFlag | (upgrades & (MeasuredFlag - 1)));
}

// Whether the settings still equal the last measurement for this profile.
inline bool MatchMeasuredQuality(const PlaceboSettings& s)
{
    return (s.measuredQuality & MeasuredFlag) && SameQuality(s, MeasuredPreset(uint32_t(s.measuredQuality)));
}
