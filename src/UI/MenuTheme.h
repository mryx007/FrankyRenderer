#pragma once
// Menu colour themes. A theme sets the background tones; the accent colour sets
// selection, switches, sliders and hover, all derived from one colour so every
// combination matches. Menu preference only, stored beside the menu settings.
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include "Settings/DisplayProfiles.h"

#pragma comment(lib, "dwmapi.lib") // title bar colours and the Windows accent

struct MenuPalette
{
    UINT32 app, sidebar, card, cardHeader, border, input, textMain, textMuted, textDim, toggleOff, glassTop, glassMid;
    UINT32 accent, accentHover, activeBg, toggleOn, sliderThumb, glassBottom;
    bool light;
};

inline constexpr const wchar_t* MenuThemeNames[] = {L"Indigo", L"Graphite", L"Midnight", L"Ocean", L"Light"};
inline constexpr const wchar_t* MenuAccentNames[] = {
    L"Violet", L"Blue", L"Teal", L"Green", L"Orange", L"Pink", L"Windows"};

inline int g_menuTheme = 0, g_menuAccent = 0;
// Bumped on every change so open views rebuild their brushes.
inline int g_menuPaletteVersion = 0;

inline std::wstring MenuAppearanceKey()
{
    return DisplayProfiles::Root() + L"\\Appearance";
}

inline void LoadMenuRememberPage();

inline void LoadMenuAppearance()
{
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"Theme", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS &&
        value < std::size(MenuThemeNames))
        g_menuTheme = int(value);
    size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"Accent", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS &&
        value < std::size(MenuAccentNames))
        g_menuAccent = int(value);
    LoadMenuRememberPage();
}

// "Remember Last Page": the window opens where it was closed (page, tab and
// scroll position), stored by name so new pages in later versions do not
// shift it.
inline bool g_menuRememberPage = false;

inline void LoadMenuRememberPage()
{
    DWORD value = 0, size = sizeof(value);
    g_menuRememberPage = RegGetValueW(HKEY_CURRENT_USER,
                                      MenuAppearanceKey().c_str(),
                                      L"RememberPage",
                                      RRF_RT_REG_DWORD,
                                      nullptr,
                                      &value,
                                      &size) == ERROR_SUCCESS &&
                         value == 1;
}

inline void SetMenuRememberPage(bool remember)
{
    g_menuRememberPage = remember;
    const DWORD value = remember ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"RememberPage", REG_DWORD, &value, sizeof(value));
}

struct MenuPosition
{
    std::wstring page, tab;
    int scroll = 0;
};

inline void SaveMenuPosition(const MenuPosition& at)
{
    const auto key = MenuAppearanceKey();
    RegSetKeyValueW(HKEY_CURRENT_USER,
                    key.c_str(),
                    L"LastPage",
                    REG_SZ,
                    at.page.c_str(),
                    DWORD((at.page.size() + 1) * sizeof(wchar_t)));
    RegSetKeyValueW(HKEY_CURRENT_USER,
                    key.c_str(),
                    L"LastTab",
                    REG_SZ,
                    at.tab.c_str(),
                    DWORD((at.tab.size() + 1) * sizeof(wchar_t)));
    const DWORD scroll = DWORD(std::max(0, at.scroll));
    RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"LastScroll", REG_DWORD, &scroll, sizeof(scroll));
}

inline MenuPosition LoadMenuPosition()
{
    MenuPosition at;
    const auto key = MenuAppearanceKey();
    auto text = [&](const wchar_t* name)
    {
        wchar_t value[256] = {};
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS
                   ? std::wstring(value)
                   : std::wstring();
    };
    at.page = text(L"LastPage");
    at.tab = text(L"LastTab");
    DWORD scroll = 0, size = sizeof(scroll);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"LastScroll", RRF_RT_REG_DWORD, nullptr, &scroll, &size) ==
            ERROR_SUCCESS &&
        scroll < 100000)
        at.scroll = int(scroll);
    return at;
}

inline void SetMenuAppearance(int theme, int accent)
{
    if (theme == g_menuTheme && accent == g_menuAccent)
        return;
    g_menuTheme = theme;
    g_menuAccent = accent;
    ++g_menuPaletteVersion;
    const DWORD values[] = {DWORD(theme), DWORD(accent)};
    RegSetKeyValueW(HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"Theme", REG_DWORD, &values[0], sizeof(DWORD));
    RegSetKeyValueW(HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), L"Accent", REG_DWORD, &values[1], sizeof(DWORD));
}

// Linear mix of two 0xRRGGBB colours; t = 0 gives a, t = 1 gives b.
inline UINT32 MixColor(UINT32 a, UINT32 b, float t)
{
    UINT32 out = 0;
    for (int shift : {16, 8, 0})
    {
        const float x = float((a >> shift) & 255), y = float((b >> shift) & 255);
        out |= UINT32(x + (y - x) * t + .5f) << shift;
    }
    return out;
}

inline UINT32 WindowsAccent()
{
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&color, &opaque)))
        return 0xB59AFF;
    return color & 0xFFFFFF;
}

inline COLORREF MenuColorRef(UINT32 c)
{
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

inline MenuPalette CurrentMenuPalette()
{
    static constexpr MenuPalette themes[] = {
        // app, sidebar, card, header, border, input, text, muted, dim, toggleOff, glassTop, glassMid
        {0x101321,
         0x16192B,
         0x20243A,
         0x2B2E49,
         0x3A405F,
         0x161B30,
         0xF3F3FF,
         0xC0C5E2,
         0x959EBE,
         0x414B68,
         0x25283F,
         0x171A30},
        {0x111214,
         0x18191C,
         0x1F2024,
         0x27292E,
         0x383A41,
         0x151619,
         0xF2F3F5,
         0xC2C5CC,
         0x8E929B,
         0x45484F,
         0x25272B,
         0x17181B},
        {0x000000,
         0x08090C,
         0x101114,
         0x17181D,
         0x2A2C33,
         0x0A0B0E,
         0xF2F3F5,
         0xB9BCC4,
         0x858993,
         0x3A3D45,
         0x15161A,
         0x0A0B0E},
        {0x0E1620,
         0x13202C,
         0x1A2A38,
         0x223446,
         0x2F4A60,
         0x101C27,
         0xEEF6FF,
         0xB8CADC,
         0x8AA2B8,
         0x3C5468,
         0x1E3040,
         0x122030},
        // Soft light grey cards on a slightly darker window, no pure white.
        {0xEEEFF2,
         0xE6E7EB,
         0xF7F7F9,
         0xEBECEF,
         0xD8DADF,
         0xEFF0F3,
         0x1B1D24,
         0x464B5A,
         0x737A8B,
         0xC3C7D2,
         0xF4F5F9,
         0xE8EAF1,
         0,
         0,
         0,
         0,
         0,
         0,
         true}};
    static constexpr UINT32 accents[] = {0xB59AFF, 0x6CB4FF, 0x4FD1C5, 0x7BD88F, 0xFFAB5C, 0xFF8FC7};
    auto p = themes[std::clamp(g_menuTheme, 0, int(std::size(themes)) - 1)];
    const UINT32 base = g_menuAccent < int(std::size(accents)) ? accents[std::max(0, g_menuAccent)] : WindowsAccent();
    if (p.light)
    {
        // On white the pastel accents need more depth to stay readable.
        p.accent = MixColor(base, 0x000000, .3f);
        p.accentHover = MixColor(base, 0x000000, .45f);
        p.activeBg = MixColor(p.card, base, .22f);
        p.toggleOn = MixColor(base, 0x000000, .25f);
        p.sliderThumb = 0xFFFFFF;
        p.glassBottom = MixColor(p.sidebar, base, .25f);
        return p;
    }
    // Lift dark system accents so text and outlines on them stay readable.
    p.accent = g_menuAccent < int(std::size(accents)) ? base : MixColor(base, 0xFFFFFF, .25f);
    p.accentHover = MixColor(p.accent, 0xFFFFFF, .5f);
    p.activeBg = MixColor(p.card, p.accent, .28f);
    p.toggleOn = MixColor(p.accent, 0x000000, .2f);
    p.sliderThumb = MixColor(p.accent, 0xFFFFFF, .8f);
    p.glassBottom = MixColor(p.sidebar, p.accent, .35f);
    return p;
}

// Title bar of the window that hosts the menu.
inline void ApplyMenuFrameColors(HWND root)
{
    const auto p = CurrentMenuPalette();
    const BOOL dark = !p.light;
    DwmSetWindowAttribute(root, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = MenuColorRef(p.sidebar), text = MenuColorRef(p.textMain), border = MenuColorRef(p.border);
    DwmSetWindowAttribute(root, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(root, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    DwmSetWindowAttribute(root, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}
