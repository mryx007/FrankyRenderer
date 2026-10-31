#pragma once
#include "Settings/SettingsChoices.h"
#include <functional>
#include <cstring>
#include "UI/Direct2DSettingsHelp.h"
#include "Settings/ShaderParameterLabels.h"
#include "Renderer/DisplayModes.h"

// The view edits a private draft. Every mutation is validated before preview.
inline std::wstring D2DWide(const char* s)
{
    return std::wstring(s, s + strlen(s));
}

// Curated presets and descriptions from https://libplacebo.org/options/#scaling.
// Keep backend validation unrestricted so existing INI/registry settings still load.
struct D2DScalerPreset
{
    const char* name;
    const wchar_t* description;
    unsigned roles;
};

inline constexpr D2DScalerPreset D2DScalerPresets[] = {
    {"nearest", L"Nearest-neighbour (box) sampling (very fast)", 1},
    {"bilinear", L"Bilinear sampling (very fast)", 1},
    {"oversample", L"Aspect-ratio preserving nearest neighbour sampling (very fast)", 1},
    {"bicubic", L"Bicubic interpolation (fast)", 3},
    {"gaussian", L"Gaussian smoothing (fast)", 3},
    {"catmull_rom", L"Catmull-Rom cubic spline", 3},
    {"lanczos", L"Lanczos reconstruction", 3},
    {"ewa_lanczos", L"EWA Lanczos (Jinc) reconstruction (slow)", 1},
    {"ewa_lanczossharp", L"Sharpened version of ewa_lanczos (slow)", 1},
    {"ewa_lanczos4sharpest", L"Very sharp version of ewa_lanczos, with anti-ringing (very slow)", 1},
    {"box", L"Box averaging (very fast)", 2},
    {"hermite", L"Hermite-weighted averaging (fast)", 2},
    {"bilinear", L"Bilinear (triangle) averaging (fast)", 2},
    {"mitchell", L"Mitchell-Netravalia cubic spline", 2},
    {"oversample", L"Oversampling, only mix edge frames while preserving FPS", 4},
    {"hermite", L"Hermite-weighted frame mixing", 4},
    {"linear", L"Linear frame mixing", 4},
    {"cubic", L"Cubic B-spline frame mixing", 4},
};

inline std::wstring D2DScalerDescription(int role, const char* name)
{
    if (!strcmp(name, "auto"))
        return L"Use the renderer's default filter.";
    if (!strcmp(name, "custom"))
        return L"Configure a custom filter using the controls below.";
    if (!strcmp(name, "none"))
        return role == 4   ? L"Disable frame mixing, show nearest frame to target PTS"
               : role == 0 ? L"No filter, only use basic GPU texture sampling"
               : role == 1 ? L"Use the same filter as specified for upscaler"
                           : L"Use the corresponding image scaling filter.";
    const unsigned usage = role == 4 ? 4 : role % 2 ? 2 : 1;
    for (const auto& preset : D2DScalerPresets)
        if ((preset.roles & usage) && !strcmp(name, preset.name))
            return preset.description;
    return L"Previously saved filter. Select a preset to replace it.";
}

inline void D2DSetCustomScaling(ScalerSettings& filter, bool enabled)
{
    if (enabled && strcmp(filter.filter, "custom"))
    {
        char base[64];
        strcpy_s(base, strcmp(filter.filter, "none") ? filter.filter : "auto");
        if (strcmp(filter.preset, base))
            filter = {};
        strcpy_s(filter.preset, base);
        strcpy_s(filter.filter, "custom");
    }
    else if (!enabled && !strcmp(filter.filter, "custom"))
        strcpy_s(filter.filter, filter.preset);
}

inline std::vector<std::string> D2DScalerNames(int role, bool custom, const char* current)
{
    std::vector<std::string> names = {"auto", "none"};
    if (custom)
        names.emplace_back("custom");
    const unsigned usage = role == 4 ? 4 : role % 2 ? 2 : 1;
    // Downscaling uses its own speed order, as in the documentation.
    if (usage == 2)
        for (const char* name :
             {"box", "hermite", "bilinear", "bicubic", "gaussian", "catmull_rom", "mitchell", "lanczos"})
            names.emplace_back(name);
    else
        for (const auto& preset : D2DScalerPresets)
            if (preset.roles & usage)
                names.emplace_back(preset.name);
    if (std::find(names.begin(), names.end(), current) == names.end())
        names.emplace_back(current);
    return names;
}

template <class T>
D2DControl D2DNumber(PlaceboSettings& s,
                     const wchar_t* name,
                     T* field,
                     float low,
                     float high,
                     float step,
                     const wchar_t* help = L"",
                     float inherited = 0,
                     bool sentinel = false)
{
    D2DControl c;
    c.name = name;
    c.desc = help;
    c.kind = std::is_integral_v<T> ? ControlKind::SliderInt : ControlKind::SliderFloat;
    c.minVal = low;
    c.maxVal = high;
    c.stepVal = step;
    const auto offset = reinterpret_cast<const char*>(field) - reinterpret_cast<const char*>(&s);
    c.get = [field, inherited, sentinel] { return sentinel && *field < 0 ? inherited : float(*field); };
    c.set = [field](float v) { *field = static_cast<T>(v); };
    c.defaultValue = [offset, inherited, sentinel]
    {
        PlaceboSettings defaults;
        const auto v = *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&defaults) + offset);
        return sentinel && v < 0 ? inherited : float(v);
    };
    c.reset = [field, offset]
    {
        PlaceboSettings defaults;
        *field = *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&defaults) + offset);
    };
    return c;
}

inline D2DControl D2DBool(PlaceboSettings& s, const wchar_t* name, BOOL* field, const wchar_t* help = L"")
{
    auto c = D2DNumber(s, name, field, 0, 1, 1, help);
    c.kind = ControlKind::Toggle;
    return c;
}

inline D2DControl D2DChoice(
    PlaceboSettings& s, const wchar_t* name, int* field, std::vector<ChoiceItem> choices, const wchar_t* help = L"")
{
    auto c = D2DNumber(s, name, field, 0, 0, 1, help);
    c.kind = ControlKind::Choice;
    c.choices = std::move(choices);
    return c;
}

template <class T, size_t N> std::vector<ChoiceItem> D2DChoices(const T (&items)[N])
{
    std::vector<ChoiceItem> out;
    for (int i = 0; i < int(N); i++)
    {
        if constexpr (std::is_same_v<T, RendererChoice>)
            out.push_back({i, items[i].label});
        else
            out.push_back({i, items[i]});
    }
    return out;
}

inline D2DControl
D2DAction(const wchar_t* name, const wchar_t* label, std::function<void(HWND)> action, const wchar_t* help = L"")
{
    D2DControl c;
    c.name = name;
    c.desc = help;
    c.kind = ControlKind::Action;
    c.actionLabel = label;
    c.action = std::move(action);
    return c;
}

inline float D2DOptionDefault(const char* key)
{
    auto o = pl_options_alloc(nullptr);
    if (!o)
        return 0;
    pl_options_reset(o, &pl_render_default_params);
    auto v = pl_options_get(o, key);
    float n = v ? (!strcmp(v->text, "yes") ? 1.f : float(atof(v->text))) : 0;
    pl_options_free(&o);
    return n;
}

inline float D2DMappingValue(const PlaceboSettings& s, int i)
{
    if (s.colorMapping.values[i][0])
        return float(atof(s.colorMapping.values[i]));
    const char* k = MappingOptions[i].key;
    if (!strcmp(k, "knee_adaptation"))
        return s.kneeAdaptation;
    if (!strcmp(k, "knee_minimum"))
        return s.kneeMinimum;
    if (!strcmp(k, "knee_maximum"))
        return s.kneeMaximum;
    if (!strcmp(k, "knee_default"))
        return std::clamp(.4f, s.kneeMinimum, s.kneeMaximum);
    if (!strcmp(k, "spline_contrast"))
        return s.splineContrast;
    if (!strcmp(k, "contrast_recovery"))
        return s.contrastRecovery;
    if (!strcmp(k, "contrast_smoothness"))
        return s.contrastSmoothness;
    if (!strcmp(k, "perceptual_strength"))
        return s.perceptualStrength;
    if (!strcmp(k, "softclip_knee"))
        return s.softclipKnee;
    return D2DOptionDefault(k);
}

inline void D2DResetMappingLegacy(PlaceboSettings& s, int i)
{
    const char* k = MappingOptions[i].key;
    PlaceboSettings d;
    if (!strcmp(k, "knee_adaptation"))
        s.kneeAdaptation = d.kneeAdaptation;
    if (!strcmp(k, "knee_minimum"))
        s.kneeMinimum = d.kneeMinimum;
    if (!strcmp(k, "knee_maximum"))
        s.kneeMaximum = d.kneeMaximum;
    if (!strcmp(k, "spline_contrast"))
        s.splineContrast = d.splineContrast;
    if (!strcmp(k, "contrast_recovery"))
        s.contrastRecovery = d.contrastRecovery;
    if (!strcmp(k, "contrast_smoothness"))
        s.contrastSmoothness = d.contrastSmoothness;
    if (!strcmp(k, "perceptual_strength"))
        s.perceptualStrength = d.perceptualStrength;
    if (!strcmp(k, "softclip_knee"))
        s.softclipKnee = d.softclipKnee;
}

inline float D2DScalingDefault(const PlaceboSettings& s, int role, int i)
{
    auto scaling = s.scaling;
    scaling.filters[role].overrides &= ~(1u << i);
    auto o = pl_options_alloc(nullptr);
    if (!o)
        return 0;
    pl_options_reset(o, &pl_render_default_params);
    const bool valid = ApplyScaling(o, scaling);
    const auto* c = valid ? ScalingFilter(o, role) : nullptr;
    float value = 0;
    if (c)
    {
        const float values[] = {c->radius > 0 ? c->radius
                                : c->kernel   ? c->kernel->radius
                                              : 0,
                                c->clamp,
                                c->blur,
                                c->taper,
                                c->antiring,
                                c->params[0],
                                c->params[1],
                                c->wparams[0],
                                c->wparams[1]};
        value = values[i];
    }
    pl_options_free(&o);
    return value;
}

inline std::vector<D2DPage> BuildD2DPages(PlaceboSettings& s)
{
    std::vector<D2DPage> pages;
    auto page = [&](const wchar_t* name, const wchar_t* title, const wchar_t* help) -> D2DPage&
    {
        pages.push_back({name, L"•", title, help, {}});
        return pages.back();
    };
    auto card = [](D2DPage& p, const wchar_t* title, const wchar_t* help = L"") -> D2DCard&
    {
        p.cards.push_back({title, help, {}});
        return p.cards.back();
    };
    auto extra = [&](D2DCard& c, int i)
    {
        auto& o = ExtraOptions[i];
        auto n = D2DNumber(s,
                           o.label,
                           &s.extras.values[i],
                           o.minimum,
                           o.maximum,
                           o.integer ? 1 : .001f,
                           o.help,
                           D2DOptionDefault(o.key),
                           true);
        if (o.integer && o.maximum == 1)
        {
            n.kind = ControlKind::Choice;
            n.choices = {{-1, L"Default (Off)"}, {0, L"Off"}, {1, L"On"}};
            n.get = [&s, i] { return s.extras.values[i]; };
            n.defaultValue = [] { return -1.f; };
        }
        if (i == 14)
        {
            n.kind = ControlKind::Choice;
            n.choices = {{-1, L"Default (SDR Calibration)"},
                         {0, L"Unknown"},
                         {1, L"Native"},
                         {2, L"Normalized"},
                         {3, L"Conversion"}};
            n.get = [&s, i] { return s.extras.values[i]; };
            n.defaultValue = [] { return -1.f; };
        }
        c.controls.push_back(std::move(n));
    };
    {
        auto& p =
            page(L"General", L"General & Presentation", L"Output precision, RGB range and presentation buffering.");
        auto& c = card(p, L"Output");
        c.controls.push_back(D2DChoice(s, L"Output Texture Format", &s.outputFormat, D2DChoices(OutputChoices)));
        c.controls.push_back(D2DChoice(s, L"RGB Output Levels", &s.outputLevels, D2DChoices(LevelChoices)));
        std::vector<ChoiceItem> depths;
        for (int i = 0; i < int(std::size(DisplayDepths)); i++)
            depths.push_back({DisplayDepths[i], DepthChoices[i]});
        c.controls.push_back(D2DChoice(s, L"Dither Target Bit Depth", &s.displayDepth, depths));
        c.controls.push_back(D2DNumber(s,
                                       L"Max Frames in Flight",
                                       &s.maximumFramesInFlight,
                                       0,
                                       16,
                                       1,
                                       L"0 retains the device default. More buffering increases latency."));
        auto& analysis = card(p, L"Image Analysis");
        auto scope = D2DBool(s, L"Image Analysis", &s.imageAnalysis);
        scope.desc =
            L"Waveform, histogram, RGB parade, false color and a gray ramp. Samples before/after color mapping; excludes subtitles. SDR nits are nominal.";
        analysis.controls.push_back(std::move(scope));
        auto& mode = card(p,
                          L"Display Mode",
                          L"Switches the display to a listed mode matching the video frame rate in fullscreen and "
                          L"restores it when leaving fullscreen or stopping.");
        mode.controls.push_back(D2DBool(s,
                                        L"Switch Display Mode in Fullscreen",
                                        &s.displayModeSwitch,
                                        L"Uses the listed mode with the lowest refresh rate that is a whole multiple of the "
                                        L"video frame rate. Applies to this display profile only."));
        D2DControl list;
        list.name = L"Allowed Modes";
        list.kind = ControlKind::Text;
        list.placeholder = L"e.g. 2160p23, 2160p24, 2160p60";
        list.invalidText = L"Enter display modes like 2160p23, 2160p24, 2160p50, 2160p60 or just 23, 24, 60.";
        list.getText = [&s] { return std::wstring(s.displayModes); };
        list.setText = [&s](const std::wstring& text)
        {
            std::vector<DisplayModes::Mode> modes;
            if (text.size() >= std::size(s.displayModes) || !DisplayModes::Parse(text.c_str(), modes))
                return false;
            wcscpy_s(s.displayModes, text.c_str());
            return true;
        };
        list.reset = [&s] { s.displayModes[0] = 0; };
        list.enabled = [&s] { return !!s.displayModeSwitch; };
        // The modes this profile's display offers, read once per profile.
        std::wstring offered;
        for (const auto& device : DisplayProfiles::Connected())
        {
            MONITORINFOEXW monitor = {};
            monitor.cbSize = sizeof(monitor);
            if (device.id != s.displayProfile || !GetMonitorInfoW(device.monitor, &monitor))
                continue;
            offered = DisplayModes::Describe(monitor.szDevice);
        }
        list.desc = L"Comma-separated, e.g. 2160p23, or just 23 to keep the resolution. 23, 29 and 59 mean "
                    L"23.976, 29.97 and 59.94 Hz.";
        if (!offered.empty())
            list.desc += L"\n" + offered;
        mode.controls.push_back(std::move(list));
        auto delay = D2DNumber(s,
                               L"Pause After Switching (seconds)",
                               &s.displayModeDelay,
                               0,
                               10,
                               .1f,
                               L"Pauses playback while the display resynchronises after a mode change.");
        delay.enabled = [&s] { return !!s.displayModeSwitch; };
        mode.controls.push_back(std::move(delay));
        auto& b = card(p, L"Background & Transparency");
        for (int i = 0; i < 14; i++)
            extra(b, i);
    }
    {
        auto& p = page(L"Processing", L"Processing", L"Image cleanup, field processing and output dithering.");
        auto& d = card(p, L"Debanding");
        d.controls.push_back(D2DBool(s, L"Debanding", &s.deband));
        const auto& def =
            pl_render_default_params.deband_params ? *pl_render_default_params.deband_params : pl_deband_default_params;
        d.controls.push_back(D2DNumber(s,
                                       L"Iterations",
                                       &s.debandIterations,
                                       0,
                                       16,
                                       1,
                                       L"More iterations remove stronger banding; may lose detail.",
                                       float(def.iterations),
                                       true));
        d.controls.push_back(D2DNumber(s,
                                       L"Threshold",
                                       &s.debandThreshold,
                                       0,
                                       1000,
                                       .001f,
                                       L"Difference threshold for detecting banding.",
                                       def.threshold,
                                       true));
        d.controls.push_back(
            D2DNumber(s, L"Radius", &s.debandRadius, 0, 1000, .1f, L"Search radius in pixels.", def.radius, true));
        d.controls.push_back(
            D2DNumber(s, L"Grain", &s.debandGrain, 0, 1000, .001f, L"Grain added after debanding.", def.grain, true));
        auto& a = card(p, L"Adaptive Sharpen");
        a.controls.push_back(D2DBool(s, L"Enhance Edges and Details", &s.adaptiveSharpen));
        a.controls.push_back(D2DNumber(s, L"Strength", &s.sharpenStrength, 0, 2, .001f));
        auto& f = card(p, L"Deinterlacing");
        f.controls.push_back(D2DChoice(s, L"Algorithm", &s.deinterlacing, {{0, L"Off"}, {1, L"Bwdif"}, {2, L"Yadif"}}));
        f.controls.push_back(
            D2DChoice(s,
                      L"Field Order",
                      &s.fieldOverride,
                      {{0, L"Auto (Source Flags)"}, {1, L"Top Field First"}, {2, L"Bottom Field First"}}));
        auto& t = card(p, L"Dithering");
        t.controls.push_back(D2DChoice(s, L"Algorithm", &s.ditherMethod, D2DChoices(DitherChoices)));
        t.controls.push_back(D2DBool(s, L"Change Dither for Every Frame", &s.ditherTemporal));
    }
    {
        auto& p = page(
            L"Scaling",
            L"Scaling & Frame Mixing",
            L"Choose a preset or enable custom settings below. Custom controls inherit the starting filter until edited.");
        auto& c = card(p, L"Filters");
        for (int role = 0; role < 5; role++)
        {
            auto& f = s.scaling.filters[role];
            const auto names = D2DScalerNames(role, true, f.filter);
            D2DControl n;
            n.name = ScalingLabels[role];
            n.kind = ControlKind::Choice;
            n.enabled = [&s, &f, role]
            { return strcmp(f.filter, "custom") != 0 && (role != 0 || !s.rtxSuperResolution); };
            for (int i = 0; i < int(names.size()); i++)
                n.choices.push_back({i, D2DWide(names[i].c_str()), D2DScalerDescription(role, names[i].c_str())});
            n.get = [&f, names]
            {
                for (int i = 0; i < int(names.size()); i++)
                    if (names[i] == f.filter)
                        return float(i);
                return 0.f;
            };
            n.set = [&f, names](float v)
            {
                if (names[int(v)] == "custom")
                    D2DSetCustomScaling(f, true);
                else
                    strcpy_s(f.filter, names[int(v)].c_str());
            };
            n.detail = [&f, role] { return D2DScalerDescription(role, f.filter); };
            n.reset = [&f] { f = {}; };
            c.controls.push_back(std::move(n));
        }
        c.controls.push_back(D2DNumber(s,
                                       L"Global Anti-Ringing",
                                       &s.scaling.antiringing,
                                       0,
                                       1,
                                       .001f,
                                       L"Suppress overshoot around edges.",
                                       pl_render_default_params.antiringing_strength,
                                       true));
        c.controls.push_back(D2DBool(s, L"Enable Smooth Motion", &s.smoothMotion));
        extra(c, 15);
        extra(c, 16);
        auto& rtx = card(p,
                         L"RTX Video Super Resolution",
                         L"Enable Super Resolution in the NVIDIA app first. Quality is set there.");
        auto rtxSwitch = D2DBool(
            s,
            L"RTX Video Super Resolution",
            &s.rtxSuperResolution,
            L"Request AI upscaling through the NVIDIA driver. Requires Super Resolution enabled in the NVIDIA app.");
        rtxSwitch.headerToggle = true;
        rtx.controls.push_back(std::move(rtxSwitch));
        for (int role = 0; role < 5; role++)
        {
            auto& f = s.scaling.filters[role];
            auto& e = card(p, (std::wstring(L"Custom: ") + ScalingLabels[role]).c_str());
            auto enabled = [&s, &f, role]
            { return !strcmp(f.filter, "custom") && (role != 0 || !s.rtxSuperResolution); };
            D2DControl toggle;
            toggle.name = L"Enable Custom Settings";
            toggle.desc = L"Use custom filter settings based on the starting filter.";
            toggle.kind = ControlKind::Toggle;
            toggle.headerToggle = true;
            toggle.enabled = [&s, role] { return role != 0 || !s.rtxSuperResolution; };
            // Show the stored state even while RTX locks the upscaler controls.
            toggle.get = [&f] { return !strcmp(f.filter, "custom") ? 1.f : 0.f; };
            toggle.set = [&f](float v) { D2DSetCustomScaling(f, v != 0); };
            toggle.reset = [&f] { D2DSetCustomScaling(f, false); };
            e.controls.push_back(std::move(toggle));
            for (int kind = 0; kind < 3; kind++)
            {
                char* field = kind == 0 ? f.preset : kind == 1 ? f.kernel : f.window;
                std::vector<std::string> names = {"auto", "none"};
                if (kind == 0)
                {
                    names = D2DScalerNames(role, false, field);
                }
                else
                    for (int j = 0; j < pl_num_filter_functions; j++)
                        names.emplace_back(pl_filter_functions[j]->name);
                D2DControl n;
                n.name = kind == 0 ? L"Starting Filter" : kind == 1 ? L"Kernel" : L"Window";
                n.kind = ControlKind::Choice;
                n.enabled = enabled;
                for (int j = 0; j < int(names.size()); j++)
                    n.choices.push_back(
                        {j, D2DWide(names[j].c_str()), kind == 0 ? D2DScalerDescription(role, names[j].c_str()) : L""});
                n.get = [field, names]
                {
                    for (int j = 0; j < int(names.size()); j++)
                        if (names[j] == field)
                            return float(j);
                    return 0.f;
                };
                n.set = [field, names](float v) { strcpy_s(field, 64, names[int(v)].c_str()); };
                if (kind == 0)
                    n.detail = [field, role] { return D2DScalerDescription(role, field); };
                n.reset = [field] { strcpy_s(field, 64, "auto"); };
                e.controls.push_back(std::move(n));
            }
            for (int i = 0; i < 9; i++)
            {
                D2DControl n;
                n.name = ScalingFieldLabels[i];
                n.kind = ControlKind::SliderFloat;
                n.minVal = i >= 5 ? -100.f : 0.f;
                n.maxVal = i == 0 ? 16.f : i == 2 ? 100.f : i >= 5 ? 100.f : 1.f;
                n.stepVal = .001f;
                n.enabled = [&s, role, i, enabled]
                {
                    if (!enabled())
                        return false;
                    auto o = pl_options_alloc(nullptr);
                    if (!o)
                        return false;
                    pl_options_reset(o, &pl_render_default_params);
                    bool valid = ApplyScaling(o, s.scaling);
                    const auto* f = valid ? ScalingFilter(o, role) : nullptr;
                    bool supported = f && f->kernel;
                    if (supported && i == 0)
                        supported = f->kernel->resizable;
                    if (supported && (i == 5 || i == 6))
                        supported = f->kernel->tunable[i - 5];
                    if (supported && (i == 7 || i == 8))
                        supported = f->window && f->window->tunable[i - 7];
                    pl_options_free(&o);
                    return supported;
                };
                n.get = [&s, role, i]
                {
                    auto& f = s.scaling.filters[role];
                    return f.overrides & (1u << i) ? f.values[i] : D2DScalingDefault(s, role, i);
                };
                n.defaultValue = [&s, role, i] { return D2DScalingDefault(s, role, i); };
                n.set = [&f, i](float v)
                {
                    f.values[i] = v;
                    f.overrides |= 1u << i;
                };
                n.reset = [&f, i]
                {
                    f.overrides &= ~(1u << i);
                    f.values[i] = 0;
                };
                e.controls.push_back(std::move(n));
            }
            auto n =
                D2DChoice(s, L"Polar (EWA)", &f.polar, {{-1, L"Inherit Starting Filter"}, {0, L"Off"}, {1, L"On"}});
            n.enabled = [enabled, role] { return role != 4 && enabled(); };
            e.controls.push_back(std::move(n));
        }
    }
    {
        auto& p = page(L"Black Bars / Zoom",
                       L"Black Bars & Zoom",
                       L"Detect bars in decoded video pixels and adjust the active image.");
        auto& c = card(p, L"Detection & Zoom");
        c.controls.push_back(D2DBool(s, L"Detect Hardcoded Black Bars", &s.blackBars.enabled));
        c.controls.push_back(D2DBool(s, L"Crop Black Bars", &s.blackBars.crop));
        c.controls.push_back(D2DBool(s,
                                     L"Protect Burned-in Subtitles",
                                     &s.blackBars.keepSubtitles,
                                     L"Temporarily restore bars if they contain visible content."));
        c.controls.push_back(D2DBool(s, L"Keep a Stable Zoom", &s.blackBars.stableZoom));
        c.controls.push_back(D2DChoice(
            s, L"Zoom Mode", &s.blackBars.zoom, {{0, L"Fit (Keep All Content)"}, {1, L"Fill (May Crop Content)"}}));
        c.controls.push_back(D2DNumber(s, L"Black Threshold", &s.blackBars.threshold, 0, 10, .01f));
        c.controls.push_back(D2DNumber(s, L"Detection Stability (seconds)", &s.blackBars.delay, 0, 10, .01f));
    }
    {
        auto& p = page(L"Color Adjustment",
                       L"Color Adjustment",
                       L"Neutral values are Brightness 0, Contrast / Saturation / Gamma 1 and Temperature 6500 K.");
        auto& c = card(p, L"Picture Controls");
        c.controls.push_back(D2DBool(s, L"Enable Color Adjustment", &s.colorAdjustment));
        c.controls.push_back(D2DNumber(s, L"Brightness", &s.brightness, -1, 1, .001f));
        c.controls.push_back(D2DNumber(s, L"Contrast", &s.contrast, 0, 100, .001f));
        c.controls.push_back(D2DNumber(s, L"Saturation", &s.saturation, 0, 100, .001f));
        c.controls.push_back(D2DNumber(s, L"Gamma", &s.gamma, 0, 100, .001f));
        auto h = D2DNumber(s, L"Hue (degrees)", &s.hue, -180, 180, .01f);
        h.defaultValue = [] { return 0.f; };
        h.get = [&s] { return s.hue * 180 / 3.141593f; };
        h.set = [&s](float v) { s.hue = v * 3.141593f / 180; };
        c.controls.push_back(std::move(h));
        auto t = D2DNumber(s, L"Temperature (K)", &s.temperature, 2500, 6500, 1);
        t.defaultValue = [] { return 6500.f; };
        t.get = [&s] { return 6500 + 3500 * s.temperature; };
        t.set = [&s](float v) { s.temperature = (v - 6500) / 3500; };
        c.controls.push_back(std::move(t));
    }
    {
        auto& p = page(L"HDR & Color Mapping",
                       L"HDR & Color Mapping",
                       L"Display output, HDR metadata, peak detection, tone mapping and gamut mapping.");
        auto& c = card(p, L"HDR Output");
        auto mode =
            D2DChoice(s, L"HDR Mode", &s.hdrMode, {{1, L"Passthrough HDR to Display"}, {2, L"Tone Map HDR to SDR"}});
        mode.set = [&s](float v)
        {
            s.hdrMode = int(v);
            s.hdrPassthrough = int(v) != 2;
        };
        c.controls.push_back(std::move(mode));
        auto protection = D2DBool(s, L"Clipping Protection", &s.clippingProtection);
        protection.enabled = [&s] { return s.hdrMode != 1; };
        c.controls.push_back(std::move(protection));
        c.controls.push_back(D2DNumber(s, L"SDR Target Brightness (nits)", &s.sdrNits, 40, 1000, 1));
        c.controls.push_back(D2DChoice(s, L"Tone Mapping Function", &s.toneMapping, D2DChoices(ToneChoices)));
        c.controls.push_back(D2DChoice(s, L"Gamut Mapping Function", &s.gamutMapping, D2DChoices(GamutChoices)));
        c.controls.push_back(D2DBool(s, L"Show Clipping", &s.showClipping));
        auto& d = card(p, L"HDR Peak Detection");
        d.controls.push_back(D2DBool(s, L"Enable Peak Detection", &s.peakDetect));
        d.controls.push_back(D2DBool(s, L"Allow Delayed Peak Detection", &s.allowDelayedPeak));
        d.controls.push_back(D2DNumber(s, L"Smoothing Period", &s.peakSmoothing, 0, 1000, .01f));
        d.controls.push_back(D2DNumber(s, L"Scene Threshold Low", &s.sceneLow, 0, 100, .001f));
        d.controls.push_back(D2DNumber(s, L"Scene Threshold High", &s.sceneHigh, 0, 100, .001f));
        d.controls.push_back(D2DNumber(s, L"Percentile", &s.peakPercentile, 0, 100, .001f));
        d.controls.push_back(D2DNumber(s, L"Black Cutoff", &s.blackCutoff, 0, 100, .001f));
        const wchar_t* groups[] = {L"Tone Mapping Knee",
                                   L"Tone Mapping Constants",
                                   L"Contrast Recovery",
                                   L"Gamut Mapping Constants",
                                   L"Mapping LUT Quality",
                                   L"Mapping Visualization",
                                   L"Expansion & Metadata"};
        for (int group = 0; group < 7; group++)
        {
            auto& g = card(p, groups[group]);
            for (int i = 0; i < MappingOptionCount; i++)
                if (MappingOptions[i].group == group)
                {
                    const auto& info = MappingOptions[i];
                    const auto opt = pl_find_option(info.key);
                    if (!opt)
                        continue;
                    D2DControl n;
                    n.name = info.label;
                    n.desc = info.help;
                    n.defaultValue = [i] { return D2DOptionDefault(MappingOptions[i].key); };
                    n.kind = opt->type == PL_OPT_INT ? ControlKind::SliderInt : ControlKind::SliderFloat;
                    n.minVal = opt->min;
                    n.maxVal = opt->max;
                    n.stepVal = opt->type == PL_OPT_INT ? 1 : .001f;
                    n.get = [&s, i] { return D2DMappingValue(s, i); };
                    n.set = [&s, i](float v) { sprintf_s(s.colorMapping.values[i], "%.9g", double(v)); };
                    n.reset = [&s, i]
                    {
                        s.colorMapping.values[i][0] = 0;
                        D2DResetMappingLegacy(s, i);
                    };
                    if (opt->type == PL_OPT_BOOL)
                    {
                        n.kind = ControlKind::Toggle;
                        n.get = [&s, i]
                        {
                            const auto* v = s.colorMapping.values[i];
                            return v[0] ? float(!strcmp(v, "yes")) : D2DOptionDefault(MappingOptions[i].key);
                        };
                        n.set = [&s, i](float v) { strcpy_s(s.colorMapping.values[i], v ? "yes" : "no"); };
                    }
                    else if (opt->type != PL_OPT_FLOAT && opt->type != PL_OPT_INT)
                    {
                        n.kind = ControlKind::Choice;
                        n.choices = {
                            {0, L"Auto (Metadata)"}, {1, L"None"}, {2, L"HDR10"}, {3, L"HDR10+"}, {4, L"CIE Y"}};
                        n.get = [&s, i]
                        {
                            const char* names[] = {"any", "none", "hdr10", "hdr10plus", "cie_y"};
                            for (int j = 0; j < 5; j++)
                                if (!strcmp(s.colorMapping.values[i], names[j]))
                                    return float(j);
                            return 0.f;
                        };
                        n.set = [&s, i](float v)
                        {
                            const char* names[] = {"any", "none", "hdr10", "hdr10plus", "cie_y"};
                            strcpy_s(s.colorMapping.values[i], names[int(v)]);
                        };
                    }
                    if (n.kind == ControlKind::SliderFloat && n.minVal == 0 && n.maxVal == 0)
                    {
                        if (i >= 25 && i <= 28)
                        {
                            n.minVal = 0;
                            n.maxVal = 1;
                        }
                        else if (i == 29)
                        {
                            n.minVal = -3.141593f;
                            n.maxVal = 3.141593f;
                        }
                        else if (i == 30)
                        {
                            n.minVal = 0;
                            n.maxVal = 1.570797f;
                        }
                    }
                    g.controls.push_back(std::move(n));
                }
        }
        const wchar_t* zones[] = {L"Shadows", L"Midtones", L"Highlights"};
        for (int i = 0; i < 3; i++)
        {
            auto& z = card(p, zones[i], L"HDR tonal adjustment; 0 is neutral. Exposure is measured in stops.");
            z.controls.push_back(D2DNumber(s, L"Exposure", &s.tonal.exposure[i], -4, 4, .001f));
            z.controls.push_back(D2DNumber(s,
                                           L"Contrast",
                                           &s.tonal.contrast[i],
                                           -1,
                                           1,
                                           .001f,
                                           L"Change contrast within this HDR zone. 0 is neutral."));
            z.controls.push_back(D2DNumber(s,
                                           L"Saturation",
                                           &s.tonal.saturation[i],
                                           -1,
                                           1,
                                           .001f,
                                           L"Change color intensity within this HDR zone. 0 is neutral."));
        }
    }
    {
        auto& p = page(L"Color Management", L"Color Management", L"ICC display profiles and 3D LUT calibration.");
        auto& c = card(p, L"ICC Profile");
        auto browse = [&s](bool icc, HWND owner)
        {
            wchar_t path[1024];
            wcscpy_s(path, icc ? s.iccPath : s.lutPath);
            OPENFILENAMEW dialog = {sizeof(dialog)};
            dialog.hwndOwner = owner;
            dialog.lpstrFile = path;
            dialog.nMaxFile = 1024;
            dialog.lpstrFilter =
                icc ? L"ICC profiles\0*.icc;*.icm\0All files\0*.*\0" : L"3D LUT\0*.cube\0All files\0*.*\0";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&dialog))
                wcscpy_s(icc ? s.iccPath : s.lutPath, 1024, path);
        };
        auto n = D2DAction(L"ICC Profile File", L"Browse…", [browse](HWND h) { browse(true, h); });
        n.detail = [&s] { return std::wstring(s.iccPath); };
        n.reset = [&s] { s.iccPath[0] = 0; };
        c.controls.push_back(std::move(n));
        c.controls.push_back(D2DBool(s, L"Enable ICC Profile", &s.iccEnabled));
        c.controls.push_back(
            D2DChoice(s, L"ICC Rendering Intent", &s.iccIntent, {{0, L"Relative Colorimetric"}, {1, L"Perceptual"}}));
        auto& l = card(p, L"3D LUT");
        n = D2DAction(L"LUT File", L"Browse…", [browse](HWND h) { browse(false, h); });
        n.detail = [&s] { return std::wstring(s.lutPath); };
        n.reset = [&s] { s.lutPath[0] = 0; };
        l.controls.push_back(std::move(n));
        l.controls.push_back(D2DBool(s, L"Enable 3D LUT", &s.lutEnabled));
        extra(l, 14);
    }
    {
        auto& p = page(
            L"Shaders",
            L"Shader Chain",
            L"Enable shaders individually. Chain position follows the stored order; separate input and output stages are retained.");
        auto& chain = card(p, L"Chain Controls");
        chain.controls.push_back(D2DAction(L"Disable All Shaders",
                                           L"All Off",
                                           [&s](HWND)
                                           {
                                               for (auto& c : s.userShaders.shaders)
                                                   c.enabled = 0;
                                           }));
        for (int id : s.userShaders.order)
        {
            const auto& info = ShaderCatalog[id];
            auto& c = card(p, info.label, info.help);
            auto& f = s.userShaders.shaders[id];
            D2DControl n;
            n.name = L"Enabled";
            n.kind = ControlKind::Toggle;
            n.get = [&f] { return float(f.enabled); };
            n.set = [&f](float v) { f.enabled = uint32_t(v); };
            n.reset = [&f] { f = {}; };
            c.controls.push_back(std::move(n));
            for (int i = 0; i < info.count; i++)
            {
                const auto& param = info.parameters[i];
                n = {};
                n.name = ShaderParameterLabel(param.name) + L" (" + D2DWide(param.name) + L")";
                n.desc = std::wstring(L"Adjust ") + ShaderParameterLabel(param.name) +
                         L" for this shader. The default preserves the shader author's setting.";
                n.kind = param.integer ? ControlKind::SliderInt : ControlKind::SliderFloat;
                n.minVal = param.minimum;
                n.maxVal = param.maximum;
                n.stepVal = param.integer ? 1 : .001f;
                n.defaultValue = [initial = param.initial] { return initial; };
                n.get = [&f, i, initial = param.initial] { return f.overrides & (1u << i) ? f.values[i] : initial; };
                n.set = [&f, i](float v)
                {
                    f.values[i] = v;
                    f.overrides |= 1u << i;
                };
                n.reset = [&f, i]
                {
                    f.values[i] = 0;
                    f.overrides &= ~(1u << i);
                };
                c.controls.push_back(std::move(n));
            }
            auto position = D2DControl{};
            position.name = L"Chain Position";
            position.kind = ControlKind::Choice;
            for (int j = 0; j < ShaderCount; j++)
                position.choices.push_back({j, std::to_wstring(j + 1)});
            position.defaultValue = [id] { return float(id); };
            position.get = [&s, id]
            {
                for (int j = 0; j < ShaderCount; j++)
                    if (s.userShaders.order[j] == id)
                        return float(j);
                return 0.f;
            };
            position.set = [&s, id](float v)
            {
                int at = 0;
                while (s.userShaders.order[at] != id)
                    at++;
                int to = int(v);
                if (at < to)
                    std::rotate(s.userShaders.order + at, s.userShaders.order + at + 1, s.userShaders.order + to + 1);
                else
                    std::rotate(s.userShaders.order + to, s.userShaders.order + at, s.userShaders.order + at + 1);
            };
            position.reset = [&s]
            {
                for (int j = 0; j < ShaderCount; j++)
                    s.userShaders.order[j] = j;
            };
            c.controls.push_back(std::move(position));
        }
    }
    for (auto& page : pages)
        for (auto& card : page.cards)
            for (auto& c : card.controls)
                if (c.desc.empty())
                    c.desc = D2DSettingHelp(c.name);
    return pages;
}
