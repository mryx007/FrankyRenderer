#pragma once
#include "Settings/SettingsIni.h"
#include <functional>

namespace SettingsBackup
{
struct Profile
{
    PlaceboSettings settings;
    DisplayProfiles::Metadata metadata;
};

using Profiles = std::map<std::wstring, Profile>;

inline Profile Snapshot(const std::wstring& id)
{
    return {LoadSettings(id), DisplayProfiles::ReadMetadata(id)};
}

inline Profiles Capture(const PlaceboSettings& selected)
{
    Profiles profiles;
    for (const auto& device : DisplayProfiles::All())
    {
        auto profile = Snapshot(device.id);
        profile.metadata.exists = true;
        if (profile.metadata.text[0].empty())
            profile.metadata.text[0] = device.name;
        if (profile.metadata.text[1].empty())
            profile.metadata.text[1] = device.identity;
        profiles.emplace(device.id, std::move(profile));
    }
    if (DisplayProfiles::ValidId(selected.displayProfile))
    {
        auto& profile = profiles[selected.displayProfile];
        profile.settings = selected;
        profile.metadata.exists = true;
    }
    return profiles;
}

inline std::string Serialize(const Profiles& profiles)
{
    if (profiles.empty() || profiles.size() > 64)
        throw std::runtime_error("Invalid number of display profiles.");
    std::string out = "; FrankyRenderer display profiles (UTF-8)\r\n"
                      "; ICC and LUT files are referenced by path, not embedded.\r\n"
                      "[FrankyRenderer]\r\nFormatVersion=2\r\n";
    for (const auto& [id, profile] : profiles)
    {
        if (!DisplayProfiles::ValidId(id))
            throw std::runtime_error("Invalid display profile identity.");
        const auto prefix = "Display." + std::string(id.begin(), id.end()) + ".";
        out += "\r\n[" + prefix + "Metadata]\r\n";
        for (int i = 0; i < 3; ++i)
            out += SettingsIni::Utf8(DisplayProfiles::MetadataKeys[i]) + "=" +
                   SettingsIni::Quote(SettingsIni::Utf8(profile.metadata.text[i].c_str())) + "\r\n";
        const auto entries = SettingsIni::ReadEntries(SettingsIni::Serialize(profile.settings));
        std::string section;
        for (const auto& [key, value] : entries)
        {
            if (key.first == "FrankyRenderer")
                continue;
            if (section != key.first)
            {
                section = key.first;
                out += "\r\n[" + prefix + section + "]\r\n";
            }
            out += key.second + "=" + value + "\r\n";
        }
    }
    return out;
}

inline Profiles Parse(const std::string& text)
{
    auto entries = SettingsIni::ReadEntries(text, 16 * 1024 * 1024, 131072);
    const auto version = entries.find({"FrankyRenderer", "FormatVersion"});
    if (version == entries.end() || version->second != "2")
        throw std::runtime_error("Unsupported display backup version.");
    entries.erase(version);
    Profiles profiles;
    std::map<std::wstring, std::string> settings;
    std::map<std::wstring, unsigned> metadataFields;
    for (const auto& [key, value] : entries)
    {
        if (!key.first.starts_with("Display.") || key.first.size() < 74 || key.first[72] != '.')
            throw std::runtime_error("Invalid display backup section.");
        const auto narrowId = key.first.substr(8, 64);
        const std::wstring id(narrowId.begin(), narrowId.end());
        if (!DisplayProfiles::ValidId(id))
            throw std::runtime_error("Invalid display profile identity.");
        auto& profile = profiles[id];
        if (profiles.size() > 64)
            throw std::runtime_error("Too many display profiles.");
        const auto section = key.first.substr(73);
        if (section == "Metadata")
        {
            int index = 0;
            while (index < 3 && key.second != SettingsIni::Utf8(DisplayProfiles::MetadataKeys[index]))
                ++index;
            if (index == 3)
                throw std::runtime_error("Unknown display metadata.");
            wchar_t decoded[1024];
            SettingsIni::Decode(value, decoded);
            profile.metadata.text[index] = decoded;
            metadataFields[id] |= 1u << index;
            if (index == 2 &&
                (profile.metadata.text[index].size() > 80 || std::any_of(profile.metadata.text[index].begin(),
                                                                         profile.metadata.text[index].end(),
                                                                         [](wchar_t c) { return c < 32; })))
                throw std::runtime_error("Invalid display name.");
        }
        else
            settings[id] += "[" + section + "]\n" + key.second + "=" + value + "\n";
    }
    if (profiles.empty())
        throw std::runtime_error("No display profiles in backup.");
    for (auto& [id, profile] : profiles)
    {
        if (!settings.count(id) || metadataFields[id] != 7)
            throw std::runtime_error("Missing display settings.");
        profile.settings = SettingsIni::Parse("[FrankyRenderer]\nFormatVersion=1\n" + settings[id]);
        wcscpy_s(profile.settings.displayProfile, id.c_str());
        profile.metadata.exists = true;
    }
    return profiles;
}

inline HRESULT Apply(const Profiles& profiles, const std::function<HRESULT(const PlaceboSettings&)>& preview)
{
    Profiles previous;
    for (const auto& [id, profile] : profiles)
    {
        if (!DisplayProfiles::ValidId(id) || id != profile.settings.displayProfile ||
            FAILED(ValidateSettings(profile.settings)))
            return E_INVALIDARG;
        previous.emplace(id, Snapshot(id));
    }
    HRESULT result = S_OK;
    for (const auto& [id, profile] : profiles)
    {
        result = preview ? preview(profile.settings) : SaveSettings(profile.settings);
        if (SUCCEEDED(result))
            result = DisplayProfiles::WriteMetadata(id, profile.metadata);
        if (FAILED(result))
            break;
    }
    if (FAILED(result))
        for (const auto& [id, profile] : previous)
        {
            if (preview)
                preview(profile.settings);
            else if (profile.metadata.exists)
                SaveSettings(profile.settings);
            DisplayProfiles::WriteMetadata(id, profile.metadata);
        }
    return result;
}

inline std::string ReadFile(const wchar_t* path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 16 * 1024 * 1024)
        throw std::runtime_error("Could not read settings backup.");
    std::string text(size_t(file.tellg()), '\0');
    file.seekg(0);
    if (!file.read(text.data(), text.size()))
        throw std::runtime_error("Could not read settings backup.");
    return text;
}
}
