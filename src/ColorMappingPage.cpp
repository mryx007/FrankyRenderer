#include "LiveSettings.h"
#include "ColorMappingPage.h"
#include "SettingsChoices.h"
#include "NumberControls.h"
#include "UiHelp.h"
#include "TonalEditor.h"
#include <commctrl.h>
#include <cwctype>
#include <algorithm>

static std::wstring Wide(const char* s)
{
    std::wstring out;
    for (; *s; s++)
        out += static_cast<unsigned char>(*s);
    return out;
}

static std::wstring Text(HWND d, int id)
{
    wchar_t v[128] = {};
    GetDlgItemTextW(d, id, v, 128);
    return v;
}

static constexpr const wchar_t* Groups[] = {L"Tone Curve - Knee",
                                            L"Tone Curve - Algorithm Constants",
                                            L"Contrast Recovery",
                                            L"Gamut Mapping Constants",
                                            L"Internal Lookup Tables",
                                            L"Visualization and Debug",
                                            L"Expansion and Metadata",
                                            L"HDR Peak Detection"};

static std::wstring DefaultText(pl_options options, const char* key)
{
    const auto opt = pl_find_option(key);
    const auto value = pl_options_get(options, key);
    if (!value)
        return L"";
    if (opt && (opt->type == PL_OPT_FLOAT || opt->type == PL_OPT_INT))
    {
        wchar_t text[40];
        swprintf_s(text, L"%.6g", atof(value->text));
        return text;
    }
    return Wide(value->text);
}

static constexpr int PeakControls[] = {410, 411, 412, 413, 414, 415, 416, 417, 980, 981, 982, 983, 984};

static bool ReadNumber(HWND d, int id, float& value)
{
    auto text = Text(d, id);
    for (auto& c : text)
        if (c == L',')
            c = L'.';
    wchar_t* end = nullptr;
    value = wcstof(text.c_str(), &end);
    while (end && iswspace(*end))
        ++end;
    return end && end != text.c_str() && !*end && std::isfinite(value);
}

// Preserve older numeric settings when first opening the new editor.
static void LegacyOverrides(const PlaceboSettings& s, ColorMappingSettings& d)
{
    auto o = pl_options_alloc(nullptr);
    pl_options_reset(o, &pl_render_default_params);
    auto copy = [&](const char* key, float value)
    {
        const int i = MappingIndex(key);
        const auto v = pl_options_get(o, key);
        if (i >= 0 && !d.values[i][0] && v && std::abs(float(atof(v->text)) - value) > 1e-6f)
            sprintf_s(d.values[i], "%.9g", double(value));
    };
    copy("knee_adaptation", s.kneeAdaptation);
    copy("spline_contrast", s.splineContrast);
    copy("knee_minimum", s.kneeMinimum);
    copy("knee_maximum", s.kneeMaximum);
    copy("contrast_recovery", s.contrastRecovery);
    copy("contrast_smoothness", s.contrastSmoothness);
    copy("perceptual_strength", s.perceptualStrength);
    copy("softclip_knee", s.softclipKnee);
    if (!d.values[MappingIndex("knee_default")][0])
    {
        const float value = std::clamp(.4f, s.kneeMinimum, s.kneeMaximum);
        copy("knee_default", value);
    }
    pl_options_free(&o);
}

static void ResetLegacy(PlaceboSettings& s)
{
    PlaceboSettings d;
    s.kneeAdaptation = d.kneeAdaptation;
    s.splineContrast = d.splineContrast;
    s.kneeMinimum = d.kneeMinimum;
    s.kneeMaximum = d.kneeMaximum;
    s.contrastRecovery = d.contrastRecovery;
    s.contrastSmoothness = d.contrastSmoothness;
    s.perceptualStrength = d.perceptualStrength;
    s.softclipKnee = d.softclipKnee;
}

ColorMappingPage::ColorMappingPage(IUnknown* p, HRESULT* hr)
    : CBasePropertyPage(NAME("FrankyRenderer color mapping"), p, 109, 209)
{
    *hr = S_OK;
    background = CreateSolidBrush(RGB(248, 250, 252));
}

ColorMappingPage::~ColorMappingPage()
{
    if (helpTip)
        DestroyWindow(helpTip);
    if (title)
        DeleteObject(title);
    if (background)
        DeleteObject(background);
}

HRESULT ColorMappingPage::OnConnect(IUnknown* o)
{
    return o->QueryInterface(IID_PPV_ARGS(&config));
}

HRESULT ColorMappingPage::OnDisconnect()
{
    if (config)
    {
        config->Release();
        config = nullptr;
    }
    return S_OK;
}

void ColorMappingPage::Dirty()
{
    m_bDirty = TRUE;
    if (m_pPageSite)
        m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
}

void ColorMappingPage::PopulateHdr()
{
    loading = true;
    wchar_t text[64];
    SendDlgItemMessageW(m_Dlg, 121, CB_RESETCONTENT, 0, 0);
    for (auto mode : {L"Passthrough HDR to Display", L"Tone Map HDR to SDR"})
        SendDlgItemMessageW(m_Dlg, 121, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode));
    SendDlgItemMessageW(m_Dlg, 121, CB_SETCURSEL, original.hdrMode - 1, 0);
    SetDlgItemTextW(m_Dlg, 122, L"Clipping Protection");
    CheckDlgButton(m_Dlg, 122, original.clippingProtection ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_Dlg, 400, original.peakDetect ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_Dlg, 415, original.allowDelayedPeak ? BST_CHECKED : BST_UNCHECKED);
    for (auto v : {std::pair{114, original.sdrNits},
                   std::pair{115, original.hdrNits},
                   std::pair{410, original.peakSmoothing},
                   std::pair{411, original.sceneLow},
                   std::pair{412, original.sceneHigh},
                   std::pair{413, original.peakPercentile},
                   std::pair{414, original.blackCutoff}})
    {
        swprintf_s(text, L"%.6g", double(v.second));
        SetDlgItemTextW(m_Dlg, v.first, text);
    }
    loading = false;
    UpdateHdr();
}

bool ColorMappingPage::ReadHdr(PlaceboSettings& next)
{
    const auto mode = SendDlgItemMessageW(m_Dlg, 121, CB_GETCURSEL, 0, 0);
    if (mode < 0 || mode > 1)
        return false;
    next.hdrMode = static_cast<int>(mode) + 1;
    next.hdrPassthrough = mode == 0;
    next.autoHdr = FALSE;
    next.clippingProtection = IsDlgButtonChecked(m_Dlg, 122) == BST_CHECKED;
    next.peakDetect = IsDlgButtonChecked(m_Dlg, 400) == BST_CHECKED;
    next.allowDelayedPeak = IsDlgButtonChecked(m_Dlg, 415) == BST_CHECKED;
    return ReadNumber(m_Dlg, 114, next.sdrNits) && ReadNumber(m_Dlg, 115, next.hdrNits) &&
           ReadNumber(m_Dlg, 410, next.peakSmoothing) && ReadNumber(m_Dlg, 411, next.sceneLow) &&
           ReadNumber(m_Dlg, 412, next.sceneHigh) && ReadNumber(m_Dlg, 413, next.peakPercentile) &&
           ReadNumber(m_Dlg, 414, next.blackCutoff);
}

void ColorMappingPage::UpdateHdr()
{
    const auto mode = SendDlgItemMessageW(m_Dlg, 121, CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(m_Dlg, 115), FALSE);
    EnableWindow(GetDlgItem(m_Dlg, 122), mode == 1);
    SetDlgItemTextW(m_Dlg,
                    823,
                    mode == 0 ? L"HDR output and metadata are passed to the display. Windows HDR is not switched."
                              : L"GPU tone mapping from HDR to SDR.");
    for (int id : {410, 411, 412, 413, 414, 415})
        EnableWindow(GetDlgItem(m_Dlg, id), IsDlgButtonChecked(m_Dlg, 400) == BST_CHECKED);
    for (const auto& n : NumberControls)
        if (HWND field = GetDlgItem(m_Dlg, n.id))
            for (bool up : {false, true})
                if (HWND button = GetDlgItem(m_Dlg, NumberButtonId(n.id, up)))
                    EnableWindow(button, IsWindowEnabled(field));
}

void ColorMappingPage::AddHdrHelp()
{
    const auto font = SendMessageW(m_Dlg, WM_GETFONT, 0, 0);
    RECT units = {0, 0, 10, 14};
    MapDialogRect(m_Dlg, &units);
    for (const auto& n : NumberControls)
        if (HWND field = GetDlgItem(m_Dlg, n.id))
        {
            if (GetDlgItem(m_Dlg, NumberButtonId(n.id, false)))
                continue;
            RECT r;
            GetWindowRect(field, &r);
            MapWindowPoints(nullptr, m_Dlg, reinterpret_cast<POINT*>(&r), 2);
            const int width = r.right - r.left - 2 * units.right - 2;
            SetWindowPos(field, nullptr, 0, 0, width, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            for (bool up : {false, true})
            {
                HWND button = CreateWindowExW(0,
                                              L"BUTTON",
                                              up ? L"\u25b6" : L"\u25c0",
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              r.left + width + 2 + (up ? units.right : 0),
                                              r.top,
                                              units.right,
                                              r.bottom - r.top,
                                              m_Dlg,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(NumberButtonId(n.id, up))),
                                              g_hInst,
                                              nullptr);
                if (button)
                    SendMessageW(button, WM_SETFONT, font, TRUE);
            }
        }
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    helpTip = CreateWindowExW(WS_EX_TOPMOST,
                              TOOLTIPS_CLASSW,
                              nullptr,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              m_Dlg,
                              nullptr,
                              g_hInst,
                              nullptr);
    if (!helpTip)
        return;
    SendMessageW(helpTip, TTM_SETMAXTIPWIDTH, 0, 420);
    for (const auto& help : UiHelp)
        for (int id : {help.id, NumberButtonId(help.id, false), NumberButtonId(help.id, true)})
            if (HWND field = GetDlgItem(m_Dlg, id))
            {
                TOOLINFOW t = {};
                t.cbSize = TTTOOLINFOW_V2_SIZE;
                t.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                t.hwnd = m_Dlg;
                t.uId = reinterpret_cast<UINT_PTR>(field);
                t.lpszText = const_cast<wchar_t*>(help.text);
                SendMessageW(helpTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t));
            }
}

HRESULT ColorMappingPage::OnDeactivate()
{
    if (helpTip)
    {
        DestroyWindow(helpTip);
        helpTip = nullptr;
    }
    return S_OK;
}

void ColorMappingPage::Populate()
{
    loading = true;
    for (int id : {434, 430, 900})
        SendDlgItemMessageW(m_Dlg, id, CB_RESETCONTENT, 0, 0);
    for (const auto& c : ToneChoices)
        SendDlgItemMessageW(m_Dlg, 434, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.label));
    for (const auto& c : GamutChoices)
        SendDlgItemMessageW(m_Dlg, 430, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.label));
    for (auto label : Groups)
        SendDlgItemMessageW(m_Dlg, 900, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(m_Dlg, 434, CB_SETCURSEL, original.toneMapping, 0);
    SendDlgItemMessageW(m_Dlg, 430, CB_SETCURSEL, original.gamutMapping, 0);
    SendDlgItemMessageW(m_Dlg, 900, CB_SETCURSEL, group, 0);
    CheckDlgButton(m_Dlg, 460, original.showClipping ? BST_CHECKED : BST_UNCHECKED);
    loading = false;
    Editor();
}

void ColorMappingPage::Editor()
{
    loading = true;
    rows.clear();
    auto o = pl_options_alloc(nullptr);
    pl_options_reset(o, &pl_render_default_params);
    for (int id : PeakControls)
    {
        ShowWindow(GetDlgItem(m_Dlg, id), group == 7 ? SW_SHOW : SW_HIDE);
        for (bool up : {false, true})
            if (HWND button = GetDlgItem(m_Dlg, NumberButtonId(id, up)))
                ShowWindow(button, group == 7 ? SW_SHOW : SW_HIDE);
    }
    for (int i = 0; i < MappingOptionCount; i++)
        if (MappingOptions[i].group == group)
            rows.push_back(i);
    for (int row = 0; row < 8; row++)
    {
        const bool visible = row < static_cast<int>(rows.size());
        for (int base : {910, 920, 930, 940, 950, 960})
            ShowWindow(GetDlgItem(m_Dlg, base + row), visible ? SW_SHOW : SW_HIDE);
        if (!visible)
            continue;
        const int i = rows[row];
        const auto& field = MappingOptions[i];
        const auto opt = pl_find_option(field.key);
        auto value = pl_options_get(o, field.key);
        SetDlgItemTextW(m_Dlg, 910 + row, field.label);
        SendDlgItemMessageW(m_Dlg, 920 + row, CB_RESETCONTENT, 0, 0);
        const auto defaultText = DefaultText(o, field.key);
        SendDlgItemMessageW(m_Dlg, 920 + row, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(defaultText.c_str()));
        const wchar_t* metadata[] = {L"any", L"none", L"hdr10", L"hdr10plus", L"cie_y"};
        if (opt && opt->type == PL_OPT_BOOL)
            for (auto label : {L"yes", L"no"})
                SendDlgItemMessageW(m_Dlg, 920 + row, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (!strcmp(field.key, "tone_map_metadata"))
            for (auto label : metadata)
                SendDlgItemMessageW(m_Dlg, 920 + row, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SetDlgItemTextW(m_Dlg, 920 + row, draft.values[i][0] ? Wide(draft.values[i]).c_str() : defaultText.c_str());
        std::wstring info = L"Default = ";
        if (value && opt && (opt->type == PL_OPT_FLOAT || opt->type == PL_OPT_INT))
        {
            wchar_t number[40];
            swprintf_s(number, L"%.6g", atof(value->text));
            info += number;
        }
        else
            info += value ? Wide(value->text) : L"unavailable";
        if (opt && (opt->type == PL_OPT_INT || opt->type == PL_OPT_FLOAT) && (opt->min != 0 || opt->max != 0))
        {
            wchar_t range[80];
            swprintf_s(range, L"; range %.6g to %.6g", double(opt->min), double(opt->max));
            info += range;
        }
        SetDlgItemTextW(m_Dlg, 930 + row, info.c_str());
        SetDlgItemTextW(m_Dlg, 960 + row, field.help);
        const bool numeric = opt && (opt->type == PL_OPT_INT || opt->type == PL_OPT_FLOAT);
        EnableWindow(GetDlgItem(m_Dlg, 920 + row), opt != nullptr);
        EnableWindow(GetDlgItem(m_Dlg, 940 + row), numeric);
        EnableWindow(GetDlgItem(m_Dlg, 950 + row), numeric);
    }
    if (!rows.empty())
        SetDlgItemTextW(m_Dlg, 970, MappingOptions[rows.front()].help);
    else
        SetDlgItemTextW(
            m_Dlg,
            970,
            L"Measure HDR brightness when dynamic source metadata is absent. Default uses fixed library values. Hover over a control for details.");
    pl_options_free(&o);
    loading = false;
    UpdateHdr();
}

bool ColorMappingPage::ReadEditor()
{
    auto next = draft;
    for (int row = 0; row < static_cast<int>(rows.size()); row++)
    {
        auto text = Text(m_Dlg, 920 + row);
        while (!text.empty() && iswspace(text.back()))
            text.pop_back();
        while (!text.empty() && iswspace(text.front()))
            text.erase(0, 1);
        auto& value = next.values[rows[row]];
        memset(value, 0, sizeof(value));
        if (!_wcsicmp(text.c_str(), L"Default"))
            continue;
        // Display inherited values without turning unchanged defaults into overrides.
        if (!draft.values[rows[row]][0])
        {
            auto o = pl_options_alloc(nullptr);
            pl_options_reset(o, &pl_render_default_params);
            const auto inherited = DefaultText(o, MappingOptions[rows[row]].key);
            pl_options_free(&o);
            if (text == inherited)
                continue;
        }
        if (text.empty() || text.size() >= 32)
        {
            LiveSettings::Focus(GetDlgItem(m_Dlg, 920 + row));
            return false;
        }
        for (size_t j = 0; j < text.size(); j++)
        {
            if (text[j] > 127)
                return false;
            value[j] = text[j] == L',' ? '.' : static_cast<char>(text[j]);
        }
    }
    if (!ValidMappingOverrides(next))
    {
        LiveSettings::Message(
            m_Dlg,
            L"Check the values and ranges. Knee Default must lie between Knee Minimum and Maximum. Visualization edges must form a rectangle inside 0 to 1.",
            L"Check Color Mapping",
            MB_OK | MB_ICONINFORMATION);
        return false;
    }
    draft = next;
    return true;
}

void ColorMappingPage::Step(int row, bool up)
{
    if (row >= static_cast<int>(rows.size()))
        return;
    const auto& field = MappingOptions[rows[row]];
    auto opt = pl_find_option(field.key);
    if (!opt)
        return;
    auto o = pl_options_alloc(nullptr);
    pl_options_reset(o, &pl_render_default_params);
    auto text = Text(m_Dlg, 920 + row);
    double value = 0;
    if (!_wcsicmp(text.c_str(), L"Default"))
    {
        auto d = pl_options_get(o, field.key);
        value = d ? atof(d->text) : 0;
    }
    else
    {
        for (auto& c : text)
            if (c == L',')
                c = L'.';
        wchar_t* end;
        value = wcstod(text.c_str(), &end);
        if (end == text.c_str() || *end || !std::isfinite(value))
        {
            pl_options_free(&o);
            return;
        }
    }
    const double step = opt->type == PL_OPT_INT ? 1 : !strcmp(field.key, "contrast_smoothness") ? .1 : .05;
    value += up ? step : -step;
    if (opt->min != 0 || opt->max != 0)
        value = std::clamp(value, double(opt->min), double(opt->max));
    if (!strcmp(field.key, "knee_minimum"))
        value = std::clamp(value, .001, .499);
    if (!strcmp(field.key, "knee_maximum"))
        value = std::clamp(value, .501, .999);
    if (!strcmp(field.key, "reinhard_contrast") || !strcmp(field.key, "linear_knee"))
        value = std::clamp(value, .001, .999);
    wchar_t out[64];
    swprintf_s(out, L"%.6g", value);
    SetDlgItemTextW(m_Dlg, 920 + row, out);
    pl_options_free(&o);
    Dirty();
}

HRESULT ColorMappingPage::OnActivate()
{
    if (!config)
        return E_UNEXPECTED;
    HRESULT hr = config->GetSettings(&original);
    if (FAILED(hr))
        return hr;
    if (!title)
    {
        LOGFONTW font = {};
        HFONT base = reinterpret_cast<HFONT>(SendMessageW(m_Dlg, WM_GETFONT, 0, 0));
        if (GetObjectW(base, sizeof(font), &font))
        {
            font.lfWeight = FW_SEMIBOLD;
            font.lfHeight = font.lfHeight * 4 / 3;
            title = CreateFontIndirectW(&font);
        }
    }
    if (title)
        SendDlgItemMessageW(m_Dlg, 150, WM_SETFONT, reinterpret_cast<WPARAM>(title), TRUE);
    AddHdrHelp();
    PopulateHdr();
    tonalDraft = original.tonal;
    draft = original.colorMapping;
    LegacyOverrides(original, draft);
    Populate();
    LiveSettings::Attach(m_Dlg,
                         config,
                         [this]()
                         {
                             auto hr = OnApplyChanges();
                             if (SUCCEEDED(hr) && m_pPageSite)
                                 m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);
                             return hr;
                         });
    return S_OK;
}

HRESULT ColorMappingPage::OnApplyChanges()
{
    if (!ReadEditor())
        return E_INVALIDARG;
    PlaceboSettings next;
    HRESULT hr = config->GetSettings(&next);
    if (FAILED(hr))
        return hr;
    if (!ReadHdr(next))
        return E_INVALIDARG;
    const auto tone = SendDlgItemMessageW(m_Dlg, 434, CB_GETCURSEL, 0, 0),
               gamut = SendDlgItemMessageW(m_Dlg, 430, CB_GETCURSEL, 0, 0);
    if (tone < 0 || tone >= std::size(ToneChoices) || gamut < 0 || gamut >= std::size(GamutChoices))
        return E_INVALIDARG;
    next.toneMapping = static_cast<int>(tone);
    next.gamutMapping = static_cast<int>(gamut);
    next.showClipping = IsDlgButtonChecked(m_Dlg, 460) == BST_CHECKED;
    ResetLegacy(next);
    next.colorMapping = draft;
    next.tonal = tonalDraft;
    hr = config->SetSettings(&next);
    if (SUCCEEDED(hr))
    {
        original = next;
        m_bDirty = FALSE;
    }
    return hr;
}

INT_PTR ColorMappingPage::OnReceiveMessage(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_CTLCOLORDLG)
        return reinterpret_cast<INT_PTR>(background);
    if (msg == WM_CTLCOLORSTATIC)
    {
        SetBkColor(reinterpret_cast<HDC>(w), RGB(248, 250, 252));
        SetTextColor(reinterpret_cast<HDC>(w), RGB(40, 49, 60));
        return reinterpret_cast<INT_PTR>(background);
    }
    if (msg == WM_COMMAND && !loading)
    {
        const int id = LOWORD(w), event = HIWORD(w);
        if (id == 902 && event == BN_CLICKED)
        {
            const auto before = tonalDraft;
            auto preview = [this](const TonalSettings& value)
            {
                tonalDraft = value;
                PlaceboSettings next;
                auto hr = config->GetSettings(&next);
                if (FAILED(hr))
                    return hr;
                next.tonal = value;
                return config->SetSettings(&next);
            };
            if (EditTonalOptions(g_hInst, m_Dlg, tonalDraft, preview))
                Dirty();
            else if (LiveSettings::Enabled())
                preview(before);
            return TRUE;
        }
        if (event == BN_CLICKED && (id == 416 || id == 417))
        {
            const auto& d = id == 417 ? pl_peak_detect_high_quality_params : pl_peak_detect_default_params;
            wchar_t text[64];
            for (auto v : {std::pair{410, d.smoothing_period},
                           std::pair{411, d.scene_threshold_low},
                           std::pair{412, d.scene_threshold_high},
                           std::pair{413, d.percentile},
                           std::pair{414, d.black_cutoff}})
            {
                swprintf_s(text, L"%.6g", double(v.second));
                SetDlgItemTextW(m_Dlg, v.first, text);
            }
            CheckDlgButton(m_Dlg, 415, d.allow_delayed ? BST_CHECKED : BST_UNCHECKED);
            UpdateHdr();
            Dirty();
            return TRUE;
        }
        if (event == BN_CLICKED)
            for (const auto& n : NumberControls)
                for (bool up : {false, true})
                    if (id == NumberButtonId(n.id, up))
                    {
                        if (!IsWindowEnabled(GetDlgItem(m_Dlg, n.id)))
                            return TRUE;
                        float value;
                        if (ReadNumber(m_Dlg, n.id, value))
                        {
                            wchar_t text[64];
                            swprintf_s(text, L"%.6g", StepNumber(n, value, up));
                            SetDlgItemTextW(m_Dlg, n.id, text);
                        }
                        return TRUE;
                    }
        if (id == 900 && event == CBN_SELCHANGE)
        {
            if (!ReadEditor())
            {
                SendDlgItemMessageW(m_Dlg, 900, CB_SETCURSEL, group, 0);
                return TRUE;
            }
            group = static_cast<int>(SendDlgItemMessageW(m_Dlg, 900, CB_GETCURSEL, 0, 0));
            Editor();
            return TRUE;
        }
        if (id == 901 && event == BN_CLICKED)
        {
            draft = {};
            original.toneMapping = original.gamutMapping = 0;
            original.showClipping = FALSE;
            Populate();
            Dirty();
            return TRUE;
        }
        if (event == BN_CLICKED && id >= 940 && id <= 957)
        {
            Step(id % 10, id >= 950);
            return TRUE;
        }
        if (id >= 920 && id <= 927 && (event == CBN_SETFOCUS || event == CBN_EDITCHANGE || event == CBN_SELCHANGE))
        {
            const int row = id - 920;
            if (row < static_cast<int>(rows.size()))
                SetDlgItemTextW(m_Dlg, 970, MappingOptions[rows[row]].help);
        }
        if (event == EN_SETFOCUS || event == CBN_SETFOCUS)
            for (const auto& help : UiHelp)
                if (help.id == id)
                    SetDlgItemTextW(m_Dlg, 970, help.text);
        if (event == CBN_SELCHANGE || event == CBN_EDITCHANGE || event == BN_CLICKED || event == EN_CHANGE)
        {
            UpdateHdr();
            Dirty();
        }
    }
    return CBasePropertyPage::OnReceiveMessage(hwnd, msg, w, l);
}
