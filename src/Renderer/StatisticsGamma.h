#pragma once
// The "Gamma" statistics row: the source's transfer and what the display gets,
// with the gamma settings as chosen (e.g. 2.35, not the nearest built-in curve).
#include <libplacebo/colorspace.h>
#include <cstdio>
#include <string>

namespace StatisticsGamma
{
inline std::wstring Name(pl_color_transfer transfer)
{
    switch (transfer)
    {
    case PL_COLOR_TRC_BT_1886:
        return L"BT.1886";
    case PL_COLOR_TRC_SRGB:
        return L"sRGB";
    case PL_COLOR_TRC_LINEAR:
        return L"linear";
    case PL_COLOR_TRC_GAMMA18:
        return L"1.80";
    case PL_COLOR_TRC_GAMMA20:
        return L"2.00";
    case PL_COLOR_TRC_GAMMA22:
        return L"2.20";
    case PL_COLOR_TRC_GAMMA24:
        return L"2.40";
    case PL_COLOR_TRC_GAMMA26:
        return L"2.60";
    case PL_COLOR_TRC_GAMMA28:
        return L"2.80";
    case PL_COLOR_TRC_PRO_PHOTO:
        return L"ProPhoto";
    case PL_COLOR_TRC_ST428:
        return L"ST 428";
    case PL_COLOR_TRC_PQ:
        return L"PQ";
    case PL_COLOR_TRC_HLG:
        return L"HLG";
    case PL_COLOR_TRC_V_LOG:
        return L"V-Log";
    case PL_COLOR_TRC_S_LOG1:
        return L"S-Log1";
    case PL_COLOR_TRC_S_LOG2:
        return L"S-Log2";
    case PL_COLOR_TRC_SCRGB:
        return L"scRGB";
    default:
        return L"unknown";
    }
}

// A gamma setting in hundredths (235 = 2.35).
inline std::wstring Curve(int hundredths)
{
    wchar_t text[16];
    swprintf_s(text, L"%d.%02d", hundredths / 100, hundredths % 100);
    return text;
}

struct Output
{
    pl_color_transfer transfer = PL_COLOR_TRC_UNKNOWN; // what the renderer writes
    bool hdr = false;                                  // HDR output (Windows HDR / native)
    bool icc = false;                                  // an ICC profile describes the display
    int sdrGamma = 0;                                  // SDR Gamma Processing in use (hundredths)
    int displayGamma = 0;                              // Display Gamma: 0 auto, 1 BT.1886, else hundredths
    int displayContrast = 0;                           // for BT.1886; 0 = infinite
};

// e.g. "source BT.1886 → output unchanged", "source PQ → output BT.1886 (1000:1)",
// "source BT.1886 → output shown as 2.40 on display 2.20"
inline std::wstring Text(pl_color_transfer source, const Output& out)
{
    const auto display = [&]
    {
        if (out.displayGamma == 1)
            return L"BT.1886 (" +
                   (out.displayContrast > 0 ? std::to_wstring(out.displayContrast) + L":1"
                                            : std::wstring(L"infinite contrast")) +
                   L")";
        if (out.displayGamma >= 100)
            return Curve(out.displayGamma);
        return Name(out.transfer);
    };
    std::wstring shown;
    if (out.icc)
        shown = L"ICC profile";
    else if (out.hdr)
        shown = Name(out.transfer) + L" (HDR)";
    else if (out.transfer == PL_COLOR_TRC_LINEAR || out.transfer == PL_COLOR_TRC_SCRGB)
        shown = Name(out.transfer);
    else if (out.sdrGamma > 0)
        shown = L"shown as " + Curve(out.sdrGamma) + L" on display " + display();
    else if (pl_color_transfer_is_hdr(source))
        shown = display();
    else
    {
        // SDR stays as it is: a chosen display gamma is not used for it.
        shown = L"unchanged";
        if (out.displayGamma == 1)
            shown += L" (Display Gamma BT.1886 not used)";
        else if (out.displayGamma >= 100)
            shown += L" (Display Gamma " + Curve(out.displayGamma) + L" not used)";
    }
    return L"source " + Name(source) + L" → output " + shown;
}
} // namespace StatisticsGamma
