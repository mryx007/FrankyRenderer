// Quality presets: each must reproduce its libplacebo render preset and be
// recognised again, while unrelated settings stay untouched.
#include "Settings/QualityPresets.h"
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* label)
{
    if (!ok)
    {
        printf("FAIL %s\n", label);
        exit(1);
    }
}

static bool SameFilter(const pl_filter_config* a, const pl_filter_config* b)
{
    return a == b || (a && b && a->name && b->name && !strcmp(a->name, b->name));
}

int main()
{
    Require(MatchQualityPreset(PlaceboSettings{}) == 1, "fresh settings are the Default preset");

    const pl_render_params* library[] = {
        &pl_render_fast_params, &pl_render_default_params, &pl_render_high_quality_params};
    pl_options o = pl_options_alloc(nullptr);
    for (int i = 0; i < 3; ++i)
    {
        PlaceboSettings s;
        s.hdrMode = 1;
        s.smoothMotion = TRUE;
        strcpy_s(s.scaling.filters[4].filter, "linear");
        s.debandThreshold = 7;
        ApplyQualityPreset(s, i);
        Require(SUCCEEDED(ValidateSettings(s)), "preset settings validate");
        Require(MatchQualityPreset(s) == i, "an applied preset is recognised");
        Require(s.hdrMode == 1 && s.smoothMotion && !strcmp(s.scaling.filters[4].filter, "linear"),
                "HDR mode, Smooth Motion and the frame mixer are kept");

        const auto& p = *library[i];
        pl_options_reset(o, &pl_render_default_params);
        Require(ApplyScaling(o, s.scaling), "preset scaling applies");
        Require(SameFilter(o->params.upscaler, p.upscaler) && SameFilter(o->params.downscaler, p.downscaler) &&
                    SameFilter(o->params.plane_upscaler, p.plane_upscaler) &&
                    SameFilter(o->params.plane_downscaler, p.plane_downscaler),
                "scalers match the libplacebo preset");
        Require(o->params.antiringing_strength == p.antiringing_strength, "anti-ringing matches");
        Require(!!s.deband == !!p.deband_params, "debanding matches");
        Require((s.ditherMethod != 3) == !!p.dither_params, "dithering matches");
        Require(!!s.peakDetect == !!p.peak_detect_params, "peak detection matches");
        if (p.peak_detect_params)
            Require(s.peakPercentile == p.peak_detect_params->percentile, "peak percentile matches");
        Require(s.contrastRecovery == p.color_map_params->contrast_recovery, "contrast recovery matches");
    }
    pl_options_free(&o);

    PlaceboSettings s;
    s.debandThreshold = 9;
    Require(MatchQualityPreset(s) == 1, "values of a disabled stage do not count");
    ApplyQualityPreset(s, 2);
    s.debandThreshold = 4;
    Require(MatchQualityPreset(s) == -1, "a hand-changed value shows Custom");
    s.debandThreshold = pl_deband_default_params.threshold;
    Require(MatchQualityPreset(s) == 2, "explicit library defaults still match");
    strcpy_s(s.scaling.filters[0].filter, "lanczos");
    Require(MatchQualityPreset(s) == -1, "another upscaler shows Custom");
    {
        // A measured mix that is no preset is recognised as Measured until changed.
        PlaceboSettings measured;
        measured.smoothMotion = TRUE;
        ApplyMeasuredQuality(measured, MeasureDither | MeasurePeakDetection | MeasureLanczos | MeasureDeband);
        Require(SUCCEEDED(ValidateSettings(measured)), "measured settings validate");
        Require(MatchQualityPreset(measured) == -1 && MatchMeasuredQuality(measured), "a measured mix shows Measured");
        Require(!strcmp(measured.scaling.filters[0].filter, "lanczos") && measured.deband && measured.smoothMotion,
                "measured upgrades apply and other settings stay");
        strcpy_s(measured.scaling.filters[0].filter, "bicubic");
        Require(!MatchMeasuredQuality(measured), "a hand change after measuring shows Custom");
        PlaceboSettings none;
        ApplyMeasuredQuality(none, 0);
        Require(MatchQualityPreset(none) == 0, "no upgrades equal the Fast preset");
        Require(!MatchMeasuredQuality(PlaceboSettings{}), "never measured is not Measured");
    }
    puts("PASS quality presets: libplacebo parity, recognition, Custom, untouched settings, measured upgrades");
}
