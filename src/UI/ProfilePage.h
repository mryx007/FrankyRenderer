#pragma once
// The Profiles page: the Default tab chooses the display whose normal settings
// the menu shows, every profile has a tab with its name, folders and displays,
// and "+" adds one. A profile's settings are edited on the other pages after
// choosing it at the top of the menu (see the host). Included by
// Direct2DSettingsHost.cpp after the settings model. Changes are saved at once.
#include "Settings/ContentProfiles.h"
#include <shobjidl.h>
#include <memory>

namespace ProfilePage
{
inline constexpr const wchar_t* NewTab = L"+";
inline constexpr const wchar_t* DefaultTab = ContentProfiles::DefaultName;

// Bumped when profiles are added, removed, renamed or changed, so the host
// rebuilds its pages and checks the profile it edits.
inline int g_version = 0;
// Tab the host selects after the next rebuild, e.g. a created profile.
inline std::wstring g_selectTab;
// Set while the translation template is generated: no user profiles in it.
inline bool g_template = false;
// Requests the host carries out after a control changed: show another
// display's normal settings, or choose a profile at the top of the menu.
inline std::wstring g_requestDisplay;
inline bool g_selectionRequested = false;
inline std::wstring g_requestSelection;
// A short notice the host shows, e.g. why a folder was not added.
inline std::wstring g_notice;
// A profile renamed on the page, so the menu keeps showing it.
inline std::wstring g_renamedFrom, g_renamedTo;

inline std::vector<ContentProfiles::Profile>& Profiles()
{
    static std::vector<ContentProfiles::Profile> profiles;
    return profiles;
}

inline size_t Find(const std::wstring& name)
{
    const auto& profiles = Profiles();
    for (size_t i = 0; i < profiles.size(); ++i)
        if (!_wcsicmp(profiles[i].name.c_str(), name.c_str()))
            return i;
    return profiles.size();
}

// Saves a profile's folders and displays. Its settings come from the file:
// the other pages may have changed them since this page was built.
inline void Save(size_t index)
{
    auto& profiles = Profiles();
    if (index < profiles.size())
    {
        const auto stored = ContentProfiles::Load();
        if (const auto* current = ContentProfiles::Find(stored, profiles[index].name))
            profiles[index].overrides = current->overrides;
        ContentProfiles::Save(profiles[index]);
    }
    ++g_version;
}

inline bool ValidName(const std::wstring& name)
{
    if (name.empty() || name.size() > 60 || name.back() == L'.' || name.back() == L' ' || name.front() == L' ' ||
        name == NewTab || !_wcsicmp(name.c_str(), DefaultTab))
        return false;
    return name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos;
}

inline std::wstring UnusedName()
{
    for (int i = 1;; ++i)
    {
        const auto name = L"Profile " + std::to_wstring(i);
        if (Find(name) == Profiles().size())
            return name;
    }
}

// Renames a profile and its file; false for an invalid or used name.
inline bool Rename(const std::wstring& from, const std::wstring& to)
{
    const size_t index = Find(from);
    auto& profiles = Profiles();
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
    // The menu keeps showing a renamed profile.
    g_renamedFrom = from;
    g_renamedTo = to;
    g_selectTab = to;
    ++g_version;
    return true;
}

// Text control for a profile's name, also edited in place of its tab.
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

// Adds an empty profile, selects its tab and asks the host to choose it at
// the top, so the other pages change it right away.
inline void Create()
{
    ContentProfiles::Profile created;
    created.name = UnusedName();
    if (ContentProfiles::Save(created))
    {
        g_selectTab = created.name;
        g_requestSelection = created.name;
        g_selectionRequested = true;
    }
    ++g_version;
}

// Deletes a profile; the host asks first in a dialog.
inline void Delete(const std::wstring& name)
{
    DeleteFileW((ContentProfiles::Directory() + L"\\" + name + L".ini").c_str());
    ++g_version;
}

inline std::wstring Fill(std::wstring text, const std::wstring& value)
{
    if (const auto at = text.find(L"%s"); at != std::wstring::npos)
        text.replace(at, 2, value);
    return text;
}

// The Default tab: which display's normal settings the menu shows.
inline void
DefaultCards(D2DPage& page, const std::wstring& display, const std::vector<DisplayProfiles::Device>& devices)
{
    D2DCard card{L"Display", L"", {}, DefaultTab};
    D2DControl choice;
    choice.name = L"Display";
    choice.kind = ControlKind::Choice;
    choice.desc = L"Normal settings for videos without a profile. Each display has its own.";
    auto ids = std::make_shared<std::vector<std::wstring>>();
    for (const auto& device : devices)
    {
        choice.choices.push_back({int(ids->size()), device.name + (device.connected ? L"" : L" (Disconnected)"), L""});
        ids->push_back(device.id);
    }
    choice.get = [ids, display] { return float(std::find(ids->begin(), ids->end(), display) - ids->begin()); };
    choice.defaultValue = choice.get;
    choice.set = [ids](float v)
    {
        if (const int i = int(v); i >= 0 && i < int(ids->size()))
            g_requestDisplay = (*ids)[size_t(i)];
    };

    // Linking copies this display's settings to all, then keeps them equal.
    D2DControl link;
    link.name = L"Same Settings on All Displays";
    link.kind = ControlKind::Toggle;
    link.desc = L"Changes apply to every display. Off: each keeps them and can be changed alone.";
    link.get = [] { return DisplayProfiles::Linked() ? 1.f : 0.f; };
    link.defaultValue = [] { return 0.f; };
    link.set = [display](float v)
    {
        if (v && !DisplayProfiles::Linked())
        {
            if (SUCCEEDED(DisplayProfiles::SetLinked(true)) && !display.empty())
                SaveSettings(LoadSettings(display));
        }
        else if (!v)
            DisplayProfiles::SetLinked(false);
        ++g_version;
    };
    card.controls.push_back(std::move(link));
    // While linked every display is the same: choosing or renaming one is off.
    choice.enabled = [] { return !DisplayProfiles::Linked(); };
    card.controls.push_back(std::move(choice));

    D2DControl name;
    name.name = L"Display Name";
    name.kind = ControlKind::Text;
    name.getText = [display]
    {
        for (const auto& device : DisplayProfiles::All())
            if (device.id == display)
                return device.name;
        return std::wstring();
    };
    name.setText = [display](const std::wstring& text)
    {
        if (FAILED(DisplayProfiles::Rename(display, text)))
            return false;
        ++g_version;
        return true;
    };
    name.invalidText = L"Enter a display name (1 to 80 characters).";
    name.enabled = [] { return !DisplayProfiles::Linked(); };
    card.controls.push_back(std::move(name));
    page.cards.push_back(std::move(card));
}

// The cards of one profile, all in the tab named after it.
inline void ProfileCards(D2DPage& page, size_t index, const std::vector<DisplayProfiles::Device>& devices)
{
    auto& profile = Profiles()[index];
    const auto tab = profile.name;

    D2DCard about{L"Profile", L"", {}, tab};
    auto name = RenameControl(tab);
    name.name = L"Name";
    name.desc = L"Up to 60 characters.";
    about.controls.push_back(std::move(name));
    page.cards.push_back(std::move(about));

    D2DCard folders{L"Folders", L"", {}, tab};
    for (size_t i = 0; i < profile.folders.size(); ++i)
        folders.controls.push_back(D2DAction(profile.folders[i].c_str(),
                                             L"Remove",
                                             [index, i](HWND)
                                             {
                                                 auto& profiles = Profiles();
                                                 if (index >= profiles.size() || i >= profiles[index].folders.size())
                                                     return;
                                                 profiles[index].folders.erase(profiles[index].folders.begin() + i);
                                                 Save(index);
                                             }));
    folders.controls.push_back(D2DAction(
        L"Add Folder",
        L"Choose…",
        [index](HWND owner)
        {
            const auto folder = PickFolder(owner);
            auto& profiles = Profiles();
            if (index >= profiles.size() || folder.empty())
                return;
            // A folder belongs to one profile, so the folder alone decides.
            if (const auto* holder = ContentProfiles::FolderOwner(profiles, folder))
            {
                if (holder != &profiles[index])
                    g_notice = Fill(Tr(L"This folder already belongs to the profile \"%s\"."), holder->name);
                return;
            }
            profiles[index].folders.push_back(folder);
            Save(index);
        },
        L"Videos in these folders and their subfolders use this profile; when folders are nested, the closest one "
        L"wins."));
    page.cards.push_back(std::move(folders));

    if (devices.empty())
        return;
    D2DCard displays{L"Displays", L"", {}, tab};
    D2DControl all;
    all.name = L"All Displays";
    all.kind = ControlKind::Toggle;
    all.desc = L"Off: only on the displays switched on below.";
    all.get = [index] { return index < Profiles().size() && Profiles()[index].monitors.empty() ? 1.f : 0.f; };
    all.defaultValue = [] { return 1.f; };
    auto ids = std::make_shared<std::vector<std::wstring>>();
    for (const auto& device : devices)
        ids->push_back(device.id);
    all.set = [index, ids](float v)
    {
        auto& profiles = Profiles();
        if (index >= profiles.size())
            return;
        if (v)
            profiles[index].monitors.clear();
        else if (profiles[index].monitors.empty())
            profiles[index].monitors = *ids;
        Save(index);
    };
    displays.controls.push_back(std::move(all));
    for (const auto& device : devices)
    {
        D2DControl one;
        one.name = device.name + (device.connected ? L"" : L" (Disconnected)");
        one.kind = ControlKind::Toggle;
        const auto id = device.id;
        one.get = [index, id]
        { return index < Profiles().size() && ContentProfiles::AllowsDisplay(Profiles()[index], id) ? 1.f : 0.f; };
        one.defaultValue = [] { return 1.f; };
        one.enabled = [index] { return index < Profiles().size() && !Profiles()[index].monitors.empty(); };
        one.set = [index, id](float v)
        {
            auto& profiles = Profiles();
            if (index >= profiles.size())
                return;
            auto& list = profiles[index].monitors;
            const auto found = std::find(list.begin(), list.end(), id);
            if (v && found == list.end())
                list.push_back(id);
            // At least one display stays on; all off would never apply.
            else if (!v && found != list.end() && list.size() > 1)
                list.erase(found);
            Save(index);
        };
        displays.controls.push_back(std::move(one));
    }
    page.cards.push_back(std::move(displays));
}

inline D2DPage Build(const PlaceboSettings& settings)
{
    auto& profiles = Profiles();
    profiles = g_template ? std::vector<ContentProfiles::Profile>{} : ContentProfiles::Load();
    // The translation template needs the texts of a profile's tab: build one
    // nameless profile there (its empty name and folders add no text).
    if (g_template)
        profiles.emplace_back();
    const std::wstring display = settings.displayProfile;
    std::vector<DisplayProfiles::Device> devices;
    if (!display.empty())
        devices = DisplayProfiles::All();
    else if (g_template)
        devices.push_back({.connected = true});

    D2DPage page{L"Profiles", L"\xE8B7", L"Profiles", L"Different settings for videos from chosen folders.", {}};
    if (!devices.empty())
        DefaultCards(page, display, devices);
    for (size_t i = 0; i < profiles.size(); ++i)
        ProfileCards(page, i, devices);

    D2DCard create{
        L"New Profile", L"Own settings for videos from chosen folders, e.g. other scaling for anime.", {}, NewTab};
    create.controls.push_back(
        D2DAction(L"New Profile", L"Create", [](HWND) { Create(); }, L"Adds an empty profile in its own tab."));
    page.cards.push_back(std::move(create));
    return page;
}
} // namespace ProfilePage
