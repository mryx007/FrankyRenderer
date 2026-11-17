#pragma once
// Readable fields for the settings blocks that are stored as one binary value
// (see SettingsStore::Codec). The structures stay the in-memory and registry
// form; only Settings.ini shows them field by field.
#include "Settings/Settings.h"
#include "Settings/SettingsIni.h"
#include "Settings/SettingsStore.h"
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace SettingsFileFields
{
using Fields = std::vector<std::pair<std::string, std::string>>;
using Map = std::map<std::string, std::string>;

// One field: its text when writing, its value from the text when reading.
struct Writer
{
    Fields& out;

    template <class T> void operator()(const std::string& name, T& value)
    {
        if constexpr (std::is_array_v<T>)
            out.push_back({name, SettingsStore::Quote(std::string(value, strnlen(value, std::extent_v<T>)))});
        else if constexpr (std::is_floating_point_v<T>)
            out.push_back({name, SettingsStore::FloatText(value)});
        else
            out.push_back({name, std::to_string(value)});
    }
};

struct Reader
{
    const Map& in;

    template <class T> void operator()(const std::string& name, T& value)
    {
        const auto found = in.find(name);
        if (found == in.end())
            return;
        const auto& text = found->second;
        if constexpr (std::is_array_v<T>)
        {
            std::string raw;
            if (SettingsStore::Unquote(text, raw) && raw.size() < std::extent_v<T> && raw.find('\0') == raw.npos)
            {
                std::fill(std::begin(value), std::end(value), char(0));
                std::copy(raw.begin(), raw.end(), value);
            }
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            float number;
            if (SettingsStore::ParseFloat(text, number))
                value = number;
        }
        else
        {
            long long number = 0;
            const auto last = text.data() + text.size();
            const auto result = std::from_chars(text.data(), last, number);
            if (!text.empty() && result.ec == std::errc{} && result.ptr == last &&
                number >= (long long)std::numeric_limits<T>::min() &&
                number <= (long long)std::numeric_limits<T>::max())
                value = T(number);
        }
    }
};

// Clears what follows the end of a text field: it means nothing, and the
// file does not keep it.
struct Cleaner
{
    template <class T> void operator()(const std::string&, T& value)
    {
        if constexpr (std::is_array_v<T>)
        {
            const size_t length = strnlen(value, std::extent_v<T>);
            std::fill(value + length, value + std::extent_v<T>, char(0));
        }
    }
};

inline void VisitFields(TonalSettings& s, auto&& field)
{
    for (int i = 0; i < 3; ++i)
    {
        field("exposure" + std::to_string(i), s.exposure[i]);
        field("contrast" + std::to_string(i), s.contrast[i]);
        field("saturation" + std::to_string(i), s.saturation[i]);
    }
}

inline void VisitFields(BlackBarSettings& s, auto&& field)
{
    field("enabled", s.enabled);
    field("crop", s.crop);
    field("keepSubtitles", s.keepSubtitles);
    field("zoom", s.zoom);
    field("threshold", s.threshold);
    field("delay", s.delay);
    field("stableZoom", s.stableZoom);
}

inline void VisitFields(RendererExtras& s, auto&& field)
{
    for (int i = 0; i < ExtraCount; ++i)
        field(ExtraOptions[i].key, s.values[i]);
}

inline void VisitFields(ColorMappingSettings& s, auto&& field)
{
    for (int i = 0; i < MappingOptionCount; ++i)
        field(MappingOptions[i].key, s.values[i]);
}

inline void VisitFields(ScalingSettings& s, auto&& field)
{
    field("antiringing", s.antiringing);
    for (int i = 0; i < 5; ++i)
    {
        auto& f = s.filters[i];
        const std::string role = std::string(ScalingRoles[i]) + ".";
        field(role + "filter", f.filter);
        field(role + "preset", f.preset);
        field(role + "kernel", f.kernel);
        field(role + "window", f.window);
        field(role + "overrides", f.overrides);
        field(role + "polar", f.polar);
        for (int j = 0; j < 9; ++j)
            field(role + ScalingFields[j], f.values[j]);
    }
}

inline void VisitFields(SIZE& s, auto&& field)
{
    field("width", s.cx);
    field("height", s.cy);
}

inline SettingsStore::Codec Base(const wchar_t* name, const char* prefix, size_t size, bool appearance)
{
    SettingsStore::Codec codec;
    codec.name = name;
    codec.prefix = prefix;
    codec.size = size;
    codec.where = [appearance](const std::wstring& path)
    {
        // Settings: the root and every display; the menu window: Appearance.
        return appearance
                   ? !_wcsicmp(path.c_str(), L"Appearance")
                   : path.empty() || (!_wcsnicmp(path.c_str(), L"Displays\\", 9) && path.find(L'\\', 9) == path.npos);
    };
    codec.clean = [](std::vector<BYTE>&) {};
    return codec;
}

template <class T> SettingsStore::Codec Simple(const wchar_t* name, const char* prefix, bool appearance)
{
    auto codec = Base(name, prefix, sizeof(T), appearance);
    codec.expand = [](const std::vector<BYTE>& data, Fields& fields)
    {
        T value;
        memcpy(&value, data.data(), sizeof(T));
        VisitFields(value, Writer{fields});
    };
    codec.collapse = [](const Map& fields, std::vector<BYTE>& data)
    {
        T value{};
        VisitFields(value, Reader{fields});
        const auto bytes = reinterpret_cast<const BYTE*>(&value);
        data.assign(bytes, bytes + sizeof(T));
    };
    codec.clean = [](std::vector<BYTE>& data)
    {
        T value;
        memcpy(&value, data.data(), sizeof(T));
        VisitFields(value, Cleaner{});
        memcpy(data.data(), &value, sizeof(T));
    };
    return codec;
}

// Shaders: the chain order on one line, then only shaders whose switches or
// parameters differ from the defaults, numbered like the catalog.
inline SettingsStore::Codec Shaders()
{
    auto codec = Base(L"UserShadersV4", "Shaders", sizeof(UserShaderSettings), false);
    codec.expand = [](const std::vector<BYTE>& data, Fields& fields)
    {
        UserShaderSettings value;
        memcpy(&value, data.data(), sizeof(value));
        std::string order;
        for (int i = 0; i < ShaderCount; ++i)
            order += (i ? "," : "") + std::to_string(value.order[i]);
        fields.push_back({"order", order});
        const ShaderConfig neutral;
        for (int i = 0; i < ShaderCount; ++i)
        {
            auto& shader = value.shaders[i];
            if (!memcmp(&shader, &neutral, sizeof(shader)))
                continue;
            if (i < ShaderBuiltinCount)
                fields.push_back({";", std::to_string(i) + ": " + BuiltinShaderCatalog[i].file});
            const auto name = std::to_string(i) + ".";
            Writer write{fields};
            write(name + "enabled", shader.enabled);
            write(name + "overrides", shader.overrides);
            for (int j = 0; j < ShaderParameterLimit; ++j)
                write(name + "parameter" + std::to_string(j), shader.values[j]);
        }
    };
    codec.collapse = [](const Map& fields, std::vector<BYTE>& data)
    {
        UserShaderSettings value;
        if (const auto order = fields.find("order"); order != fields.end())
        {
            int parsed[ShaderCount], count = 0;
            size_t at = 0;
            const auto& text = order->second;
            bool valid = true;
            while (valid && at <= text.size() && count < ShaderCount)
            {
                auto end = text.find(',', at);
                if (end == text.npos)
                    end = text.size();
                const auto result = std::from_chars(text.data() + at, text.data() + end, parsed[count]);
                valid = result.ec == std::errc{} && result.ptr == text.data() + end;
                ++count;
                at = end + 1;
            }
            if (valid && count == ShaderCount && at > text.size())
                std::copy(std::begin(parsed), std::end(parsed), value.order);
        }
        for (int i = 0; i < ShaderCount; ++i)
        {
            const auto name = std::to_string(i) + ".";
            Reader read{fields};
            auto& shader = value.shaders[i];
            read(name + "enabled", shader.enabled);
            read(name + "overrides", shader.overrides);
            for (int j = 0; j < ShaderParameterLimit; ++j)
                read(name + "parameter" + std::to_string(j), shader.values[j]);
        }
        const auto bytes = reinterpret_cast<const BYTE*>(&value);
        data.assign(bytes, bytes + sizeof(value));
    };
    return codec;
}

// Content profiles and Custom quality: lines "Section.key=value" as in INI
// backups (SettingsIni::JoinEntries), shown without a prefix.
inline SettingsStore::Codec Overrides()
{
    SettingsStore::Codec codec;
    codec.name = L"Overrides";
    codec.where = [](const std::wstring& path)
    {
        for (const wchar_t* parent : {L"Profiles\\", L"CustomQuality\\"})
            if (const size_t length = wcslen(parent); !_wcsnicmp(path.c_str(), parent, length) &&
                                                      path.size() > length && path.find(L'\\', length) == path.npos)
                return true;
        return false;
    };
    codec.expand = [](const std::vector<BYTE>& data, Fields& fields)
    {
        for (const auto& [key, value] : SettingsIni::SplitEntries(std::string(data.begin(), data.end())))
            fields.push_back({key.first + "." + key.second, value});
    };
    codec.collapse = [](const Map& fields, std::vector<BYTE>& data)
    {
        std::string text;
        for (const auto& [field, value] : fields)
            text += field + "=" + value + "\n";
        data.assign(text.begin(), text.end());
    };
    codec.clean = [](std::vector<BYTE>&) {};
    return codec;
}

inline void Register()
{
    auto& codecs = SettingsStore::Codecs();
    if (!codecs.empty())
        return;
    codecs.push_back(Simple<TonalSettings>(L"HdrTonalV1", "Tonal", false));
    codecs.push_back(Simple<BlackBarSettings>(L"BlackBarsV1", "BlackBars", false));
    codecs.push_back(Simple<RendererExtras>(L"RendererExtrasV1", "Extras", false));
    codecs.push_back(Simple<ColorMappingSettings>(L"ColorMappingV1", "ColorMapping", false));
    codecs.push_back(Simple<ScalingSettings>(L"ScalingV1", "Scaling", false));
    codecs.push_back(Shaders());
    codecs.push_back(Simple<SIZE>(L"WindowSize", "WindowSize", true));
    codecs.push_back(Overrides());
}
} // namespace SettingsFileFields
