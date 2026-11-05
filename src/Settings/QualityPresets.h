#pragma once
#include "Settings/Settings.h"
#include "Settings/SettingsIni.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// The settings the presets and the measurement change.
inline void CopyQualitySettings(const PlaceboSettings& from, PlaceboSettings& to)
{
    for (int role = 0; role < 4; role++)
        to.scaling.filters[role] = from.scaling.filters[role];
    to.scaling.antiringing = from.scaling.antiringing;
    to.deband = from.deband;
    to.debandIterations = from.debandIterations;
    to.debandThreshold = from.debandThreshold;
    to.debandRadius = from.debandRadius;
    to.debandGrain = from.debandGrain;
    to.ditherMethod = from.ditherMethod;
    to.ditherTemporal = from.ditherTemporal;
    to.peakDetect = from.peakDetect;
    to.peakPercentile = from.peakPercentile;
    to.contrastRecovery = from.contrastRecovery;
}

// Own values a preset replaced, kept for the Custom entry: one INI file per
// display or profile in %APPDATA%\FrankyRenderer\CustomQuality.
inline std::filesystem::path CustomQualityPath(const std::wstring& key)
{
    wchar_t appData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (key.empty() || !length || length >= MAX_PATH)
        return {};
    return std::filesystem::path(appData) / L"FrankyRenderer" / L"CustomQuality" / (key + L".ini");
}

inline bool SaveCustomQuality(const std::wstring& key, const PlaceboSettings& s)
{
    const auto path = CustomQualityPath(key);
    if (path.empty())
        return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::wstring message;
    return SUCCEEDED(SettingsIni::ExportText(path.c_str(), SettingsIni::Serialize(s), message));
}

inline bool LoadCustomQuality(const std::wstring& key, PlaceboSettings& out)
{
    const auto path = CustomQualityPath(key);
    std::ifstream file(path, std::ios::binary);
    if (path.empty() || !file)
        return false;
    try
    {
        out = SettingsIni::Parse(std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// PlaceboSettings::measuredQuality records where the values came from and
// qualityStamp fingerprints every setting at that moment: any later change,
// covered by a preset or not, reads as Custom.
inline constexpr int PresetSource = 256;

inline uint32_t QualityStamp(const PlaceboSettings& s)
{
    PlaceboSettings copy = s;
    uint32_t hash = 2166136261u;
    SettingsIni::Visit(copy,
                       [&](const std::string& section, const std::string& name, auto& value)
                       {
                           if (name == "measuredQuality" || name == "qualityStamp")
                               return;
                           for (char c : section + "." + name + "=" + SettingsIni::Encode(value) + ";")
                               hash = (hash ^ uint8_t(c)) * 16777619u;
                       });
    return hash ? hash : 1;
}

inline void MarkQuality(PlaceboSettings& s, int source)
{
    s.measuredQuality = source;
    s.qualityStamp = QualityStamp(s);
}

inline void ApplyQualityPreset(PlaceboSettings& s, int index)
{
    ApplyQualityPreset(s, QualityPresets[index]);
    MarkQuality(s, PresetSource + index);
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
    MarkQuality(s, int(MeasuredFlag | (upgrades & (MeasuredFlag - 1))));
}

// What the quality selector shows: the chosen preset, Measured, or Custom
// (preset < 0 and !measured) once anything changed since.
struct QualityOrigin
{
    int preset = -1;
    bool measured = false;
};

inline QualityOrigin CurrentQuality(const PlaceboSettings& s)
{
    QualityOrigin origin;
    const uint32_t stamp = QualityStamp(s);
    if (s.measuredQuality && s.qualityStamp == stamp)
    {
        if (s.measuredQuality >= PresetSource)
            origin.preset = s.measuredQuality - PresetSource;
        else
            origin.measured = true;
    }
    else if (!s.measuredQuality)
    {
        // Nothing chosen yet: untouched defaults are the Default preset.
        PlaceboSettings defaults;
        wcscpy_s(defaults.displayProfile, s.displayProfile);
        if (QualityStamp(defaults) == stamp)
            origin.preset = 1;
    }
    return origin;
}

// The preset a set of upgrades equals, or -1.
inline int MeasuredPresetMatch(uint32_t upgrades)
{
    PlaceboSettings applied;
    ApplyQualityPreset(applied, MeasuredPreset(upgrades));
    return MatchQualityPreset(applied);
}
