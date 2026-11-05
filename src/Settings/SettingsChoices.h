#pragma once
#include <iterator>
#include <string_view>

struct RendererChoice
{
    const char* name;
    const wchar_t* label;
};

inline constexpr RendererChoice ScalerChoices[] = {
    {"bilinear", L"Bilinear"},
    {"bicubic", L"Bicubic"},
    {"ewa_lanczos", L"EWA Lanczos (Jinc)"},
    {"lanczos", L"Lanczos"},
    {"spline36", L"Spline36"},
    {"mitchell", L"Mitchell"},
    {"spline16", L"Spline16"},
    {"spline64", L"Spline64"},
    {"ewa_lanczossharp", L"EWA Lanczos Sharp"},
    {"ewa_lanczos4sharpest", L"EWA Lanczos4 Sharpest"},
    {"gaussian", L"Gaussian"},
    {"sinc", L"Sinc"},
    {"ginseng", L"Ginseng"},
    {"ewa_jinc", L"EWA Jinc (unwindowed)"},
    {"ewa_ginseng", L"EWA Ginseng"},
    {"ewa_hann", L"EWA Hann"},
    {"hermite", L"Hermite"},
    {"catmull_rom", L"Catmull-Rom"},
    {"robidoux", L"Robidoux"},
    {"robidouxsharp", L"Robidoux Sharp"},
    {"ewa_robidoux", L"EWA Robidoux"},
    {"ewa_robidouxsharp", L"EWA Robidoux Sharp"},
};

inline bool ScalerHasWidth(size_t index)
{
    if (index >= std::size(ScalerChoices))
        return false;
    const std::string_view name = ScalerChoices[index].name;
    return name != "bilinear" && name != "bicubic" && name != "nearest";
}

inline constexpr RendererChoice ToneChoices[] = {
    {"auto", L"Auto (Metadata)"},
    {"spline", L"Spline"},
    {"bt2390", L"BT.2390"},
    {"bt2446a", L"BT.2446A"},
    {"reinhard", L"Reinhard"},
    {"mobius", L"Mobius"},
    {"hable", L"Hable"},
    {"st2094-40", L"ST 2094-40 (HDR10+)"},
    {"st2094-10", L"ST 2094-10"},
    {"clip", L"Clip"},
    {"gamma", L"Gamma"},
    {"linear", L"Linear"},
    {"linearlight", L"Linear Light"},
};
inline constexpr const wchar_t* DitherChoices[] = {
    L"Blue Noise (recommended)", L"Ordered Dithering", L"Random Dithering", L"Off"};
inline constexpr const wchar_t* OutputChoices[] = {
    L"Auto (libplacebo)", L"8-bit Integer (SDR)", L"Prefer 10-bit Integer", L"Prefer 16-bit Float"};
// How the display decodes the signal; HDR tone mapped to SDR is encoded for it.
inline constexpr const wchar_t* DisplayGammaChoices[] = {L"Auto", L"2.2", L"2.4", L"2.6", L"BT.1886"};
// The colour gamut the display shows; pictures are mapped into it.
inline constexpr const wchar_t* DisplayPrimariesChoices[] = {
    L"Auto (BT.709)", L"SMPTE C", L"EBU / PAL", L"BT.2020", L"DCI-P3", L"Display P3"};
// SDR gamma processing: off, or the gamma SDR should look like on the display.
inline constexpr const wchar_t* SdrGammaChoices[] = {L"Off", L"1.8", L"2.0", L"2.2", L"2.4", L"2.6", L"2.8"};
inline constexpr const wchar_t* LevelChoices[] = {L"Auto (Output)", L"PC Levels (0–255)", L"TV Levels (16–235)"};
inline constexpr int DisplayDepths[] = {0, 4, 5, 6, 8, 10, 12, 16};
inline constexpr const wchar_t* DepthChoices[] = {
    L"Auto (Output Texture)", L"4 Bit", L"5 Bit", L"6 Bit", L"8 Bit", L"10 Bit", L"12 Bit", L"16 Bit"};

inline constexpr RendererChoice GamutChoices[] = {{"auto", L"Default (libplacebo)"},
                                                  {"perceptual", L"Perceptual"},
                                                  {"relative", L"Relative Colorimetric"},
                                                  {"softclip", L"Softclip"},
                                                  {"saturation", L"Saturation"},
                                                  {"desaturate", L"Desaturate"},
                                                  {"darken", L"Darken"},
                                                  {"clip", L"Clip"},
                                                  {"absolute", L"Absolute Colorimetric"},
                                                  {"highlight", L"Highlight Out-of-Gamut"},
                                                  {"linear", L"Linear"}};
