#pragma once
#include "Settings/Settings.h"
#include "Settings/SettingsIni.h"
#include "Settings/SettingsStore.h"
#include <algorithm>
#include <mutex>
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

// Own values a preset replaced, kept for the Custom entry per display or
// profile in the settings file: [Settings\CustomQuality\<key>], only the
// settings CopyQualitySettings takes over, as "Section.key" lines.
inline std::wstring CustomQualityPath(const std::wstring& key)
{
    return SettingsStore::Root() + L"\\CustomQuality\\" + key;
}

inline bool QualityEntry(const std::string& section, const std::string& key)
{
    static const char* const renderer[] = {"deband",
                                           "debandIterations",
                                           "debandThreshold",
                                           "debandRadius",
                                           "debandGrain",
                                           "ditherMethod",
                                           "ditherTemporal",
                                           "peakDetect",
                                           "peakPercentile",
                                           "contrastRecovery"};
    if (section == "Renderer")
        return std::any_of(std::begin(renderer), std::end(renderer), [&](const char* name) { return key == name; });
    return section == "Scaling" || (section.starts_with("Scaling.") && section != "Scaling.frame_mixer");
}

inline bool StoreCustomQuality(const std::wstring& key, const PlaceboSettings& s)
{
    if (key.empty())
        return false;
    PlaceboSettings quality;
    CopyQualitySettings(s, quality);
    SettingsIni::Entries entries;
    SettingsIni::Visit(quality,
                       [&](const std::string& section, const std::string& name, auto& value)
                       {
                           if (QualityEntry(section, name))
                               entries[{section, name}] = SettingsIni::Encode(value);
                       });
    const auto text = SettingsIni::JoinEntries(entries);
    SettingsStore::Node node;
    node.values[L"Overrides"] = {REG_BINARY, std::vector<BYTE>(text.begin(), text.end())};
    return SettingsStore::ReplaceSubtree(CustomQualityPath(key), node) == ERROR_SUCCESS;
}

// Files of earlier versions in %APPDATA%\FrankyRenderer\CustomQuality are
// taken over once, then removed.
inline void MoveOldCustomQuality()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       const auto folder = SettingsStore::Folder();
                       const auto directory = folder.empty() ? L"" : folder + L"\\CustomQuality";
                       std::error_code error;
                       if (directory.empty() || !std::filesystem::is_directory(directory, error))
                           return;
                       std::vector<std::filesystem::path> moved;
                       for (const auto& entry : std::filesystem::directory_iterator(directory, error))
                       {
                           std::error_code itemError;
                           if (!entry.is_regular_file(itemError) || _wcsicmp(entry.path().extension().c_str(), L".ini"))
                               continue;
                           try
                           {
                               std::ifstream file(entry.path(), std::ios::binary);
                               // Entries this version no longer knows are skipped.
                               auto entries = SettingsIni::ReadEntries(std::string(
                                   (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
                               PlaceboSettings saved;
                               SettingsIni::Visit(saved,
                                                  [&](const std::string& section, const std::string& name, auto& value)
                                                  {
                                                      const auto found = entries.find({section, name});
                                                      if (found == entries.end())
                                                          return;
                                                      try
                                                      {
                                                          SettingsIni::Decode(found->second, value);
                                                      }
                                                      catch (const std::exception&)
                                                      {
                                                      }
                                                  });
                               const auto key = entry.path().stem().wstring();
                               if (!SettingsStore::Exists(CustomQualityPath(key)) && !StoreCustomQuality(key, saved))
                                   continue;
                           }
                           catch (const std::exception&)
                           {
                               continue; // a broken file stays where it is
                           }
                           moved.push_back(entry.path());
                       }
                       if (!SettingsStore::Flush())
                           return;
                       for (const auto& path : moved)
                           std::filesystem::remove(path, error);
                       std::filesystem::remove(directory, error); // only when empty
                   });
}

inline bool SaveCustomQuality(const std::wstring& key, const PlaceboSettings& s)
{
    MoveOldCustomQuality();
    return StoreCustomQuality(key, s);
}

// The default settings with the kept quality values; false when none are kept.
inline bool LoadCustomQuality(const std::wstring& key, PlaceboSettings& out)
{
    MoveOldCustomQuality();
    if (key.empty())
        return false;
    SettingsStore::Key stored;
    if (SettingsStore::Open(CustomQualityPath(key), stored) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    if (stored.Query(L"Overrides", &type, nullptr, &size) != ERROR_SUCCESS || type != REG_BINARY)
        return false;
    std::string text(size, '\0');
    if (stored.Query(L"Overrides", &type, text.data(), &size) != ERROR_SUCCESS)
        return false;
    text.resize(size);
    const auto entries = SettingsIni::SplitEntries(text);
    PlaceboSettings result;
    try
    {
        SettingsIni::Visit(result,
                           [&](const std::string& section, const std::string& name, auto& value)
                           {
                               if (const auto found = entries.find({section, name}); found != entries.end())
                                   SettingsIni::Decode(found->second, value);
                           });
    }
    catch (const std::exception&)
    {
        return false;
    }
    if (FAILED(ValidateSettings(result)))
        return false;
    out = result;
    return true;
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
