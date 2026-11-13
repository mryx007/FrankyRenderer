#pragma once
#include "Settings/Settings.h"
#include "Settings/DisplayProfiles.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

// Keyboard shortcuts for the playing video. One combination per action, the
// same for every display and profile, stored in HKCU\...\Shortcuts. Only
// Ctrl+J (statistics) is assigned by default: the players use most keys.
namespace Shortcuts
{
// A combination: virtual key in the low word, modifiers above.
enum : DWORD
{
    Ctrl = 1u << 16,
    Alt = 1u << 17,
    Shift = 1u << 18,
};

inline DWORD Combo(UINT key, bool ctrl, bool alt, bool shift)
{
    return (key & 0xffff) | (ctrl ? Ctrl : 0) | (alt ? Alt : 0) | (shift ? Shift : 0);
}

inline UINT Key(DWORD combo)
{
    return combo & 0xffff;
}

// Keys that only modify another key cannot be a shortcut on their own.
inline bool ModifierKey(UINT key)
{
    switch (key)
    {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
        return true;
    }
    return false;
}

inline std::wstring KeyName(UINT key)
{
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
        return std::wstring(1, wchar_t(key));
    if (key >= VK_F1 && key <= VK_F24)
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        return L"Num " + std::to_wstring(key - VK_NUMPAD0);
    switch (key)
    {
    case VK_LEFT:
        return L"Left";
    case VK_RIGHT:
        return L"Right";
    case VK_UP:
        return L"Up";
    case VK_DOWN:
        return L"Down";
    case VK_PRIOR:
        return L"Page Up";
    case VK_NEXT:
        return L"Page Down";
    case VK_HOME:
        return L"Home";
    case VK_END:
        return L"End";
    case VK_INSERT:
        return L"Insert";
    case VK_DELETE:
        return L"Delete";
    case VK_SPACE:
        return L"Space";
    case VK_RETURN:
        return L"Enter";
    case VK_TAB:
        return L"Tab";
    case VK_BACK:
        return L"Backspace";
    case VK_ADD:
        return L"Num +";
    case VK_SUBTRACT:
        return L"Num -";
    case VK_MULTIPLY:
        return L"Num *";
    case VK_DIVIDE:
        return L"Num /";
    case VK_DECIMAL:
        return L"Num .";
    }
    // Punctuation follows the keyboard layout, e.g. "+" or "#".
    if (const UINT character = MapVirtualKeyW(key, MAPVK_VK_TO_CHAR) & 0xffff; character > 32)
        return std::wstring(1, wchar_t(character));
    wchar_t name[64] = {};
    const LONG scan = LONG(MapVirtualKeyW(key, MAPVK_VK_TO_VSC)) << 16;
    if (scan && GetKeyNameTextW(scan, name, 64) > 0)
        return name;
    wchar_t code[16];
    swprintf_s(code, L"Key %u", key);
    return code;
}

// "Ctrl+Alt+J"; empty for no shortcut. name turns the English key names
// ("Ctrl", "Page Up") into the menu language.
inline std::wstring Format(DWORD combo, std::wstring (*name)(const std::wstring&) = nullptr)
{
    if (!Key(combo))
        return {};
    const auto shown = [name](const std::wstring& text) { return name ? name(text) : text; };
    std::wstring text;
    if (combo & Ctrl)
        text += shown(L"Ctrl") + L"+";
    if (combo & Alt)
        text += shown(L"Alt") + L"+";
    if (combo & Shift)
        text += shown(L"Shift") + L"+";
    return text + shown(KeyName(Key(combo)));
}

// Actions of the renderer itself; every menu setting has its own keys in the
// tree on the Shortcuts page (Custom entries below).
enum class Action
{
    Statistics,
    Shaders,
    Quality,
    Profile,
    PictureReset,
    ResetStatistics,
    Count
};

inline constexpr int Count = int(Action::Count);

struct ActionInfo
{
    const wchar_t* id;   // registry value name, never translated
    const wchar_t* name; // menu text
    const wchar_t* help; // what a key press does
};

inline constexpr ActionInfo Actions[Count] = {
    {L"Statistics", L"Statistics", L"Shows or hides the statistics."},
    {L"Shaders", L"All Shaders Off / Back On", L"Turns all shaders off; pressed again, back on."},
    {L"Quality", L"Next Quality Preset", L"Switches to the next quality preset."},
    {L"Profile", L"Next Profile", L"Switches this file to the next profile."},
    {L"PictureReset", L"Reset Picture Controls", L"Sets brightness, contrast, saturation and gamma back to neutral."},
    {L"ResetStatistics", L"Reset Statistics", L"Counters, averages and peaks start from zero."},
};

inline DWORD DefaultCombo(Action action)
{
    return action == Action::Statistics ? Combo('J', true, false, false) : 0;
}

inline std::wstring KeyPath()
{
    return DisplayProfiles::Root() + L"\\Shortcuts";
}

using Table = std::array<DWORD, Count>;

// Saved combinations; a missing value keeps the default, 0 means none.
inline Table Load()
{
    Table table;
    HKEY key = nullptr;
    const bool open = RegOpenKeyExW(HKEY_CURRENT_USER, KeyPath().c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS;
    for (int i = 0; i < Count; ++i)
    {
        DWORD value = 0, size = sizeof(value), type = 0;
        table[i] =
            open &&
                    RegQueryValueExW(key, Actions[i].id, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
                        ERROR_SUCCESS &&
                    type == REG_DWORD && size == sizeof(value) && !ModifierKey(Key(value))
                ? value
                : DefaultCombo(Action(i));
    }
    if (open)
        RegCloseKey(key);
    return table;
}

// Load() at most once a second: the key hook and the menu read it often,
// and Assign() in this process refreshes it at once.
inline uint64_t g_cachedAt = 0;
inline Table g_cache = {};

inline const Table& Cached()
{
    const uint64_t now = GetTickCount64();
    if (!g_cachedAt || now - g_cachedAt > 1000)
    {
        g_cache = Load();
        g_cachedAt = now;
    }
    return g_cache;
}

// The action using a combination, or -1.
inline int Find(const Table& table, DWORD combo)
{
    if (!Key(combo))
        return -1;
    for (int i = 0; i < Count; ++i)
        if (table[i] == combo)
            return i;
    return -1;
}

// Keys for menu settings, found by page, card and English name in the
// settings model. Only assigned keys are stored, one registry value per entry
// under Shortcuts\Custom ("0", "1", ...): combo|mode|value|page|card|name.
enum class CustomMode
{
    Toggle, // a switch on/off
    Next,   // a choice: the next value
    Set,    // a choice: one value
    Up,     // a number: one step up
    Down,   // a number: one step down
};

struct Custom
{
    DWORD combo = 0;
    CustomMode mode = CustomMode::Toggle;
    float value = 0;
    std::wstring page, card, name;
};

inline std::wstring CustomPath()
{
    return KeyPath() + L"\\Custom";
}

inline std::wstring Serialize(const Custom& c)
{
    wchar_t head[64];
    swprintf_s(head, L"%lu|%d|%.9g|", c.combo, int(c.mode), double(c.value));
    return head + c.page + L"|" + c.card + L"|" + c.name;
}

inline bool Parse(const std::wstring& text, Custom& out)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (int i = 0; i < 5; ++i)
    {
        const auto bar = text.find(L'|', start);
        if (bar == std::wstring::npos)
            return false;
        parts.push_back(text.substr(start, bar - start));
        start = bar + 1;
    }
    parts.push_back(text.substr(start));
    const int mode = _wtoi(parts[1].c_str());
    if (mode < 0 || mode > int(CustomMode::Down) || parts[3].empty() || parts[5].empty())
        return false;
    out.combo = wcstoul(parts[0].c_str(), nullptr, 10);
    if (ModifierKey(Key(out.combo)))
        out.combo = 0;
    out.mode = CustomMode(mode);
    out.value = float(_wtof(parts[2].c_str()));
    out.page = parts[3];
    out.card = parts[4];
    out.name = parts[5];
    return true;
}

inline std::vector<Custom> LoadCustom()
{
    std::vector<Custom> list;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CustomPath().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return list;
    for (int i = 0;; ++i)
    {
        wchar_t text[1024];
        DWORD size = sizeof(text), type = 0;
        if (RegQueryValueExW(key, std::to_wstring(i).c_str(), nullptr, &type, reinterpret_cast<BYTE*>(text), &size) !=
                ERROR_SUCCESS ||
            type != REG_SZ)
            break;
        text[std::min<size_t>(size / sizeof(wchar_t), 1023)] = 0;
        Custom entry;
        if (Parse(text, entry))
            list.push_back(entry);
    }
    RegCloseKey(key);
    return list;
}

inline uint64_t g_customAt = 0;
inline std::vector<Custom> g_custom;

inline const std::vector<Custom>& CachedCustom()
{
    const uint64_t now = GetTickCount64();
    if (!g_customAt || now - g_customAt > 1000)
    {
        g_custom = LoadCustom();
        g_customAt = now;
    }
    return g_custom;
}

inline bool SaveCustom(const std::vector<Custom>& list)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, CustomPath().c_str());
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, CustomPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    bool ok = true;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto text = Serialize(list[i]);
        ok &= RegSetValueExW(key,
                             std::to_wstring(i).c_str(),
                             0,
                             REG_SZ,
                             reinterpret_cast<const BYTE*>(text.c_str()),
                             DWORD((text.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    RegCloseKey(key);
    g_customAt = 0;
    return ok;
}

// The shortcut using a combination: a fixed action (0..Count-1), a custom
// entry (Count + its index), or -1.
inline int FindAny(DWORD combo)
{
    if (const int fixed = Find(Cached(), combo); fixed >= 0)
        return fixed;
    if (!Key(combo))
        return -1;
    const auto& list = CachedCustom();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].combo == combo)
            return Count + int(i);
    return -1;
}

// The same setting and the same effect, whatever the keys.
inline bool SameTarget(const Custom& a, const Custom& b)
{
    return a.mode == b.mode && a.value == b.value && a.page == b.page && a.card == b.card && a.name == b.name;
}

inline DWORD CustomCombo(const Custom& target)
{
    for (const auto& entry : CachedCustom())
        if (SameTarget(entry, target))
            return entry.combo;
    return 0;
}

// Gives a menu setting's key a combination (0 removes it). Returns the index
// that already uses it (as FindAny), else -1 after saving.
inline int AssignTarget(const Custom& target, DWORD combo)
{
    auto list = LoadCustom();
    const auto at = std::find_if(list.begin(), list.end(), [&](const Custom& c) { return SameTarget(c, target); });
    const int self = at == list.end() ? -1 : Count + int(at - list.begin());
    g_cachedAt = g_customAt = 0;
    if (const int other = FindAny(combo); other >= 0 && other != self)
        return other;
    if (!Key(combo))
    {
        if (at == list.end())
            return -1;
        list.erase(at);
    }
    else if (at != list.end())
        at->combo = combo;
    else
    {
        auto entry = target;
        entry.combo = combo;
        list.push_back(entry);
    }
    SaveCustom(list);
    return -1;
}

// Assigns a combination (0 removes it). A combination another action uses is
// refused: returns that action's index, else -1 after saving.
inline int Assign(Action action, DWORD combo)
{
    g_cachedAt = g_customAt = 0;
    if (const int other = FindAny(combo); other >= 0 && other != int(action))
        return other;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, KeyPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS)
    {
        RegSetValueExW(
            key, Actions[int(action)].id, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&combo), sizeof(combo));
        RegCloseKey(key);
    }
    g_cachedAt = 0;
    return -1;
}

// Reset Picture Controls: the neutral picture.
inline void ResetPicture(PlaceboSettings& s)
{
    const PlaceboSettings defaults;
    s.brightness = defaults.brightness;
    s.contrast = defaults.contrast;
    s.saturation = defaults.saturation;
    s.gamma = defaults.gamma;
    s.hue = defaults.hue;
    s.temperature = defaults.temperature;
}
} // namespace Shortcuts
