#pragma once
#include "Renderer/Backend.h"
#include "Renderer/RendererExtras.h"
#include "Settings/ColorMappingSettings.h"
#include "Settings/ScalingSettings.h"
#include "Settings/SettingsChoices.h"
#include <algorithm>

// libplacebo options for the given renderer settings. Playback and the
// quality measurement share this, so measured costs match playback.
inline HRESULT BuildRenderOptions(pl_options options, const RenderSettings& settings)
{
    pl_options_reset(options, &pl_render_default_params);
    if (!settings.scaling)
    {
        if (!pl_options_set_str(options, "upscaler", settings.upscaler.c_str()) ||
            !pl_options_set_str(options, "downscaler", settings.downscaler.c_str()))
            return E_INVALIDARG;
        if (settings.chromaUpscaler != "auto" &&
            !pl_options_set_str(options, "plane_upscaler", settings.chromaUpscaler.c_str()))
            return E_INVALIDARG;
    }
    else if (!ApplyScaling(options, *settings.scaling))
        return E_INVALIDARG;
    if (settings.toneMapping != "auto")
    {
        if (!pl_options_set_str(options, "tone_mapping", settings.toneMapping.c_str()))
            return E_INVALIDARG;
        options->params.color_map_params = &options->color_map_params;
    }
    options->peak_detect_params = pl_peak_detect_default_params;
    options->peak_detect_params.smoothing_period = settings.peakSmoothing;
    options->peak_detect_params.scene_threshold_low = settings.sceneLow;
    options->peak_detect_params.scene_threshold_high = settings.sceneHigh;
    options->peak_detect_params.percentile = settings.peakPercentile;
    options->params.peak_detect_params = settings.peakDetect ? &options->peak_detect_params : nullptr;
    options->peak_detect_params.black_cutoff = settings.blackCutoff;
    options->peak_detect_params.allow_delayed = settings.allowDelayedPeak;
    if (settings.gamutMapping &&
        !pl_options_set_str(options, "gamut_mapping", GamutChoices[settings.gamutMapping].name))
        return E_INVALIDARG;
    options->color_map_params.contrast_recovery = settings.contrastRecovery;
    options->color_map_params.contrast_smoothness = settings.contrastSmoothness;
    options->color_map_params.tone_constants.knee_adaptation = settings.kneeAdaptation;
    options->color_map_params.tone_constants.spline_contrast = settings.splineContrast;
    options->color_map_params.tone_constants.knee_minimum = settings.kneeMinimum;
    options->color_map_params.tone_constants.knee_maximum = settings.kneeMaximum;
    options->color_map_params.tone_constants.knee_default =
        std::clamp(options->color_map_params.tone_constants.knee_default, settings.kneeMinimum, settings.kneeMaximum);
    options->color_map_params.gamut_constants.perceptual_strength = settings.perceptualStrength;
    options->color_map_params.gamut_constants.softclip_knee = settings.softclipKnee;
    options->color_map_params.show_clipping = settings.showClipping;
    options->params.color_map_params = &options->color_map_params;
    if (!ApplyMappingOverrides(options, settings.colorMapping))
        return E_INVALIDARG;
    if (!settings.scaling)
    {
        options->params.antiringing_strength = settings.antiringing;
        if (settings.upscalerBlur > 0 && options->params.upscaler && options->params.upscaler->kernel)
        {
            options->upscaler = *options->params.upscaler;
            options->upscaler.blur = settings.upscalerBlur;
            options->params.upscaler = &options->upscaler;
        }
        if (settings.downscalerBlur > 0 && options->params.downscaler && options->params.downscaler->kernel)
        {
            options->downscaler = *options->params.downscaler;
            options->downscaler.blur = settings.downscalerBlur;
            options->params.downscaler = &options->downscaler;
        }
    }
    options->deband_params = pl_deband_default_params;
    options->deband_params.iterations = settings.debandIterations;
    options->deband_params.threshold = settings.debandThreshold;
    options->deband_params.radius = settings.debandRadius;
    options->deband_params.grain = settings.debandGrain;
    options->params.deband_params = settings.deband ? &options->deband_params : nullptr;
    options->dither_params = pl_dither_default_params;
    const pl_dither_method methods[] = {PL_DITHER_BLUE_NOISE, PL_DITHER_ORDERED_FIXED, PL_DITHER_WHITE_NOISE};
    if (settings.ditherMethod < 3)
        options->dither_params.method = methods[settings.ditherMethod];
    options->dither_params.temporal = settings.ditherTemporal;
    options->params.dither_params = settings.ditherMethod == 3 ? nullptr : &options->dither_params;
    if (!ApplyExtras(options, settings.extras))
        return E_INVALIDARG;
    return S_OK;
}
