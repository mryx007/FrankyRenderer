#pragma once
// Content profiles: settings that differ for videos from chosen folders, e.g.
// other scalers for anime. Each profile is one INI file (written by the Profiles
// menu page) in
// %APPDATA%\FrankyRenderer\Profiles (file name = profile name):
//
//   [Profile]
//   folders="D:\Anime;E:\Serien\Anime"
//   [Renderer]
//   smoothMotion=0
//
// Folder entries include their subfolders; the folder closest to the file
// wins. Only keys present in the file override the normal settings, in the
// same format as the settings backup. Display-dependent settings stay with the
// display profile and are ignored here.
#include "Settings/SettingsIni.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ContentProfiles
{
struct Profile
{
    std::wstring name;
    std::vector<std::wstring> folders;
    SettingsIni::Entries overrides;
};

// Describe the output or the display rather than the video.
inline bool DisplayDependent(const std::string& section, const std::string& key)
{
    if (section != "Renderer")
        return false;
    for (const char* name : {"outputFormat",
                             "outputLevels",
                             "displayDepth",
                             "hdrMode",
                             "hdrPassthrough",
                             "autoHdr",
                             "sdrNits",
                             "hdrNits",
                             "iccEnabled",
                             "lutEnabled",
                             "iccIntent",
                             "iccPath",
                             "lutPath",
                             "displayModeSwitch",
                             "displayModes",
                             "displayModeDelay",
                             "maximumFramesInFlight",
                             "measuredQuality",
                             "qualityStamp"})
        if (key == name)
            return true;
    return false;
}

// Lower case, backslashes and a trailing separator, so "D:\Anime" does not
// also match "D:\Anime2".
inline std::wstring FolderKey(std::wstring path)
{
    std::replace(path.begin(), path.end(), L'/', L'\\');
    std::error_code error;
    auto full = std::filesystem::absolute(std::filesystem::path(path), error);
    if (!error)
        path = full.lexically_normal().wstring();
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    CharLowerBuffW(path.data(), DWORD(path.size()));
    return path;
}

inline std::wstring Directory()
{
    wchar_t appData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return {};
    return std::wstring(appData) + L"\\FrankyRenderer\\Profiles";
}

// Throws on malformed files; Load() skips those. A profile without folders is
// valid (just created in the menu) and matches nothing.
inline Profile Parse(const std::wstring& name, const std::string& text)
{
    auto entries = SettingsIni::ReadEntries(text, 256 * 1024);
    Profile profile;
    profile.name = name;
    const auto folders = entries.find({"Profile", "folders"});
    // Taken literally (no escapes), so Windows paths need no doubled backslashes.
    auto raw = folders == entries.end() ? std::string() : folders->second;
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    std::wstring list(raw.size(), L'\0');
    list.resize(size_t(
        std::max(0,
                 MultiByteToWideChar(
                     CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), list.data(), int(list.size())))));
    if (folders != entries.end())
        entries.erase(folders);
    for (std::wstring rest = list; !rest.empty();)
    {
        const auto end = rest.find(L';');
        auto folder = rest.substr(0, end);
        rest = end == std::wstring::npos ? L"" : rest.substr(end + 1);
        folder.erase(0, folder.find_first_not_of(L" \t"));
        folder.erase(folder.find_last_not_of(L" \t") + 1);
        if (!folder.empty())
            profile.folders.push_back(folder);
    }
    for (auto& [key, value] : entries)
        if (key.first != "Profile" && !DisplayDependent(key.first, key.second))
            profile.overrides.emplace(key, value);
    return profile;
}

inline std::vector<Profile> Load(const std::wstring& directory = Directory())
{
    std::vector<Profile> profiles;
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error))
        return profiles;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (!entry.is_regular_file() || _wcsicmp(entry.path().extension().c_str(), L".ini"))
            continue;
        try
        {
            std::ifstream file(entry.path(), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            profiles.push_back(Parse(entry.path().stem().wstring(), text));
        }
        catch (const std::exception&)
        {
        }
    }
    return profiles;
}

// Writes the profile as <directory>\<name>.ini; folders are stored literally.
inline bool Save(const Profile& profile, const std::wstring& directory = Directory())
{
    try
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        std::wstring folders;
        for (const auto& folder : profile.folders)
            folders += (folders.empty() ? L"" : L";") + folder;
        std::string text = "; FrankyRenderer content profile (UTF-8)\r\n[Profile]\r\nfolders=\"" +
                           (folders.empty() ? std::string() : SettingsIni::Utf8(folders.c_str())) + "\"\r\n";
        std::string section;
        for (const auto& [key, value] : profile.overrides)
        {
            if (key.first != section)
            {
                section = key.first;
                text += "\r\n[" + section + "]\r\n";
            }
            text += key.second + "=" + value + "\r\n";
        }
        std::wstring message;
        return SUCCEEDED(SettingsIni::ExportText((directory + L"\\" + profile.name + L".ini").c_str(), text, message));
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// The profile whose folder is closest to the file, or nullptr.
inline const Profile* Match(const std::vector<Profile>& profiles, const std::wstring& file)
{
    if (file.empty())
        return nullptr;
    auto path = FolderKey(file);
    path.pop_back(); // a file, not a folder
    const Profile* best = nullptr;
    size_t bestLength = 0;
    for (const auto& profile : profiles)
        for (const auto& entry : profile.folders)
            if (const auto folder = FolderKey(entry);
                folder.size() > bestLength && path.size() > folder.size() && !path.compare(0, folder.size(), folder))
            {
                best = &profile;
                bestLength = folder.size();
            }
    return best;
}

// The normal settings with the profile's values on top. A profile that would
// produce invalid settings is ignored.
inline PlaceboSettings Apply(const PlaceboSettings& base, const Profile& profile)
{
    if (profile.overrides.empty())
        return base;
    PlaceboSettings result = base;
    try
    {
        SettingsIni::Visit(result,
                           [&](const std::string& section, const std::string& key, auto& value)
                           {
                               const auto found = profile.overrides.find({section, key});
                               if (found != profile.overrides.end())
                                   SettingsIni::Decode(found->second, value);
                           });
    }
    catch (const std::exception&)
    {
        return base;
    }
    return SUCCEEDED(ValidateSettings(result)) ? result : base;
}
} // namespace ContentProfiles
