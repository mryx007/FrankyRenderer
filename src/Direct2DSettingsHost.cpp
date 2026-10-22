#include "Direct2DSettingsHost.h"
#include "SettingsIni.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cerrno>
#include "ChoicePopup.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

enum class ControlKind
{
    Toggle,
    SliderInt,
    SliderFloat,
    Choice,
    Action
};

struct ChoiceItem
{
    int value;
    std::wstring label;
    std::wstring description;
};

struct D2DControl
{
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
    std::wstring actionLabel;
    bool headerToggle = false;

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
};

struct D2DPage
{
    std::wstring name;
    std::wstring icon;
    std::wstring title;
    std::wstring subtitle;
    std::vector<D2DCard> cards;
};

#include "Direct2DSettingsModel.h"

static float SliderFraction(const D2DControl& c, float value)
{
    const double low = c.minVal, high = c.maxVal, neutral = std::clamp(double(c.Default()), low, high);
    if (neutral <= low || neutral >= high)
        return high > low ? float(std::clamp((double(value) - low) / (high - low), 0., 1.)) : 0.f;
    if (value == neutral)
        return .5f;
    const double fraction = value < neutral
                                ? (neutral > low ? .5 * (double(value) - low) / (neutral - low) : .5)
                                : (high > neutral ? .5 + .5 * (double(value) - neutral) / (high - neutral) : .5);
    return float(std::clamp(fraction, 0., 1.));
}

static float SliderValue(const D2DControl& c, float fraction)
{
    const double low = c.minVal, high = c.maxVal, neutral = std::clamp(double(c.Default()), low, high);
    if (neutral <= low || neutral >= high)
        return float(low + std::clamp(double(fraction), 0., 1.) * (high - low));
    return float(fraction <= .5f ? low + 2 * double(fraction) * (neutral - low)
                                 : neutral + 2 * (double(fraction) - .5) * (high - neutral));
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
        ApplyBtn,
        ImportBtn,
        ExportBtn,
        DisplayProfile,
        RenameDisplay,
        Number,
        Action,
        ResetValue,
        Scrollbar,
        ShaderItem
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
    return MatchesFilter(page.name + L" " + card.title + L" " + control.name + L" " + control.desc, query);
}

class Direct2DHostWindow
{
  public:
    HWND m_hwnd = nullptr;
    PlaceboSettings* m_pSettings = nullptr;
    bool m_standalone = false;
    bool m_displayProfiles = false;
    std::vector<DisplayProfiles::Device> m_devices;
    std::function<HRESULT(const PlaceboSettings&)> m_preview;
    PlaceboSettings m_accepted, m_applied;
    HWND m_edit = nullptr;
    bool m_renamingDisplay = false;
    D2D1_RECT_F m_displayRect = {};
    D2DControl* m_editControl = nullptr;
    HFONT m_editFont = nullptr;
    HBRUSH m_editBrush = CreateSolidBrush(RGB(18, 19, 22));
    std::wstring m_error;
    bool m_pending = false;
    int m_keyboardIndex = -1;

    float Scale() const
    {
        return float(GetDpiForWindow(m_hwnd)) / 96.f;
    }

    void Changed();
    void Preview();
    void SelectDisplayProfile(const D2D1_RECT_F& rect);
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
    ComPtr<IDWriteTextFormat> m_fmtValue;
    ComPtr<IDWriteTextFormat> m_fmtIcon;

    // Layout & State
    int m_activePage = 0;
    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    int m_selectedShader = 0, m_shaderOffset = 0;
    int m_scrollDrag = 0;
    float m_scrollGrab = 0;
    D2D1_RECT_F m_scrollTrack = {}, m_scrollThumb = {}, m_shaderList = {};
    void DrawScrollbar(D2D1_RECT_F track, float offset, float extent, float viewport, int target);
    std::wstring m_searchQuery;
    bool m_searchFocused = false;

    // Dragging / Active interaction
    D2DControl* m_pDraggingSlider = nullptr;
    D2D1_RECT_F m_draggingTrackRect = {};

    // Hit regions collected during render pass
    std::vector<HitRegion> m_hitRegions;

    // Pages model
    std::vector<D2DPage> m_pages;

    Direct2DHostWindow(HWND hwnd, PlaceboSettings* settings, bool standalone)
        : m_hwnd(hwnd), m_pSettings(settings), m_standalone(standalone)
    {
        m_accepted = m_applied = *settings;
        InitPages();
        InitD2D();
    }

    ~Direct2DHostWindow()
    {
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

void Direct2DHostWindow::InitPages()
{
    m_pages = BuildD2DPages(*m_pSettings);
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
                                       12.0f,
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
                                       11.0f,
                                       L"en-us",
                                       m_fmtCardSubtitle.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       13.0f,
                                       L"en-us",
                                       m_fmtLabel.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       11.0f,
                                       L"en-us",
                                       m_fmtDesc.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_MEDIUM,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       12.0f,
                                       L"en-us",
                                       m_fmtValue.GetAddressOf());
    m_pDWriteFactory->CreateTextFormat(fontName,
                                       nullptr,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                       DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL,
                                       26.0f,
                                       L"en-us",
                                       m_fmtIcon.GetAddressOf());

    if (!m_fmtTitle || !m_fmtSubtitle || !m_fmtNav || !m_fmtCardTitle || !m_fmtCardSubtitle || !m_fmtLabel ||
        !m_fmtDesc || !m_fmtValue || !m_fmtIcon)
        return E_FAIL;
    if (m_fmtDesc)
        m_fmtDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (m_fmtValue)
        m_fmtValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
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
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(15.0f / 255.0f, 16.0f / 255.0f, 18.0f / 255.0f),
                                           m_brushBgApp.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(21.0f / 255.0f, 22.0f / 255.0f, 24.0f / 255.0f),
                                           m_brushBgSidebar.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(24.0f / 255.0f, 25.0f / 255.0f, 28.0f / 255.0f),
                                           m_brushBgCard.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(30.0f / 255.0f, 32.0f / 255.0f, 36.0f / 255.0f),
                                           m_brushBgCardHeader.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(39.0f / 255.0f, 40.0f / 255.0f, 45.0f / 255.0f),
                                           m_brushBorderCard.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f), m_brushBorderSubtle.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(241.0f / 255.0f, 245.0f / 255.0f, 249.0f / 255.0f),
                                           m_brushTextMain.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(190.0f / 255.0f, 198.0f / 255.0f, 210.0f / 255.0f),
                                           m_brushTextMuted.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(164.0f / 255.0f, 176.0f / 255.0f, 190.0f / 255.0f),
                                           m_brushTextDim.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(203.0f / 255.0f, 213.0f / 255.0f, 225.0f / 255.0f),
                                           m_brushAccent.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), m_brushAccentHover.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(18.0f / 255.0f, 19.0f / 255.0f, 22.0f / 255.0f),
                                           m_brushInputBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.04f), m_brushHoverBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(36.0f / 255.0f, 38.0f / 255.0f, 43.0f / 255.0f),
                                           m_brushActiveBg.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(71.0f / 255.0f, 85.0f / 255.0f, 105.0f / 255.0f),
                                           m_brushToggleOn.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(39.0f / 255.0f, 40.0f / 255.0f, 45.0f / 255.0f),
                                           m_brushToggleOff.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(39.0f / 255.0f, 40.0f / 255.0f, 45.0f / 255.0f),
                                           m_brushSliderTrack.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(164.0f / 255.0f, 176.0f / 255.0f, 190.0f / 255.0f),
                                           m_brushSliderFill.GetAddressOf());
    m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(226.0f / 255.0f, 232.0f / 255.0f, 240.0f / 255.0f),
                                           m_brushSliderThumb.GetAddressOf());

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
        !m_fmtDesc || !m_fmtValue || !m_fmtIcon)
        return;
    if (FAILED(CreateDeviceResources()))
        return;

    m_hitRegions.clear();
    m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    D2D1_SIZE_F size = m_pRenderTarget->GetSize();
    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear(D2D1::ColorF(15.0f / 255.0f, 16.0f / 255.0f, 18.0f / 255.0f));

    const float sidebarW = 240.0f;

    // 1. Sidebar Background
    D2D1_RECT_F sidebarRect = D2D1::RectF(0, 0, sidebarW, size.height);
    m_pRenderTarget->FillRectangle(sidebarRect, m_brushBgSidebar.Get());
    m_pRenderTarget->DrawLine(
        D2D1::Point2F(sidebarW, 0), D2D1::Point2F(sidebarW, size.height), m_brushBorderCard.Get(), 1.0f);

    // 2. Search Input in Sidebar
    D2D1_RECT_F searchRect = D2D1::RectF(16, 16, sidebarW - 16, 48);
    D2D1_ROUNDED_RECT roundedSearch = D2D1::RoundedRect(searchRect, 6.0f, 6.0f);
    m_pRenderTarget->FillRoundedRectangle(roundedSearch, m_brushInputBg.Get());
    m_pRenderTarget->DrawRoundedRectangle(
        roundedSearch, m_searchFocused ? m_brushAccent.Get() : m_brushBorderCard.Get(), m_searchFocused ? 1.5f : 1.0f);

    D2D1_RECT_F searchPromptRect = D2D1::RectF(26, 23, sidebarW - 46, 43);
    const bool nativeSearch = m_edit && !m_editControl && !m_renamingDisplay;
    if (nativeSearch)
    {
        if (!m_searchQuery.empty())
        {
            auto clear = D2D1::RectF(sidebarW - 38, 20, sidebarW - 20, 44);
            m_pRenderTarget->DrawText(L"✕", 1, m_fmtNav.Get(), clear, m_brushTextMuted.Get());
            HitRegion hit;
            hit.type = HitRegion::SearchClearBtn;
            hit.rect = clear;
            m_hitRegions.push_back(hit);
        }
    }
    else if (m_searchQuery.empty())
    {
        if (m_searchFocused)
        {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(26.0f, 22.0f), D2D1::Point2F(26.0f, 42.0f), m_brushAccentHover.Get(), 1.5f);
            D2D1_RECT_F hintRect = D2D1::RectF(32, 23, sidebarW - 46, 43);
            m_pRenderTarget->DrawText(L"Type to filter...", 16, m_fmtNav.Get(), hintRect, m_brushTextDim.Get());
        }
        else
        {
            m_pRenderTarget->DrawText(
                L"🔍  Filter settings...", 21, m_fmtNav.Get(), searchPromptRect, m_brushTextDim.Get());
        }
    }
    else
    {
        m_pRenderTarget->DrawText(m_searchQuery.c_str(),
                                  (UINT32)m_searchQuery.length(),
                                  m_fmtNav.Get(),
                                  searchPromptRect,
                                  m_brushTextMain.Get());
        if (m_searchFocused && m_pDWriteFactory)
        {
            float textW = 0.0f;
            ComPtr<IDWriteTextLayout> pLayout;
            HRESULT hrLayout = m_pDWriteFactory->CreateTextLayout(m_searchQuery.c_str(),
                                                                  (UINT32)m_searchQuery.length(),
                                                                  m_fmtNav.Get(),
                                                                  1000.0f,
                                                                  100.0f,
                                                                  pLayout.GetAddressOf());
            if (SUCCEEDED(hrLayout) && pLayout)
            {
                DWRITE_TEXT_METRICS metrics = {};
                pLayout->GetMetrics(&metrics);
                textW = metrics.widthIncludingTrailingWhitespace;
            }
            float caretX = std::min(26.0f + textW + 1.0f, sidebarW - 46.0f);
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(caretX, 22.0f), D2D1::Point2F(caretX, 42.0f), m_brushAccentHover.Get(), 1.5f);
        }

        // Clear (x) button
        D2D1_RECT_F clearRect = D2D1::RectF(sidebarW - 38, 20, sidebarW - 20, 44);
        m_pRenderTarget->DrawText(L"✕", 1, m_fmtNav.Get(), clearRect, m_brushTextDim.Get());

        HitRegion hitClear;
        hitClear.type = HitRegion::SearchClearBtn;
        hitClear.rect = clearRect;
        m_hitRegions.push_back(hitClear);
    }

    HitRegion hitSearch;
    hitSearch.type = HitRegion::SearchBox;
    hitSearch.rect = searchRect;
    m_hitRegions.push_back(hitSearch);

    // 3. Navigation List
    float navY = 64.0f;
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        D2D1_RECT_F itemRect = D2D1::RectF(12, navY, sidebarW - 12, navY + 40);
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
        else if (isSearching && hasMatches)
        {
            m_pRenderTarget->FillRoundedRectangle(roundedItem, m_brushHoverBg.Get());
        }

        ID2D1SolidColorBrush* textBrush = m_brushTextMuted.Get();
        if (isActive)
            textBrush = m_brushTextMain.Get();
        else if (isSearching && !hasMatches)
            textBrush = m_brushTextDim.Get();
        else if (isSearching && hasMatches)
            textBrush = m_brushTextMain.Get();

        D2D1_RECT_F textRect = D2D1::RectF(28, navY + 10, sidebarW - 12, navY + 35);
        m_pRenderTarget->DrawText(
            m_pages[i].name.c_str(), (UINT32)m_pages[i].name.length(), m_fmtNav.Get(), textRect, textBrush);

        HitRegion hitNav;
        hitNav.type = HitRegion::NavItem;
        hitNav.rect = itemRect;
        hitNav.pageIndex = static_cast<int>(i);
        m_hitRegions.push_back(hitNav);

        navY += 44.0f;
    }

    // 4. Sidebar Bottom Action Buttons
    float btnY = size.height - 48.0f;
    const float transferY = btnY - (m_standalone ? 44.f : 0.f);
    if (m_displayProfiles)
    {
        const auto deviceRect = D2D1::RectF(16, transferY - 48, 224, transferY - 14);
        m_displayRect = deviceRect;
        auto caption = D2D1::RectF(16, transferY - 70, 140, transferY - 48);
        m_pRenderTarget->DrawText(L"Display", 7, m_fmtNav.Get(), caption, m_brushTextMuted.Get());
        const auto renameRect = D2D1::RectF(157, transferY - 72, 224, transferY - 49);
        m_pRenderTarget->DrawText(L"Rename", 6, m_fmtNav.Get(), renameRect, m_brushTextMain.Get());
        HitRegion renameHit;
        renameHit.type = HitRegion::RenameDisplay;
        renameHit.rect = renameRect;
        m_hitRegions.push_back(renameHit);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(deviceRect, 6, 6), m_brushInputBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(deviceRect, 6, 6), m_brushBorderCard.Get(), 1.f);
        auto name = std::wstring(L"Display");
        for (const auto& device : m_devices)
            if (device.id == m_pSettings->displayProfile)
                name = device.name + (device.connected ? L"" : L" (Disconnected)");
        auto text = deviceRect;
        text.left += 12;
        text.top += 8;
        text.right -= 28;
        const auto oldWrap = m_fmtNav->GetWordWrapping();
        m_fmtNav->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_pRenderTarget->DrawText(name.c_str(),
                                  UINT32(name.size()),
                                  m_fmtNav.Get(),
                                  text,
                                  m_brushTextMain.Get(),
                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_fmtNav->SetWordWrapping(oldWrap);
        const float x = deviceRect.right - 16, y = deviceRect.top + 16;
        m_pRenderTarget->DrawLine(D2D1::Point2F(x - 5, y - 2), D2D1::Point2F(x, y + 3), m_brushTextMuted.Get(), 1.8f);
        m_pRenderTarget->DrawLine(D2D1::Point2F(x, y + 3), D2D1::Point2F(x + 5, y - 2), m_brushTextMuted.Get(), 1.8f);
        HitRegion deviceHit;
        deviceHit.type = HitRegion::DisplayProfile;
        deviceHit.rect = deviceRect;
        m_hitRegions.push_back(deviceHit);
    }
    for (int i = 0; i < 2; ++i)
    {
        const float left = i ? 124.f : 16.f;
        const auto rect = D2D1::RectF(left, transferY, left + 100.f, transferY + 34.f);
        m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), m_brushInputBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), m_brushBorderCard.Get(), 1.f);
        auto text = rect;
        text.left += 12;
        text.top += 8;
        m_pRenderTarget->DrawText(i ? L"Export" : L"Import", 6, m_fmtNav.Get(), text, m_brushTextMain.Get());
        HitRegion hit;
        hit.type = i ? HitRegion::ExportBtn : HitRegion::ImportBtn;
        hit.rect = rect;
        m_hitRegions.push_back(hit);
    }
    if (m_standalone)
    {
        D2D1_RECT_F applyRect = D2D1::RectF(124, btnY, sidebarW - 16, btnY + 34);
        D2D1_ROUNDED_RECT roundedApply = D2D1::RoundedRect(applyRect, 6.0f, 6.0f);
        m_pRenderTarget->FillRoundedRectangle(roundedApply, m_brushActiveBg.Get());
        m_pRenderTarget->DrawRoundedRectangle(roundedApply, m_brushBorderCard.Get(), 1.0f);
        D2D1_RECT_F applyTextRect = D2D1::RectF(124, btnY + 8, sidebarW - 16, btnY + 30);
        m_pRenderTarget->DrawText(m_standalone ? L"Save" : L"Live Preview",
                                  m_standalone ? 4 : 12,
                                  m_fmtNav.Get(),
                                  applyTextRect,
                                  m_brushTextMain.Get());
        if (m_fmtNav)
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        HitRegion hitApply;
        hitApply.type = HitRegion::ApplyBtn;
        hitApply.rect = applyRect;
        if (m_standalone)
            m_hitRegions.push_back(hitApply);
    }
    if (!m_error.empty())
    {
        D2D1_RECT_F r = D2D1::RectF(16, transferY - 65, 224, transferY - 8);
        m_pRenderTarget->DrawText(
            m_error.c_str(), (UINT32)m_error.size(), m_fmtDesc.Get(), r, m_brushAccentHover.Get());
    }

    // 5. Content Area
    const float contentX = sidebarW + 28.0f;
    const float contentW = size.width - contentX - 36.0f;

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
    float currentY = 24.0f - m_scrollY;

    if (!isSearching)
    {
        if (m_activePage >= 0 && m_activePage < static_cast<int>(m_pages.size()))
        {
            const auto& page = m_pages[m_activePage];

            auto reset = D2D1::RectF(contentX + contentW - 118, currentY, contentX + contentW, currentY + 32);
            m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(reset, 5, 5), m_brushInputBg.Get());
            auto resetText = reset;
            resetText.left += 16;
            resetText.top += 8;
            m_pRenderTarget->DrawText(L"Reset Page", 10, m_fmtNav.Get(), resetText, m_brushTextMuted.Get());
            HitRegion resetHit;
            resetHit.type = HitRegion::ResetBtn;
            resetHit.rect = reset;
            if (reset.bottom > 0)
                m_hitRegions.push_back(resetHit);
            // Page Header
            D2D1_RECT_F titleRect = D2D1::RectF(contentX, currentY, contentX + contentW - 132, currentY + 28.0f);
            m_pRenderTarget->DrawText(
                page.title.c_str(), (UINT32)page.title.length(), m_fmtTitle.Get(), titleRect, m_brushTextMain.Get());

            D2D1_RECT_F subRect = D2D1::RectF(contentX, currentY + 34.0f, contentX + contentW, currentY + 54.0f);
            m_pRenderTarget->DrawText(page.subtitle.c_str(),
                                      (UINT32)page.subtitle.length(),
                                      m_fmtSubtitle.Get(),
                                      subRect,
                                      m_brushTextMuted.Get());

            currentY += 70.0f;

            for (const auto& card : page.cards)
            {
                if (page.name == L"Shaders" && card.title != ShaderCatalog[m_selectedShader].label)
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
        D2D1_RECT_F titleRect = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + 28.0f);
        m_pRenderTarget->DrawText(L"Search Results", 14, m_fmtTitle.Get(), titleRect, m_brushTextMain.Get());

        std::wstring queryHint = L"Matching settings for: \"" + m_searchQuery + L"\"";
        D2D1_RECT_F subRect = D2D1::RectF(contentX, currentY + 34.0f, contentX + contentW, currentY + 54.0f);
        m_pRenderTarget->DrawText(
            queryHint.c_str(), (UINT32)queryHint.length(), m_fmtSubtitle.Get(), subRect, m_brushTextMuted.Get());

        currentY += 70.0f;

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
        m_pRenderTarget->DrawText(
            L"No settings matching this term found.", 37, m_fmtNav.Get(), emptyRect, m_brushTextDim.Get());
        if (m_fmtNav)
            m_fmtNav->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

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
            std::wstring text = (m_pSettings->userShaders.shaders[id].enabled ? L"[On]  " : L"[Off]  ") +
                                std::wstring(ShaderCatalog[id].label);
            auto label = r;
            label.left += 10;
            label.top += 7;
            m_pRenderTarget->DrawText(text.c_str(), UINT32(text.size()), m_fmtNav.Get(), label, m_brushTextMain.Get());
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
        auto text = button;
        text.left += 18;
        text.top += 7;
        m_pRenderTarget->DrawText(L"All Off", 7, m_fmtNav.Get(), text, m_brushTextMain.Get());
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
        const bool on = ctrl.Value() != 0;
        m_pRenderTarget->FillRoundedRectangle(rounded, on ? m_brushToggleOn.Get() : m_brushToggleOff.Get());
        m_pRenderTarget->DrawRoundedRectangle(rounded, m_brushBorderCard.Get(), 1.f);
        m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + (on ? 29.f : 11.f), y + 11.f), 7.5f, 7.5f),
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
    for (const auto& item : cardsToRender)
    {
        const auto& card = *item.pCard;
        const auto& visibleControls = item.visibleControls;

        const bool compact = !isSearching && (m_pages[m_activePage].name == L"Black Bars / Zoom" ||
                                              m_pages[m_activePage].name == L"Color Adjustment" ||
                                              m_pages[m_activePage].name == L"Color Management");
        const float baseRowHeight = compact ? 70.f : 88.f;
        std::vector<float> rowHeights;
        float cardHeight = 52.f;
        for (const auto* control : visibleControls)
        {
            if (control->headerToggle)
            {
                rowHeights.push_back(0);
                continue;
            }
            auto description = control->detail ? control->detail() : control->desc;
            if (description.empty())
                description = control->desc;
            ComPtr<IDWriteTextLayout> layout;
            float textHeight = 0;
            if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(description.c_str(),
                                                             UINT32(description.size()),
                                                             m_fmtDesc.Get(),
                                                             std::max(1.f, contentW - 316.f),
                                                             4096.f,
                                                             &layout)))
            {
                DWRITE_TEXT_METRICS metrics = {};
                if (SUCCEEDED(layout->GetMetrics(&metrics)))
                    textHeight = metrics.height;
            }
            const float height = std::max(baseRowHeight, 30.f + textHeight + (compact ? 8.f : 28.f));
            rowHeights.push_back(height);
            cardHeight += height;
        }
        D2D1_RECT_F cardRect = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + cardHeight);
        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 8.0f, 8.0f);

        // Card background & border
        m_pRenderTarget->FillRoundedRectangle(roundedCard, m_brushBgCard.Get());
        m_pRenderTarget->DrawRoundedRectangle(roundedCard, m_brushBorderCard.Get(), 1.0f);

        // Card Header (50px height with clean padding & separation)
        D2D1_RECT_F headerRect = D2D1::RectF(contentX, currentY, contentX + contentW, currentY + 50.0f);
        D2D1_ROUNDED_RECT roundedHeader = D2D1::RoundedRect(headerRect, 8.0f, 8.0f);
        m_pRenderTarget->FillRoundedRectangle(roundedHeader, m_brushBgCardHeader.Get());
        m_pRenderTarget->DrawLine(D2D1::Point2F(contentX, currentY + 50.0f),
                                  D2D1::Point2F(contentX + contentW, currentY + 50.0f),
                                  m_brushBorderCard.Get(),
                                  1.0f);

        std::wstring headerTitle = card.title;
        D2DControl* headerToggle = nullptr;
        for (const auto& control : card.controls)
            if (control.headerToggle)
                headerToggle = const_cast<D2DControl*>(&control);
        if (isSearching)
        {
            headerTitle += L"  (" + item.pageName + L")";
        }

        // Clean vertical gap between Title and Subtitle
        D2D1_RECT_F cardTitleRect = D2D1::RectF(contentX + 16,
                                                currentY + (headerToggle ? 16.f : 9.f),
                                                contentX + contentW - (headerToggle ? 72.f : 16.f),
                                                currentY + (headerToggle ? 36.f : 28.f));
        m_pRenderTarget->DrawText(headerTitle.c_str(),
                                  (UINT32)headerTitle.length(),
                                  m_fmtCardTitle.Get(),
                                  cardTitleRect,
                                  m_brushTextMain.Get());
        if (headerToggle)
            drawToggle(*headerToggle, contentX + contentW - 56.f, currentY + 14.f);

        D2D1_RECT_F cardSubRect =
            D2D1::RectF(contentX + 16, currentY + 31.0f, contentX + contentW - 16, currentY + 47.0f);
        m_pRenderTarget->DrawText(card.subtitle.c_str(),
                                  (UINT32)card.subtitle.length(),
                                  m_fmtCardSubtitle.Get(),
                                  cardSubRect,
                                  m_brushTextMuted.Get());

        // Controls in Card
        float rowY = currentY + 52.0f;
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

            // Left: Name & Description with clean gap
            D2D1_RECT_F labelRect = D2D1::RectF(contentX + 16, rowY + 9.0f, contentX + contentW - 300, rowY + 27.0f);
            m_pRenderTarget->DrawText(
                ctrl.name.c_str(), (UINT32)ctrl.name.length(), m_fmtLabel.Get(), labelRect, m_brushTextMain.Get());

            D2D1_RECT_F descRect = D2D1::RectF(
                contentX + 16, rowY + 30.0f, contentX + contentW - 300, rowY + rowHeight - (compact ? 8.f : 26.f));
            auto description = ctrl.detail ? ctrl.detail() : ctrl.desc;
            if (description.empty())
                description = ctrl.desc;
            m_pRenderTarget->DrawText(
                description.c_str(), (UINT32)description.length(), m_fmtDesc.Get(), descRect, m_brushTextDim.Get());

            // Right: Control area aligned to cardRight - 16
            const float rightEdge = contentX + contentW - 16.0f;

            if (ctrl.kind == ControlKind::Toggle)
            {
                drawToggle(ctrl, rightEdge - 40.f, rowY + 18.f);
            }
            else if (ctrl.kind == ControlKind::SliderInt || ctrl.kind == ControlKind::SliderFloat)
            {
                // Exact Flight Line Alignment:
                // [track: 140px] [gap: 10px] [num-box: 64px] [gap: 10px] [unit: 48px]
                const float unitW = 48.0f;
                const float numBoxW = 64.0f;
                const float trackW = 140.0f;
                const float gap = 10.0f;

                float unitX = rightEdge - unitW;
                float numBoxX = unitX - gap - numBoxW;
                float trackX = numBoxX - gap - trackW;
                float trackY = rowY + 28.0f;

                // Slider Track
                D2D1_RECT_F trackBarRect = D2D1::RectF(trackX, trackY - 2.5f, trackX + trackW, trackY + 2.5f);
                D2D1_ROUNDED_RECT roundedTrack = D2D1::RoundedRect(trackBarRect, 2.5f, 2.5f);
                m_pRenderTarget->FillRoundedRectangle(roundedTrack, m_brushSliderTrack.Get());

                // Current Value
                float curVal = ctrl.Value();

                float frac = SliderFraction(ctrl, curVal);

                float thumbX = trackX + frac * trackW;

                // Active fill bar
                if (thumbX > trackX)
                {
                    D2D1_RECT_F fillRect = D2D1::RectF(trackX, trackY - 2.5f, thumbX, trackY + 2.5f);
                    D2D1_ROUNDED_RECT roundedFill = D2D1::RoundedRect(fillRect, 2.5f, 2.5f);
                    m_pRenderTarget->FillRoundedRectangle(roundedFill, m_brushSliderFill.Get());
                }

                const float defaultX = trackX + trackW * SliderFraction(ctrl, ctrl.Default());
                if (ctrl.Default() != 0 && ctrl.Default() > ctrl.minVal && ctrl.Default() < ctrl.maxVal)
                    m_pRenderTarget->DrawLine(D2D1::Point2F(defaultX, trackY - 9),
                                              D2D1::Point2F(defaultX, trackY + 9),
                                              m_brushTextMuted.Get(),
                                              1);
                // Thumb
                D2D1_ELLIPSE thumb = D2D1::Ellipse(D2D1::Point2F(thumbX, trackY), 7.0f, 7.0f);
                m_pRenderTarget->FillEllipse(thumb, m_brushSliderThumb.Get());

                // Number Box
                D2D1_RECT_F numRect = D2D1::RectF(numBoxX, rowY + 16.0f, numBoxX + numBoxW, rowY + 42.0f);
                D2D1_ROUNDED_RECT roundedNum = D2D1::RoundedRect(numRect, 4.0f, 4.0f);
                m_pRenderTarget->FillRoundedRectangle(roundedNum, m_brushInputBg.Get());
                m_pRenderTarget->DrawRoundedRectangle(roundedNum, m_brushBorderCard.Get(), 1.0f);

                wchar_t valBuf[32];
                if (ctrl.kind == ControlKind::SliderInt)
                    swprintf_s(valBuf, L"%d", static_cast<int>(curVal));
                else
                    swprintf_s(valBuf, L"%.6g", double(curVal));

                D2D1_RECT_F numTextRect = D2D1::RectF(numBoxX + 4, rowY + 20.0f, numBoxX + numBoxW - 6, rowY + 38.0f);
                m_pRenderTarget->DrawText(
                    valBuf, (UINT32)wcslen(valBuf), m_fmtValue.Get(), numTextRect, m_brushTextMain.Get());

                HitRegion number;
                number.type = HitRegion::Number;
                number.rect = numRect;
                number.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(number);
                // Unit Text (strictly fixed 48px width)
                D2D1_RECT_F unitRect = D2D1::RectF(unitX, rowY + 11.0f, unitX + 34.0f, rowY + 45.0f);
                if (ctrl.reset)
                {
                    auto buttonRect = unitRect;
                    buttonRect.left += 6;
                    buttonRect.top += 6;
                    buttonRect.right -= 6;
                    buttonRect.bottom -= 6;
                    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(buttonRect, 4, 4), m_brushInputBg.Get());
                    // Draw the reference orientation explicitly: clockwise
                    // arc with its arrowhead at the upper-right corner.
                    const auto center = D2D1::Point2F(unitRect.left + 17, unitRect.top + 17);
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
                    reset.rect = unitRect;
                    reset.pControl = &ctrl;
                    if (enabled)
                        m_hitRegions.push_back(reset);
                }

                HitRegion hr;
                hr.type = HitRegion::Slider;
                hr.rect = D2D1::RectF(trackX - 6, rowY + 14.0f, trackX + trackW + 6, rowY + 44.0f);
                hr.trackRect = D2D1::RectF(trackX, trackY - 2.5f, trackX + trackW, trackY + 2.5f);
                hr.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(hr);
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

                D2D1_RECT_F textRect = D2D1::RectF(boxX + 10, boxY + 6, boxX + boxW - 24, boxY + 24);
                m_pRenderTarget->DrawText(
                    curText.c_str(), (UINT32)curText.length(), m_fmtDesc.Get(), textRect, m_brushTextMain.Get());

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

            else if (ctrl.kind == ControlKind::Action)
            {
                D2D1_RECT_F r = D2D1::RectF(rightEdge - 220, rowY + 15, rightEdge, rowY + 43);
                m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_brushInputBg.Get());
                auto text = r;
                text.left += 10;
                text.top += 6;
                m_pRenderTarget->DrawText(ctrl.actionLabel.c_str(),
                                          (UINT32)ctrl.actionLabel.size(),
                                          m_fmtDesc.Get(),
                                          text,
                                          m_brushTextMain.Get());
                HitRegion h;
                h.type = HitRegion::Action;
                h.rect = r;
                h.pControl = &ctrl;
                if (enabled)
                    m_hitRegions.push_back(h);
            }
            std::wstring defaultText;
            if (ctrl.kind == ControlKind::SliderInt || ctrl.kind == ControlKind::SliderFloat)
            {
                wchar_t defaults[160];
                swprintf_s(defaults,
                           L"Default: %.6g   |   Range: %.6g to %.6g",
                           double(ctrl.Default()),
                           double(ctrl.minVal),
                           double(ctrl.maxVal));
                defaultText = defaults;
            }
            else if (ctrl.kind == ControlKind::Toggle)
                defaultText = ctrl.Default() ? L"Default: On" : L"Default: Off";
            else if (ctrl.kind == ControlKind::Choice)
            {
                for (const auto& choice : ctrl.choices)
                    if (choice.value == int(ctrl.Default()))
                    {
                        defaultText = L"Default: " + choice.label;
                        break;
                    }
            }
            if (!defaultText.empty())
            {
                auto r = D2D1::RectF(compact ? rightEdge - 300 : contentX + 16,
                                     rowY + (compact ? 48 : rowHeight - 26),
                                     rightEdge,
                                     rowY + rowHeight - 4);
                m_pRenderTarget->DrawText(
                    defaultText.c_str(), (UINT32)defaultText.size(), m_fmtDesc.Get(), r, m_brushTextMuted.Get());
            }
            if (!enabled)
            {
                auto old = m_brushBgCard->GetOpacity();
                m_brushBgCard->SetOpacity(.65f);
                m_pRenderTarget->FillRectangle(D2D1::RectF(contentX + 8, rowY, rightEdge, rowY + rowHeight),
                                               m_brushBgCard.Get());
                m_brushBgCard->SetOpacity(old);
            }
            rowY += rowHeight;
        }

        currentY += cardHeight + 20.0f;
    }

    m_maxScrollY = std::max(0.0f, (currentY + m_scrollY) - size.height + 40.0f);

    m_pRenderTarget->PopAxisAlignedClip();
    DrawScrollbar(D2D1::RectF(size.width - 16, 8, size.width - 5, size.height - 8),
                  m_scrollY,
                  m_maxScrollY + size.height,
                  size.height,
                  -1);

    if (m_keyboardIndex >= 0 && m_keyboardIndex < int(m_hitRegions.size()))
        m_pRenderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(m_hitRegions[m_keyboardIndex].rect, 4, 4), m_brushAccent.Get(), 1.5f);
    HRESULT hr = m_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        DiscardDeviceResources();
    }
}

void Direct2DHostWindow::Changed()
{
    if (FAILED(ValidateSettings(*m_pSettings)))
    {
        *m_pSettings = m_accepted;
        m_error = L"Value or combination is outside the supported range.";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    const bool reordered =
        memcmp(m_accepted.userShaders.order, m_pSettings->userShaders.order, sizeof(m_pSettings->userShaders.order)) !=
        0;
    m_accepted = *m_pSettings;
    m_error.clear();
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

void Direct2DHostWindow::DrawScrollbar(D2D1_RECT_F track, float offset, float extent, float viewport, int target)
{
    if (extent <= viewport || viewport <= 0)
        return;
    const float length = track.bottom - track.top;
    const float thumb = std::min(length, std::max(28.f, length * viewport / extent));
    const float top = track.top + (length - thumb) * std::clamp(offset / (extent - viewport), 0.f, 1.f);
    auto r = D2D1::RectF(track.left, top, track.right, top + thumb);
    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(track, 5, 5), m_brushInputBg.Get());
    const auto old = m_brushTextDim->GetOpacity();
    m_brushTextDim->SetOpacity(.45f);
    m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_brushTextDim.Get());
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
    if (m_preview)
    {
        auto hr = m_preview(*m_pSettings);
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
    if (!(importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog)))
    {
        if (CommDlgExtendedError())
            m_error = L"Could not open the INI file dialog.";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (!importing)
    {
        if (SUCCEEDED(SettingsIni::Export(path, *m_pSettings, m_error)))
            m_error = L"INI backup exported.";
    }
    else
    {
        PlaceboSettings candidate;
        if (SUCCEEDED(SettingsIni::Import(path, candidate, m_error)))
        {
            const auto hr = ApplyImported(candidate);
            if (FAILED(hr))
                m_error = L"Could not apply INI settings. Check external ICC and LUT files.";
            else
            {
                m_error = m_standalone ? L"INI imported. Select Save to keep it." : L"INI settings imported.";
            }
        }
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
    *m_pSettings = m_accepted = selected;
    if (m_preview)
        m_applied = selected;
    m_pending = false;
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_scrollY = 0;
    m_searchQuery.clear();
    m_searchFocused = false;
    m_hitRegions.clear();
    InitPages();
    InvalidateRect(m_hwnd, nullptr, FALSE);
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
    const int selected = popup.Show(m_hwnd, anchor, Scale());
    if (selected < 0 || devices[selected].id == m_pSettings->displayProfile)
        return;
    AdoptDisplayProfile(devices[selected].id);
}

void Direct2DHostWindow::AdoptDisplayProfile(const std::wstring& id)
{
    if (!DisplayProfiles::ValidId(id))
        return;
    EndEdit();
    *m_pSettings = m_accepted = m_applied = LoadSettings(id);
    m_pending = false;
    m_pDraggingSlider = nullptr;
    m_keyboardIndex = -1;
    m_hitRegions.clear();
    InitPages();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Direct2DHostWindow::EndEdit(bool accept)
{
    if (m_edit)
    {
        HWND edit = m_edit;
        if (m_renamingDisplay && accept)
        {
            wchar_t name[128];
            GetWindowTextW(edit, name, 128);
            if (SUCCEEDED(DisplayProfiles::Rename(m_pSettings->displayProfile, name)))
                m_devices = DisplayProfiles::All();
            else
                m_error = L"Enter a display name (1 to 80 characters).";
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        m_edit = nullptr;
        m_editControl = nullptr;
        m_renamingDisplay = false;
        DestroyWindow(edit);
    }
}

void Direct2DHostWindow::BeginEdit(D2DControl* control, const D2D1_RECT_F& r, bool renameDisplay)
{
    EndEdit();
    m_editControl = control;
    m_renamingDisplay = renameDisplay;
    float scale = Scale();
    wchar_t text[64] = {};
    if (control)
        swprintf_s(text, L"%.9g", double(control->Value()));
    m_edit = CreateWindowExW(0,
                             L"EDIT",
                             text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             int(r.left * scale),
                             int(r.top * scale),
                             int((r.right - r.left) * scale),
                             int((r.bottom - r.top) * scale),
                             m_hwnd,
                             reinterpret_cast<HMENU>(7001),
                             nullptr,
                             nullptr);
    SendMessageW(m_edit, EM_SETLIMITTEXT, 127, 0);
    if (renameDisplay)
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
    m_editFont = CreateFontW(-int(14 * scale),
                             0,
                             0,
                             0,
                             FW_NORMAL,
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
    std::wstring number = text;
    std::replace(number.begin(), number.end(), L',', L'.');
    if (number.empty() || number == L"-" || number == L"+" || number.back() == L'.')
        return;
    wchar_t* end = nullptr;
    errno = 0;
    double v = wcstod(number.c_str(), &end);
    auto c = m_editControl;
    if (end == number.c_str() || *end || errno == ERANGE || !std::isfinite(v) || v < c->minVal || v > c->maxVal ||
        (c->kind == ControlKind::SliderInt && std::floor(v) != v))
    {
        m_error = L"Enter a number within the supported range.";
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    c->set(float(v));
    Changed();
}

LRESULT CALLBACK Direct2DHostWindow::EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto p = reinterpret_cast<Direct2DHostWindow*>(data);
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTALLKEYS;
    if (msg == WM_KILLFOCUS && p->m_renamingDisplay)
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
            p->EndEdit();
            SetFocus(p->m_hwnd);
            return 0;
        }
        if (p->m_editControl && (w == VK_UP || w == VK_DOWN))
        {
            auto c = p->m_editControl;
            float v = std::clamp(c->Value() + (w == VK_UP ? c->stepVal : -c->stepVal), c->minVal, c->maxVal);
            wchar_t text[64];
            swprintf_s(text, L"%.9g", double(v));
            SetWindowTextW(h, text);
            SendMessageW(h, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    return DefSubclassProc(h, msg, w, l);
}

void Direct2DHostWindow::OnLButtonDown(float x, float y)
{
    EndEdit();
    SetFocus(m_hwnd);
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
        if (h.type == HitRegion::NavItem)
        {
            m_activePage = h.pageIndex;
            m_scrollY = 0;
            m_searchQuery.clear();
            m_searchFocused = false;
        }
        else if (h.type == HitRegion::SearchBox)
        {
            m_searchFocused = true;
            auto r = h.rect;
            r.left += 10;
            r.top += 6;
            r.right -= 32;
            r.bottom -= 5;
            BeginEdit(nullptr, r);
        }
        else if (h.type == HitRegion::SearchClearBtn)
        {
            m_searchQuery.clear();
            m_scrollY = 0;
            m_searchFocused = true;
            BeginEdit(nullptr, D2D1::RectF(26, 22, 194, 43));
        }
        else if (h.type == HitRegion::ShaderItem)
        {
            m_selectedShader = h.pageIndex;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        else if (h.type == HitRegion::ResetBtn)
        {
            for (auto& card : m_pages[m_activePage].cards)
                for (auto& c : card.controls)
                    if (c.reset)
                        c.reset();
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
        else if (h.type == HitRegion::ImportBtn || h.type == HitRegion::ExportBtn)
        {
            TransferIni(h.type == HitRegion::ImportBtn);
        }
        else if (h.type == HitRegion::ApplyBtn)
        {
            const auto hr = SaveSettings(*m_pSettings);
            m_error = SUCCEEDED(hr) ? L"Settings saved." : L"Could not save settings.";
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
                c->reset();
                Changed();
            }
            else if (h.type == HitRegion::Number)
            {
                BeginEdit(c, h.rect);
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
    if (key == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        BeginEdit(nullptr, D2D1::RectF(26, 22, 194, 43));
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
            OnLButtonDown((h.rect.left + h.rect.right) / 2, (h.rect.top + h.rect.bottom) / 2);
            OnLButtonUp(0, 0);
            return;
        }
        if ((key == VK_UP || key == VK_DOWN) && h.pControl &&
            (h.pControl->kind == ControlKind::SliderInt || h.pControl->kind == ControlKind::SliderFloat))
        {
            auto c = h.pControl;
            c->set(std::clamp(c->Value() + (key == VK_UP ? c->stepVal : -c->stepVal), c->minVal, c->maxVal));
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
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 237, 240));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(18, 19, 22));
            return reinterpret_cast<LRESULT>(pThis->m_editBrush);
        }
        break;
    case WM_CAPTURECHANGED:
        if (pThis)
        {
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
            pThis->EndEdit();
            RECT rc;
            GetClientRect(hwnd, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            pThis->m_pRenderTarget->Resize(size);
            InvalidateRect(hwnd, nullptr, FALSE);
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
            float x = float(GET_X_LPARAM(lParam)) / pThis->Scale(), y = float(GET_Y_LPARAM(lParam)) / pThis->Scale();
            for (const auto& h : pThis->m_hitRegions)
                if (h.type == HitRegion::ShaderItem && x >= h.rect.left && x <= h.rect.right && y >= h.rect.top &&
                    y <= h.rect.bottom)
                {
                    auto& shader = pThis->m_pSettings->userShaders.shaders[h.pageIndex];
                    shader.enabled = !shader.enabled;
                    pThis->Changed();
                    break;
                }
        }
        return 0;
    }
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
    if (pThis && pThis->m_pSettings)
    {
        *outSettings = *pThis->m_pSettings;
    }
}
