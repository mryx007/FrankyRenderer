#pragma once
// Content profiles: settings that differ for videos from chosen folders, e.g.
// other scalers for anime. Each profile is a key in the settings file
// (Settings.ini), named like the profile:
//
//   [Settings\Profiles\Anime]
//   Folders="D:\Anime;E:\Serien\Anime"
//   Monitors="<display profile id>;<display profile id>"
//   Renderer.smoothMotion=0
//
// A profile only stores what differs from the display's normal settings
// ("Main"), as "Section.key" lines of the settings backup. The folder
// decides: a folder belongs to one profile, includes its subfolders, and the
// folder closest to the file wins. Without monitors the profile applies on
// every display, otherwise only on the listed ones. While the menu shows
// another profile, the playing video previews it.
#include "Settings/SettingsIni.h"
#include "Settings/QualityPresets.h"
#include "Settings/SettingsStore.h"
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

// The normal settings ("Main") in the menu and in a forced choice.
inline constexpr const wchar_t* DefaultName = L"Main";

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

inline std::wstring KeyPath()
{
    return SettingsStore::Root() + L"\\Profiles";
}

// Earlier versions kept one INI file per profile in this folder.
inline std::wstring OldDirectory()
{
    const auto folder = SettingsStore::Folder();
    return folder.empty() ? L"" : folder + L"\\Profiles";
}

inline std::vector<std::wstring> SplitList(const std::wstring& list)
{
    std::vector<std::wstring> result;
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

// A quoted, ';'-separated list taken literally (no escapes), so Windows paths
// need no doubled backslashes.
inline std::vector<std::wstring> ReadList(SettingsIni::Entries& entries, const char* key)
{
    const auto found = entries.find({"Profile", key});
    if (found == entries.end())
        return {};
    auto raw = found->second;
    entries.erase(found);
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    std::wstring list(raw.size(), L'\0');
    list.resize(size_t(
        std::max(0,
                 MultiByteToWideChar(
                     CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), list.data(), int(list.size())))));
    return SplitList(list);
}

inline std::wstring JoinList(const std::vector<std::wstring>& items)
{
    std::wstring list;
    for (const auto& item : items)
        list += (list.empty() ? L"" : L";") + item;
    return list;
}

// A profile file of earlier versions; throws on malformed files. A profile without folders is
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

inline SettingsStore::Value Text(const std::wstring& text)
{
    const auto bytes = reinterpret_cast<const BYTE*>(text.c_str());
    return {REG_SZ, std::vector<BYTE>(bytes, bytes + (text.size() + 1) * sizeof(wchar_t))};
}

// Writes the profile as it is, replacing what was stored under its name.
inline bool Store(const Profile& profile)
{
    SettingsStore::Node node;
    node.values[L"Folders"] = Text(JoinList(profile.folders));
    node.values[L"Monitors"] = Text(JoinList(profile.monitors));
    if (!profile.overrides.empty())
    {
        const auto text = SettingsIni::JoinEntries(profile.overrides);
        node.values[L"Overrides"] = {REG_BINARY, std::vector<BYTE>(text.begin(), text.end())};
    }
    return SettingsStore::ReplaceSubtree(KeyPath() + L"\\" + profile.name, node) == ERROR_SUCCESS;
}

// Profile files of earlier versions are taken over once, then removed.
inline void MoveOldFiles()
{
    // Custom quality files of earlier versions too: the profiles are read on
    // every start of the menu and of playback.
    MoveOldCustomQuality();
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       const auto directory = OldDirectory();
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
                               const std::string text((std::istreambuf_iterator<char>(file)),
                                                      std::istreambuf_iterator<char>());
                               const auto name = entry.path().stem().wstring();
                               if (!SettingsStore::Exists(KeyPath() + L"\\" + name) && !Store(Parse(name, text)))
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

inline std::vector<Profile> Load()
{
    MoveOldFiles();
    std::vector<Profile> profiles;
    SettingsStore::Key root;
    if (SettingsStore::Open(KeyPath(), root) != ERROR_SUCCESS)
        return profiles;
    for (const auto& name : root.SubKeys())
    {
        Profile profile;
        profile.name = name;
        for (const auto& [value, data] : SettingsStore::At(KeyPath() + L"\\" + name).Values())
        {
            std::wstring text;
            if (!_wcsicmp(value.c_str(), L"Overrides") && data.type == REG_BINARY)
                profile.overrides = SettingsIni::SplitEntries(std::string(data.data.begin(), data.data.end()));
            else if (!_wcsicmp(value.c_str(), L"Folders") && SettingsStore::PlainString(data, text))
                profile.folders = SplitList(text);
            else if (!_wcsicmp(value.c_str(), L"Monitors") && SettingsStore::PlainString(data, text))
                profile.monitors = SplitList(text);
        }
        profiles.push_back(std::move(profile));
    }
    std::sort(profiles.begin(),
              profiles.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return profiles;
}

inline const Profile* Find(const std::vector<Profile>& profiles, const std::wstring& name)
{
    for (const auto& profile : profiles)
        if (!_wcsicmp(profile.name.c_str(), name.c_str()))
            return &profile;
    return nullptr;
}

inline bool Save(const Profile& profile)
{
    MoveOldFiles();
    return Store(profile);
}

inline bool Remove(const std::wstring& name)
{
    MoveOldFiles();
    return SettingsStore::DeleteTree(KeyPath() + L"\\" + name) == ERROR_SUCCESS;
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

// Every profile as stored, so a cancelled menu can put them back as they were.
inline SettingsStore::Node Capture()
{
    MoveOldFiles();
    return SettingsStore::Subtree(KeyPath());
}

inline void Restore(const SettingsStore::Node& profiles)
{
    SettingsStore::ReplaceSubtree(KeyPath(), profiles);
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

// The profile the playing video gets by its folder; empty for Main.
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

// What a new profile stores: the built-in value of every setting in which
// Main differs from it, so it starts as after a fresh install.
inline SettingsIni::Entries Neutral(const PlaceboSettings& main)
{
    PlaceboSettings fresh;
    wcscpy_s(fresh.displayProfile, main.displayProfile);
    return Differences(main, fresh);
}

} // namespace ContentProfiles
