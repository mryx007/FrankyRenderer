#pragma once
#include <libplacebo/options.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <charconv>
#include <string>
#include <cstdint>

// A missing override means inherit, not a numeric zero. Keep this POD for the
// versioned COM settings interface and the versioned registry block.
struct ScalerSettings
{
    char filter[64] = "auto", preset[64] = "auto", kernel[64] = "auto", window[64] = "auto";
    float values[9] = {};
    uint32_t overrides = 0;
    int polar = -1;
};

struct ScalingSettings
{
    ScalerSettings filters[5];
    float antiringing = -1;
};

inline constexpr const char* ScalingRoles[] = {
    "upscaler", "downscaler", "plane_upscaler", "plane_downscaler", "frame_mixer"};
inline constexpr const wchar_t* ScalingLabels[] = {L"Image Upscaler",
                                                   L"Image Downscaler",
                                                   L"Plane Upscaler (Chroma / Alpha)",
                                                   L"Plane Downscaler (Chroma / Alpha)",
                                                   L"Frame Mixer"};
inline constexpr const char* ScalingFields[] = {
    "radius", "clamp", "blur", "taper", "antiring", "param1", "param2", "wparam1", "wparam2"};
inline constexpr const wchar_t* ScalingFieldLabels[] = {L"Radius",
                                                        L"Clamp",
                                                        L"Blur",
                                                        L"Taper",
                                                        L"Anti-Ringing",
                                                        L"Kernel Parameter 1",
                                                        L"Kernel Parameter 2",
                                                        L"Window Parameter 1",
                                                        L"Window Parameter 2"};

inline bool ScalingName(const char (&name)[64])
{
    if (!memchr(name, 0, 64) || !name[0])
        return false;
    for (const char* p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
            return false;
    return true;
}

inline std::string ScalingNumber(float value)
{
    char text[64];
    auto converted = std::to_chars(text, text + 64, value);
    return converted.ec == std::errc{} ? std::string(text, converted.ptr) : std::string{};
}

inline pl_filter_usage ScalingUsage(int role)
{
    return role == 4 ? PL_FILTER_FRAME_MIXING : role % 2 ? PL_FILTER_DOWNSCALING : PL_FILTER_UPSCALING;
}

inline const pl_filter_config* ScalingFilter(pl_options o, int role)
{
    switch (role)
    {
    case 0:
        return o->params.upscaler;
    case 1:
        return o->params.downscaler;
    case 2:
        return o->params.plane_upscaler;
    case 3:
        return o->params.plane_downscaler;
    default:
        return o->params.frame_mixer;
    }
}

inline bool ApplyScaling(pl_options o, const ScalingSettings& settings)
{
    // Auto follows the actual library defaults, independently of legacy UI
    // values and without resetting unrelated image/HDR parameters.
    o->params.upscaler = pl_render_default_params.upscaler;
    o->params.downscaler = pl_render_default_params.downscaler;
    o->params.plane_upscaler = pl_render_default_params.plane_upscaler;
    o->params.plane_downscaler = pl_render_default_params.plane_downscaler;
    o->params.frame_mixer = pl_render_default_params.frame_mixer;
    o->params.antiringing_strength =
        settings.antiringing < 0 ? pl_render_default_params.antiringing_strength : settings.antiringing;
    if (!std::isfinite(settings.antiringing) || (settings.antiringing < 0 && settings.antiringing != -1) ||
        settings.antiringing > 1)
        return false;
    for (int role = 0; role < 5; role++)
    {
        const auto& s = settings.filters[role];
        if (!ScalingName(s.filter) || !ScalingName(s.preset) || !ScalingName(s.kernel) || !ScalingName(s.window) ||
            s.overrides & ~511u || s.polar < -1 || s.polar > 1)
            return false;
        for (float n : s.values)
            if (!std::isfinite(n))
                return false;
        const std::string key = ScalingRoles[role];
        if (!strcmp(s.filter, "auto"))
            continue;
        if (strcmp(s.filter, "custom"))
        {
            // This runtime exports the documented box filter but omits its name
            // from the options preset registry. Use the same built-in config.
            if (!strcmp(s.filter, "box") && (role == 1 || role == 3))
            {
                if (role == 1)
                    o->params.downscaler = &pl_filter_box;
                else
                    o->params.plane_downscaler = &pl_filter_box;
            }
            else if (!pl_options_set_str(o, key.c_str(), s.filter))
                return false;
            continue;
        }
        const bool boxPreset = !strcmp(s.preset, "box") && (role == 1 || role == 3);
        if (!strcmp(s.preset, "auto") || boxPreset)
        {
            const auto* base = boxPreset ? &pl_filter_box : ScalingFilter(o, role);
            if (!base && role == 2)
                base = o->params.upscaler;
            if (!base && role == 3)
                base = o->params.downscaler;
            if (!pl_options_set_str(o, key.c_str(), "custom"))
                return false;
            auto* storage = const_cast<pl_filter_config*>(ScalingFilter(o, role));
            const auto name = storage->name;
            const auto allowed = storage->allowed;
            *storage = base ? *base : pl_filter_bilinear;
            storage->name = name;
            storage->allowed = allowed;
        }
        else if (!pl_options_set_str(o, (key + "_preset").c_str(), s.preset) ||
                 !pl_options_set_str(o, key.c_str(), "custom"))
            return false;
        if (strcmp(s.kernel, "auto") && !pl_options_set_str(o, (key + "_kernel").c_str(), s.kernel))
            return false;
        if (strcmp(s.window, "auto") && !pl_options_set_str(o, (key + "_window").c_str(), s.window))
            return false;
        auto* config = const_cast<pl_filter_config*>(ScalingFilter(o, role));
        if (!config || !config->kernel)
            return false;
        // A newly selected kernel/window has its own parameter defaults.
        if (strcmp(s.kernel, "auto"))
            std::copy_n(config->kernel->params, 2, config->params);
        if (strcmp(s.window, "auto") && config->window)
            std::copy_n(config->window->params, 2, config->wparams);
        for (int field = 0; field < 9; field++)
            if (s.overrides & (1u << field))
            {
                if (!pl_options_set_str(
                        o, (key + "_" + ScalingFields[field]).c_str(), ScalingNumber(s.values[field]).c_str()))
                    return false;
            }
        if (s.polar >= 0 && !pl_options_set_str(o, (key + "_polar").c_str(), s.polar ? "yes" : "no"))
            return false;
        if (role == 4 && config->polar)
            return false; // temporal filters are one-dimensional
    }
    return true;
}

inline float ScalingRadius(const pl_filter_config* f)
{
    if (!f || !f->kernel || f->kernel == &pl_filter_function_oversample)
        return 0;
    return (f->kernel->resizable && f->radius > 0 ? f->radius : f->kernel->radius) * (f->blur > 0 ? f->blur : 1.f);
}

inline bool ValidScaling(const ScalingSettings& settings, float* radius = nullptr)
{
    auto o = pl_options_alloc(nullptr);
    if (!o)
        return false;
    pl_options_reset(o, &pl_render_default_params);
    bool valid = ApplyScaling(o, settings);
    const float r = valid ? ScalingRadius(o->params.frame_mixer) : 0;
    // A bounded temporal window prevents unbounded decoder lookahead/VRAM.
    valid = valid && std::isfinite(r) && r <= 8;
    if (radius)
        *radius = r;
    pl_options_free(&o);
    return valid;
}
