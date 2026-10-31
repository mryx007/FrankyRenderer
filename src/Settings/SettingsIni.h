#pragma once
#include "Settings/Settings.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

// INI backups contain values rather than registry blobs or structure bytes.
// Keep keys stable; incompatible schema changes require a new FormatVersion.
namespace SettingsIni
{
using Entries = std::map<std::pair<std::string, std::string>, std::string>;

template <class F> void Visit(PlaceboSettings& s, F field)
{
#define INI_FIELD(name) field("Renderer", #name, s.name)
    INI_FIELD(saturation);
    INI_FIELD(contrast);
    INI_FIELD(brightness);
    INI_FIELD(gamma);
    INI_FIELD(sdrNits);
    INI_FIELD(deband);
    INI_FIELD(hdrPassthrough);
    INI_FIELD(autoHdr);
    INI_FIELD(clippingProtection);
    INI_FIELD(upscaler);
    INI_FIELD(downscaler);
    INI_FIELD(deinterlacing);
    INI_FIELD(fieldOverride);
    INI_FIELD(toneMapping);
    INI_FIELD(hdrNits);
    INI_FIELD(antiringing);
    INI_FIELD(upscalerBlur);
    INI_FIELD(downscalerBlur);
    INI_FIELD(debandIterations);
    INI_FIELD(debandThreshold);
    INI_FIELD(debandRadius);
    INI_FIELD(debandGrain);
    INI_FIELD(ditherMethod);
    INI_FIELD(ditherTemporal);
    INI_FIELD(chromaScaler);
    INI_FIELD(outputFormat);
    INI_FIELD(iccEnabled);
    INI_FIELD(lutEnabled);
    INI_FIELD(iccIntent);
    INI_FIELD(iccPath);
    INI_FIELD(lutPath);
    INI_FIELD(adaptiveSharpen);
    INI_FIELD(rtxSuperResolution);
    INI_FIELD(imageAnalysis);
    INI_FIELD(sharpenStrength);
    INI_FIELD(smoothMotion);
    INI_FIELD(outputLevels);
    INI_FIELD(displayDepth);
    INI_FIELD(peakDetect);
    INI_FIELD(peakSmoothing);
    INI_FIELD(sceneLow);
    INI_FIELD(sceneHigh);
    INI_FIELD(peakPercentile);
    INI_FIELD(contrastRecovery);
    INI_FIELD(contrastSmoothness);
    INI_FIELD(gamutMapping);
    INI_FIELD(kneeAdaptation);
    INI_FIELD(splineContrast);
    INI_FIELD(kneeMinimum);
    INI_FIELD(kneeMaximum);
    INI_FIELD(blackCutoff);
    INI_FIELD(perceptualStrength);
    INI_FIELD(softclipKnee);
    INI_FIELD(showClipping);
    INI_FIELD(hue);
    INI_FIELD(temperature);
    INI_FIELD(colorAdjustment);
    INI_FIELD(allowDelayedPeak);
    INI_FIELD(hdrMode);
    INI_FIELD(maximumFramesInFlight);
    INI_FIELD(displayModeSwitch);
    INI_FIELD(displayModes);
    INI_FIELD(displayModeDelay);
#undef INI_FIELD
    for (int i = 0; i < 3; ++i)
    {
        field("Tonal", "exposure" + std::to_string(i), s.tonal.exposure[i]);
        field("Tonal", "contrast" + std::to_string(i), s.tonal.contrast[i]);
        field("Tonal", "saturation" + std::to_string(i), s.tonal.saturation[i]);
    }
#define INI_BAR(name) field("BlackBars", #name, s.blackBars.name)
    INI_BAR(enabled);
    INI_BAR(crop);
    INI_BAR(keepSubtitles);
    INI_BAR(zoom);
    INI_BAR(threshold);
    INI_BAR(delay);
    INI_BAR(stableZoom);
#undef INI_BAR
    for (int i = 0; i < ExtraCount; ++i)
        field("Extras", ExtraOptions[i].key, s.extras.values[i]);
    for (int i = 0; i < MappingOptionCount; ++i)
        field("ColorMapping", MappingOptions[i].key, s.colorMapping.values[i]);
    field("Scaling", "antiringing", s.scaling.antiringing);
    for (int i = 0; i < 5; ++i)
    {
        auto& f = s.scaling.filters[i];
        const auto section = std::string("Scaling.") + ScalingRoles[i];
        field(section, "filter", f.filter);
        field(section, "preset", f.preset);
        field(section, "kernel", f.kernel);
        field(section, "window", f.window);
        field(section, "overrides", f.overrides);
        field(section, "polar", f.polar);
        for (int j = 0; j < 9; ++j)
            field(section, ScalingFields[j], f.values[j]);
    }
    for (int i = 0; i < ShaderCount; ++i)
    {
        auto& shader = s.userShaders.shaders[i];
        const auto section = std::string("Shader.") + ShaderCatalog[i].file;
        field(section, "enabled", shader.enabled);
        field(section, "overrides", shader.overrides);
        for (int j = 0; j < ShaderParameterLimit; ++j)
            field(section, "parameter" + std::to_string(j), shader.values[j]);
        field("ShaderOrder", "slot" + std::to_string(i), s.userShaders.order[i]);
    }
}

inline std::string Utf8(const wchar_t* value)
{
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!count)
        throw std::runtime_error("Invalid text encoding.");
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, out.data(), count, nullptr, nullptr);
    out.pop_back();
    return out;
}

inline std::string Quote(const std::string& value)
{
    std::string out = "\"";
    for (char ch : value)
    {
        if (ch == '\\' || ch == '"')
            out += '\\';
        if (ch == '\n')
            out += "\\n";
        else if (ch == '\r')
            out += "\\r";
        else if (ch == '\t')
            out += "\\t";
        else
            out += ch;
    }
    return out + '"';
}

inline std::string Unquote(const std::string& value)
{
    if (value.size() < 2 || value.front() != '"' || value.back() != '"')
        throw std::runtime_error("Text values must be quoted.");
    std::string out;
    for (size_t i = 1; i + 1 < value.size(); ++i)
    {
        char ch = value[i];
        if (ch == '"')
            throw std::runtime_error("Unexpected quote in text value.");
        if (ch == '\\')
        {
            if (++i + 1 >= value.size())
                throw std::runtime_error("Incomplete text escape.");
            ch = value[i];
            if (ch == 'n')
                ch = '\n';
            else if (ch == 'r')
                ch = '\r';
            else if (ch == 't')
                ch = '\t';
            else if (ch != '\\' && ch != '"')
                throw std::runtime_error("Unknown text escape.");
        }
        if (!ch)
            throw std::runtime_error("NUL is not allowed in text.");
        out += ch;
    }
    return out;
}

template <class T> std::string Encode(const T& value)
{
    if constexpr (std::is_array_v<T>)
    {
        if constexpr (std::is_same_v<std::remove_extent_t<T>, wchar_t>)
            return Quote(Utf8(value));
        else
            return Quote(value);
    }
    else
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            throw std::runtime_error("Could not format value.");
        return {buffer, result.ptr};
    }
}

template <class T> void Decode(const std::string& text, T& value)
{
    if constexpr (std::is_array_v<T>)
    {
        const auto decoded = Unquote(text);
        constexpr size_t capacity = std::extent_v<T>;
        if constexpr (std::is_same_v<std::remove_extent_t<T>, wchar_t>)
        {
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded.c_str(), -1, nullptr, 0);
            if (!count || count > capacity)
                throw std::runtime_error("Invalid or oversized text value.");
            std::fill(std::begin(value), std::end(value), std::remove_extent_t<T>{});
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded.c_str(), -1, value, int(capacity));
        }
        else
        {
            if (decoded.size() >= capacity)
                throw std::runtime_error("Text value is too long.");
            std::fill(std::begin(value), std::end(value), std::remove_extent_t<T>{});
            std::copy(decoded.begin(), decoded.end(), value);
        }
    }
    else
    {
        T parsed{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            throw std::runtime_error("Invalid numeric value.");
        if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(parsed))
                throw std::runtime_error("Non-finite numeric value.");
        value = parsed;
    }
}

inline std::string Serialize(const PlaceboSettings& settings)
{
    if (FAILED(ValidateSettings(settings)))
        throw std::runtime_error("Settings are outside the supported range.");
    PlaceboSettings copy = settings;
    Entries entries;
    Visit(copy,
          [&](const std::string& section, const std::string& name, auto& value)
          { entries[{section, name}] = Encode(value); });
    std::string out = "; FrankyRenderer settings backup (UTF-8)\r\n"
                      "; External ICC and LUT files are referenced by path, not embedded.\r\n"
                      "[FrankyRenderer]\r\nFormatVersion=1\r\n";
    std::string section;
    for (const auto& [key, value] : entries)
    {
        if (key.first != section)
        {
            section = key.first;
            out += "\r\n[" + section + "]\r\n";
        }
        out += key.second + "=" + value + "\r\n";
    }
    return out;
}

inline std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r");
    return first == std::string::npos ? std::string{}
                                      : value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

inline Entries ReadEntries(const std::string& text, size_t limit = 1024 * 1024, size_t maxEntries = 2048)
{
    if (text.size() > limit || text.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid or oversized INI file.");
    Entries entries;
    std::istringstream input(text.substr(text.starts_with("\xef\xbb\xbf") ? 3 : 0));
    std::string line, section;
    while (std::getline(input, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[' && line.back() == ']')
            section = line.substr(1, line.size() - 2);
        else
        {
            const auto equals = line.find('=');
            if (section.empty() || equals == std::string::npos || !equals)
                throw std::runtime_error("Malformed INI line.");
            if (!entries.emplace(std::make_pair(section, Trim(line.substr(0, equals))), Trim(line.substr(equals + 1)))
                     .second)
                throw std::runtime_error("Duplicate INI setting.");
            if (entries.size() > maxEntries)
                throw std::runtime_error("Too many INI settings.");
        }
    }
    return entries;
}

inline PlaceboSettings Parse(const std::string& text)
{
    auto entries = ReadEntries(text);
    const auto version = entries.find({"FrankyRenderer", "FormatVersion"});
    if (version == entries.end() || version->second != "1")
        throw std::runtime_error("Unsupported settings file version.");
    entries.erase(version);
    PlaceboSettings candidate;
    Visit(candidate,
          [&](const std::string& group, const std::string& name, auto& value)
          {
              const auto found = entries.find({group, name});
              if (found != entries.end())
              {
                  Decode(found->second, value);
                  entries.erase(found);
              }
          });
    if (!entries.empty())
        throw std::runtime_error("Unknown INI setting.");
    for (BOOL flag : {candidate.deband,
                      candidate.hdrPassthrough,
                      candidate.autoHdr,
                      candidate.clippingProtection,
                      candidate.ditherTemporal,
                      candidate.iccEnabled,
                      candidate.lutEnabled,
                      candidate.adaptiveSharpen,
                      candidate.smoothMotion,
                      candidate.peakDetect,
                      candidate.showClipping,
                      candidate.imageAnalysis,
                      candidate.colorAdjustment,
                      candidate.allowDelayedPeak})
        if (flag != FALSE && flag != TRUE)
            throw std::runtime_error("Boolean settings must be 0 or 1.");
    NormalizeLegacyHdrSettings(candidate);
    if (FAILED(ValidateSettings(candidate)))
        throw std::runtime_error("Imported settings are outside the supported range.");
    return candidate;
}

inline HRESULT Import(const wchar_t* path, PlaceboSettings& out, std::wstring& error)
{
    try
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 1024 * 1024)
            throw std::runtime_error("Could not read settings file.");
        std::string text(size_t(file.tellg()), '\0');
        file.seekg(0);
        if (!file.read(text.data(), text.size()))
            throw std::runtime_error("Could not read settings file.");
        auto candidate = Parse(text);
        out = candidate;
        error.clear();
        return S_OK;
    }
    catch (const std::exception& ex)
    {
        const std::string message = ex.what();
        error.assign(message.begin(), message.end());
        return E_INVALIDARG;
    }
}

inline HRESULT ExportText(const wchar_t* path, const std::string& text, std::wstring& error)
{
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    try
    {
        temporary = std::wstring(path) + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64());
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            temporary.clear();
            throw std::runtime_error("Could not create backup file.");
        }
        DWORD written = 0;
        const bool saved = WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) &&
                           written == text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        if (!saved || !MoveFileExW(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not save backup file.");
        temporary.clear();
        error.clear();
        return S_OK;
    }
    catch (const std::exception& ex)
    {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        if (!temporary.empty())
            DeleteFileW(temporary.c_str());
        const std::string message = ex.what();
        error.assign(message.begin(), message.end());
        return E_FAIL;
    }
}

inline HRESULT Export(const wchar_t* path, const PlaceboSettings& settings, std::wstring& error)
{
    try
    {
        return ExportText(path, Serialize(settings), error);
    }
    catch (const std::exception& ex)
    {
        const std::string message = ex.what();
        error.assign(message.begin(), message.end());
        return E_INVALIDARG;
    }
}
}
