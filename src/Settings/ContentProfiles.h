#pragma once
// Content profiles: settings that differ for videos from chosen folders, e.g.
// other scalers for anime. Each profile is one INI file (written by the menu)
// in %APPDATA%\FrankyRenderer\Profiles (file name = profile name):
//
//   [Profile]
//   folders="D:\Anime;E:\Serien\Anime"
//   monitors="<display profile id>;<display profile id>"
//   [Renderer]
//   smoothMotion=0
//
// A profile only stores what differs from the display's normal settings
// ("Default"), in the same format as the settings backup. The folder decides:
// a folder belongs to one profile, includes its subfolders, and the folder
// closest to the file wins. Without monitors the profile applies on every
// display, otherwise only on the listed ones. While the menu shows another
// profile, the playing video previews it.
#include "Settings/SettingsIni.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ContentProfiles
{
struct Profile
{
    std::wstring name;
    std::vector<std::wstring> folders, monitors;
    SettingsIni::Entries overrides;
};

// Shown for the normal settings in the menu and in a forced choice.
inline constexpr const wchar_t* DefaultName = L"Default";

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

// A quoted, ';'-separated list taken literally (no escapes), so Windows paths
// need no doubled backslashes.
inline std::vector<std::wstring> ReadList(SettingsIni::Entries& entries, const char* key)
{
    std::vector<std::wstring> result;
    const auto found = entries.find({"Profile", key});
    if (found == entries.end())
        return result;
    auto raw = found->second;
    entries.erase(found);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    std::wstring list(raw.size(), L'\0');
    list.resize(size_t(
        std::max(0,
                 MultiByteToWideChar(
                     CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), list.data(), int(list.size())))));
    for (std::wstring rest = list; !rest.empty();)
    {
        const auto end = rest.find(L';');
        auto item = rest.substr(0, end);
        rest = end == std::wstring::npos ? L"" : rest.substr(end + 1);
        item.erase(0, item.find_first_not_of(L" \t"));
        item.erase(item.find_last_not_of(L" \t") + 1);
        if (!item.empty())
            result.push_back(item);
    }
    return result;
}

inline std::string WriteList(const std::vector<std::wstring>& items)
{
    std::wstring list;
    for (const auto& item : items)
        list += (list.empty() ? L"" : L";") + item;
    return "\"" + (list.empty() ? std::string() : SettingsIni::Utf8(list.c_str())) + "\"";
}

// Throws on malformed files; Load() skips those. A profile without folders is
// valid (just created in the menu) and matches nothing.
inline Profile Parse(const std::wstring& name, const std::string& text)
{
    auto entries = SettingsIni::ReadEntries(text, 256 * 1024);
    Profile profile;
    profile.name = name;
    profile.folders = ReadList(entries, "folders");
    profile.monitors = ReadList(entries, "monitors");
    for (auto& [key, value] : entries)
        if (key.first != "Profile")
            profile.overrides.emplace(key, value);
    return profile;
}

inline std::vector<Profile> Load(const std::wstring& directory = Directory())
{
    std::vector<Profile> profiles;
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error))
        return profiles;

    // Every settings change reads the profiles, in the menu and in each
    // renderer. Read and parse the files again only when one was added,
    // removed or written since: name, time and size come with the listing.
    struct Stamp
    {
        std::wstring name;
        long long written;
        uintmax_t size;
        bool operator==(const Stamp&) const = default;
    };

    std::vector<Stamp> stamps;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        std::error_code itemError;
        if (!entry.is_regular_file(itemError) || _wcsicmp(entry.path().extension().c_str(), L".ini"))
            continue;
        stamps.push_back({entry.path().filename().wstring(),
                          entry.last_write_time(itemError).time_since_epoch().count(),
                          entry.file_size(itemError)});
        files.push_back(entry.path());
    }
    static std::mutex cacheLock;
    static std::wstring cachedDirectory;
    static std::vector<Stamp> cachedStamps;
    static std::vector<Profile> cachedProfiles;
    {
        std::lock_guard guard(cacheLock);
        if (!error && cachedDirectory == directory && cachedStamps == stamps)
            return cachedProfiles;
    }
    for (const auto& path : files)
    {
        try
        {
            std::ifstream file(path, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            profiles.push_back(Parse(path.stem().wstring(), text));
        }
        catch (const std::exception&)
        {
        }
    }
    std::sort(profiles.begin(),
              profiles.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    if (!error)
    {
        // A file written while it was read has a newer time by now and is
        // read again next time.
        std::lock_guard cache(cacheLock);
        cachedDirectory = directory;
        cachedStamps = std::move(stamps);
        cachedProfiles = profiles;
    }
    return profiles;
}

inline const Profile* Find(const std::vector<Profile>& profiles, const std::wstring& name)
{
    for (const auto& profile : profiles)
        if (!_wcsicmp(profile.name.c_str(), name.c_str()))
            return &profile;
    return nullptr;
}

// Writes the profile as <directory>\<name>.ini.
inline bool Save(const Profile& profile, const std::wstring& directory = Directory())
{
    try
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        std::string text =
            "; FrankyRenderer content profile (UTF-8)\r\n[Profile]\r\nfolders=" + WriteList(profile.folders) +
            "\r\nmonitors=" + WriteList(profile.monitors) + "\r\n";
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

inline bool AllowsDisplay(const Profile& profile, const std::wstring& display)
{
    if (profile.monitors.empty())
        return true;
    for (const auto& id : profile.monitors)
        if (id == display)
            return true;
    return false;
}

// The profile whose folder is closest to the file, or nullptr. On a display
// the profile does not allow, the normal settings apply.
inline const Profile*
Match(const std::vector<Profile>& profiles, const std::wstring& file, const std::wstring& display = {})
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
    return best && AllowsDisplay(*best, display) ? best : nullptr;
}

// The profile that already holds this folder, if any (a folder belongs to one).
inline const Profile* FolderOwner(const std::vector<Profile>& profiles, const std::wstring& folder)
{
    const auto key = FolderKey(folder);
    for (const auto& profile : profiles)
        for (const auto& entry : profile.folders)
            if (FolderKey(entry) == key)
                return &profile;
    return nullptr;
}

// Every file of the profile folder by name,
// so a cancelled menu can put the folder back as it was.
using Files = std::map<std::wstring, std::string>;

inline Files ReadFiles(const std::wstring& directory = Directory())
{
    Files files;
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error))
        return files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        const auto extension = entry.path().extension().wstring();
        if (!entry.is_regular_file() || (_wcsicmp(extension.c_str(), L".ini") && _wcsicmp(extension.c_str(), L".txt")))
            continue;
        std::ifstream file(entry.path(), std::ios::binary);
        files[entry.path().filename().wstring()] =
            std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
    return files;
}

inline void WriteFiles(const Files& files, const std::wstring& directory = Directory())
{
    if (directory.empty())
        return;
    std::error_code error;
    for (const auto& [name, text] : ReadFiles(directory))
        if (!files.count(name))
            std::filesystem::remove(std::filesystem::path(directory) / name, error);
    if (!files.empty())
        std::filesystem::create_directories(directory, error);
    for (const auto& [name, text] : files)
    {
        std::ofstream file(std::filesystem::path(directory) / name, std::ios::binary | std::ios::trunc);
        file << text;
    }
}

// The profile for a file on a display: the one the menu previews (a profile
// name or DefaultName), else by folder.
inline const Profile* Resolve(const std::vector<Profile>& profiles,
                              const std::wstring& preview,
                              const std::wstring& file,
                              const std::wstring& display)
{
    if (preview.empty())
        return Match(profiles, file, display);
    return _wcsicmp(preview.c_str(), DefaultName) ? Find(profiles, preview) : nullptr;
}

// Shared by the renderer and its menu in the player's process: the playing
// file and the profile its folder gives it, and the profile the open menu
// shows instead. Only the renderer playing that file previews it.
struct SharedState
{
    std::mutex lock;
    std::wstring file, automatic, preview, previewFile;
    bool previewing = false;
};

inline SharedState& Shared()
{
    static SharedState state;
    return state;
}

inline void PublishPlaying(const std::wstring& file, const std::wstring& automatic)
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    state.file = file;
    state.automatic = automatic;
}

// The profile the playing video gets by its folder; empty for Default.
inline std::wstring PlayingProfile()
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    return state.automatic;
}

inline void StartPreview(const std::wstring& name)
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    state.preview = name;
    state.previewFile = state.file;
    state.previewing = true;
}

inline void StopPreview()
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    state.previewing = false;
}

// The profile the menu previews for this file, empty when it previews none.
inline std::wstring Preview(const std::wstring& file)
{
    auto& state = Shared();
    std::lock_guard guard(state.lock);
    return state.previewing && !_wcsicmp(state.previewFile.c_str(), file.c_str()) ? state.preview : std::wstring();
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

inline SettingsIni::Entries Snapshot(PlaceboSettings settings)
{
    SettingsIni::Entries out;
    SettingsIni::Visit(settings,
                       [&](const std::string& section, const std::string& key, auto& value)
                       { out[{section, key}] = SettingsIni::Encode(value); });
    return out;
}

// What a profile stores: every setting that differs from the normal ones.
inline SettingsIni::Entries Differences(const PlaceboSettings& base, const PlaceboSettings& edited)
{
    const auto before = Snapshot(base);
    SettingsIni::Entries out;
    for (const auto& [key, value] : Snapshot(edited))
        if (const auto found = before.find(key); found == before.end() || found->second != value)
            out.emplace(key, value);
    return out;
}

} // namespace ContentProfiles
