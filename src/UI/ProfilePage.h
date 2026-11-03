#pragma once
// The Profiles page: content profiles (Settings/ContentProfiles.h) edited with
// the same controls as the other pages, one tab per profile and a tab to add
// one. Double-clicking a profile tab renames it (see the host). Included by
// Direct2DSettingsHost.cpp after the settings model. Changes are saved at once;
// the renderer reads them with the next settings preview or video.
#include "Settings/ContentProfiles.h"
#include <shobjidl.h>
#include <deque>
#include <map>
#include <memory>
#include <set>

namespace ProfilePage
{
using Key = std::pair<std::string, std::string>;

inline constexpr const wchar_t* NewTab = L"+";

// Bumped when profiles are added, removed, renamed or get other settings, so
// the host rebuilds its pages.
inline int g_version = 0;
// Tab the host selects after the next rebuild, e.g. a created profile.
inline std::wstring g_selectTab;
// Set while the translation template is generated: no user profiles in it.
inline bool g_template = false;

struct Store
{
    std::vector<ContentProfiles::Profile> profiles;
    // Per profile: the normal settings with the profile on top, which its
    // controls edit, and the pages built on that copy. Deques keep addresses.
    std::deque<PlaceboSettings> views;
    std::deque<std::vector<D2DPage>> viewPages;
    // Category shown in each profile's tab.
    std::map<std::wstring, int> categories;
};

inline Store& State()
{
    static Store store;
    return store;
}

inline size_t Find(const std::wstring& name)
{
    const auto& profiles = State().profiles;
    for (size_t i = 0; i < profiles.size(); ++i)
        if (!_wcsicmp(profiles[i].name.c_str(), name.c_str()))
            return i;
    return profiles.size();
}

inline void Reload()
{
    auto& s = State();
    s.profiles = g_template ? decltype(s.profiles){} : ContentProfiles::Load();
    std::sort(s.profiles.begin(),
              s.profiles.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
}

inline void Save(size_t index, bool rebuild)
{
    auto& profiles = State().profiles;
    if (index < profiles.size())
        ContentProfiles::Save(profiles[index]);
    if (rebuild)
        ++g_version;
}

inline SettingsIni::Entries Snapshot(PlaceboSettings& settings)
{
    SettingsIni::Entries out;
    SettingsIni::Visit(settings,
                       [&](const std::string& section, const std::string& key, auto& value)
                       { out[{section, key}] = SettingsIni::Encode(value); });
    return out;
}

// The settings keys a control writes, found by changing it once on the copy it
// is bound to and comparing. Display-dependent keys never belong to a profile.
inline std::vector<Key> KeysOf(D2DControl& c, PlaceboSettings& view)
{
    if (!c.get || !c.set || c.kind == ControlKind::Action || c.kind == ControlKind::Text)
        return {};
    const auto backup = view;
    const auto before = Snapshot(view);
    const float value = c.Value();
    float other = value;
    if (c.kind == ControlKind::Toggle)
        other = value ? 0.f : 1.f;
    else if (c.kind == ControlKind::Choice)
    {
        for (const auto& choice : c.choices)
            if (float(choice.value) != value)
            {
                other = float(choice.value);
                break;
            }
    }
    else
        other = value + c.stepVal <= c.maxVal ? value + c.stepVal : value - c.stepVal;
    if (other == value)
        return {};
    std::vector<Key> keys;
    try
    {
        c.set(other);
        for (const auto& [key, encoded] : Snapshot(view))
            if (before.at(key) != encoded && !ContentProfiles::DisplayDependent(key.first, key.second))
                keys.push_back(key);
    }
    catch (const std::exception&)
    {
        keys.clear();
    }
    view = backup;
    return keys;
}

// Copies the profile's view values of `keys` into the profile.
inline void Capture(size_t index, const std::vector<Key>& keys)
{
    auto& s = State();
    if (index >= s.profiles.size())
        return;
    const auto values = Snapshot(s.views[index]);
    // Display mode keys are kept per display but read from the Renderer section.
    for (const auto& key : keys)
        s.profiles[index].overrides[key] =
            values.at(key.first.rfind("DisplayMode.", 0) == 0 ? Key{"Renderer", key.second} : key);
}

inline bool ValidName(const std::wstring& name)
{
    if (name.empty() || name.size() > 60 || name.back() == L'.' || name.back() == L' ' || name.front() == L' ' ||
        name == NewTab)
        return false;
    return name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos;
}

inline std::wstring UnusedName()
{
    for (int i = 1;; ++i)
    {
        const auto name = L"Profile " + std::to_wstring(i);
        if (Find(name) == State().profiles.size())
            return name;
    }
}

// Renames a profile and its file; false for an invalid or used name.
inline bool Rename(const std::wstring& from, const std::wstring& to)
{
    const size_t index = Find(from);
    auto& profiles = State().profiles;
    if (index >= profiles.size() || !ValidName(to))
        return false;
    if (to == from)
        return true;
    const size_t other = Find(to);
    if (other < profiles.size() && other != index)
        return false;
    const auto directory = ContentProfiles::Directory();
    const auto oldFile = directory + L"\\" + from + L".ini", newFile = directory + L"\\" + to + L".ini";
    if (!MoveFileExW(oldFile.c_str(), newFile.c_str(), 0) && _wcsicmp(oldFile.c_str(), newFile.c_str()))
        return false;
    profiles[index].name = to;
    ContentProfiles::Save(profiles[index]);
    g_selectTab = to;
    ++g_version;
    return true;
}

// Text control the host edits in place of a profile tab.
inline D2DControl RenameControl(const std::wstring& name)
{
    D2DControl c;
    c.name = L"Profile Name";
    c.kind = ControlKind::Text;
    c.getText = [name] { return name; };
    c.setText = [name](const std::wstring& text) { return Rename(name, text); };
    c.invalidText = L"Enter a profile name that is not in use (1 to 60 characters).";
    return c;
}

inline std::wstring PickFolder(HWND owner)
{
    std::wstring result;
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return result;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)) &&
        SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
        result = path;
    CoTaskMemFree(path);
    return result;
}

struct Candidate
{
    D2DControl control;
    std::wstring where;
    std::vector<Key> keys;
};

// Every control of the other pages that changes video-related settings. The
// keys a control writes never change, so they are found once per process.
inline std::vector<Candidate> Candidates(std::vector<D2DPage>& pages, PlaceboSettings& view)
{
    static std::map<std::wstring, std::vector<Key>> known;
    std::vector<Candidate> out;
    for (auto& page : pages)
    {
        if (page.name == L"Shaders")
            continue;
        for (auto& card : page.cards)
            for (auto& control : card.controls)
            {
                const auto id = page.name + L'|' + card.title + L'|' + control.name;
                auto found = known.find(id);
                if (found == known.end())
                    found = known.emplace(id, KeysOf(control, view)).first;
                if (!found->second.empty())
                    out.push_back({control, card.title, found->second});
            }
    }
    return out;
}

// Sidebar category of a settings-model page (as the host groups them); -1 for
// pages without profile settings.
inline int Category(const std::wstring& page)
{
    if (page == L"Processing" || page == L"Black Bars / Zoom")
        return 0;
    if (page == L"Scaling")
        return 1;
    if (page == L"Color Adjustment" || page == L"Color Management")
        return 2;
    if (page == L"HDR & Color Mapping")
        return 3;
    return -1; // General holds output, analysis and menu settings, not video ones
}

inline constexpr const wchar_t* Categories[] = {L"Processing", L"Scaling", L"Color", L"HDR", L"Display Mode"};
inline constexpr int DisplayModeCategory = 4;
inline constexpr const wchar_t* DisplayModeNote = L"Applies only to the display selected at the top of the menu.";

// The Display Mode card of the General page and the settings key of each of
// its controls.
inline constexpr std::pair<const wchar_t*, const char*> DisplayModeControls[] = {
    {L"Switch Display Mode in Fullscreen", "displayModeSwitch"},
    {L"Allowed Modes", "displayModes"},
    {L"Pause After Switching (seconds)", "displayModeDelay"}};

// Adds an empty profile and selects its tab (the "+" tab or the Create button).
inline void Create()
{
    ContentProfiles::Profile created;
    created.name = UnusedName();
    if (ContentProfiles::Save(created))
        g_selectTab = created.name;
    ++g_version;
}

// Deletes a profile; the host asks first in a dialog.
inline void Delete(const std::wstring& name)
{
    DeleteFileW((ContentProfiles::Directory() + L"\\" + name + L".ini").c_str());
    ++g_version;
}

// The cards of one profile, all in the tab named after it: its folders, the
// category tabs, and per section of that category every setting with an
// "in this profile" switch before its own control.
inline void ProfileCards(D2DPage& page, size_t index)
{
    auto& s = State();
    auto& profile = s.profiles[index];
    const auto tab = profile.name;

    D2DCard folders{L"Profile", L"", {}, tab};
    auto name = RenameControl(tab);
    name.name = L"Name";
    name.desc = L"Shown on the tab. Up to 60 characters.";
    folders.controls.push_back(std::move(name));
    for (size_t i = 0; i < profile.folders.size(); ++i)
        folders.controls.push_back(D2DAction(profile.folders[i].c_str(),
                                             L"Remove",
                                             [index, i](HWND)
                                             {
                                                 auto& profiles = State().profiles;
                                                 if (index >= profiles.size() || i >= profiles[index].folders.size())
                                                     return;
                                                 profiles[index].folders.erase(profiles[index].folders.begin() + i);
                                                 Save(index, true);
                                             }));
    folders.controls.push_back(D2DAction(
        L"Add Folder",
        L"Choose…",
        [index](HWND owner)
        {
            const auto folder = PickFolder(owner);
            auto& profiles = State().profiles;
            if (index >= profiles.size() || folder.empty())
                return;
            auto& list = profiles[index].folders;
            for (const auto& existing : list)
                if (!_wcsicmp(existing.c_str(), folder.c_str()))
                    return;
            list.push_back(folder);
            Save(index, true);
        },
        L"Videos in these folders and their subfolders use this profile; when folders "
        L"are nested, the closest one wins."));
    page.cards.push_back(std::move(folders));

    // Settings by category, each setting (set of keys) once.
    struct Entry
    {
        Candidate candidate;
        int category;
    };

    std::vector<Entry> entries;
    std::set<Key> listed;
    for (auto& viewPage : s.viewPages[index])
    {
        const int category = Category(viewPage.name);
        if (category < 0)
            continue;
        std::vector<D2DPage> one{viewPage};
        for (auto& candidate : Candidates(one, s.views[index]))
        {
            if (std::all_of(
                    candidate.keys.begin(), candidate.keys.end(), [&](const Key& k) { return listed.count(k) > 0; }))
                continue;
            listed.insert(candidate.keys.begin(), candidate.keys.end());
            entries.push_back({candidate, category});
        }
    }
    // Display mode switching belongs to the display selected in the menu, so
    // the profile keeps it for that display only.
    const bool display = s.views[index].displayProfile[0] != 0;
    if (display)
    {
        const auto section = ContentProfiles::DisplayModeSection(s.views[index].displayProfile);
        for (auto& viewPage : s.viewPages[index])
            for (auto& card : viewPage.cards)
                if (viewPage.name == L"General" && card.title == L"Display Mode")
                    for (auto& control : card.controls)
                        for (const auto& [name, key] : DisplayModeControls)
                            if (control.name == name)
                                entries.push_back({{control, card.title, {{section, key}}}, DisplayModeCategory});
    }
    auto inProfile = [&](const Candidate& c)
    { return std::any_of(c.keys.begin(), c.keys.end(), [&](const Key& k) { return profile.overrides.count(k) > 0; }); };

    D2DCard settings{L"Profile Settings", L"", {}, tab};
    D2DControl category;
    category.kind = ControlKind::Tabs;
    for (int i = 0; i < int(std::size(Categories)); ++i)
        if (i != DisplayModeCategory || display)
            category.choices.push_back({i, Categories[i]});
    if (s.categories[tab] == DisplayModeCategory)
        category.desc = display ? DisplayModeNote : L"";
    category.get = [tab] { return float(State().categories[tab]); };
    category.defaultValue = [] { return 0.f; };
    category.set = [tab](float v)
    {
        State().categories[tab] = int(v);
        ++g_version;
    };
    settings.controls.push_back(std::move(category));
    page.cards.push_back(std::move(settings));

    const int shown = s.categories[tab] == DisplayModeCategory && !display ? 0 : s.categories[tab];
    std::vector<D2DCard> sections;
    for (const auto& entry : entries)
    {
        if (entry.category != shown)
            continue;
        const auto& section = entry.candidate.where;
        auto found =
            std::find_if(sections.begin(), sections.end(), [&](const D2DCard& c) { return c.title == section; });
        if (found == sections.end())
        {
            sections.push_back({section, L"", {}, tab});
            found = sections.end() - 1;
        }
        const auto keys = entry.candidate.keys;
        // One row per setting: a switch before the name puts it into the
        // profile; until then its control shows the normal value, greyed out.
        const bool on = inProfile(entry.candidate);
        auto control = entry.candidate.control;
        auto include = std::make_shared<D2DControl>();
        include->name = control.name;
        include->kind = ControlKind::Toggle;
        include->get = [on] { return on ? 1.f : 0.f; };
        include->defaultValue = [] { return 0.f; };
        include->set = [keys, index](float v)
        {
            auto& profiles = State().profiles;
            if (index >= profiles.size())
                return;
            if (v)
                Capture(index, keys);
            else
                for (const auto& key : keys)
                    profiles[index].overrides.erase(key);
            Save(index, true);
        };
        control.include = include;
        if (!on)
            control.enabled = [] { return false; };
        if (auto set = control.set)
            control.set = [set, keys, index](float v)
            {
                set(v);
                Capture(index, keys);
                Save(index, false);
            };
        if (auto setText = control.setText)
            control.setText = [setText, keys, index](const std::wstring& text)
            {
                if (!setText(text))
                    return false;
                Capture(index, keys);
                Save(index, false);
                return true;
            };
        if (auto reset = control.reset)
            control.reset = [reset, keys, index]
            {
                reset();
                Capture(index, keys);
                Save(index, false);
            };
        found->controls.push_back(std::move(control));
    }
    for (auto& section : sections)
        page.cards.push_back(std::move(section));
}

inline D2DPage Build(const PlaceboSettings& base)
{
    auto& s = State();
    Reload();
    // The translation template needs the texts of a profile's tab: build one
    // nameless profile there (its empty name and folders add no text).
    if (g_template)
        s.profiles.emplace_back();
    s.views.clear();
    s.viewPages.clear();
    for (const auto& profile : s.profiles)
    {
        s.views.push_back(ContentProfiles::Apply(base, profile));
        s.viewPages.push_back(BuildD2DPages(s.views.back()));
    }

    D2DPage page{L"Profiles", L"\xE8B7", L"Profiles", L"Different settings for videos from chosen folders.", {}};
    for (size_t i = 0; i < s.profiles.size(); ++i)
        ProfileCards(page, i);

    D2DCard create{L"New Profile",
                   L"A profile gives videos from chosen folders their own settings, e.g. other scaling for anime. "
                   L"Only the settings you add to it differ from your normal settings.",
                   {},
                   NewTab};
    create.controls.push_back(D2DAction(
        L"New Profile",
        L"Create",
        [](HWND) { Create(); },
        L"Adds an empty profile in its own tab. Then add folders and settings."));
    page.cards.push_back(std::move(create));
    return page;
}
} // namespace ProfilePage
