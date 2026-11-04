#include "UI/Direct2DSettingsHost.h"
#include "Settings/SettingsLayout.h"
#include "Settings/SettingsIni.h"
#include "Settings/SettingsBackup.h"
#include "Settings/LiveSettings.h"
#include "UI/MenuTheme.h"
#include "UI/Translation.h"
#include "Settings/QualityPresets.h"
#include "Renderer/QualityBenchmark.h"
#include <thread>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cerrno>
#include "UI/ChoicePopup.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;
#include "UI/GlassSidebar.h"

enum class ControlKind
{
    Toggle,
    SliderInt,
    SliderFloat,
    Choice,
    Action,
    Text,
    Tabs // choices as a row of tabs across the card
};

struct ChoiceItem
{
    int value;
    std::wstring label;
    std::wstring description;
};

struct D2DControl
{
    std::wstring id; // English name; name is translated for display
    std::wstring name;
    std::wstring desc;
    ControlKind kind;
    float minVal = 0.0f;
    float maxVal = 100.0f;
    float stepVal = 1.0f;
    std::vector<ChoiceItem> choices;

    std::function<float()> get;
    std::function<float()> defaultValue;
    std::function<void(float)> set;
    std::function<void()> reset;
    std::function<bool()> enabled;
    std::function<void(HWND)> action;
    std::function<std::wstring()> detail;
    // Switch drawn before the name, e.g. whether a setting belongs to a profile.
    std::shared_ptr<D2DControl> include;
    // Text controls; setText rejects invalid input.
    std::function<std::wstring()> getText;
    std::function<bool(const std::wstring&)> setText;
    std::wstring placeholder, invalidText;
    std::wstring actionLabel;
    bool headerToggle = false;
    bool logarithmic = false;

    bool Enabled() const
    {
        return !enabled || enabled();
    }

    float Value() const
    {
        return get ? get() : 0;
    }

    float Default() const
    {
        return defaultValue ? defaultValue() : 0;
    }
};

struct D2DCard
{
    std::wstring title;
    std::wstring subtitle;
    std::vector<D2DControl> controls;
    std::wstring tab; // explicit tab, e.g. a profile name; otherwise TabGroup decides
};

struct D2DPage
{
    std::wstring name;
    std::wstring icon;
    std::wstring title;
    std::wstring subtitle;
    std::vector<D2DCard> cards;
};

#include "UI/Direct2DSettingsModel.h"
#include "UI/ProfilePage.h"

// Menu texts outside the settings model, for the translation template.
static constexpr const wchar_t* MenuTexts[] = {L"At %sp this display offers %s Hz.",
                                               L"No 23 or 24 Hz mode, so films cannot switch here.",
                                               L"Other resolutions: %s.",
                                               L"Delete profile?",
                                               L"\"%s\" and its settings will be removed.",
                                               L"Cancel",
                                               L"Delete",
                                               L"Profile",
                                               L"Normal settings of this display.",
                                               L"Used by the playing video.",
                                               L"The menu now shows \"%s\". Changes on all pages go into the profile.",
                                               L"Remove all settings of the profile \"%s\"? It then matches Default.",
                                               L"The profile matches Default again.",
                                               L"This folder already belongs to the profile \"%s\".",
                                               L"Could not save the profile.",
                                               L"Display",
                                               L"Rename",
                                               L"Import",
                                               L"Export",
                                               L"Reset Page",
                                               L"Reset Shader",
                                               L"Saved",
                                               L"Save (%d)",
                                               L"Search settings",
                                               L"Search Results",
                                               L"Matching settings for: \"%s\"",
                                               L"No settings matching this term found.",
                                               L"All Off",
                                               L"Settings saved.",
                                               L"Could not save settings.",
                                               L"Select an ICC profile file first.",
                                               L"Select a 3D LUT file first.",
                                               L"Value or combination is outside the supported range.",
                                               L"Could not open the INI file dialog.",
                                               L"All display profiles exported.",
                                               L"INI backup exported.",
                                               L"All display profiles imported.",
                                               L"INI imported. Select Save to keep it.",
                                               L"INI settings imported.",
                                               L"Could not apply INI settings. Check external ICC and LUT files.",
                                               L"Could not read display profiles.",
                                               L"Enter a display name (1 to 80 characters).",
                                               L"Enter a number within the supported range.",
                                               L"Automatic (Windows)",
                                               L"English",
                                               L"Quality",
                                               L"Custom",
                                               L"Measured",
                                               L"Measuring…",
                                               L"Measure this PC",
                                               L"Picks the best settings for this PC.",
                                               L"Measured: %s (%.1f of %.1f ms)",
                                               L"Measured: same as %s (%.1f of %.1f ms)",
                                               L"Could not measure this display.",
                                               L"Measurement failed.",
                                               L"Reset All",
                                               L"Reset all settings for this display to their defaults? Theme and "
                                               L"language stay.",
                                               L"All settings reset to defaults."};

// Translates everything the settings model only displays. Page names, card
// titles and tab names stay English here: the menu logic uses them as
// identifiers, so they are translated when drawn.
static void TranslatePages(std::vector<D2DPage>& pages)
{
    for (auto& page : pages)
    {
        page.subtitle = Tr(page.subtitle);
        for (auto& card : page.cards)
        {
            card.subtitle = Tr(card.subtitle);
            for (auto& c : card.controls)
            {
                c.id = c.name;
                c.name = Tr(c.name);
                // Some descriptions append live details on a new line, e.g. the
                // display modes a monitor offers; translate the text before them.
                const auto lineBreak = c.desc.find(L'\n');
                const auto whole = Tr(c.desc);
                c.desc = whole != c.desc || lineBreak == std::wstring::npos
                             ? whole
                             : Tr(c.desc.substr(0, lineBreak)) + c.desc.substr(lineBreak);
                c.placeholder = Tr(c.placeholder);
                c.invalidText = Tr(c.invalidText);
                c.actionLabel = Tr(c.actionLabel);
                for (auto& choice : c.choices)
                {
                    choice.label = Tr(choice.label);
                    choice.description = Tr(choice.description);
                }
                if (c.detail)
                    c.detail = [detail = c.detail] { return Tr(detail()); };
            }
        }
    }
}

// The native search field inside the drawn header search box.
static D2D1_RECT_F SearchEditRect(const D2D1_RECT_F& box)
{
    return D2D1::RectF(box.left + 32, box.top + 7, box.right - 30, box.bottom - 6);
}

// Decimal places for a slider value, from its step: 0.01 gives two, 1 none.
// The display shows at most two; typing shows the full step precision.
static int SliderDecimals(const D2DControl& c, bool editing = false)
{
    if (c.kind == ControlKind::SliderInt || c.stepVal >= 1.f)
        return 0;
    const int step = c.stepVal > 0 ? std::clamp(int(std::ceil(-std::log10(c.stepVal) - 1e-4)), 0, 4) : 3;
    return editing ? step : std::min(step, 2);
}

// Slider value as shown: two decimals below 10 (0.40), none from 10 on
// (100), one only when a larger value really has a fraction (12.5).
static std::wstring SliderText(const D2DControl& c, float value)
{
    int decimals = SliderDecimals(c);
    if (std::abs(value) >= 10.f)
        decimals = std::min(decimals, std::abs(value - std::round(value)) >= .05f ? 1 : 0);
    wchar_t text[32];
    swprintf_s(text, L"%.*f", decimals, double(value));
    return text;
}

static float SliderFraction(const D2DControl& c, float value)
{
    const double low = c.minVal, high = c.maxVal;
    if (c.logarithmic && high > low)
        return float(std::log1p(std::clamp(double(value), low, high) - low) / std::log1p(high - low));
    return high > low ? float(std::clamp((double(value) - low) / (high - low), 0., 1.)) : 0.f;
}

static float SliderValue(const D2DControl& c, float fraction)
{
    const double low = c.minVal, high = c.maxVal;
    if (fraction <= 0)
        return c.minVal;
    if (fraction >= 1)
        return c.maxVal;
    if (c.logarithmic && high > low)
        return float(std::clamp(low + std::expm1(double(fraction) * std::log1p(high - low)), low, high));
    return float(low + std::clamp(double(fraction), 0., 1.) * (high - low));
}

static std::wstring NumberText(const D2DControl& control, float value, bool range = false)
{
    wchar_t text[64];
    const int minimumDecimals = range && control.kind == ControlKind::SliderFloat && control.stepVal < 1.f &&
                                        value != 0 && std::abs(value) < 10.f
                                    ? 1
                                    : 0;
    swprintf_s(text, L"%.*f", std::max(minimumDecimals, SliderDecimals(control, true)), double(value));
    std::wstring result(text);
    const auto point = result.find(L'.');
    if (point != std::wstring::npos)
    {
        while (result.ends_with(L"0") && result.size() > point + 1 + size_t(minimumDecimals))
            result.pop_back();
        if (result.ends_with(L"."))
            result.pop_back();
    }
    if (result.starts_with(L"-0") && result.find_first_not_of(L"-0.") == std::wstring::npos)
        result.erase(0, 1);
    return result;
}

static float NumberStep(const D2DControl& control)
{
    const float step = control.stepVal > 0 ? control.stepVal : 1.f;
    if (control.kind == ControlKind::SliderInt || step >= 1.f)
        return step;
    return step < .01f ? step * 10.f : step * 5.f;
}

static float AdjustedNumber(const D2DControl& control, int direction, int multiplier = 1)
{
    double value = double(control.Value()) + direction * double(NumberStep(control)) * multiplier;
    if (control.stepVal > 0)
        value = std::round(value / double(control.stepVal)) * double(control.stepVal);
    return float(std::clamp(value, double(control.minVal), double(control.maxVal)));
}

// Hit-test region helper
struct HitRegion
{
    enum Type
    {
        None,
        NavItem,
        Toggle,
        Slider,
        DropdownHeader,
        SearchBox,
        SearchClearBtn,
        ResetBtn,
        ResetAllBtn,
        ApplyBtn,
        ImportBtn,
        ExportBtn,
        DisplayProfile,
        QualityPresetBtn,
        ProfileSelectBtn,
        RenameDisplay,
        Number,
        Decrement,
        Increment,
        Action,
        ResetValue,
        Scrollbar,
        ShaderItem,
        ShaderToggle,
        TabClose,
        ChoiceTab,
        CardTab
    } type = None;

    D2D1_RECT_F rect = {};
    int pageIndex = -1;
    D2DControl* pControl = nullptr;
    D2D1_RECT_F trackRect = {};
};

#include <cwctype>

static bool MatchesFilter(const std::wstring& text, const std::wstring& query)
{
    size_t start = 0;
    while (start < query.size())
    {
        while (start < query.size() && iswspace(query[start]))
            start++;
        if (start == query.size())
            break;
        size_t end = start;
        while (end < query.size() && !iswspace(query[end]))
            end++;
        const auto term = query.substr(start, end - start);
        if (std::search(text.begin(),
                        text.end(),
                        term.begin(),
                        term.end(),
                        [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); }) == text.end())
            return false;
        start = end;
    }
    return true;
}

static bool
ControlMatches(const D2DControl& control, const D2DCard& card, const D2DPage& page, const std::wstring& query)
{
    return MatchesFilter(page.name + L" " + Tr(page.name) + L" " + card.title + L" " + Tr(card.title) + L" " +
                             control.id + L" " + control.name + L" " + control.desc,
                         query);
}

class Direct2DHostWindow
{
  public:
    HWND m_hwnd = nullptr;
    // The settings the controls edit: with a profile shown, the normal settings
    // with it on top. Direct2DSettingsHost::Save gives the normal settings.
    PlaceboSettings* m_pSettings = nullptr;
    bool m_standalone = false;
    bool m_navigationOpen = false;
    float m_navigationProgress = 0, m_navigationFrom = 0, m_hoverProgress = 1;
    ULONGLONG m_navigationStarted = 0, m_hoverStarted = 0;
    int m_hoverPage = -1, m_hoverTab = -1;
    D2D1_RECT_F m_hoverRect = {}, m_previousHoverRect = {}, m_pressRect = {};
    bool m_hoverSlider = false, m_previousHoverSlider = false, m_pressSlider = false;
    ULONGLONG m_contentStarted = 0, m_pressedAt = 0;
    float m_contentProgress = 1;
    int m_previousCard = -1;
    int m_paletteVersion = -1;
    int m_languageVersion = g_menuLanguageVersion;
    int m_profileVersion = ProfilePage::g_version;
    // When a profile was added or the delete dialog closed: the second click
    // of a double-click must not act on the tabs that moved meanwhile.
    ULONGLONG m_profileCreated = 0;
    // Edited in place of a profile tab after a double-click.
    D2DControl m_tabRename;
    // Profile whose close mark was clicked: a dialog asks before deleting it.
    std::wstring m_deleteTab;
    D2D1_RECT_F m_dialogBox = {}, m_dialogConfirm = {}, m_dialogCancel = {};
    D2D1_POINT_2F m_dialogMouse = {-1, -1};

    struct ToggleTween
    {
        float from, target;
        ULONGLONG started;
    };

    std::unordered_map<const D2DControl*, ToggleTween> m_toggleTweens;

    void ContentTransition()
    {
        m_contentStarted = GetTickCount64();
        m_contentProgress = 0;
        m_hoverRect = m_previousHoverRect = {};
        SetTimer(m_hwnd, 17, 16, nullptr);
    }

    void SelectPage(int index)
    {
        if (index == m_activePage && m_searchQuery.empty())
            return;
        m_activePage = index;
        m_activeCard = 0;
        m_previousCard = -1;
        ContentTransition();
        m_scrollY = 0;
        m_searchQuery.clear();
        m_searchFocused = false;
    }

    float ToggleProgress(const D2DControl& control)
    {
        const float target = control.Value() != 0 ? 1.f : 0.f;
        auto [item, inserted] = m_toggleTweens.try_emplace(&control, ToggleTween{target, target, 0});
        auto& tween = item->second;
        const auto now = GetTickCount64();
        const float t = std::clamp(float(now - tween.started) / 160.f, 0.f, 1.f);
        const float eased = t * t * (3.f - 2.f * t);
        const float position = tween.from + (tween.target - tween.from) * eased;
        if (tween.target != target)
        {
            tween = {position, target, now};
            SetTimer(m_hwnd, 17, 16, nullptr);
        }
        return position;
    }

    void DrawInteractionFeedback()
    {
        // A profile tab being renamed shows its field, not a hover frame.
        if (m_editControl == &m_tabRename)
            return;
        auto draw = [&](D2D1_RECT_F rect, float opacity, bool slider)
        {
            if (rect.right <= rect.left || rect.bottom <= rect.top || opacity <= 0)
                return;
            m_brushHoverBg->SetOpacity(opacity * .65f);
            m_brushAccent->SetOpacity(opacity * .65f);
            if (slider)
            {
                const auto circle =
                    D2D1::Ellipse(D2D1::Point2F((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2),
                                  8.f + opacity * 3.f,
                                  8.f + opacity * 3.f);
                m_pRenderTarget->FillEllipse(circle, m_brushHoverBg.Get());
                m_pRenderTarget->DrawEllipse(circle, m_brushAccent.Get(), 1.2f);
            }
            else
            {
                // Small controls (switches) are capsules; follow their shape.
                const float height = rect.bottom - rect.top;
                const float radius = height <= 24.f ? height / 2 : 6.f;
                m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brushHoverBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brushAccent.Get(), 1);
            }
            m_brushHoverBg->SetOpacity(1);
            m_brushAccent->SetOpacity(1);
        };
        draw(m_previousHoverRect, 1.f - m_hoverProgress, m_previousHoverSlider);
        draw(m_hoverRect, m_hoverProgress, m_hoverSlider);
        const float pressed = 1.f - std::clamp(float(GetTickCount64() - m_pressedAt) / 160.f, 0.f, 1.f);
        draw(m_pressRect, pressed, m_pressSlider);
    }

    void SetNavigationOpen(bool open)
    {
        if (m_navigationOpen == open)
            return;
        m_navigationFrom = m_navigationProgress;
        m_navigationOpen = open;
        m_navigationStarted = GetTickCount64();
        m_hoverPage = m_hoverTab = -1;
        m_hoverRect = m_previousHoverRect = {};
        SetTimer(m_hwnd, 17, 16, nullptr);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Hover(int page, int tab = -1, D2D1_RECT_F rect = {}, bool slider = false)
    {
        if (page == m_hoverPage && tab == m_hoverTab && slider == m_hoverSlider && rect.left == m_hoverRect.left &&
            rect.top == m_hoverRect.top && rect.right == m_hoverRect.right && rect.bottom == m_hoverRect.bottom)
            return;
        m_previousHoverSlider = m_hoverSlider;
        m_hoverSlider = slider;
        m_previousHoverRect = m_hoverRect;
        m_hoverRect = rect;
        m_hoverPage = page;
        m_hoverTab = tab;
        m_hoverStarted = GetTickCount64();
        m_hoverProgress = 0;
        SetTimer(m_hwnd, 17, 16, nullptr);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void Animate()
    {
        const auto now = GetTickCount64();
        const float t = std::clamp(float(now - m_navigationStarted) / 180.f, 0.f, 1.f);
        const float ease = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
        m_navigationProgress = m_navigationFrom + ((m_navigationOpen ? 1.f : 0.f) - m_navigationFrom) * ease;
        m_hoverProgress = std::clamp(float(now - m_hoverStarted) / 110.f, 0.f, 1.f);
        const float contentT = std::clamp(float(now - m_contentStarted) / 170.f, 0.f, 1.f);
        m_contentProgress = 1.f - (1.f - contentT) * (1.f - contentT) * (1.f - contentT);
        bool toggles = false;
        for (const auto& item : m_toggleTweens)
            toggles |= now - item.second.started < 160;
        if (t >= 1 && m_hoverProgress >= 1 && contentT >= 1 && now - m_pressedAt >= 160 && !toggles)
            KillTimer(m_hwnd, 17);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    bool m_displayProfiles = false;
    std::vector<DisplayProfiles::Device> m_devices;
    std::function<HRESULT(const PlaceboSettings&)> m_preview;
    PlaceboSettings m_accepted, m_applied;
    // The choice at the top of the menu: ContentProfiles::DefaultName or a
    // profile, at first the one of the playing video. The menu edits m_editing
    // (empty: Default); m_base holds the display's normal settings and
    // *m_pSettings them with that profile on top. Another choice is previewed
    // on the playing video until the menu closes.
    std::wstring m_selection, m_editing;
    bool m_previewing = false;
    PlaceboSettings m_base;
    std::wstring EditTarget() const;
    void LoadEditTarget();
    void SelectProfile(const std::wstring& selection);
    void SelectProfileMenu(const D2D1_RECT_F& rect);
    void SyncProfiles();
    void ResetControl(D2DControl& c);
    bool HandleProfileRequests();
    HWND m_edit = nullptr;
    bool m_renamingDisplay = false;
    D2D1_RECT_F m_displayRect = {};
    D2DControl* m_editControl = nullptr;
    HFONT m_editFont = nullptr;
    HBRUSH m_editBrush = CreateSolidBrush(RGB(18, 19, 22));
    std::wstring m_error;
    bool m_pending = false;
    int m_keyboardIndex = -1;
    float m_editOriginal = 0;
    bool m_editAdjusted = false;
    // Popups and file dialogs run nested message loops in which the player
    // may destroy this window. Callers check this flag before touching members.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    float Scale() const
    {
        return float(GetDpiForWindow(m_hwnd)) / 96.f;
    }

    void Changed();
    void Preview();
    void SelectDisplayProfile(const D2D1_RECT_F& rect);
    void SelectQualityPreset(const D2D1_RECT_F& rect);

    // Quality measurement runs on its own thread; timer 19 polls it.
    struct Measurement
    {
        std::atomic<float> progress = 0;
        std::atomic<bool> cancel = false, done = false;
        BenchmarkResult result;
        std::wstring profile;
    };

    std::shared_ptr<Measurement> m_measurement;
    std::thread m_measureThread;
    void StartMeasurement();
    void FinishMeasurement();
    void ResetAll();
    // Short notice at the bottom of the window, visible with a closed sidebar.
    std::wstring m_toast;
    // Quality origin, recomputed only when the settings change.
    PlaceboSettings m_qualitySettings = {};
    QualityOrigin m_quality;
    bool m_qualityKnown = false;

    QualityOrigin Quality()
    {
        if (!m_qualityKnown || memcmp(&m_qualitySettings, m_pSettings, sizeof(PlaceboSettings)))
        {
            m_qualitySettings = *m_pSettings;
            m_quality = CurrentQuality(*m_pSettings);
            m_qualityKnown = true;
        }
        return m_quality;
    }

    void ShowToast(const std::wstring& text);
    void DrawToast(D2D1_SIZE_F size);
    void DrawDeleteDialog(D2D1_SIZE_F size);
    void CloseDeleteDialog(bool confirm);
    void AdoptDisplayProfile(const std::wstring& id);
    void TransferIni(bool importing);
    HRESULT ApplyImported(const PlaceboSettings& candidate);
    void BeginEdit(D2DControl*, const D2D1_RECT_F&, bool renameDisplay = false);
    void EndEdit(bool accept = true);
    void EditChanged();
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    // Direct2D & DirectWrite core
    ComPtr<ID2D1Factory> m_pD2DFactory;
    ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
    ComPtr<IDWriteFactory> m_pDWriteFactory;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushBgApp;
    ComPtr<ID2D1SolidColorBrush> m_brushBgSidebar;
    ComPtr<ID2D1SolidColorBrush> m_brushBgCard;
    ComPtr<ID2D1SolidColorBrush> m_brushBgCardHeader;
    ComPtr<ID2D1SolidColorBrush> m_brushBorderCard;
    ComPtr<ID2D1SolidColorBrush> m_brushBorderSubtle;
    ComPtr<ID2D1SolidColorBrush> m_brushTextMain;
    ComPtr<ID2D1SolidColorBrush> m_brushTextMuted;
    ComPtr<ID2D1SolidColorBrush> m_brushTextDim;
    ComPtr<ID2D1SolidColorBrush> m_brushAccent;
    ComPtr<ID2D1SolidColorBrush> m_brushAccentHover;
    ComPtr<ID2D1SolidColorBrush> m_brushInputBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHoverBg;
    ComPtr<ID2D1SolidColorBrush> m_brushActiveBg;
    ComPtr<ID2D1SolidColorBrush> m_brushToggleOn;
    ComPtr<ID2D1SolidColorBrush> m_brushToggleOff;
    ComPtr<ID2D1SolidColorBrush> m_brushSliderTrack;
    ComPtr<ID2D1SolidColorBrush> m_brushSliderFill;
    ComPtr<ID2D1SolidColorBrush> m_brushSliderThumb;

    // Text formats
    ComPtr<IDWriteTextFormat> m_fmtTitle;
    ComPtr<IDWriteTextFormat> m_fmtSubtitle;
    ComPtr<IDWriteTextFormat> m_fmtNav;
    ComPtr<IDWriteTextFormat> m_fmtCardTitle;
    ComPtr<IDWriteTextFormat> m_fmtCardSubtitle;
    ComPtr<IDWriteTextFormat> m_fmtLabel;
    ComPtr<IDWriteTextFormat> m_fmtDesc;
    // Text fields: regular weight, drawn with GDI metrics like their edit control.
    ComPtr<IDWriteTextFormat> m_fmtTextField;
    ComPtr<IDWriteTextFormat> m_fmtValue;
    ComPtr<IDWriteTextFormat> m_fmtChoice;
    ComPtr<IDWriteTextFormat> m_fmtRange;
    ComPtr<IDWriteTextFormat> m_fmtIcon;

    // Layout & State
    int m_activePage = 0;
    int m_activeCard = 0;
    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    int m_selectedShader = 0, m_shaderOffset = 0;
    int m_scrollDrag = 0;
    float m_scrollGrab = 0;
    D2D1_RECT_F m_scrollTrack = {}, m_scrollThumb = {}, m_shaderList = {};
    void DrawScrollbar(D2D1_RECT_F track, float offset, float extent, float viewport, int target);
    std::wstring m_searchQuery;
    bool m_searchFocused = false;
    D2D1_RECT_F m_searchBox = {};
    // Values as last loaded or saved; the Save button shows how many differ.
    PlaceboSettings m_saved;
    int m_unsaved = 0;
    void DrawSearchBox(D2D1_RECT_F box);
    void CountUnsaved();

    // Dragging / Active interaction
    D2DControl* m_pDraggingSlider = nullptr;
    D2D1_RECT_F m_draggingTrackRect = {};
    D2DControl* m_stepControl = nullptr;
    int m_stepDirection = 0;
    ULONGLONG m_stepStarted = 0;

    void StopNumberRepeat()
    {
        const bool wasRepeating = m_stepControl != nullptr;
        m_stepControl = nullptr;
        m_stepDirection = 0;
        if (m_hwnd)
            KillTimer(m_hwnd, 18);
        if (wasRepeating && m_hwnd && GetCapture() == m_hwnd)
            ReleaseCapture();
    }

    // Hit regions collected during render pass
    std::vector<HitRegion> m_hitRegions;

    // Pages model
    std::vector<D2DPage> m_pages;

    Direct2DHostWindow(HWND hwnd, PlaceboSettings* settings, bool standalone)
        : m_hwnd(hwnd), m_pSettings(settings), m_standalone(standalone)
    {
        m_accepted = m_applied = m_saved = m_base = *settings;
        const auto playing = ContentProfiles::PlayingProfile();
        m_selection = playing.empty() ? ContentProfiles::DefaultName : playing;
        m_editing = EditTarget();
        if (!m_editing.empty())
            LoadEditTarget();
        InitPages();
        InitD2D();
    }

    ~Direct2DHostWindow()
    {
        *m_alive = false;
        // The playing video returns to the profile its folder gives it.
        if (m_previewing)
        {
            ContentProfiles::StopPreview();
            if (m_preview)
                m_preview(m_base);
        }
        if (m_measureThread.joinable())
        {
            m_measurement->cancel = true;
            m_measureThread.join();
        }
        if (m_editFont)
            DeleteObject(m_editFont);
        if (m_editBrush)
            DeleteObject(m_editBrush);
        DiscardDeviceResources();
    }

    void InitPages();
    HRESULT InitD2D();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();
    void Render();
    void OnLButtonDown(float x, float y);
    void OnMouseMove(float x, float y, WPARAM flags);
    void OnLButtonUp(float x, float y);
    void OnMouseWheel(short delta, float x, float y);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

// Custom filter role shown in the Scaling / Custom Filters tab. Menu state
// only; it selects which of the five identical role cards is edited.
static int g_customRole = 0;
static const wchar_t* const CustomRoleCard = L"Custom Filter Role";

static std::vector<D2DPage> PreviewPages(PlaceboSettings& settings)
{
    auto source = BuildD2DPages(settings);
    const wchar_t* names[] = {L"General", L"Processing", L"Scaling", L"Color", L"HDR", L"Shaders"};
    // Glyphs every Segoe MDL2 version has; Windows 10 lacks newer ones.
    const wchar_t* icons[] = {L"\xE713", L"\xE9E9", L"\xE9A6", L"\xE790", L"\xE793", L"\xE81E"};
    const wchar_t* help[] = {L"Output precision, display mode switching, background and image analysis.",
                             L"Image cleanup, field processing, dithering and black bar handling.",
                             L"Built-in scaling filters, RTX Video Super Resolution and custom filters.",
                             L"Picture controls and display calibration.",
                             L"HDR output, tone mapping, gamut mapping and tonal zones.",
                             L"User shader chain."};
    std::vector<D2DPage> result;
    for (size_t i = 0; i < 6; ++i)
        result.push_back({names[i], icons[i], names[i], help[i], {}});
    bool roleAdded = false;
    for (auto& page : source)
        for (auto& card : page.cards)
        {
            int target = 0;
            if (page.name == L"General")
                target = 0;
            else if (page.name == L"Processing" || page.name == L"Black Bars / Zoom")
                target = 1;
            else if (page.name == L"Scaling")
                target = 2;
            else if (page.name == L"Color Adjustment" || page.name == L"Color Management")
                target = 3;
            else if (page.name == L"HDR & Color Mapping")
                target = 4;
            else if (page.name == L"Shaders")
                target = 5;
            if (card.title.starts_with(L"Custom:") && !roleAdded)
            {
                D2DCard role{
                    CustomRoleCard, L"The custom filter settings below apply to the selected scaler role.", {}};
                D2DControl c;
                c.name = L"Role";
                c.desc = L"Scaler role whose custom filter is edited.";
                c.kind = ControlKind::Choice;
                for (int i = 0; i < int(std::size(ScalingLabels)); ++i)
                    c.choices.push_back({i, ScalingLabels[i]});
                c.get = [] { return float(g_customRole); };
                c.set = [](float v) { g_customRole = int(v); };
                c.defaultValue = [] { return 0.f; };
                c.reset = [] { g_customRole = 0; };
                role.controls.push_back(std::move(c));
                result[target].cards.push_back(std::move(role));
                roleAdded = true;
            }
            result[target].cards.push_back(std::move(card));
        }
    {
        D2DCard look{L"Menu Appearance", L"Colours of this settings window. Applies immediately.", {}};
        auto choice = [](const wchar_t* name, const wchar_t* desc, const auto& names, int* field, bool theme)
        {
            D2DControl c;
            c.name = name;
            c.desc = desc;
            c.kind = ControlKind::Choice;
            for (int i = 0; i < int(std::size(names)); ++i)
                c.choices.push_back({i, names[i]});
            c.get = [field] { return float(*field); };
            c.set = [theme](float v)
            { theme ? SetMenuAppearance(int(v), g_menuAccent) : SetMenuAppearance(g_menuTheme, int(v)); };
            c.defaultValue = [] { return 0.f; };
            c.reset = [theme] { theme ? SetMenuAppearance(0, g_menuAccent) : SetMenuAppearance(g_menuTheme, 0); };
            return c;
        };
        look.controls.push_back(choice(L"Theme", L"", MenuThemeNames, &g_menuTheme, true));
        look.controls.push_back(choice(L"Accent Color", L"", MenuAccentNames, &g_menuAccent, false));
        {
            // Automatic follows the Windows display language; embedded
            // translations are listed by their own name ("Deutsch").
            auto languages = std::make_shared<std::vector<std::wstring>>(std::vector<std::wstring>{L"", L"en"});
            for (const auto& name : MenuTranslation::Embedded())
                languages->push_back(name);
            D2DControl c;
            c.name = L"Language";
            c.desc = L"Language of this settings window. Automatic follows Windows; missing translations show English.";
            c.kind = ControlKind::Choice;
            c.choices.push_back({0, L"Automatic (Windows)"});
            c.choices.push_back({1, L"English"});
            for (int i = 2; i < int(languages->size()); ++i)
                c.choices.push_back({i, MenuTranslation::NativeName((*languages)[i])});
            c.get = [languages]
            {
                const auto current = g_menuLanguage == L"en" ? g_menuLanguage : MenuTranslation::Upper(g_menuLanguage);
                const auto found = std::find(languages->begin(), languages->end(), current);
                return float(found == languages->end() ? 0 : found - languages->begin());
            };
            c.set = [languages](float v)
            { SetMenuLanguage((*languages)[std::clamp(int(v), 0, int(languages->size()) - 1)]); };
            c.defaultValue = [] { return 0.f; };
            c.reset = [] { SetMenuLanguage(L""); };
            look.controls.push_back(std::move(c));
        }
        result[0].cards.push_back(std::move(look));
    }
    result.push_back(ProfilePage::Build(settings));
    return result;
}

static std::wstring TabGroup(const D2DPage& page, const D2DCard& card)
{
    const auto& title = card.title;
    if (!card.tab.empty())
        return card.tab;
    if (page.name == L"General")
        return title == L"Background & Transparency" ? L"Background"
               : title == L"Menu Appearance"         ? L"Themes"
                                                     : L"Output";
    if (page.name == L"Processing")
        return title == L"Debanding" || title == L"Adaptive Sharpen" ? L"Image Cleanup"
               : title == L"Detection & Zoom"                        ? L"Black Bars / Zoom"
                                                                     : L"Deinterlacing & Dithering";
    if (page.name == L"Scaling")
        return title.starts_with(L"Custom:") || title == CustomRoleCard ? L"Custom Filters" : L"Filters";
    if (page.name == L"Color")
        return title == L"Picture Controls" ? L"Picture Controls" : L"Calibration";
    if (page.name == L"HDR")
    {
        if (title == L"HDR Output" || title == L"HDR Peak Detection")
            return L"Output & Peak Detection";
        if (title == L"Shadows" || title == L"Midtones" || title == L"Highlights")
            return L"Tone Controls";
        if (title == L"Gamut Mapping Constants" || title == L"Mapping LUT Quality" || title == L"Mapping Visualization")
            return L"Gamut Mapping";
        return L"Tone Mapping";
    }
    return title;
}

// Only the custom filter card of the selected role is shown.
static bool HiddenCustomRole(const D2DPage& page, const D2DCard& card)
{
    if (!card.title.starts_with(L"Custom:"))
        return false;
    int index = 0;
    for (const auto& other : page.cards)
    {
        if (&other == &card)
            return index != g_customRole;
        index += other.title.starts_with(L"Custom:");
    }
    return false;
}

static std::vector<std::wstring> CardTabs(const D2DPage& page)
{
    std::vector<std::wstring> result;
    for (const auto& card : page.cards)
    {
        const auto group = TabGroup(page, card);
        if (std::find(result.begin(), result.end(), group) == result.end())
            result.push_back(group);
    }
    return result;
}

void Direct2DHostWindow::InitPages()
{
    StopNumberRepeat();
    m_pages = PreviewPages(*m_pSettings);
    TranslatePages(m_pages);
    CountUnsaved();
}

HRESULT Direct2DHostWindow::InitD2D()
{
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_pD2DFactory.GetAddressOf());
    if (FAILED(hr))
        return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf()));
    if (FAILED(hr))
        return hr;

    const wchar_t* fontName = L"Segoe UI";

    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       24.0f,
                                       L"en-us",
                                       m_fmtTitle.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       13.0f,
                                       L"en-us",
                                       m_fmtSubtitle.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       14.0f,
                                       L"en-us",
                                       m_fmtNav.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       16.0f,
                                       L"en-us",
                                       m_fmtCardTitle.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       12.0f,
                                       L"en-us",
                                       m_fmtCardSubtitle.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       14.0f,
                                       L"en-us",
                                       m_fmtLabel.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       12.0f,
                                       L"en-us",
                                       m_fmtDesc.GetAddressOf());
    // Text fields match the choice boxes beside them.
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       14.0f,
                                       L"en-us",
                                       m_fmtTextField.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       13.0f,
                                       L"en-us",
                                       m_fmtValue.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       14.f,
                                       L"en-us",
                                       m_fmtChoice.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       12.f,
                                       L"en-us",
                                       m_fmtRange.GetAddressOf());
    // Windows 11 icons where installed; Segoe MDL2 uses the same code points.
    const wchar_t* iconFont = L"Segoe MDL2 Assets";
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL fluent = FALSE;
    if (SUCCEEDED(m_pDWriteFactory->GetSystemFontCollection(fonts.GetAddressOf())) &&
        SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &fluent)) && fluent)
        iconFont = L"Segoe Fluent Icons";
    m_pDWriteFactory->CreateTextFormat(iconFont,
                                       nullptr,
                                       fluent ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       20.0f,
                                       L"en-us",
                                       m_fmtIcon.GetAddressOf());

    if (!m_fmtTitle || !m_fmtSubtitle || !m_fmtNav || !m_fmtCardTitle || !m_fmtCardSubtitle || !m_fmtLabel ||
        !m_fmtDesc || !m_fmtTextField || !m_fmtValue || !m_fmtChoice || !m_fmtRange || !m_fmtIcon)
        return E_FAIL;
    if (m_fmtDesc)
        m_fmtDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (m_fmtValue)
    {
        m_fmtValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtValue->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    m_fmtChoice->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_fmtChoice->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_fmtRange->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_fmtRange->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    // One line like the single-line edit control.
    m_fmtTextField->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_fmtTextField->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_fmtIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_fmtIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return S_OK;
}

HRESULT Direct2DHostWindow::CreateDeviceResources()
{
    if (m_pRenderTarget)
        return S_OK;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    if (size.width == 0 || size.height == 0)
        size = D2D1::SizeU(1024, 720);

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(m_hwnd, size);

    if (!m_pD2DFactory)
        return E_FAIL;
    HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(rtp, hrtp, m_pRenderTarget.GetAddressOf());
    if (FAILED(hr))
        return hr;
    m_pRenderTarget->SetDpi(float(GetDpiForWindow(m_hwnd)), float(GetDpiForWindow(m_hwnd)));

    // Solid color brushes
    const auto p = CurrentMenuPalette();
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.app), m_brushBgApp.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.sidebar), m_brushBgSidebar.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.card), m_brushBgCard.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.cardHeader), m_brushBgCardHeader.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.border), m_brushBorderCard.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(p.light ? D2D1::ColorF(0, 0, 0, .07f) : D2D1::ColorF(1, 1, 1, .05f),
                                           m_brushBorderSubtle.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.textMain), m_brushTextMain.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.textMuted), m_brushTextMuted.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.textDim), m_brushTextDim.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.accent), m_brushAccent.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.accentHover), m_brushAccentHover.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.input), m_brushInputBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.accent, .18f), m_brushHoverBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.activeBg), m_brushActiveBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.toggleOn), m_brushToggleOn.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.toggleOff), m_brushToggleOff.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.toggleOff), m_brushSliderTrack.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.accent), m_brushSliderFill.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(p.sliderThumb), m_brushSliderThumb.GetAddressOf());

    if (!m_brushBgApp || !m_brushBgSidebar || !m_brushBgCard || !m_brushBgCardHeader || !m_brushBorderCard ||
        !m_brushBorderSubtle || !m_brushTextMain || !m_brushTextMuted || !m_brushTextDim || !m_brushAccent ||
        !m_brushAccentHover || !m_brushInputBg || !m_brushHoverBg || !m_brushActiveBg || !m_brushToggleOn ||
        !m_brushToggleOff || !m_brushSliderTrack || !m_brushSliderFill || !m_brushSliderThumb)
    {
        DiscardDeviceResources();
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

void Direct2DHostWindow::DiscardDeviceResources()
{
    m_pRenderTarget.Reset();
    m_brushBgApp.Reset();
    m_brushBgSidebar.Reset();
    m_brushBgCard.Reset();
    m_brushBgCardHeader.Reset();
    m_brushBorderCard.Reset();
    m_brushBorderSubtle.Reset();
    m_brushTextMain.Reset();
    m_brushTextMuted.Reset();
    m_brushTextDim.Reset();
    m_brushAccent.Reset();
    m_brushAccentHover.Reset();
    m_brushInputBg.Reset();
    m_brushHoverBg.Reset();
    m_brushActiveBg.Reset();
    m_brushToggleOn.Reset();
    m_brushToggleOff.Reset();
    m_brushSliderTrack.Reset();
    m_brushSliderFill.Reset();
    m_brushSliderThumb.Reset();
}

void Direct2DHostWindow::Render()
{
    if (!m_fmtTitle || !m_fmtSubtitle || !m_fmtNav || !m_fmtCardTitle || !m_fmtCardSubtitle || !m_fmtLabel ||
        !m_fmtDesc || !m_fmtTextField || !m_fmtValue || !m_fmtIcon)
        return;
    // A new theme or accent colour rebuilds all brushes.
    // A new menu language rebuilds the translated settings model.
    if (m_languageVersion != g_menuLanguageVersion || m_profileVersion != ProfilePage::g_version)
    {
        m_languageVersion = g_menuLanguageVersion;
        m_profileVersion = ProfilePage::g_version;
        InitPages();
        SyncProfiles();
        // Controls and tabs may have moved: drop highlights left at old places.
        m_hoverRect = m_previousHoverRect = m_pressRect = {};
        m_hoverTab = -1;
        // Profiles come and go: open a requested profile tab, keep the index valid.
        if (m_activePage >= 0 && m_activePage < int(m_pages.size()) && m_pages[m_activePage].name == L"Profiles")
        {
            const auto tabs = CardTabs(m_pages[m_activePage]);
            const auto wanted = std::find(tabs.begin(), tabs.end(), ProfilePage::g_selectTab);
            if (wanted != tabs.end())
                m_activeCard = int(wanted - tabs.begin());
            else if (m_activeCard >= int(tabs.size()))
                m_activeCard = 0;
            m_previousCard = -1;
        }
        ProfilePage::g_selectTab.clear();
    }
    if (m_paletteVersion != g_menuPaletteVersion)
    {
        DiscardDeviceResources();
        m_paletteVersion = g_menuPaletteVersion;
        if (m_editBrush)
            DeleteObject(m_editBrush);
        m_editBrush = CreateSolidBrush(MenuColorRef(CurrentMenuPalette().input));
        // The window around the menu (standalone window or the player's
        // property frame) follows the theme as well.
        const HWND root = GetAncestor(m_hwnd, GA_ROOT);
        ApplyMenuFrameColors(root);
        if (!m_standalone)
            RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
    if (FAILED(CreateDeviceResources()))
        return;

    m_hitRegions.clear();
    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    D2D1_SIZE_F size = m_pRenderTarget->GetSize();
    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear(D2D1::ColorF(CurrentMenuPalette().app));

    const float sidebarW = 60.f;

    // Draw the expanded navigation after the settings so it overlays the content.
    auto drawNavigation = [&]()
    {
        const float sidebarW = SettingsLayout::SidebarWidth;
        m_hitRegions.clear();
        // 1. Sidebar Background
        D2D1_RECT_F sidebarRect = D2D1::RectF(0, 0, sidebarW, size.height);
        const auto sidebarShape = D2D1::RoundedRect(sidebarRect, SidebarCornerRadius, SidebarCornerRadius);
        if (!DrawFrostedSidebar(m_pRenderTarget.Get(), sidebarW, size.height))
            m_pRenderTarget->FillRoundedRectangle(sidebarShape, m_brushBgSidebar.Get());
        m_pRenderTarget->DrawRoundedRectangle(sidebarShape, m_brushBorderCard.Get(), 1.f);

        // The menu button stays where the rail shows it and closes the drawer.
        for (int line = 0; line < 3; ++line)
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(20, 20.f + line * 6), D2D1::Point2F(40, 20.f + line * 6), m_brushTextMain.Get(), 2);
        m_pRenderTarget->DrawText(
            L"FrankyRenderer", 14, m_fmtNav.Get(), D2D1::RectF(60, 17, sidebarW - 16, 40), m_brushTextMain.Get());

        // 3. Navigation List
        float navY = 64.0f;
        for (size_t i = 0; i < m_pages.size(); ++i)
        {
            D2D1_RECT_F itemRect = D2D1::RectF(12, navY, sidebarW - 12, navY + 28);
            D2D1_ROUNDED_RECT roundedItem = D2D1::RoundedRect(itemRect, 6.0f, 6.0f);

            bool hasMatches = false;
            if (!m_searchQuery.empty())
            {
                for (const auto& card : m_pages[i].cards)
                {
                    for (const auto& c : card.controls)
                    {
                        if (ControlMatches(c, card, m_pages[i], m_searchQuery))
                        {
                            hasMatches = true;
                            break;
                        }
                    }
                    if (hasMatches)
                        break;
                }
            }

            bool isSearching = !m_searchQuery.empty();
            bool isActive = !isSearching && (static_cast<int>(i) == m_activePage);

            if (isActive)
            {
                m_pRenderTarget->FillRoundedRectangle(roundedItem, m_brushActiveBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(roundedItem, m_brushBorderSubtle.Get(), 1.0f);
            }
            else if (int(i) == m_hoverPage || (isSearching && hasMatches))
            {
                m_brushHoverBg->SetOpacity(int(i) == m_hoverPage ? m_hoverProgress : 1.f);
                m_pRenderTarget->FillRoundedRectangle(roundedItem, m_brushHoverBg.Get());
                m_brushHoverBg->SetOpacity(1.f);
            }

            ID2D1SolidColorBrush* textBrush = m_brushTextMuted.Get();
            if (isActive)
                textBrush = m_brushTextMain.Get();
            else if (int(i) == m_hoverPage)
                textBrush = m_brushAccentHover.Get();
            else if (isSearching && !hasMatches)
                textBrush = m_brushTextDim.Get();
            else if (isSearching && hasMatches)
                textBrush = m_brushTextMain.Get();

            m_pRenderTarget->DrawText(m_pages[i].icon.c_str(),
                                      1,
                                      m_fmtIcon.Get(),
                                      D2D1::RectF(21, navY + 3, 43, navY + 27),
                                      isActive ? m_brushAccent.Get() : m_brushTextMuted.Get());
            D2D1_RECT_F textRect = D2D1::RectF(48, navY + 6, sidebarW - 12, navY + 26);
            const auto pageName = Tr(m_pages[i].name);
            m_pRenderTarget->DrawText(pageName.c_str(), (UINT32)pageName.length(), m_fmtNav.Get(), textRect, textBrush);

            HitRegion hitNav;
            hitNav.type = HitRegion::NavItem;
            hitNav.rect = itemRect;
            hitNav.pageIndex = static_cast<int>(i);
            m_hitRegions.push_back(hitNav);

            navY += 30.0f;
        }

        // 4. Sidebar Bottom Action Buttons
        float btnY = size.height - 48.0f;
        const float transferY = btnY;
        for (int i = 0; i < 2; ++i)
        {
            const float buttonWidth = (sidebarW - 40.f) / 2.f;
            const float left = i ? sidebarW / 2.f + 4.f : 16.f;
            const auto rect = D2D1::RectF(left, transferY, left + buttonWidth, transferY + 34.f);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), m_brushInputBg.Get());
            m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), m_brushBorderCard.Get(), 1.f);
            auto text = rect;
            text.top += 8;
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const auto transferLabel = Tr(i ? L"Export" : L"Import");
            m_pRenderTarget->DrawText(
                transferLabel.c_str(), UINT32(transferLabel.size()), m_fmtNav.Get(), text, m_brushTextMain.Get());
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            HitRegion hit;
            hit.type = i ? HitRegion::ExportBtn : HitRegion::ImportBtn;
            hit.rect = rect;
            m_hitRegions.push_back(hit);
        }
        if (!m_error.empty())
        {
            const float statusBottom = transferY - 8.f;
            const auto r = D2D1::RectF(16, statusBottom - 57.f, sidebarW - 16, statusBottom);
            const auto status = Tr(m_error);
            m_pRenderTarget->DrawText(status.c_str(),
                                      (UINT32)status.size(),
                                      m_fmtDesc.Get(),
                                      r,
                                      m_brushAccentHover.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    };

    // 5. Content Area
    const float contentX = sidebarW + 20.0f;
    const float contentW = size.width - contentX - 24.0f;

    m_pRenderTarget->PushAxisAlignedClip(D2D1::RectF(sidebarW, 0, size.width, size.height),
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    struct RenderCardItem
    {
        const D2DCard* pCard = nullptr;
        std::wstring pageName;
        std::vector<const D2DControl*> visibleControls;
    };

    std::vector<RenderCardItem> cardsToRender;

    bool isSearching = !m_searchQuery.empty();
    float currentY = 16.0f;

    // Search stays at the right edge; page reset sits on the row below it.
    const float searchRight = contentX + contentW;
    auto measure = [&](const std::wstring& text)
    {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                text.c_str(), UINT32(text.size()), m_fmtNav.Get(), 400, 40, layout.GetAddressOf())) &&
            SUCCEEDED(layout->GetMetrics(&metrics)))
            return std::ceil(metrics.widthIncludingTrailingWhitespace);
        return 120.f;
    };
    // The quality preset stays visible on every page; it shows Custom once a
    // setting it covers was changed by hand. In a narrow window the page title
    // keeps its room: the caption goes first, then the search narrows.
    const auto origin = Quality();
    auto presetName = Tr(origin.preset >= 0 ? QualityPresetNames[origin.preset]
                         : origin.measured  ? L"Measured"
                                            : L"Custom");
    if (m_measurement)
        presetName = Tr(L"Measuring…") + L" " + std::to_wstring(int(m_measurement->progress * 100)) + L"%";
    auto presetLabel = Tr(L"Quality") + L": " + presetName;
    // The profile the menu shows: at first the one of the playing video.
    const auto profileName =
        !_wcsicmp(m_selection.c_str(), ContentProfiles::DefaultName) ? Tr(L"Default") : m_selection;
    auto profileLabel = Tr(L"Profile") + L": " + profileName;
    const float saveWidth = m_standalone ? 114.f : 0.f, titleRoom = 200.f;
    float searchWidth = std::min(260.f, contentW);
    float presetWidth = measure(presetLabel) + 50, profileWidth = measure(profileLabel) + 50;
    if (contentW - searchWidth - saveWidth - presetWidth - profileWidth - 20 < titleRoom)
    {
        presetLabel = presetName;
        presetWidth = measure(presetLabel) + 50;
        profileLabel = profileName;
        profileWidth = measure(profileLabel) + 50;
    }
    searchWidth =
        std::min(searchWidth, std::max(180.f, contentW - saveWidth - presetWidth - profileWidth - 20 - titleRoom));
    float headerActionsLeft = searchRight - searchWidth;
    // Left edge of Reset All / Reset Page; the first row of tabs ends before it.
    float resetLeft = searchRight;
    if (!isSearching && m_activePage >= 0 && m_activePage < int(m_pages.size()))
    {
        // The shader page shows one shader at a time; reset only that one.
        const auto resetText = Tr(m_pages[m_activePage].name == L"Shaders" ? L"Reset Shader" : L"Reset Page");
        const wchar_t* resetLabel = resetText.c_str();
        // Sized to its label so the hover highlight hugs the text.
        float labelWidth = 80.f;
        ComPtr<IDWriteTextLayout> measured;
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                resetLabel, UINT32(wcslen(resetLabel)), m_fmtNav.Get(), 400, 40, measured.GetAddressOf())) &&
            SUCCEEDED(measured->GetMetrics(&metrics)))
            labelWidth = std::ceil(metrics.widthIncludingTrailingWhitespace);
        // One row below the header controls, with room between the two rows.
        const auto reset = D2D1::RectF(searchRight - labelWidth - 24, currentY + 48, searchRight, currentY + 76);
        const auto paragraph = m_fmtNav->GetParagraphAlignment();
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(
            resetLabel, UINT32(wcslen(resetLabel)), m_fmtNav.Get(), reset, m_brushTextMuted.Get());
        m_fmtNav->SetParagraphAlignment(paragraph);
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        HitRegion resetHit;
        resetHit.type = HitRegion::ResetBtn;
        resetHit.rect = reset;
        m_hitRegions.push_back(resetHit);
        // Everything for this display, e.g. to start over.
        const auto allText = Tr(L"Reset All");
        float allWidth = 80.f;
        ComPtr<IDWriteTextLayout> allLayout;
        if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                allText.c_str(), UINT32(allText.size()), m_fmtNav.Get(), 400, 40, allLayout.GetAddressOf())) &&
            SUCCEEDED(allLayout->GetMetrics(&metrics)))
            allWidth = std::ceil(metrics.widthIncludingTrailingWhitespace);
        const auto all = D2D1::RectF(reset.left - allWidth - 28, reset.top, reset.left - 4, reset.bottom);
        resetLeft = all.left;
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(allText.c_str(), UINT32(allText.size()), m_fmtNav.Get(), all, m_brushTextMuted.Get());
        m_fmtNav->SetParagraphAlignment(paragraph);
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        HitRegion allHit;
        allHit.type = HitRegion::ResetAllBtn;
        allHit.rect = all;
        m_hitRegions.push_back(allHit);
    }
    // The standalone settings window edits a draft; Save keeps it and shows
    // how many settings differ from what was last saved.
    if (m_standalone)
    {
        wchar_t label[64] = {};
        wcsncpy_s(label, Tr(L"Saved").c_str(), _TRUNCATE);
        if (m_unsaved)
            swprintf_s(label, Tr(L"Save (%d)").c_str(), m_unsaved);
        const auto save = D2D1::RectF(headerActionsLeft - 114, currentY, headerActionsLeft - 10, currentY + 32);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(save, 5, 5),
                                              m_unsaved ? m_brushActiveBg.Get() : m_brushInputBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(save, 5, 5), m_unsaved ? m_brushAccent.Get() : m_brushBorderCard.Get(), 1);
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(label,
                                  UINT32(wcslen(label)),
                                  m_fmtNav.Get(),
                                  D2D1::RectF(save.left, save.top + 8, save.right, save.bottom),
                                  m_unsaved ? m_brushTextMain.Get() : m_brushTextDim.Get());
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        HitRegion saveHit;
        saveHit.type = HitRegion::ApplyBtn;
        saveHit.rect = save;
        m_hitRegions.push_back(saveHit);
        headerActionsLeft = save.left - 10;
    }
    {
        const auto& label = presetLabel;
        const auto box = D2D1::RectF(headerActionsLeft - presetWidth, currentY, headerActionsLeft - 10, currentY + 32);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 5, 5), m_brushInputBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 5, 5), m_brushBorderCard.Get(), 1);
        const auto paragraph = m_fmtNav->GetParagraphAlignment();
        m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(label.c_str(),
                                  UINT32(label.size()),
                                  m_fmtNav.Get(),
                                  D2D1::RectF(box.left + 12, box.top, box.right - 26, box.bottom),
                                  m_brushTextMain.Get(),
                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_fmtNav->SetParagraphAlignment(paragraph);
        const float x = box.right - 16, y = box.top + 15;
        m_pRenderTarget->DrawLine(D2D1::Point2F(x - 4, y - 2), D2D1::Point2F(x, y + 2), m_brushTextMuted.Get(), 1.6f);
        m_pRenderTarget->DrawLine(D2D1::Point2F(x, y + 2), D2D1::Point2F(x + 4, y - 2), m_brushTextMuted.Get(), 1.6f);
        HitRegion presetHit;
        presetHit.type = HitRegion::QualityPresetBtn;
        presetHit.rect = box;
        m_hitRegions.push_back(presetHit);
        headerActionsLeft = box.left - 10;
    }
    {
        // Outlined in the accent colour while the menu edits a profile.
        const auto box = D2D1::RectF(headerActionsLeft - profileWidth, currentY, headerActionsLeft - 10, currentY + 32);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 5, 5), m_brushInputBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 5, 5),
                                              m_editing.empty() ? m_brushBorderCard.Get() : m_brushAccent.Get(),
                                              m_editing.empty() ? 1.f : 1.5f);
        const auto paragraph = m_fmtNav->GetParagraphAlignment();
        m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(profileLabel.c_str(),
                                  UINT32(profileLabel.size()),
                                  m_fmtNav.Get(),
                                  D2D1::RectF(box.left + 12, box.top, box.right - 26, box.bottom),
                                  m_brushTextMain.Get(),
                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_fmtNav->SetParagraphAlignment(paragraph);
        const float x = box.right - 16, y = box.top + 15;
        m_pRenderTarget->DrawLine(D2D1::Point2F(x - 4, y - 2), D2D1::Point2F(x, y + 2), m_brushTextMuted.Get(), 1.6f);
        m_pRenderTarget->DrawLine(D2D1::Point2F(x, y + 2), D2D1::Point2F(x + 4, y - 2), m_brushTextMuted.Get(), 1.6f);
        HitRegion profileHit;
        profileHit.type = HitRegion::ProfileSelectBtn;
        profileHit.rect = box;
        m_hitRegions.push_back(profileHit);
        headerActionsLeft = box.left - 10;
    }
    DrawSearchBox(D2D1::RectF(searchRight - searchWidth, currentY, searchRight, currentY + 32));

    if (!isSearching)
    {
        if (m_activePage >= 0 && m_activePage < static_cast<int>(m_pages.size()))
        {
            const auto& page = m_pages[m_activePage];
            const auto tabs = CardTabs(page);

            // Page Header
            D2D1_RECT_F titleRect = D2D1::RectF(contentX, currentY, headerActionsLeft - 16, currentY + 28.0f);
            const auto pageTitle = Tr(page.title);
            m_pRenderTarget->DrawText(
                pageTitle.c_str(), (UINT32)pageTitle.length(), m_fmtTitle.Get(), titleRect, m_brushTextMain.Get());
            // A subtle line between the header controls and the tabs / reset row.
            const auto lineOpacity = m_brushBorderCard->GetOpacity();
            m_brushBorderCard->SetOpacity(.7f);
            m_pRenderTarget->DrawLine(D2D1::Point2F(contentX, currentY + 41.5f),
                                      D2D1::Point2F(contentX + contentW, currentY + 41.5f),
                                      m_brushBorderCard.Get(),
                                      1.f);
            m_brushBorderCard->SetOpacity(lineOpacity);

            currentY += 52.f;
            if (page.name != L"Shaders" && tabs.size() > 1)
            {
                const float firstTabY = currentY;
                float tabX = contentX;
                for (size_t i = 0; i < tabs.size(); ++i)
                {
                    auto label = Tr(tabs[i]);
                    if (label.starts_with(L"Custom: "))
                        label.erase(0, 8);
                    const auto suffix = label.find(L" (Chroma / Alpha)");
                    if (suffix != std::wstring::npos)
                        label.erase(suffix);
                    // Profile tabs carry a close mark; it opens a dialog that asks first.
                    const bool closable = page.name == L"Profiles" && tabs[i] != ProfilePage::NewTab &&
                                          tabs[i] != ProfilePage::DefaultTab;
                    ComPtr<IDWriteTextLayout> measured;
                    DWRITE_TEXT_METRICS metrics = {};
                    float textWidth = float(label.size()) * 8.f;
                    if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                            label.c_str(), UINT32(label.size()), m_fmtNav.Get(), 2000, 30, measured.GetAddressOf())))
                    {
                        measured->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        if (SUCCEEDED(measured->GetMetrics(&metrics)))
                            textWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                    const float closeWidth = closable ? 22.f : 0.f;
                    // The tab adding a profile is a small square with a drawn plus.
                    const bool adds = page.name == L"Profiles" && tabs[i] == ProfilePage::NewTab;
                    const float width =
                        adds ? 40.f : std::min(contentW, std::max(100.f, std::ceil(textWidth) + 32.f + closeWidth));
                    // Tabs share the reset row, keeping the reset buttons free; their
                    // width depends on the menu language.
                    const float tabRight = currentY == firstTabY ? resetLeft - 8.f : contentX + contentW;
                    if (tabX + width > tabRight && (tabX > contentX || currentY == firstTabY))
                    {
                        currentY += 38.f;
                        tabX = contentX;
                    }
                    const auto tab = D2D1::RectF(tabX, currentY, tabX + width, currentY + 30.f);
                    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tab, 8, 8), m_brushInputBg.Get());
                    const float selectedOpacity = int(i) == m_activeCard     ? m_contentProgress
                                                  : int(i) == m_previousCard ? 1.f - m_contentProgress
                                                                             : 0.f;
                    if (selectedOpacity > 0)
                    {
                        m_brushActiveBg->SetOpacity(selectedOpacity);
                        m_brushAccent->SetOpacity(selectedOpacity);
                        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tab, 8, 8), m_brushActiveBg.Get());
                        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tab, 8, 8), m_brushAccent.Get(), 1);
                        m_brushActiveBg->SetOpacity(1);
                        m_brushAccent->SetOpacity(1);
                    }
                    // Centre the label in the tab, which is at least 100 pixels wide.
                    const auto wrap = m_fmtNav->GetWordWrapping();
                    const auto paragraph = m_fmtNav->GetParagraphAlignment();
                    m_fmtNav->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    auto labelRect = tab;
                    labelRect.right -= closeWidth;
                    ID2D1Brush* textBrush = int(i) == m_activeCard ? m_brushAccentHover.Get() : m_brushTextMuted.Get();
                    if (adds)
                    {
                        const auto center = D2D1::Point2F((tab.left + tab.right) / 2, (tab.top + tab.bottom) / 2);
                        m_pRenderTarget->DrawLine(D2D1::Point2F(center.x - 6.f, center.y),
                                                  D2D1::Point2F(center.x + 6.f, center.y),
                                                  textBrush,
                                                  2.f);
                        m_pRenderTarget->DrawLine(D2D1::Point2F(center.x, center.y - 6.f),
                                                  D2D1::Point2F(center.x, center.y + 6.f),
                                                  textBrush,
                                                  2.f);
                    }
                    else
                        m_pRenderTarget->DrawText(label.c_str(),
                                                  UINT32(label.size()),
                                                  m_fmtNav.Get(),
                                                  labelRect,
                                                  textBrush,
                                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    if (closable)
                    {
                        const auto close =
                            D2D1::RectF(tab.right - 28.f, tab.top + 4.f, tab.right - 6.f, tab.bottom - 4.f);
                        // Drawn as two strokes: crisper and larger than a text glyph.
                        const auto center =
                            D2D1::Point2F((close.left + close.right) / 2, (close.top + close.bottom) / 2);
                        m_pRenderTarget->DrawLine(D2D1::Point2F(center.x - 4.5f, center.y - 4.5f),
                                                  D2D1::Point2F(center.x + 4.5f, center.y + 4.5f),
                                                  textBrush,
                                                  1.6f);
                        m_pRenderTarget->DrawLine(D2D1::Point2F(center.x - 4.5f, center.y + 4.5f),
                                                  D2D1::Point2F(center.x + 4.5f, center.y - 4.5f),
                                                  textBrush,
                                                  1.6f);
                        HitRegion closeHit;
                        closeHit.type = HitRegion::TabClose;
                        closeHit.pageIndex = int(i);
                        closeHit.rect = close;
                        m_hitRegions.push_back(closeHit);
                    }
                    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    m_fmtNav->SetParagraphAlignment(paragraph);
                    m_fmtNav->SetWordWrapping(wrap);
                    HitRegion hit;
                    hit.type = HitRegion::CardTab;
                    hit.pageIndex = int(i);
                    hit.rect = tab;
                    m_hitRegions.push_back(hit);
                    tabX += width + 7.f;
                }
                currentY += 42.f;
            }
            else
                currentY += 34.f;

            for (const auto& card : page.cards)
            {
                if (page.name == L"Shaders" && card.title != ShaderCatalog[m_selectedShader].label)
                    continue;
                if (page.name != L"Shaders" && TabGroup(page, card) != tabs[size_t(m_activeCard)])
                    continue;
                if (HiddenCustomRole(page, card))
                    continue;
                RenderCardItem item;
                item.pCard = &card;
                item.pageName = page.name;
                for (const auto& c : card.controls)
                    item.visibleControls.push_back(&c);
                cardsToRender.push_back(item);
            }
        }
    }
    else
    {
        // Global Search Header
        D2D1_RECT_F titleRect = D2D1::RectF(contentX, currentY, headerActionsLeft - 16, currentY + 28.0f);
        const auto searchTitle = Tr(L"Search Results");
        m_pRenderTarget->DrawText(
            searchTitle.c_str(), UINT32(searchTitle.size()), m_fmtTitle.Get(), titleRect, m_brushTextMain.Get());

        wchar_t hint[512] = {};
        swprintf_s(hint, Tr(L"Matching settings for: \"%s\"").c_str(), m_searchQuery.c_str());
        std::wstring queryHint = hint;
        D2D1_RECT_F subRect = D2D1::RectF(contentX, currentY + 34.0f, contentX + contentW, currentY + 54.0f);
        m_pRenderTarget->DrawText(
            queryHint.c_str(), (UINT32)queryHint.length(), m_fmtSubtitle.Get(), subRect, m_brushTextMuted.Get());

        currentY += 62.0f;

        // Gather matches from ALL pages
        for (const auto& page : m_pages)
        {
            for (const auto& card : page.cards)
            {
                RenderCardItem item;
                item.pCard = &card;
                item.pageName = page.name;
                for (const auto& c : card.controls)
                {
                    if (ControlMatches(c, card, page, m_searchQuery))
                    {
                        item.visibleControls.push_back(&c);
                    }
                }
                if (!item.visibleControls.empty())
                {
                    cardsToRender.push_back(item);
                }
            }
        }
    }

    if (isSearching && cardsToRender.empty())
    {
        D2D1_RECT_F emptyRect = D2D1::RectF(contentX, currentY + 40.0f, contentX + contentW, currentY + 70.0f);
        if (m_fmtNav)
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto noMatches = Tr(L"No settings matching this term found.");
        m_pRenderTarget->DrawText(
            noMatches.c_str(), UINT32(noMatches.size()), m_fmtNav.Get(), emptyRect, m_brushTextDim.Get());
        if (m_fmtNav)
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    const float viewportTop = currentY;
    const size_t fixedHitCount = m_hitRegions.size();
    m_pRenderTarget->PushAxisAlignedClip(D2D1::RectF(sidebarW, viewportTop, size.width, size.height),
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_pRenderTarget->PushLayer(D2D1::LayerParameters(D2D1::RectF(sidebarW, viewportTop, size.width, size.height),
                                                     nullptr,
                                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::Matrix3x2F::Identity(),
                                                     .3f + .7f * m_contentProgress),
                               nullptr);
    currentY -= m_scrollY;
    m_shaderList = {};
    if (!isSearching && m_pages[m_activePage].name == L"Shaders")
    {
        m_shaderList = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + 278);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_shaderList, 8, 8), m_brushBgCard.Get());
        for (int row = 0; row < 7; row++)
        {
            int pos = m_shaderOffset + row;
            if (pos >= ShaderCount)
                break;
            int id = m_pSettings->userShaders.order[pos];
            auto r =
                D2D1::RectF(contentX + 6, currentY + 6 + row * 32, contentX + contentW - 20, currentY + 38 + row * 32);
            if (id == m_selectedShader)
                m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4), m_brushActiveBg.Get());
            const bool on = m_pSettings->userShaders.shaders[id].enabled != 0;
            std::wstring text = ShaderCatalog[id].label;
            auto label = r;
            label.left += 12;
            label.top += 7;
            m_pRenderTarget->DrawText(text.c_str(),
                                      UINT32(text.size()),
                                      m_fmtNav.Get(),
                                      label,
                                      on ? m_brushTextMain.Get() : m_brushTextMuted.Get());
            // Small switch: enables the shader without selecting it.
            const auto track = D2D1::RectF(r.right - 46, r.top + 7, r.right - 14, r.top + 25);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(track, 9, 9),
                                                  on ? m_brushToggleOn.Get() : m_brushToggleOff.Get());
            m_pRenderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(on ? track.right - 9 : track.left + 9, track.top + 9), 6, 6),
                m_brushSliderThumb.Get());
            HitRegion toggle;
            toggle.type = HitRegion::ShaderToggle;
            // Hug the track so hover and press feedback draw a capsule.
            toggle.rect = D2D1::RectF(track.left - 3, track.top - 3, track.right + 3, track.bottom + 3);
            toggle.pageIndex = id;
            m_hitRegions.push_back(toggle);
            HitRegion h;
            h.type = HitRegion::ShaderItem;
            h.rect = r;
            h.pageIndex = id;
            m_hitRegions.push_back(h);
        }
        DrawScrollbar(D2D1::RectF(contentX + contentW - 14, currentY + 6, contentX + contentW - 4, currentY + 230),
                      float(m_shaderOffset),
                      float(ShaderCount),
                      7,
                      -2);
        auto button = D2D1::RectF(contentX + contentW - 110, currentY + 238, contentX + contentW - 16, currentY + 268);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(button, 4, 4), m_brushInputBg.Get());
        const auto allOff = Tr(L"All Off");
        const auto paragraph = m_fmtNav->GetParagraphAlignment();
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(allOff.c_str(), UINT32(allOff.size()), m_fmtNav.Get(), button, m_brushTextMain.Get());
        m_fmtNav->SetParagraphAlignment(paragraph);
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        auto& controls = m_pages[m_activePage].cards.front().controls;
        if (!controls.empty())
        {
            HitRegion h;
            h.type = HitRegion::Action;
            h.rect = button;
            h.pControl = &controls.front();
            m_hitRegions.push_back(h);
        }
        currentY += 292;
    }

    auto drawToggle = [&](D2DControl& ctrl, float x, float y)
    {
        const auto track = D2D1::RectF(x, y, x + 40.f, y + 22.f);
        const auto rounded = D2D1::RoundedRect(track, 11.f, 11.f);
        const float position = ToggleProgress(ctrl);
        const auto off = m_brushToggleOff->GetColor(), on = m_brushToggleOn->GetColor();
        ComPtr<ID2D1SolidColorBrush> trackBrush;
        const auto color = D2D1::ColorF(
            off.r + (on.r - off.r) * position, off.g + (on.g - off.g) * position, off.b + (on.b - off.b) * position);
        m_pRenderTarget->CreateSolidColorBrush(color, trackBrush.GetAddressOf());
        m_pRenderTarget->FillRoundedRectangle(rounded, trackBrush ? trackBrush.Get() : m_brushToggleOff.Get());
        m_pRenderTarget->DrawRoundedRectangle(rounded, m_brushBorderCard.Get(), 1.f);
        m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 11.f + 18.f * position, y + 11.f), 7.5f, 7.5f),
                                     m_brushSliderThumb.Get());
        if (ctrl.Enabled())
        {
            HitRegion hit;
            hit.type = HitRegion::Toggle;
            hit.rect = track;
            hit.pControl = &ctrl;
            m_hitRegions.push_back(hit);
        }
    };
    // In a profile, a setting is changed when it differs from Default: read
    // every control once with the profile and once with the normal settings.
    std::unordered_map<const D2DControl*, bool> profileChanged;
    if (!m_editing.empty())
    {
        const auto read = [&]
        {
            std::vector<std::wstring> values;
            for (const auto& page : m_pages)
                for (const auto& card : page.cards)
                    for (const auto& c : card.controls)
                        values.push_back(c.kind == ControlKind::Text ? (c.getText ? c.getText() : std::wstring())
                                         : c.get                     ? std::to_wstring(c.Value())
                                                                     : std::wstring());
            return values;
        };
        const auto now = read();
        const auto current = *m_pSettings;
        *m_pSettings = m_base;
        const auto normal = read();
        *m_pSettings = current;
        size_t i = 0;
        for (const auto& page : m_pages)
            for (const auto& card : page.cards)
                for (const auto& c : card.controls)
                {
                    profileChanged[&c] = page.name != L"Profiles" && now[i] != normal[i];
                    ++i;
                }
    }
    // A setting is changed when it differs from its default (in a profile:
    // from Default); only then its reset button is shown, and its name gets a dot.
    const auto modifiedFor = [&](const D2DControl& ctrl)
    {
        if (!m_editing.empty())
        {
            const auto found = profileChanged.find(&ctrl);
            return found != profileChanged.end() && found->second;
        }
        if (ctrl.kind == ControlKind::SliderInt || ctrl.kind == ControlKind::SliderFloat)
            return std::abs(ctrl.Value() - ctrl.Default()) > std::max(1e-6f, ctrl.stepVal * .5f);
        if (ctrl.kind == ControlKind::Toggle || ctrl.kind == ControlKind::Choice)
            return ctrl.Value() != ctrl.Default();
        return false;
    };
    const auto textHeightFor = [&](const std::wstring& text, IDWriteTextFormat* format, float width)
    {
        if (text.empty())
            return 0.f;
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                text.c_str(), UINT32(text.size()), format, std::max(1.f, width), 4096.f, &layout)) &&
            SUCCEEDED(layout->GetMetrics(&metrics)))
            return metrics.height;
        return 0.f;
    };
    float contentBottom = currentY + m_scrollY;
    for (const auto& item : cardsToRender)
    {
        const auto& card = *item.pCard;
        const auto& visibleControls = item.visibleControls;

        std::wstring headerTitle = Tr(card.title);
        D2DControl* headerToggle = nullptr;
        for (const auto& control : card.controls)
            if (control.headerToggle)
                headerToggle = const_cast<D2DControl*>(&control);
        if (isSearching)
        {
            headerTitle += L"  (" + Tr(item.pageName) + L")";
        }

        std::vector<float> rowHeights, descriptionOffsets;
        const float titleHeight =
            textHeightFor(headerTitle, m_fmtCardTitle.Get(), contentW - (headerToggle ? 88.f : 32.f));
        const float headerHeight = std::max(38.f, 18.f + titleHeight);
        float cardHeight = headerHeight;
        for (const auto* control : visibleControls)
        {
            if (control->headerToggle)
            {
                rowHeights.push_back(0);
                descriptionOffsets.push_back(0);
                continue;
            }
            auto description = control->detail ? control->detail() : control->desc;
            if (description.empty())
                description = control->desc;
            if (control->kind == ControlKind::Tabs)
            {
                // Tab row, then its description across the card.
                const float descriptionHeight = textHeightFor(description, m_fmtDesc.Get(), contentW - 32.f);
                const float height = 54.f + (descriptionHeight > 0 ? descriptionHeight + 8.f : 0.f);
                rowHeights.push_back(height);
                descriptionOffsets.push_back(52.f);
                cardHeight += height;
                continue;
            }
            const float indent = control->include ? 52.f : 0.f;
            const float labelHeight = textHeightFor(control->name, m_fmtLabel.Get(), contentW - 316.f - indent);
            const float descriptionTop = std::max(30.f, 9.f + labelHeight + 4.f);
            const float descriptionHeight = textHeightFor(description, m_fmtDesc.Get(), contentW - 316.f - indent);
            const float height = std::max(SettingsLayout::RowHeight, descriptionTop + descriptionHeight + 8.f);
            descriptionOffsets.push_back(descriptionTop);
            rowHeights.push_back(height);
            cardHeight += height;
        }
        D2D1_RECT_F cardRect = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + cardHeight);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

        // Card background; the border is drawn after the header so the header
        // fill cannot cover the top edge.
        m_pRenderTarget->FillRoundedRectangle(roundedCard, m_brushBgCard.Get());

        // Section headings contain only the title.
        D2D1_RECT_F headerRect = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + headerHeight);
        D2D1_ROUNDED_RECT roundedHeader = D2D1::RoundedRect(headerRect, 8.0f, 8.0f);
        m_pRenderTarget->FillRoundedRectangle(roundedHeader, m_brushBgCardHeader.Get());
        m_pRenderTarget->DrawLine(D2D1::Point2F(contentX, currentY + headerHeight),
                                  D2D1::Point2F(contentX + contentW, currentY + headerHeight),
                                  m_brushBorderCard.Get(),
                                  1.0f);
        m_pRenderTarget->DrawRoundedRectangle(roundedCard, m_brushBorderCard.Get(), 1.0f);

        m_pRenderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(contentX + 5, currentY + 10, contentX + 8, currentY + headerHeight - 10), 2, 2),
            m_brushAccent.Get());
        D2D1_RECT_F cardTitleRect = D2D1::RectF(contentX + 16,
                                                currentY + 9.f,
                                                contentX + contentW - (headerToggle ? 72.f : 16.f),
                                                currentY + 9.f + titleHeight);
        m_pRenderTarget->DrawText(headerTitle.c_str(),
                                  (UINT32)headerTitle.length(),
                                  m_fmtCardTitle.Get(),
                                  cardTitleRect,
                                  m_brushTextMain.Get());
        if (headerToggle)
            drawToggle(*headerToggle, contentX + contentW - 56.f, currentY + (headerHeight - 22.f) / 2.f);

        // Controls in Card
        float rowY = currentY + headerHeight;
        for (size_t ci = 0; ci < visibleControls.size(); ++ci)
        {
            auto& ctrl = const_cast<D2DControl&>(*visibleControls[ci]);
            if (ctrl.headerToggle)
                continue;
            const float rowHeight = rowHeights[ci];
            if (rowY + rowHeight < 0 || rowY > size.height)
            {
                rowY += rowHeight;
                continue;
            }
            const bool enabled = ctrl.Enabled();

            if (ci > 0)
            {
                m_pRenderTarget->DrawLine(D2D1::Point2F(contentX + 16, rowY),
                                          D2D1::Point2F(contentX + contentW - 16, rowY),
                                          m_brushBorderSubtle.Get(),
                                          1.0f);
            }

            if (ctrl.kind == ControlKind::Tabs)
            {
                // Choices as tabs in one row, styled like the page tabs.
                float tabX = contentX + 16;
                for (const auto& choice : ctrl.choices)
                {
                    const auto label = choice.label;
                    float textWidth = float(label.size()) * 8.f;
                    ComPtr<IDWriteTextLayout> measured;
                    DWRITE_TEXT_METRICS metrics = {};
                    if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(
                            label.c_str(), UINT32(label.size()), m_fmtNav.Get(), 2000, 30, measured.GetAddressOf())) &&
                        SUCCEEDED(measured->GetMetrics(&metrics)))
                        textWidth = metrics.widthIncludingTrailingWhitespace;
                    const auto tab = D2D1::RectF(tabX, rowY + 12.f, tabX + std::ceil(textWidth) + 32.f, rowY + 42.f);
                    const bool selected = int(ctrl.Value()) == choice.value;
                    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tab, 8, 8),
                                                          selected ? m_brushActiveBg.Get() : m_brushInputBg.Get());
                    if (selected)
                        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tab, 8, 8), m_brushAccent.Get(), 1);
                    const auto paragraph = m_fmtNav->GetParagraphAlignment();
                    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    m_fmtNav->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    m_pRenderTarget->DrawText(label.c_str(),
                                              UINT32(label.size()),
                                              m_fmtNav.Get(),
                                              tab,
                                              selected ? m_brushAccentHover.Get() : m_brushTextMuted.Get());
                    m_fmtNav->SetParagraphAlignment(paragraph);
                    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    HitRegion hit;
                    hit.type = HitRegion::ChoiceTab;
                    hit.rect = tab;
                    hit.pControl = &ctrl;
                    hit.pageIndex = choice.value;
                    m_hitRegions.push_back(hit);
                    tabX = tab.right + 7.f;
                }
                if (!ctrl.desc.empty())
                    m_pRenderTarget->DrawText(
                        ctrl.desc.c_str(),
                        UINT32(ctrl.desc.size()),
                        m_fmtDesc.Get(),
                        D2D1::RectF(
                            contentX + 16, rowY + descriptionOffsets[ci], contentX + contentW - 16, rowY + rowHeight),
                        m_brushTextDim.Get());
                rowY += rowHeight;
                continue;
            }

            // Left: Name & Description with clean gap; after the include switch if any.
            const float indent = ctrl.include ? 52.f : 0.f;
            D2D1_RECT_F labelRect = D2D1::RectF(
                contentX + 16 + indent, rowY + 9.0f, contentX + contentW - 300, rowY + descriptionOffsets[ci] - 4.f);
            if (isSearching && m_pDWriteFactory)
            {
                // Mark the matched part of the name.
                auto lowerName = ctrl.name, lowerQuery = m_searchQuery;
                CharLowerBuffW(lowerName.data(), DWORD(lowerName.size()));
                CharLowerBuffW(lowerQuery.data(), DWORD(lowerQuery.size()));
                const auto at = lowerName.find(lowerQuery);
                ComPtr<IDWriteTextLayout> layout;
                if (at != std::wstring::npos &&
                    SUCCEEDED(m_pDWriteFactory->CreateTextLayout(ctrl.name.c_str(),
                                                                 UINT32(ctrl.name.size()),
                                                                 m_fmtLabel.Get(),
                                                                 labelRect.right - labelRect.left,
                                                                 labelRect.bottom - labelRect.top,
                                                                 layout.GetAddressOf())))
                {
                    DWRITE_HIT_TEST_METRICS ranges[8];
                    UINT32 count = 0;
                    if (SUCCEEDED(layout->HitTestTextRange(
                            UINT32(at), UINT32(lowerQuery.size()), labelRect.left, labelRect.top, ranges, 8, &count)))
                    {
                        m_brushHoverBg->SetOpacity(2.2f);
                        for (UINT32 i = 0; i < count; ++i)
                            m_pRenderTarget->FillRoundedRectangle(
                                D2D1::RoundedRect(D2D1::RectF(ranges[i].left - 1,
                                                              ranges[i].top,
                                                              ranges[i].left + ranges[i].width + 1,
                                                              ranges[i].top + ranges[i].height),
                                                  3,
                                                  3),
                                m_brushHoverBg.Get());
                        m_brushHoverBg->SetOpacity(1.f);
                    }
                }
            }
            m_pRenderTarget->DrawText(
                ctrl.name.c_str(), (UINT32)ctrl.name.length(), m_fmtLabel.Get(), labelRect, m_brushTextMain.Get());
            if (modifiedFor(ctrl) && !ctrl.include)
                m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(contentX + 8, rowY + 18.5f), 3, 3),
                                             m_brushAccent.Get());

            D2D1_RECT_F descRect = D2D1::RectF(contentX + 16 + indent,
                                               rowY + descriptionOffsets[ci],
                                               contentX + contentW - 300,
                                               rowY + rowHeight - 8.f);
            auto description = ctrl.detail ? ctrl.detail() : ctrl.desc;
            if (description.empty())
                description = ctrl.desc;
            m_pRenderTarget->DrawText(
                description.c_str(), (UINT32)description.length(), m_fmtDesc.Get(), descRect, m_brushTextDim.Get());

            // Right: Control area aligned to cardRight - 16
            const float rightEdge = contentX + contentW - 16.0f;

            // Reset arrow left of a slider's value.
            auto drawReset = [&](const D2D1_RECT_F& resetRect)
            {
                if (!ctrl.reset || !modifiedFor(ctrl))
                    return;
                // Draw the reference orientation explicitly: clockwise
                // arc with its arrowhead at the upper-right corner.
                const auto center = D2D1::Point2F(resetRect.left + 17, resetRect.top + 17);
                const auto end = D2D1::Point2F(center.x + 4.14f, center.y - 4.14f);
                ComPtr<ID2D1PathGeometry> arc;
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(m_pD2DFactory->CreatePathGeometry(&arc)) && SUCCEEDED(arc->Open(&sink)))
                {
                    sink->BeginFigure(D2D1::Point2F(center.x + 5.85f, center.y), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddArc(D2D1::ArcSegment(
                        end, D2D1::SizeF(5.85f, 5.85f), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    if (SUCCEEDED(sink->Close()))
                        m_pRenderTarget->DrawGeometry(arc.Get(), m_brushTextMain.Get(), 1.7f);
                    m_pRenderTarget->DrawLine(D2D1::Point2F(end.x - 4.55f, end.y + .65f),
                                              D2D1::Point2F(end.x + .65f, end.y + .65f),
                                              m_brushTextMain.Get(),
                                              1.7f);
                    m_pRenderTarget->DrawLine(D2D1::Point2F(end.x + .65f, end.y + .65f),
                                              D2D1::Point2F(end.x + .65f, end.y - 4.55f),
                                              m_brushTextMain.Get(),
                                              1.7f);
                }
                HitRegion reset;
                reset.type = HitRegion::ResetValue;
                reset.rect = resetRect;
                reset.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(reset);
            };

            if (ctrl.kind == ControlKind::Toggle)
            {
                drawToggle(ctrl, rightEdge - 40.f, rowY + 18.f);
            }
            else if (ctrl.kind == ControlKind::SliderInt || ctrl.kind == ControlKind::SliderFloat)
            {
                const float numBoxW = 104.f, buttonW = 28.f, gap = 4.f;
                const float plusX = rightEdge - buttonW;
                const float numBoxX = plusX - gap - numBoxW;
                const float minusX = numBoxX - gap - buttonW;
                const auto numRect = D2D1::RectF(numBoxX, rowY + 16.f, numBoxX + numBoxW, rowY + 44.f);
                m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(numRect, 4, 4), m_brushInputBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(numRect, 4, 4), m_brushBorderCard.Get(), 1.f);
                const auto value = NumberText(ctrl, ctrl.Value());
                m_pRenderTarget->DrawText(value.c_str(),
                                          UINT32(value.size()),
                                          m_fmtValue.Get(),
                                          D2D1::RectF(numRect.left + 4, numRect.top, numRect.right - 4, numRect.bottom),
                                          m_brushTextMain.Get());
                HitRegion number;
                number.type = HitRegion::Number;
                number.rect = numRect;
                number.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(number);
                const auto drawStep = [&](float left, bool increase)
                {
                    const auto rect = D2D1::RectF(left, rowY + 16.f, left + buttonW, rowY + 44.f);
                    const bool available =
                        enabled && (increase ? ctrl.Value() < ctrl.maxVal : ctrl.Value() > ctrl.minVal);
                    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 4, 4), m_brushInputBg.Get());
                    m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, 4, 4), m_brushBorderCard.Get(), 1.f);
                    const float cx = left + buttonW / 2.f, cy = rowY + 30.f;
                    auto brush = available ? m_brushTextMain.Get() : m_brushTextDim.Get();
                    m_pRenderTarget->DrawLine(D2D1::Point2F(cx - 4, cy), D2D1::Point2F(cx + 4, cy), brush, 1.5f);
                    if (increase)
                        m_pRenderTarget->DrawLine(D2D1::Point2F(cx, cy - 4), D2D1::Point2F(cx, cy + 4), brush, 1.5f);
                    if (available)
                    {
                        HitRegion hit;
                        hit.type = increase ? HitRegion::Increment : HitRegion::Decrement;
                        hit.rect = rect;
                        hit.pControl = &ctrl;
                        m_hitRegions.push_back(hit);
                    }
                };
                drawStep(minusX, false);
                drawStep(plusX, true);
                const auto range = NumberText(ctrl, ctrl.minVal, true) + L" - " + NumberText(ctrl, ctrl.maxVal, true);
                m_pRenderTarget->DrawText(range.c_str(),
                                          UINT32(range.size()),
                                          m_fmtRange.Get(),
                                          D2D1::RectF(numRect.left, rowY + 49.f, numRect.right, rowY + 65.f),
                                          m_brushTextDim.Get());
                drawReset(D2D1::RectF(minusX - 40.f, rowY + 13.f, minusX - 6.f, rowY + 47.f));
            }
            else if (ctrl.kind == ControlKind::Choice)
            {
                const float boxW = 220.0f;
                const float boxH = 28.0f;
                float boxX = rightEdge - boxW;
                float boxY = rowY + 15.0f;

                D2D1_RECT_F dropRect = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
                D2D1_ROUNDED_RECT roundedDrop = D2D1::RoundedRect(dropRect, 6.0f, 6.0f);
                m_pRenderTarget->FillRoundedRectangle(roundedDrop, m_brushInputBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(roundedDrop, m_brushBorderCard.Get(), 1.0f);

                // Find selected label
                int curChoice = int(ctrl.Value());
                std::wstring curText = L"Select...";
                for (const auto& item : ctrl.choices)
                {
                    if (item.value == curChoice)
                    {
                        curText = item.label;
                        break;
                    }
                }

                D2D1_RECT_F textRect = D2D1::RectF(boxX + 10, boxY, boxX + boxW - 28, boxY + boxH);
                m_pRenderTarget->DrawText(curText.c_str(),
                                          (UINT32)curText.length(),
                                          m_fmtChoice.Get(),
                                          textRect,
                                          m_brushTextMain.Get(),
                                          D2D1_DRAW_TEXT_OPTIONS_CLIP);

                // Draw at a fixed logical size so the arrow stays clear at every DPI.
                const auto arrowCenter = D2D1::Point2F(boxX + boxW - 16, boxY + boxH / 2);
                m_pRenderTarget->DrawLine(D2D1::Point2F(arrowCenter.x - 5, arrowCenter.y - 2.5f),
                                          D2D1::Point2F(arrowCenter.x, arrowCenter.y + 2.5f),
                                          m_brushTextDim.Get(),
                                          1.8f);
                m_pRenderTarget->DrawLine(D2D1::Point2F(arrowCenter.x, arrowCenter.y + 2.5f),
                                          D2D1::Point2F(arrowCenter.x + 5, arrowCenter.y - 2.5f),
                                          m_brushTextDim.Get(),
                                          1.8f);

                HitRegion hr;
                hr.type = HitRegion::DropdownHeader;
                hr.rect = dropRect;
                hr.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(hr);
            }

            else if (ctrl.kind == ControlKind::Text)
            {
                D2D1_RECT_F r = D2D1::RectF(rightEdge - 220, rowY + 15, rightEdge, rowY + 43);
                const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(r, 4, 4);
                m_pRenderTarget->FillRoundedRectangle(rounded, m_brushInputBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(rounded, m_brushBorderCard.Get(), 1.0f);
                const auto value = ctrl.getText ? ctrl.getText() : std::wstring();
                const auto& shown = value.empty() ? ctrl.placeholder : value;
                auto text = r;
                text.left += 8;
                text.right -= 6;
                m_pRenderTarget->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
                m_pRenderTarget->DrawText(shown.c_str(),
                                          (UINT32)shown.size(),
                                          m_fmtTextField.Get(),
                                          text,
                                          value.empty() ? m_brushTextMuted.Get() : m_brushTextMain.Get(),
                                          D2D1_DRAW_TEXT_OPTIONS_CLIP,
                                          DWRITE_MEASURING_MODE_GDI_CLASSIC);
                m_pRenderTarget->PopAxisAlignedClip();
                HitRegion h;
                h.type = HitRegion::Number;
                h.rect = r;
                h.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(h);
            }
            else if (ctrl.kind == ControlKind::Action)
            {
                D2D1_RECT_F r = D2D1::RectF(rightEdge - 220, rowY + 15, rightEdge, rowY + 43);
                m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_brushInputBg.Get());
                // Same font as the choice boxes beside them.
                auto text = r;
                text.left += 10;
                m_pRenderTarget->DrawText(ctrl.actionLabel.c_str(),
                                          (UINT32)ctrl.actionLabel.size(),
                                          m_fmtChoice.Get(),
                                          text,
                                          m_brushTextMain.Get());
                HitRegion h;
                h.type = HitRegion::Action;
                h.rect = r;
                h.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(h);
            }
            if (!enabled)
            {
                // A row with its own include switch keeps its text readable and
                // greys only the control.
                const float left = ctrl.include ? contentX + contentW - 300 : contentX + 8;
                auto old = m_brushBgCard->GetOpacity();
                m_brushBgCard->SetOpacity(.65f);
                m_pRenderTarget->FillRectangle(D2D1::RectF(left, rowY, rightEdge, rowY + rowHeight),
                                               m_brushBgCard.Get());
                m_brushBgCard->SetOpacity(old);
            }
            if (ctrl.include)
                drawToggle(*ctrl.include, contentX + 16, rowY + 16.f);
            rowY += rowHeight;
        }

        contentBottom = currentY + m_scrollY + cardHeight;
        currentY += cardHeight + 14.0f;
    }

    m_pRenderTarget->PopLayer();
    // Clip hit areas as well as drawing so scrolled controls cannot cover the fixed tabs.
    for (size_t i = fixedHitCount; i < m_hitRegions.size(); ++i)
    {
        auto& rect = m_hitRegions[i].rect;
        rect.top = std::max(rect.top, viewportTop);
        rect.bottom = std::min(rect.bottom, size.height);
        if (rect.bottom < rect.top)
            rect = {};
    }
    m_pRenderTarget->PopAxisAlignedClip();
    // Spacing after the final card must not make an otherwise complete page scroll.
    const float overflow = contentBottom - size.height;
    m_maxScrollY = overflow > .5f ? std::ceil(overflow + 8.f) : 0.f;
    const float clampedScroll = std::clamp(m_scrollY, 0.f, m_maxScrollY);
    if (clampedScroll != m_scrollY)
    {
        m_scrollY = clampedScroll;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    m_pRenderTarget->PopAxisAlignedClip();
    DrawScrollbar(D2D1::RectF(size.width - 16, 8, size.width - 5, size.height - 8),
                  m_scrollY,
                  m_maxScrollY + size.height,
                  size.height,
                  -1);

    if (m_keyboardIndex >= 0 && m_keyboardIndex < int(m_hitRegions.size()))
        m_pRenderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(m_hitRegions[m_keyboardIndex].rect.left - 3,
                                          m_hitRegions[m_keyboardIndex].rect.top - 3,
                                          m_hitRegions[m_keyboardIndex].rect.right + 3,
                                          m_hitRegions[m_keyboardIndex].rect.bottom + 3),
                              6,
                              6),
            m_brushAccentHover.Get(),
            2.f);
    // Compact icon rail; its width never changes when navigation opens.
    m_pRenderTarget->FillRectangle(D2D1::RectF(0, 0, 60, size.height), m_brushBgSidebar.Get());
    for (int line = 0; line < 3; ++line)
        m_pRenderTarget->DrawLine(
            D2D1::Point2F(20, 20.f + line * 6), D2D1::Point2F(40, 20.f + line * 6), m_brushTextMain.Get(), 2);
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        const float top = 62.f + float(i) * 36.f;
        auto rect = D2D1::RectF(9, top, 51, top + 32);
        if (int(i) == m_activePage)
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), m_brushActiveBg.Get());
        else if (int(i) == m_hoverPage)
        {
            m_brushHoverBg->SetOpacity(m_hoverProgress);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), m_brushHoverBg.Get());
            m_brushHoverBg->SetOpacity(1.f);
        }
        if (int(i) == m_activePage)
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2, top + 7, 5, top + 29), 2, 2),
                                                  m_brushAccent.Get());
        m_fmtIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(m_pages[i].icon.c_str(), 1, m_fmtIcon.Get(), rect, m_brushTextMain.Get());
        m_fmtIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    if (m_navigationProgress > 0.f)
    {
        m_pRenderTarget->SetTransform(
            D2D1::Matrix3x2F::Translation(-(1.f - m_navigationProgress) * SettingsLayout::SidebarWidth, 0));
        drawNavigation();
        m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    DrawInteractionFeedback();
    DrawDeleteDialog(size);
    DrawToast(size);
    HRESULT hr = m_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Direct2DHostWindow::Changed()
{
    if (HandleProfileRequests())
        return;
    if (FAILED(ValidateSettings(*m_pSettings)))
    {
        m_error = m_pSettings->iccEnabled && !m_pSettings->iccPath[0] ? L"Select an ICC profile file first."
                  : m_pSettings->lutEnabled && !m_pSettings->lutPath[0]
                      ? L"Select a 3D LUT file first."
                      : L"Value or combination is outside the supported range.";
        *m_pSettings = m_accepted;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    const bool reordered =
        memcmp(m_accepted.userShaders.order, m_pSettings->userShaders.order, sizeof(m_pSettings->userShaders.order)) !=
        0;
    m_accepted = *m_pSettings;
    if (m_editing.empty())
        m_base = *m_pSettings;
    m_error.clear();
    CountUnsaved();
    if (reordered)
    {
        EndEdit();
        m_pDraggingSlider = nullptr;
        m_keyboardIndex = -1;
        InitPages();
    }
    if (!m_standalone)
        SendMessageW(GetParent(m_hwnd), WM_APP + 302, 0, 0);
    if (!m_pending)
    {
        m_pending = true;
        PostMessageW(m_hwnd, WM_APP + 301, 0, 0);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::DrawSearchBox(D2D1_RECT_F box)
{
    m_searchBox = box;
    const auto rounded = D2D1::RoundedRect(box, 6, 6);
    m_pRenderTarget->FillRoundedRectangle(rounded, m_brushInputBg.Get());
    m_pRenderTarget->DrawRoundedRectangle(
        rounded, m_searchFocused ? m_brushAccent.Get() : m_brushBorderCard.Get(), m_searchFocused ? 1.5f : 1.f);
    // Magnifier
    const float middle = (box.top + box.bottom) / 2;
    m_pRenderTarget->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(box.left + 16, middle - 1), 5, 5), m_brushTextMuted.Get(), 1.5f);
    m_pRenderTarget->DrawLine(D2D1::Point2F(box.left + 19.5f, middle + 2.5f),
                              D2D1::Point2F(box.left + 23, middle + 6),
                              m_brushTextMuted.Get(),
                              1.5f);
    const auto text = SearchEditRect(box);
    const bool typing = m_edit && !m_editControl && !m_renamingDisplay;
    if (typing)
    {
        // Keep the native field inside the box when the layout changes.
        const float scale = Scale();
        SetWindowPos(m_edit,
                     nullptr,
                     int(text.left * scale),
                     int(text.top * scale),
                     int((text.right - text.left) * scale),
                     int((text.bottom - text.top) * scale),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (!typing)
    {
        const bool empty = m_searchQuery.empty();
        const auto hint = Tr(L"Search settings");
        const auto& shown = empty ? hint : m_searchQuery;
        const auto wrap = m_fmtNav->GetWordWrapping();
        m_fmtNav->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_pRenderTarget->DrawText(shown.c_str(),
                                  UINT32(shown.size()),
                                  m_fmtNav.Get(),
                                  D2D1::RectF(text.left, text.top + 1, text.right, text.bottom),
                                  empty ? m_brushTextDim.Get() : m_brushTextMain.Get(),
                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_fmtNav->SetWordWrapping(wrap);
    }
    if (!m_searchQuery.empty())
    {
        const auto clear = D2D1::RectF(box.right - 28, box.top, box.right - 4, box.bottom);
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(L"\u2715",
                                  1,
                                  m_fmtNav.Get(),
                                  D2D1::RectF(clear.left, clear.top + 8, clear.right, clear.bottom),
                                  m_brushTextMuted.Get());
        m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        HitRegion hit;
        hit.type = HitRegion::SearchClearBtn;
        hit.rect = clear;
        m_hitRegions.push_back(hit);
    }
    HitRegion hit;
    hit.type = HitRegion::SearchBox;
    hit.rect = box;
    m_hitRegions.push_back(hit);
}

// Counts the settings that differ from the last loaded or saved values. The
// controls read the live settings, so they are evaluated once against each.
void Direct2DHostWindow::CountUnsaved()
{
    const auto collect = [&]()
    {
        std::vector<std::wstring> values;
        for (const auto& page : m_pages)
            for (const auto& card : page.cards)
            {
                if (card.title == CustomRoleCard || card.title == L"Menu Appearance")
                    continue; // menu-local, applies immediately
                for (const auto& c : card.controls)
                {
                    if (c.kind == ControlKind::Action)
                        continue;
                    if (c.kind == ControlKind::Text)
                        values.push_back(c.getText ? c.getText() : std::wstring());
                    else
                        values.push_back(std::to_wstring(c.Value()));
                }
            }
        return values;
    };
    // A profile is saved at once; Save keeps the normal settings.
    if (!m_editing.empty())
    {
        m_unsaved = 0;
        return;
    }
    const auto now = collect();
    const auto current = *m_pSettings;
    *m_pSettings = m_saved;
    const auto before = collect();
    *m_pSettings = current;
    m_unsaved = 0;
    for (size_t i = 0; i < now.size() && i < before.size(); ++i)
        m_unsaved += now[i] != before[i];
}

void Direct2DHostWindow::DrawScrollbar(D2D1_RECT_F track, float offset, float extent, float viewport, int target)
{
    if (extent <= viewport || viewport <= 0)
        return;
    const float length = track.bottom - track.top;
    const float thumb = std::min(length, std::max(28.f, length * viewport / extent));
    const float top = track.top + (length - thumb) * std::clamp(offset / (extent - viewport), 0.f, 1.f);
    auto r = D2D1::RectF(track.left, top, track.right, top + thumb);
    // Thin thumb without a track, wider while dragged (Windows 11 style).
    const float width = m_scrollDrag == target ? 6.f : 3.f;
    const auto visible = D2D1::RectF(track.right - 2 - width, top + 2, track.right - 2, top + thumb - 2);
    const auto old = m_brushTextDim->GetOpacity();
    m_brushTextDim->SetOpacity(m_scrollDrag == target ? .8f : .5f);
    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(visible, width / 2, width / 2), m_brushTextDim.Get());
    m_brushTextDim->SetOpacity(old);
    HitRegion hit;
    hit.type = HitRegion::Scrollbar;
    hit.rect = track;
    hit.trackRect = r;
    hit.pageIndex = target;
    m_hitRegions.push_back(hit);
}

void Direct2DHostWindow::Preview()
{
    m_pending = false;
    // A profile keeps what differs from Default; the renderer gets the normal
    // settings and reads the profile from disk.
    if (!m_editing.empty())
    {
        const auto profiles = ContentProfiles::Load();
        if (const auto* found = ContentProfiles::Find(profiles, m_editing))
        {
            auto profile = *found;
            profile.overrides = ContentProfiles::Differences(m_base, *m_pSettings);
            if (profile.overrides != found->overrides && !ContentProfiles::Save(profile))
                m_error = L"Could not save the profile.";
        }
    }
    if (m_preview)
    {
        auto hr = m_preview(m_base);
        if (FAILED(hr))
        {
            *m_pSettings = m_accepted = m_applied;
            wchar_t text[100];
            swprintf_s(text, L"Could not apply settings (0x%08X).", unsigned(hr));
            m_error = text;
        }
        else
            m_applied = *m_pSettings;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::TransferIni(bool importing)
{
    EndEdit();
    if (m_pending)
        Preview();
    wchar_t path[32768] = L"FrankyRenderer.ini";
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = m_hwnd;
    dialog.lpstrFile = path;
    dialog.nMaxFile = DWORD(std::size(path));
    dialog.lpstrFilter = L"INI settings (*.ini)\0*.ini\0\0";
    dialog.lpstrDefExt = L"ini";
    dialog.lpstrTitle = importing ? L"Import renderer settings" : L"Export renderer settings";
    dialog.Flags =
        OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    const auto alive = m_alive;
    const BOOL chosen = importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog);
    if (!*alive)
        return;
    if (!chosen)
    {
        if (CommDlgExtendedError())
            m_error = L"Could not open the INI file dialog.";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    try
    {
        if (!importing)
        {
            if (m_displayProfiles)
            {
                const auto text = SettingsBackup::Serialize(SettingsBackup::Capture(m_base));
                if (SUCCEEDED(SettingsIni::ExportText(path, text, m_error)))
                    m_error = L"All display profiles exported.";
            }
            else if (SUCCEEDED(SettingsIni::Export(path, m_base, m_error)))
                m_error = L"INI backup exported.";
        }
        else
        {
            const auto text = SettingsBackup::ReadFile(path);
            const auto entries = SettingsIni::ReadEntries(text, 16 * 1024 * 1024, 131072);
            const auto version = entries.find({"FrankyRenderer", "FormatVersion"});
            HRESULT hr;
            if (version != entries.end() && version->second == "2")
            {
                const auto profiles = SettingsBackup::Parse(text);
                for (const auto& [id, profile] : profiles)
                    LiveSettings::RememberMetadata(GetParent(m_hwnd), id);
                hr = SettingsBackup::Apply(profiles, m_preview);
                if (SUCCEEDED(hr))
                {
                    const std::wstring selected = m_pSettings->displayProfile;
                    m_devices = DisplayProfiles::All();
                    AdoptDisplayProfile(selected);
                    m_error = L"All display profiles imported.";
                }
            }
            else
            {
                hr = ApplyImported(SettingsIni::Parse(text));
                if (SUCCEEDED(hr))
                    m_error = m_standalone ? L"INI imported. Select Save to keep it." : L"INI settings imported.";
            }
            if (FAILED(hr))
                m_error = L"Could not apply INI settings. Check external ICC and LUT files.";
        }
    }
    catch (const std::exception& ex)
    {
        const std::string message = ex.what();
        m_error.assign(message.begin(), message.end());
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

HRESULT Direct2DHostWindow::ApplyImported(const PlaceboSettings& candidate)
{
    auto selected = candidate;
    wcscpy_s(selected.displayProfile, m_pSettings->displayProfile);
    auto hr = ValidateSettings(selected);
    if (FAILED(hr))
        return hr;
    hr = m_preview ? m_preview(selected) : S_OK;
    if (FAILED(hr))
        return hr;
    EndEdit();
    // An import replaces the normal settings; a profile shown stays on top.
    m_base = selected;
    m_pending = false;
    m_scrollY = 0;
    m_searchQuery.clear();
    m_searchFocused = false;
    LoadEditTarget();
    return S_OK;
}

void Direct2DHostWindow::SelectDisplayProfile(const D2D1_RECT_F& rect)
{
    EndEdit();
    if (m_pending)
        Preview();
    const auto result = EnsureDisplayProfiles(DisplayProfiles::Connected(), LoadSettings());
    if (FAILED(result))
    {
        m_error = L"Could not read display profiles.";
        return;
    }
    m_devices = DisplayProfiles::All();
    const auto devices = m_devices;
    ChoicePopup popup;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        popup.labels.push_back(devices[i].name + (devices[i].connected ? L"" : L" (Disconnected)"));
        if (devices[i].id == m_pSettings->displayProfile)
            popup.selected = int(i);
    }
    if (devices.empty())
        return;
    RECT anchor = {
        int(rect.left * Scale()), int(rect.top * Scale()), int(rect.right * Scale()), int(rect.bottom * Scale())};
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    const auto alive = m_alive;
    const int selected = popup.Show(m_hwnd, anchor, Scale());
    if (!*alive || selected < 0 || devices[selected].id == m_pSettings->displayProfile)
        return;
    AdoptDisplayProfile(devices[selected].id);
}

void Direct2DHostWindow::SelectQualityPreset(const D2D1_RECT_F& rect)
{
    EndEdit();
    if (m_measurement)
        return;
    ChoicePopup popup;
    for (size_t i = 0; i < std::size(QualityPresetNames); ++i)
    {
        popup.labels.push_back(Tr(QualityPresetNames[i]));
        popup.descriptions.push_back(Tr(QualityPresetDescriptions[i]));
    }
    // The last entry measures instead of applying fixed values.
    const int measure = int(popup.labels.size());
    popup.labels.push_back(Tr(L"Measure this PC"));
    popup.descriptions.push_back(Tr(L"Picks the best settings for this PC."));
    const auto origin = Quality();
    popup.selected = origin.measured ? measure : origin.preset;
    RECT anchor = {
        int(rect.left * Scale()), int(rect.top * Scale()), int(rect.right * Scale()), int(rect.bottom * Scale())};
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    const auto alive = m_alive;
    const int selected = popup.Show(m_hwnd, anchor, Scale());
    if (!*alive || selected < 0)
        return;
    if (selected == measure)
    {
        StartMeasurement();
        return;
    }
    ApplyQualityPreset(*m_pSettings, selected);
    // Controls hold their own copies of choice lists; rebuild them for the new values.
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_hitRegions.clear();
    InitPages();
    Changed();
}

void Direct2DHostWindow::StartMeasurement()
{
    // The display profile being edited decides the GPU and display mode.
    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    for (const auto& device : m_devices)
        if (device.connected && device.id == m_pSettings->displayProfile && device.monitor)
            monitor = device.monitor;
    BenchmarkTarget target;
    if (!FindBenchmarkTarget(monitor, target) &&
        !FindBenchmarkTarget(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), target))
    {
        ShowToast(Tr(L"Could not measure this display."));
        return;
    }
    auto job = std::make_shared<Measurement>();
    job->profile = m_pSettings->displayProfile;
    const PlaceboSettings settings = *m_pSettings;
    m_measurement = job;
    m_measureThread = std::thread(
        [job, settings, target]
        {
            QualityBenchmark benchmark;
            job->result = benchmark.Run(settings, target, &job->progress, &job->cancel);
            job->done = true;
        });
    SetTimer(m_hwnd, 19, 100, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::FinishMeasurement()
{
    KillTimer(m_hwnd, 19);
    m_measureThread.join();
    const auto job = std::move(m_measurement);
    // A profile switch while measuring leaves the new profile untouched.
    if (job->profile != m_pSettings->displayProfile)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (!job->result.ok)
    {
        ShowToast(Tr(L"Measurement failed."));
        return;
    }
    EndEdit();
    ApplyMeasuredQuality(*m_pSettings, job->result.upgrades);
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_hitRegions.clear();
    InitPages();
    Changed();
    // Name the preset when the result equals one, so an unchanged header
    // label still shows that the measurement happened.
    wchar_t summary[256] = {};
    const int same = MeasuredPresetMatch(job->result.upgrades);
    if (same >= 0)
        swprintf_s(summary,
                   Tr(L"Measured: same as %s (%.1f of %.1f ms)").c_str(),
                   Tr(QualityPresetNames[same]).c_str(),
                   job->result.milliseconds,
                   job->result.budget);
    else
        swprintf_s(summary,
                   Tr(L"Measured: %s (%.1f of %.1f ms)").c_str(),
                   DescribeMeasuredQuality(job->result.upgrades).c_str(),
                   job->result.milliseconds,
                   job->result.budget);
    ShowToast(summary);
}

void Direct2DHostWindow::ResetAll()
{
    EndEdit();
    const auto alive = m_alive;
    if (!m_editing.empty())
    {
        const int answer = MessageBoxW(
            m_hwnd,
            ProfilePage::Fill(Tr(L"Remove all settings of the profile \"%s\"? It then matches Default."), m_editing)
                .c_str(),
            Tr(L"Reset All").c_str(),
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (!*alive || answer != IDYES)
            return;
        *m_pSettings = m_base;
        m_pDraggingSlider = nullptr;
        m_keyboardIndex = -1;
        m_hitRegions.clear();
        InitPages();
        Changed();
        ShowToast(Tr(L"The profile matches Default again."));
        return;
    }
    const int answer =
        MessageBoxW(m_hwnd,
                    Tr(L"Reset all settings for this display to their defaults? Theme and language stay.").c_str(),
                    Tr(L"Reset All").c_str(),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (!*alive || answer != IDYES)
        return;
    PlaceboSettings defaults;
    wcscpy_s(defaults.displayProfile, m_base.displayProfile);
    *m_pSettings = defaults;
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_hitRegions.clear();
    InitPages();
    Changed();
    ShowToast(Tr(L"All settings reset to defaults."));
}

void Direct2DHostWindow::ShowToast(const std::wstring& text)
{
    m_toast = text;
    SetTimer(m_hwnd, 20, 4000, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::DrawToast(D2D1_SIZE_F size)
{
    if (m_toast.empty())
        return;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics = {};
    const float maxWidth = std::max(120.f, size.width - 48.f);
    if (FAILED(m_pDWriteFactory->CreateTextLayout(
            m_toast.c_str(), UINT32(m_toast.size()), m_fmtNav.Get(), maxWidth - 32, 200, layout.GetAddressOf())) ||
        FAILED(layout->GetMetrics(&metrics)))
        return;
    const float w = std::ceil(metrics.width) + 32, h = std::ceil(metrics.height) + 20;
    const auto box = D2D1::RectF((size.width - w) / 2, size.height - h - 20, (size.width + w) / 2, size.height - 20);
    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 8, 8), m_brushBgCardHeader.Get());
    m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 8, 8), m_brushAccent.Get(), 1);
    m_pRenderTarget->DrawTextLayout(D2D1::Point2F(box.left + 16, box.top + 10), layout.Get(), m_brushTextMain.Get());
}

// Asks before a profile is deleted: the menu dims and a card in the theme's
// colours offers Delete and Cancel.
void Direct2DHostWindow::DrawDeleteDialog(D2D1_SIZE_F size)
{
    if (m_deleteTab.empty())
    {
        m_dialogBox = m_dialogConfirm = m_dialogCancel = {};
        return;
    }
    ComPtr<ID2D1SolidColorBrush> shade, danger;
    if (FAILED(m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, .55f), shade.GetAddressOf())) ||
        FAILED(m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0xE5484D), danger.GetAddressOf())))
        return;
    m_pRenderTarget->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), shade.Get());

    const float w = std::min(380.f, size.width - 32.f), h = 158.f;
    m_dialogBox = D2D1::RectF((size.width - w) / 2, (size.height - h) / 2, (size.width + w) / 2, (size.height + h) / 2);
    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(m_dialogBox, 10, 10), m_brushBgCardHeader.Get());
    m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(m_dialogBox, 10, 10), m_brushBorderCard.Get(), 1);

    const auto title = Tr(L"Delete profile?");
    m_pRenderTarget->DrawText(
        title.c_str(),
        UINT32(title.size()),
        m_fmtCardTitle.Get(),
        D2D1::RectF(m_dialogBox.left + 22, m_dialogBox.top + 18, m_dialogBox.right - 22, m_dialogBox.top + 44),
        m_brushTextMain.Get());
    auto body = Tr(L"\"%s\" and its settings will be removed.");
    if (const auto at = body.find(L"%s"); at != std::wstring::npos)
        body.replace(at, 2, m_deleteTab);
    m_pRenderTarget->DrawText(
        body.c_str(),
        UINT32(body.size()),
        m_fmtDesc.Get(),
        D2D1::RectF(m_dialogBox.left + 22, m_dialogBox.top + 52, m_dialogBox.right - 22, m_dialogBox.bottom - 56),
        m_brushTextMuted.Get());

    m_dialogConfirm =
        D2D1::RectF(m_dialogBox.right - 122, m_dialogBox.bottom - 50, m_dialogBox.right - 22, m_dialogBox.bottom - 18);
    m_dialogCancel =
        D2D1::RectF(m_dialogConfirm.left - 110, m_dialogConfirm.top, m_dialogConfirm.left - 10, m_dialogConfirm.bottom);
    const auto over = [&](const D2D1_RECT_F& r)
    {
        return m_dialogMouse.x >= r.left && m_dialogMouse.x <= r.right && m_dialogMouse.y >= r.top &&
               m_dialogMouse.y <= r.bottom;
    };
    const auto button = [&](const D2D1_RECT_F& r, const std::wstring& text, bool destructive)
    {
        const auto rounded = D2D1::RoundedRect(r, 6, 6);
        if (destructive)
        {
            danger->SetOpacity(over(r) ? 1.f : .85f);
            m_pRenderTarget->FillRoundedRectangle(rounded, danger.Get());
        }
        else
        {
            m_pRenderTarget->FillRoundedRectangle(rounded, m_brushInputBg.Get());
            if (over(r))
                m_pRenderTarget->FillRoundedRectangle(rounded, m_brushHoverBg.Get());
        }
        const auto paragraph = m_fmtChoice->GetParagraphAlignment();
        m_fmtChoice->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtChoice->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_pRenderTarget->DrawText(text.c_str(), UINT32(text.size()), m_fmtChoice.Get(), r, m_brushTextMain.Get());
        m_fmtChoice->SetParagraphAlignment(paragraph);
        m_fmtChoice->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };
    button(m_dialogCancel, Tr(L"Cancel"), false);
    button(m_dialogConfirm, Tr(L"Delete"), true);
}

void Direct2DHostWindow::CloseDeleteDialog(bool confirm)
{
    const auto name = m_deleteTab;
    m_deleteTab.clear();
    if (confirm && !name.empty())
        ProfilePage::Delete(name);
    // The tabs may move; drop highlights and ignore a following double-click.
    m_hoverRect = m_previousHoverRect = m_pressRect = {};
    m_hoverTab = -1;
    m_profileCreated = GetTickCount64();
    m_dialogMouse = {-1, -1};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// The profile the menu edits; empty for Default or a profile that is gone.
std::wstring Direct2DHostWindow::EditTarget() const
{
    if (m_selection.empty() || !_wcsicmp(m_selection.c_str(), ContentProfiles::DefaultName))
        return {};
    const auto profiles = ContentProfiles::Load();
    const auto* found = ContentProfiles::Find(profiles, m_selection);
    return found ? found->name : std::wstring();
}

// Shows the edited profile on top of the normal settings.
void Direct2DHostWindow::LoadEditTarget()
{
    m_editing = EditTarget();
    auto view = m_base;
    if (!m_editing.empty())
    {
        const auto profiles = ContentProfiles::Load();
        if (const auto* found = ContentProfiles::Find(profiles, m_editing))
            view = ContentProfiles::Apply(m_base, *found);
    }
    *m_pSettings = m_accepted = m_applied = view;
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_hitRegions.clear();
    InitPages();
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

// Chooses Default or a profile for the menu; the playing video previews it
// while the menu is open.
void Direct2DHostWindow::SelectProfile(const std::wstring& selection)
{
    EndEdit();
    if (m_pending)
        Preview();
    m_selection = selection;
    ContentProfiles::StartPreview(selection);
    m_previewing = true;
    LoadEditTarget();
    if (m_preview)
        m_preview(m_base);
}

void Direct2DHostWindow::SelectProfileMenu(const D2D1_RECT_F& rect)
{
    EndEdit();
    const auto profiles = ContentProfiles::Load();
    const auto playing = ContentProfiles::PlayingProfile();
    std::vector<std::wstring> choices = {ContentProfiles::DefaultName};
    ChoicePopup popup;
    popup.labels = {Tr(L"Default")};
    popup.descriptions = {Tr(L"Normal settings of this display.")};
    for (const auto& profile : profiles)
    {
        choices.push_back(profile.name);
        popup.labels.push_back(profile.name);
        popup.descriptions.push_back(
            !_wcsicmp(profile.name.c_str(), playing.c_str()) ? Tr(L"Used by the playing video.") : L"");
    }
    popup.selected = 0;
    for (size_t i = 1; i < choices.size(); ++i)
        if (!_wcsicmp(choices[i].c_str(), m_selection.c_str()))
            popup.selected = int(i);
    RECT anchor = {
        int(rect.left * Scale()), int(rect.top * Scale()), int(rect.right * Scale()), int(rect.bottom * Scale())};
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    const auto alive = m_alive;
    const int selected = popup.Show(m_hwnd, anchor, Scale());
    if (!*alive || selected < 0)
        return;
    SelectProfile(choices[size_t(selected)]);
}

// After the Profiles page changed profiles: follow a renamed profile, fall
// back to Default for a deleted one, and let the renderer read them again.
void Direct2DHostWindow::SyncProfiles()
{
    if (m_pending)
        Preview();
    if (!ProfilePage::g_renamedFrom.empty() && !_wcsicmp(m_selection.c_str(), ProfilePage::g_renamedFrom.c_str()))
    {
        m_selection = ProfilePage::g_renamedTo;
        if (m_previewing)
            ContentProfiles::StartPreview(m_selection);
    }
    ProfilePage::g_renamedFrom.clear();
    if (_wcsicmp(m_selection.c_str(), ContentProfiles::DefaultName) && EditTarget().empty())
    {
        m_selection = ContentProfiles::DefaultName;
        if (m_previewing)
            ContentProfiles::StartPreview(m_selection);
    }
    if (EditTarget() != m_editing)
        LoadEditTarget();
    if (m_preview)
        m_preview(m_base);
}

// In a profile a reset returns to the value in Default, not the built-in one.
void Direct2DHostWindow::ResetControl(D2DControl& c)
{
    if (m_editing.empty() || (!c.get && !c.getText) || (c.kind != ControlKind::Text && !c.set))
    {
        if (c.reset)
            c.reset();
        return;
    }
    const auto current = *m_pSettings;
    *m_pSettings = m_base;
    const float value = c.Value();
    const auto text = c.getText ? c.getText() : std::wstring();
    *m_pSettings = current;
    if (c.kind == ControlKind::Text)
    {
        if (c.setText)
            c.setText(text);
    }
    else
        c.set(value);
}

// Carries out what a Profiles page control asked for; true when the settings
// shown were replaced and the change needs no further handling.
bool Direct2DHostWindow::HandleProfileRequests()
{
    if (!ProfilePage::g_notice.empty())
    {
        ShowToast(ProfilePage::g_notice);
        ProfilePage::g_notice.clear();
    }
    if (!ProfilePage::g_requestDisplay.empty())
    {
        const auto id = ProfilePage::g_requestDisplay;
        ProfilePage::g_requestDisplay.clear();
        if (id != m_base.displayProfile)
        {
            if (m_pending)
                Preview();
            if (SUCCEEDED(EnsureDisplayProfiles(DisplayProfiles::Connected(), LoadSettings())))
            {
                m_devices = DisplayProfiles::All();
                AdoptDisplayProfile(id);
            }
            else
                m_error = L"Could not read display profiles.";
        }
        return true;
    }
    if (ProfilePage::g_selectionRequested)
    {
        ProfilePage::g_selectionRequested = false;
        const auto name = ProfilePage::g_requestSelection;
        SelectProfile(name);
        ShowToast(ProfilePage::Fill(Tr(L"The menu now shows \"%s\". Changes on all pages go into the profile."), name));
        return true;
    }
    return false;
}

void Direct2DHostWindow::AdoptDisplayProfile(const std::wstring& id)
{
    if (!DisplayProfiles::ValidId(id))
        return;
    EndEdit();
    m_base = m_saved = LoadSettings(id);
    m_pending = false;
    LoadEditTarget();
}

// Parses an edited number; rejects partial input and values outside the control.
static bool ParseEditNumber(const D2DControl& c, std::wstring number, float& out)
{
    std::replace(number.begin(), number.end(), L',', L'.');
    if (number.empty())
        return false;
    wchar_t* end = nullptr;
    errno = 0;
    const double v = wcstod(number.c_str(), &end);
    if (end == number.c_str() || *end || errno == ERANGE || !std::isfinite(v) || v < c.minVal || v > c.maxVal ||
        (c.kind == ControlKind::SliderInt && std::floor(v) != v))
        return false;
    out = float(v);
    return true;
}

void Direct2DHostWindow::EndEdit(bool accept)
{
    if (m_edit)
    {
        HWND edit = m_edit;
        wchar_t text[256];
        GetWindowTextW(edit, text, 256);
        if (m_renamingDisplay && accept)
        {
            if (SUCCEEDED(DisplayProfiles::Rename(m_pSettings->displayProfile, text)))
                m_devices = DisplayProfiles::All();
            else
                m_error = L"Enter a display name (1 to 80 characters).";
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        // Clear the edit state first: committing can rebuild the pages.
        auto control = m_editControl;
        m_edit = nullptr;
        m_editControl = nullptr;
        m_renamingDisplay = false;
        DestroyWindow(edit);
        if (!control)
            return;
        float value = 0;
        if (control->kind == ControlKind::Text)
        {
            if (!accept || (control->getText && control->getText() == text))
                return;
            if (!control->setText || !control->setText(text))
            {
                m_error = control->invalidText.empty() ? L"Invalid text." : control->invalidText;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            Changed();
        }
        else if (!accept)
        {
            if (m_editAdjusted)
            {
                control->set(m_editOriginal);
                Changed();
            }
        }
        else if (!ParseEditNumber(*control, text, value))
        {
            if (m_editAdjusted)
            {
                control->set(m_editOriginal);
                Changed();
            }
            m_error = L"Enter a number within the supported range.";
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        else if (value != control->Value())
        {
            control->set(value);
            Changed();
        }
    }
}

void Direct2DHostWindow::BeginEdit(D2DControl* control, const D2D1_RECT_F& r, bool renameDisplay)
{
    EndEdit();
    m_editControl = control;
    m_renamingDisplay = renameDisplay;
    m_editOriginal = control ? control->Value() : 0;
    m_editAdjusted = false;
    float scale = Scale();
    wchar_t text[64] = {};
    if (control)
    {
        if (control->kind == ControlKind::SliderInt || control->kind == ControlKind::SliderFloat)
            swprintf_s(text, L"%s", NumberText(*control, control->Value()).c_str());
        else
            swprintf_s(text, L"%.9g", double(control->Value()));
    }
    // Numbers are centred in their box; keep them in place while typing.
    const bool number =
        control && (control->kind == ControlKind::SliderInt || control->kind == ControlKind::SliderFloat);
    m_edit = CreateWindowExW(0,
                             L"EDIT",
                             text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (number ? ES_CENTER : 0),
                             int(r.left * scale),
                             int(r.top * scale),
                             int((r.right - r.left) * scale),
                             int((r.bottom - r.top) * scale),
                             m_hwnd,
                             reinterpret_cast<HMENU>(7001),
                             nullptr,
                             nullptr);
    SendMessageW(m_edit, EM_SETLIMITTEXT, 127, 0);
    if (control && control->kind == ControlKind::Text)
    {
        SendMessageW(m_edit, EM_SETLIMITTEXT, 255, 0);
        SetWindowTextW(m_edit, control->getText ? control->getText().c_str() : L"");
    }
    else if (renameDisplay)
    {
        SendMessageW(m_edit, EM_SETLIMITTEXT, 80, 0);
        for (const auto& device : m_devices)
            if (device.id == m_pSettings->displayProfile)
                SetWindowTextW(m_edit, device.name.c_str());
    }
    else if (!control)
        SetWindowTextW(m_edit, m_searchQuery.c_str());
    if (m_editFont)
        DeleteObject(m_editFont);
    const bool textControl = control && control->kind == ControlKind::Text;
    // Text controls match the 14 DIP medium font they are drawn with. GDI
    // renders Segoe UI medium as regular, so semibold matches it instead.
    m_editFont = CreateFontW(-int((textControl ? 14 : 13) * scale + .5f),
                             0,
                             0,
                             0,
                             textControl ? FW_SEMIBOLD : FW_NORMAL,
                             FALSE,
                             FALSE,
                             FALSE,
                             DEFAULT_CHARSET,
                             0,
                             0,
                             CLEARTYPE_QUALITY,
                             0,
                             L"Segoe UI");
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    if (textControl)
        SendMessageW(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SetWindowSubclass(m_edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
}

void Direct2DHostWindow::EditChanged()
{
    if (!m_edit)
        return;
    wchar_t text[128];
    GetWindowTextW(m_edit, text, 128);
    if (m_renamingDisplay)
        return;
    if (!m_editControl)
    {
        m_searchQuery = text;
        const auto first = m_searchQuery.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            m_searchQuery.clear();
        else
            m_searchQuery = m_searchQuery.substr(first, m_searchQuery.find_last_not_of(L" \t\r\n") - first + 1);
        m_scrollY = 0;
        m_keyboardIndex = -1;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // Numbers apply on Enter, Tab or leaving the field; partial input such as
    // "10" on the way to "1000" must not reach the renderer.
}

LRESULT CALLBACK Direct2DHostWindow::EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto p = reinterpret_cast<Direct2DHostWindow*>(data);
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTALLKEYS;
    // Leaving the field (for example to the host's OK button) commits it.
    if (msg == WM_KILLFOCUS && (p->m_renamingDisplay || p->m_editControl))
    {
        p->EndEdit();
        return 0;
    }
    if (msg == WM_KEYDOWN)
    {
        if (p->m_renamingDisplay && (w == VK_RETURN || w == VK_ESCAPE))
        {
            p->EndEdit(w == VK_RETURN);
            InvalidateRect(p->m_hwnd, nullptr, FALSE);
            SetFocus(p->m_hwnd);
            return 0;
        }
        if (w == VK_ESCAPE && !p->m_editControl)
        {
            p->m_searchQuery.clear();
            p->m_scrollY = 0;
            p->m_searchFocused = false;
            InvalidateRect(p->m_hwnd, nullptr, FALSE);
        }
        if (w == VK_RETURN || w == VK_ESCAPE || w == VK_TAB)
        {
            p->EndEdit(w != VK_ESCAPE);
            SetFocus(p->m_hwnd);
            return 0;
        }
        if (p->m_editControl && p->m_editControl->kind != ControlKind::Text && (w == VK_UP || w == VK_DOWN))
        {
            // Arrow steps preview immediately; Escape still restores the opening value.
            auto c = p->m_editControl;
            wchar_t current[128];
            GetWindowTextW(h, current, 128);
            float v = c->Value();
            ParseEditNumber(*c, current, v);
            v = std::clamp(v + (w == VK_UP ? NumberStep(*c) : -NumberStep(*c)), c->minVal, c->maxVal);
            c->set(v);
            p->m_editAdjusted = true;
            p->Changed();
            wchar_t text[64];
            swprintf_s(text, L"%s", NumberText(*c, c->Value()).c_str());
            SetWindowTextW(h, text);
            SendMessageW(h, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    return DefSubclassProc(h, msg, w, l);
}

void Direct2DHostWindow::OnLButtonDown(float x, float y)
{
    StopNumberRepeat();
    const auto alive = m_alive;
    EndEdit();
    SetFocus(m_hwnd);
    // The delete dialog takes every click; outside it counts as Cancel.
    if (!m_deleteTab.empty())
    {
        const auto inside = [&](const D2D1_RECT_F& r)
        { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
        if (inside(m_dialogConfirm))
            CloseDeleteDialog(true);
        else if (inside(m_dialogCancel) || !inside(m_dialogBox))
            CloseDeleteDialog(false);
        return;
    }
    if (m_navigationProgress > 0.f && m_navigationProgress < 1.f)
        return;
    if (!m_navigationOpen && x < 60)
    {
        if (y >= 62 && y < 62 + float(m_pages.size()) * 36)
        {
            SelectPage(int((y - 62) / 36));
        }
        if (y < 54)
            SetNavigationOpen(true);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_navigationOpen && (x > SettingsLayout::SidebarWidth || (x < 60 && y < 54)))
    {
        SetNavigationOpen(false);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    for (const auto& h : m_hitRegions)
        if (h.type == HitRegion::Scrollbar && x >= h.rect.left && x <= h.rect.right && y >= h.rect.top &&
            y <= h.rect.bottom)
        {
            m_scrollDrag = h.pageIndex;
            m_scrollTrack = h.rect;
            m_scrollThumb = h.trackRect;
            m_scrollGrab = y >= h.trackRect.top && y <= h.trackRect.bottom ? y - h.trackRect.top
                                                                           : (h.trackRect.bottom - h.trackRect.top) / 2;
            SetCapture(m_hwnd);
            OnMouseMove(x, y, MK_LBUTTON);
            return;
        }
    // Copy the region: popup/file dialogs can cause paint and rebuild the list.
    for (const auto region : m_hitRegions)
    {
        const auto& h = region;
        if (x < h.rect.left || x > h.rect.right || y < h.rect.top || y > h.rect.bottom)
            continue;
        // The search field shows focus with its border instead of a highlight;
        // a tab's close mark has no highlight of its own.
        m_pressRect = h.type == HitRegion::SearchBox || h.type == HitRegion::TabClose ? D2D1_RECT_F{} : h.rect;
        m_pressSlider = h.type == HitRegion::Slider;
        if (m_pressSlider && h.pControl)
        {
            const float center = h.trackRect.left + SliderFraction(*h.pControl, h.pControl->Value()) *
                                                        (h.trackRect.right - h.trackRect.left);
            const float middle = (h.trackRect.top + h.trackRect.bottom) / 2;
            m_pressRect = D2D1::RectF(center - 9, middle - 9, center + 9, middle + 9);
        }
        m_pressedAt = GetTickCount64();
        SetTimer(m_hwnd, 17, 16, nullptr);
        if (h.type == HitRegion::NavItem)
        {
            SetNavigationOpen(false);
            SelectPage(h.pageIndex);
        }
        else if (h.type == HitRegion::ChoiceTab && h.pControl)
        {
            if (int(h.pControl->Value()) != h.pageIndex)
            {
                h.pControl->set(float(h.pageIndex));
                Changed();
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        else if (h.type == HitRegion::TabClose)
        {
            // Opens the dialog that asks before deleting.
            const auto tabs = CardTabs(m_pages[size_t(m_activePage)]);
            if (size_t(h.pageIndex) < tabs.size())
            {
                m_deleteTab = tabs[size_t(h.pageIndex)];
                m_hoverRect = m_previousHoverRect = m_pressRect = {};
                m_hoverTab = -1;
                POINT cursor = {};
                GetCursorPos(&cursor);
                ScreenToClient(m_hwnd, &cursor);
                m_dialogMouse = D2D1::Point2F(float(cursor.x) / Scale(), float(cursor.y) / Scale());
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        else if (h.type == HitRegion::CardTab)
        {
            // The "+" tab adds a profile at once and opens its tab.
            if (m_pages[m_activePage].name == L"Profiles")
                if (const auto tabs = CardTabs(m_pages[m_activePage]);
                    size_t(h.pageIndex) < tabs.size() && tabs[size_t(h.pageIndex)] == ProfilePage::NewTab)
                {
                    ProfilePage::Create();
                    HandleProfileRequests();
                    m_profileCreated = GetTickCount64();
                    // The tabs move; drop the highlight left on the old "+" place.
                    m_hoverRect = m_previousHoverRect = m_pressRect = {};
                    m_hoverTab = -1;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
            if (m_activeCard != h.pageIndex)
            {
                m_previousCard = m_activeCard;
                m_activeCard = h.pageIndex;
                ContentTransition();
            }
            m_scrollY = 0;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        else if (h.type == HitRegion::SearchBox)
        {
            m_searchFocused = true;
            BeginEdit(nullptr, SearchEditRect(m_searchBox));
        }
        else if (h.type == HitRegion::SearchClearBtn)
        {
            m_searchQuery.clear();
            m_scrollY = 0;
            m_searchFocused = true;
            BeginEdit(nullptr, SearchEditRect(m_searchBox));
        }
        else if (h.type == HitRegion::ShaderToggle)
        {
            auto& shader = m_pSettings->userShaders.shaders[h.pageIndex];
            shader.enabled = !shader.enabled;
            Changed();
        }
        else if (h.type == HitRegion::ShaderItem)
        {
            m_selectedShader = h.pageIndex;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        else if (h.type == HitRegion::ResetBtn)
        {
            const bool shaders = m_pages[m_activePage].name == L"Shaders";
            for (auto& card : m_pages[m_activePage].cards)
            {
                if (shaders && card.title != ShaderCatalog[m_selectedShader].label)
                    continue;
                for (auto& c : card.controls)
                    // The chain order belongs to all shaders, not the selected one.
                    if (c.reset && !(shaders && c.id == L"Chain Position"))
                        ResetControl(c);
            }
            Changed();
        }
        else if (h.type == HitRegion::RenameDisplay)
        {
            auto rect = m_displayRect;
            rect.left += 10;
            rect.right -= 10;
            rect.top += 6;
            rect.bottom -= 5;
            BeginEdit(nullptr, rect, true);
            return;
        }
        else if (h.type == HitRegion::DisplayProfile)
        {
            const auto rect = h.rect;
            SelectDisplayProfile(rect);
            return;
        }
        else if (h.type == HitRegion::ResetAllBtn)
        {
            ResetAll();
            return;
        }
        else if (h.type == HitRegion::QualityPresetBtn)
        {
            const auto rect = h.rect;
            SelectQualityPreset(rect);
            return;
        }
        else if (h.type == HitRegion::ProfileSelectBtn)
        {
            const auto rect = h.rect;
            SelectProfileMenu(rect);
            return;
        }
        else if (h.type == HitRegion::ImportBtn || h.type == HitRegion::ExportBtn)
        {
            TransferIni(h.type == HitRegion::ImportBtn);
            if (!*alive)
                return;
        }
        else if (h.type == HitRegion::ApplyBtn)
        {
            const auto hr = SaveSettings(m_base);
            m_error = SUCCEEDED(hr) ? L"Settings saved." : L"Could not save settings.";
            if (SUCCEEDED(hr))
            {
                m_saved = m_base;
                CountUnsaved();
            }
        }
        else if (h.pControl)
        {
            auto c = h.pControl;
            m_searchFocused = false;
            if (h.type == HitRegion::Toggle)
            {
                c->set(c->Value() ? 0 : 1);
                Changed();
            }
            else if (h.type == HitRegion::ResetValue)
            {
                ResetControl(*c);
                Changed();
            }
            else if (h.type == HitRegion::Increment || h.type == HitRegion::Decrement)
            {
                const int direction = h.type == HitRegion::Increment ? 1 : -1;
                const float value = AdjustedNumber(*c, direction);
                if (value != c->Value())
                {
                    m_stepControl = c;
                    m_stepDirection = direction;
                    m_stepStarted = GetTickCount64();
                    SetCapture(m_hwnd);
                    SetTimer(m_hwnd, 18, 350, nullptr);
                    c->set(value);
                    Changed();
                }
            }
            else if (h.type == HitRegion::Number && c->kind == ControlKind::Text)
            {
                // Edit exactly where the drawn text sits, so it does not jump.
                auto r = h.rect;
                r.left += 8;
                r.right -= 6;
                r.top += 4;
                r.bottom -= 4;
                BeginEdit(c, r);
            }
            else if (h.type == HitRegion::Number)
            {
                auto rect = h.rect;
                rect.left += 4;
                rect.right -= 4;
                rect.top += 4;
                rect.bottom -= 4;
                BeginEdit(c, rect);
            }
            else if (h.type == HitRegion::Slider)
            {
                m_pDraggingSlider = c;
                m_draggingTrackRect = h.trackRect;
                SetCapture(m_hwnd);
                OnMouseMove(x, y, MK_LBUTTON);
            }
            else if (h.type == HitRegion::Action)
            {
                if (c->action)
                    c->action(m_hwnd);
                if (!*alive)
                    return;
                Changed();
            }
            else if (h.type == HitRegion::DropdownHeader)
            {
                ChoicePopup popup;
                const auto choices = c->choices;
                for (size_t i = 0; i < choices.size(); ++i)
                {
                    popup.labels.push_back(choices[i].label);
                    popup.descriptions.push_back(choices[i].description);
                    if (int(c->Value()) == choices[i].value)
                        popup.selected = int(i);
                }
                RECT anchor = {int(h.rect.left * Scale()),
                               int(h.rect.top * Scale()),
                               int(h.rect.right * Scale()),
                               int(h.rect.bottom * Scale())};
                MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
                const int selected = popup.Show(m_hwnd, anchor, Scale());
                if (!*alive)
                    return;
                if (selected >= 0)
                {
                    c->set(float(choices[selected].value));
                    Changed();
                }
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    m_searchFocused = false;
}

void Direct2DHostWindow::OnMouseMove(float x, float y, WPARAM flags)
{
    TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, m_hwnd, 0};
    TrackMouseEvent(&tracking);
    if (!m_deleteTab.empty())
    {
        m_dialogMouse = D2D1::Point2F(x, y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    int hovered = -1;
    if (m_navigationOpen && m_navigationProgress >= 1.f)
    {
        for (const auto& hit : m_hitRegions)
            if (hit.type == HitRegion::NavItem && x >= hit.rect.left && x <= hit.rect.right && y >= hit.rect.top &&
                y <= hit.rect.bottom)
                hovered = hit.pageIndex;
    }
    else if (!m_navigationOpen && x >= 9 && x <= 51 && y >= 62)
    {
        const int index = int((y - 62) / 36);
        if (index < int(m_pages.size()) && y < 62 + index * 36 + 32)
            hovered = index;
    }
    int tab = -1;
    bool slider = false;
    D2D1_RECT_F interactive = {};
    for (const auto& hit : m_hitRegions)
        if (x >= hit.rect.left && x <= hit.rect.right && y >= hit.rect.top && y <= hit.rect.bottom &&
            hit.type != HitRegion::Scrollbar && hit.type != HitRegion::NavItem && hit.type != HitRegion::SearchBox &&
            hit.type != HitRegion::TabClose)
        {
            if (hit.type == HitRegion::CardTab)
                tab = hit.pageIndex;
            interactive = hit.rect;
            if (hit.type == HitRegion::Slider && hit.pControl)
            {
                const float center = hit.trackRect.left + SliderFraction(*hit.pControl, hit.pControl->Value()) *
                                                              (hit.trackRect.right - hit.trackRect.left);
                const float middle = (hit.trackRect.top + hit.trackRect.bottom) / 2;
                if (std::abs(x - center) <= 12.f && std::abs(y - middle) <= 12.f)
                {
                    interactive = D2D1::RectF(center - 9, middle - 9, center + 9, middle + 9);
                    slider = true;
                }
                else
                    interactive = {};
            }
            break;
        }
    Hover(hovered, tab, interactive, slider);

    if (m_scrollDrag && (flags & MK_LBUTTON))
    {
        float thumb = m_scrollThumb.bottom - m_scrollThumb.top;
        float travel = m_scrollTrack.bottom - m_scrollTrack.top - thumb;
        float f = travel > 0 ? std::clamp((y - m_scrollTrack.top - m_scrollGrab) / travel, 0.f, 1.f) : 0;
        EndEdit();
        if (m_scrollDrag == -2)
            m_shaderOffset = int(std::round(f * (ShaderCount - 7)));
        else
            m_scrollY = f * m_maxScrollY;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_pDraggingSlider && (flags & MK_LBUTTON))
    {
        auto c = m_pDraggingSlider;
        float fraction = std::clamp(
            (x - m_draggingTrackRect.left) / (m_draggingTrackRect.right - m_draggingTrackRect.left), 0.f, 1.f);
        float value = SliderValue(*c, fraction);
        if (c->stepVal > 0)
            value = std::round(value / c->stepVal) * c->stepVal;
        c->set(std::clamp(value, c->minVal, c->maxVal));
        Changed();
    }
}

void Direct2DHostWindow::OnLButtonUp(float x, float y)
{
    if (m_stepControl)
    {
        StopNumberRepeat();
        if (GetCapture() == m_hwnd)
            ReleaseCapture();
    }
    if (m_scrollDrag)
    {
        m_scrollDrag = 0;
        ReleaseCapture();
    }
    if (m_pDraggingSlider)
    {
        m_pDraggingSlider = nullptr;
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Direct2DHostWindow::OnMouseWheel(short delta, float x, float y)
{
    if (!m_deleteTab.empty())
        return;
    if (m_shaderList.right > m_shaderList.left && x >= m_shaderList.left && x <= m_shaderList.right &&
        y >= m_shaderList.top && y <= m_shaderList.bottom)
    {
        m_shaderOffset = std::clamp(m_shaderOffset - int(delta / 120) * 3, 0, ShaderCount - 7);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    EndEdit();
    m_scrollY = std::clamp(m_scrollY - static_cast<float>(delta) * 0.45f, 0.0f, m_maxScrollY);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::OnChar(wchar_t ch)
{
    if (!m_searchFocused)
        return;
    if (ch == VK_BACK)
    {
        if (!m_searchQuery.empty())
            m_searchQuery.pop_back();
    }
    else if (ch >= 32)
    {
        m_searchQuery.push_back(ch);
    }
    m_scrollY = 0;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::OnKeyDown(WPARAM key)
{
    if (!m_deleteTab.empty())
    {
        if (key == VK_ESCAPE || key == VK_RETURN)
            CloseDeleteDialog(key == VK_RETURN);
        return;
    }
    if (key == VK_ESCAPE && m_navigationOpen)
    {
        EndEdit();
        SetNavigationOpen(false);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (key == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        SetNavigationOpen(false);
        m_searchFocused = true;
        BeginEdit(nullptr, SearchEditRect(m_searchBox));
        return;
    }
    if (key == VK_NEXT || key == VK_PRIOR || key == VK_HOME || key == VK_END)
    {
        EndEdit();
        m_scrollY = key == VK_HOME ? 0
                    : key == VK_END
                        ? m_maxScrollY
                        : std::clamp(m_scrollY + (key == VK_NEXT ? 1 : -1) * m_pRenderTarget->GetSize().height * .8f,
                                     0.f,
                                     m_maxScrollY);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (key == VK_TAB && !m_hitRegions.empty())
    {
        const int n = int(m_hitRegions.size());
        m_keyboardIndex = (m_keyboardIndex + ((GetKeyState(VK_SHIFT) & 0x8000) ? n - 1 : 1) + n) % n;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_keyboardIndex >= 0 && m_keyboardIndex < int(m_hitRegions.size()))
    {
        const auto h = m_hitRegions[m_keyboardIndex];
        if (key == VK_SPACE || key == VK_RETURN)
        {
            const auto alive = m_alive;
            OnLButtonDown((h.rect.left + h.rect.right) / 2, (h.rect.top + h.rect.bottom) / 2);
            if (*alive)
                OnLButtonUp(0, 0);
            return;
        }
        if ((key == VK_UP || key == VK_DOWN) && h.pControl &&
            (h.pControl->kind == ControlKind::SliderInt || h.pControl->kind == ControlKind::SliderFloat))
        {
            auto c = h.pControl;
            c->set(AdjustedNumber(*c, key == VK_UP ? 1 : -1));
            Changed();
            return;
        }
    }
    if (key == VK_ESCAPE)
    {
        if (m_searchFocused)
        {
            m_searchFocused = false;
            m_searchQuery.clear();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

LRESULT CALLBACK Direct2DHostWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Direct2DHostWindow* pThis = reinterpret_cast<Direct2DHostWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTTAB | DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        static_cast<Direct2DHostWindow*>(cs->lpCreateParams)->m_hwnd = hwnd;
        return TRUE;
    }
    case WM_APP + 301:
        if (pThis)
            pThis->Preview();
        return 0;
    case WM_DISPLAYCHANGE:
        if (pThis && pThis->m_displayProfiles)
        {
            EnsureDisplayProfiles(DisplayProfiles::Connected(), LoadSettings());
            pThis->m_devices = DisplayProfiles::All();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_COMMAND:
        if (pThis && LOWORD(wParam) == 7001 && HIWORD(wParam) == EN_CHANGE)
            pThis->EditChanged();
        return 0;
    case WM_CTLCOLOREDIT:
        if (pThis)
        {
            const auto palette = CurrentMenuPalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), MenuColorRef(palette.textMain));
            SetBkColor(reinterpret_cast<HDC>(wParam), MenuColorRef(palette.input));
            return reinterpret_cast<LRESULT>(pThis->m_editBrush);
        }
        break;
    case WM_CAPTURECHANGED:
        if (pThis)
        {
            pThis->StopNumberRepeat();
            pThis->m_pDraggingSlider = nullptr;
            pThis->m_scrollDrag = 0;
        }
        return 0;
    case WM_DPICHANGED_AFTERPARENT:
        if (pThis)
        {
            pThis->EndEdit();
            pThis->DiscardDeviceResources();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (pThis)
            pThis->Render();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
    {
        if (pThis && pThis->m_pRenderTarget)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            const auto previous = pThis->m_pRenderTarget->GetPixelSize();
            if (!size.width || !size.height || (previous.width == size.width && previous.height == size.height))
                return 0;
            pThis->EndEdit();
            if (FAILED(pThis->m_pRenderTarget->Resize(size)))
                pThis->DiscardDeviceResources();
            // Resize discards the back buffer; paint it before the frame can
            // present the newly exposed area with stale or empty contents.
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (pThis)
            pThis->OnLButtonDown(float(GET_X_LPARAM(lParam)) / pThis->Scale(),
                                 float(GET_Y_LPARAM(lParam)) / pThis->Scale());
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        if (pThis)
        {
            // A double-click on "+" creates one profile, not two or a rename.
            if (!pThis->m_deleteTab.empty() || GetTickCount64() - pThis->m_profileCreated <= GetDoubleClickTime())
                return 0;
            float x = float(GET_X_LPARAM(lParam)) / pThis->Scale(), y = float(GET_Y_LPARAM(lParam)) / pThis->Scale();
            bool toggled = false;
            for (const auto& h : pThis->m_hitRegions)
                if (h.type == HitRegion::ShaderItem && x >= h.rect.left && x <= h.rect.right && y >= h.rect.top &&
                    y <= h.rect.bottom)
                {
                    auto& shader = pThis->m_pSettings->userShaders.shaders[h.pageIndex];
                    shader.enabled = !shader.enabled;
                    pThis->Changed();
                    toggled = true;
                    break;
                }
            // A profile tab is renamed in place; its close mark keeps its clicks.
            const auto& active = pThis->m_pages[size_t(pThis->m_activePage)];
            const auto inside = [&](const HitRegion& h)
            { return x >= h.rect.left && x <= h.rect.right && y >= h.rect.top && y <= h.rect.bottom; };
            const bool onClose =
                std::any_of(pThis->m_hitRegions.begin(),
                            pThis->m_hitRegions.end(),
                            [&](const HitRegion& h) { return h.type == HitRegion::TabClose && inside(h); });
            for (const auto& h : pThis->m_hitRegions)
                if (!toggled && !onClose && h.type == HitRegion::CardTab && active.name == L"Profiles" && inside(h))
                {
                    const auto tabs = CardTabs(active);
                    if (size_t(h.pageIndex) < tabs.size() && tabs[size_t(h.pageIndex)] != ProfilePage::NewTab &&
                        tabs[size_t(h.pageIndex)] != ProfilePage::DefaultTab)
                    {
                        // The field takes the label's place inside the tab.
                        pThis->m_tabRename = ProfilePage::RenameControl(tabs[size_t(h.pageIndex)]);
                        pThis->m_hoverRect = pThis->m_previousHoverRect = pThis->m_pressRect = {};
                        pThis->BeginEdit(
                            &pThis->m_tabRename,
                            D2D1::RectF(
                                h.rect.left + 12.f, h.rect.top + 6.f, h.rect.right - 30.f, h.rect.bottom - 6.f));
                        toggled = true;
                    }
                    break;
                }
            // Elsewhere a quick second click is an ordinary click (e.g. the menu button).
            if (!toggled)
                pThis->OnLButtonDown(x, y);
        }
        return 0;
    }
    case WM_TIMER:
        if (pThis && wParam == 18)
        {
            auto control = pThis->m_stepControl;
            if (!control || !control->Enabled() || GetCapture() != hwnd)
            {
                pThis->StopNumberRepeat();
                return 0;
            }
            const auto held = GetTickCount64() - pThis->m_stepStarted;
            const int multiplier = held >= 4500 ? 1000 : held >= 3000 ? 100 : held >= 1500 ? 10 : 1;
            const float value = AdjustedNumber(*control, pThis->m_stepDirection, multiplier);
            if (value == control->Value())
            {
                pThis->StopNumberRepeat();
                ReleaseCapture();
                return 0;
            }
            SetTimer(hwnd, 18, 75, nullptr);
            control->set(value);
            pThis->Changed();
            return 0;
        }
        if (pThis && wParam == 17)
        {
            pThis->Animate();
            return 0;
        }
        if (pThis && wParam == 20)
        {
            KillTimer(hwnd, 20);
            pThis->m_toast.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (pThis && wParam == 19)
        {
            if (pThis->m_measurement && pThis->m_measurement->done)
                pThis->FinishMeasurement();
            else
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (pThis)
            pThis->Hover(-1);
        return 0;
    case WM_MOUSEMOVE:
    {
        if (pThis)
            pThis->OnMouseMove(
                float(GET_X_LPARAM(lParam)) / pThis->Scale(), float(GET_Y_LPARAM(lParam)) / pThis->Scale(), wParam);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (pThis)
            pThis->OnLButtonUp(float(GET_X_LPARAM(lParam)) / pThis->Scale(),
                               float(GET_Y_LPARAM(lParam)) / pThis->Scale());
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        if (pThis)
        {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            pThis->OnMouseWheel(
                GET_WHEEL_DELTA_WPARAM(wParam), float(pt.x) / pThis->Scale(), float(pt.y) / pThis->Scale());
        }
        return 0;
    }
    case WM_CHAR:
    {
        if (pThis)
            pThis->OnChar(static_cast<wchar_t>(wParam));
        return 0;
    }
    case WM_KEYDOWN:
    {
        if (pThis)
            pThis->OnKeyDown(wParam);
        return 0;
    }
    case WM_SETFOCUS:
    {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KILLFOCUS:
    {
        if (pThis)
        {
            pThis->StopNumberRepeat();
            pThis->m_searchFocused = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // Prevent flicker
    case WM_NCDESTROY:
    {
        if (pThis)
        {
            delete pThis;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND Direct2DSettingsHost::Create(HWND parent,
                                  const RECT& bounds,
                                  PlaceboSettings* settings,
                                  bool standalone,
                                  std::function<HRESULT(const PlaceboSettings&)> preview,
                                  bool displayProfiles)
{
    LoadMenuAppearance();
    LoadMenuLanguage();
    HMODULE hInstance = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Direct2DSettingsHost::Create),
                       &hInstance);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = Direct2DHostWindow::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FrankyDirect2DSettingsHost";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;

    Direct2DHostWindow* pHost = new Direct2DHostWindow(nullptr, settings, standalone);

    pHost->m_preview = std::move(preview);
    pHost->m_displayProfiles = displayProfiles;
    if (displayProfiles)
        pHost->m_devices = DisplayProfiles::All();

    DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP;
    if (standalone)
        style = WS_CHILD | WS_VISIBLE;

    HWND hwnd = CreateWindowExW(
        0, L"FrankyDirect2DSettingsHost", L"", style, bounds.left, bounds.top, w, h, parent, nullptr, hInstance, pHost);

    if (hwnd)
    {
        pHost->m_hwnd = hwnd;
    }

    return hwnd;
}

void Direct2DSettingsHost::Destroy(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
    {
        auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        DestroyWindow(hwnd);
        UnregisterClassW(L"FrankyDirect2DSettingsHost", instance);
    }
}

void Direct2DSettingsHost::Resize(HWND hwnd, const RECT& bounds)
{
    if (hwnd && IsWindow(hwnd))
    {
        SetWindowPos(hwnd,
                     nullptr,
                     bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Direct2DSettingsHost::Save(HWND hwnd, PlaceboSettings* outSettings)
{
    if (!hwnd || !outSettings)
        return;
    Direct2DHostWindow* pThis = reinterpret_cast<Direct2DHostWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (pThis)
        *outSettings = pThis->m_base;
}

std::vector<std::pair<std::wstring, std::wstring>> Direct2DSettingsHost::TranslatableTexts()
{
    std::vector<std::pair<std::wstring, std::wstring>> out;
    auto add = [&](const std::wstring& text, const std::wstring& note)
    {
        // Technical names (filter ids such as ewa_lanczos) and plain numbers stay as they are.
        const bool technical =
            std::all_of(text.begin(),
                        text.end(),
                        [](wchar_t c) { return iswlower(c) || iswdigit(c) || c == L'_' || c == L'.' || c == L'-'; });
        if (!text.empty() && !technical && std::any_of(text.begin(), text.end(), iswalpha))
            out.emplace_back(text, note);
    };
    PlaceboSettings draft;
    ProfilePage::g_template = true; // no user profiles or folders in the template
    const auto pages = PreviewPages(draft);
    ProfilePage::g_template = false;
    for (const auto& page : pages)
    {
        add(page.name, L"Category");
        add(page.title, L"Category heading");
        add(page.subtitle, L"Category description");
        for (const auto& tab : CardTabs(page))
            add(tab, L"Tab (" + page.name + L")");
        for (const auto& card : page.cards)
        {
            const auto where = page.name + L" > " + card.title;
            add(card.title, L"Section heading (" + page.name + L")");
            add(card.subtitle, L"Section description (" + where + L")");
            for (const auto& c : card.controls)
            {
                add(c.name, L"Setting name (" + where + L")");
                add(c.desc, L"Setting description (" + where + L" > " + c.name + L")");
                add(c.placeholder, L"Input hint (" + where + L" > " + c.name + L")");
                add(c.invalidText, L"Input error (" + where + L" > " + c.name + L")");
                add(c.actionLabel, L"Button (" + where + L" > " + c.name + L")");
                for (const auto& choice : c.choices)
                {
                    add(choice.label, L"Choice (" + where + L" > " + c.name + L")");
                    add(choice.description, L"Choice description (" + where + L" > " + c.name + L")");
                }
            }
        }
    }
    for (const auto* text : MenuTexts)
        add(text, L"Menu text; keep %d and %s placeholders");
    for (size_t i = 0; i < std::size(QualityPresetNames); ++i)
    {
        add(QualityPresetNames[i], L"Quality preset (header)");
        add(QualityPresetDescriptions[i], L"Quality preset description (header)");
    }
    return out;
}
