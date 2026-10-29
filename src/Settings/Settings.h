#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include "Renderer/Backend.h"
#include "Settings/DisplayProfiles.h"
inline constexpr CLSID CLSID_PlaceboRenderer = {
    0xb864e92a, 0x1bdf, 0x4da1, {0x90, 0x04, 0xcb, 0xf4, 0x67, 0xdf, 0x23, 0x9a}};
inline constexpr CLSID CLSID_MPCCompatible = {
    0x71f080aa, 0x8661, 0x4093, {0xb1, 0x5e, 0x4f, 0x69, 0x03, 0xe7, 0x7d, 0x0a}};

#ifdef FRANKY_POTPLAYER
inline constexpr CLSID CLSID_RegisteredRenderer = CLSID_MPCCompatible;
inline constexpr wchar_t RendererFilterName[] = L"FrankyRenderer (PotPlayer)";
// Separate page classes let both packages coexist without redirecting each other's UI.
inline constexpr CLSID CLSID_FrankyDirect2DPage = {
    0x98f20bc7, 0x42a9, 0x4fba, {0xb1, 0x71, 0x39, 0xca, 0x08, 0x65, 0x1f, 0xca}};
#else
inline constexpr CLSID CLSID_RegisteredRenderer = CLSID_PlaceboRenderer;
inline constexpr wchar_t RendererFilterName[] = L"FrankyRenderer (MPC-BE)";
inline constexpr CLSID CLSID_FrankyDirect2DPage = {
    0x19a4f3e2, 0xf52e, 0x4dbe, {0x91, 0x67, 0x33, 0x2e, 0x7c, 0x38, 0xe8, 0x0c}};
#endif

struct PlaceboSettings
{
    TonalSettings tonal;
    UserShaderSettings userShaders;
    BlackBarSettings blackBars;
    RendererExtras extras;
    ColorMappingSettings colorMapping;
    float saturation = 1, contrast = 1, brightness = 0, gamma = 1, sdrNits = 203;
    // Debanding numeric -1 means Default; explicit zero remains a manual value.
    BOOL deband = FALSE, hdrPassthrough = FALSE, autoHdr = FALSE;
    BOOL clippingProtection = FALSE;
    wchar_t upscaler[64] = L"ewa_lanczos", downscaler[64] = L"mitchell";
    int deinterlacing = 1;
    int fieldOverride = 0; // 0=sample flags, 1=force TFF, 2=force BFF
    int toneMapping = 0;
    float hdrNits = 0, antiringing = 0, upscalerBlur = 0, downscalerBlur = 0;
    int debandIterations = -1, ditherMethod = 0;
    float debandThreshold = -1, debandRadius = -1, debandGrain = -1;
    BOOL ditherTemporal = FALSE;
    int chromaScaler = 0; // 0=libplacebo default, otherwise ScalerChoices index + 1
    int outputFormat = 0; // auto, 8-bit SDR, prefer 10-bit, prefer 16-bit float
    BOOL iccEnabled = FALSE, lutEnabled = FALSE;
    int iccIntent = 0;
    wchar_t iccPath[1024] = {}, lutPath[1024] = {};
    BOOL adaptiveSharpen = FALSE;
    float sharpenStrength = .5f;
    BOOL smoothMotion = FALSE;
    int outputLevels = 0, displayDepth = 0;
    BOOL peakDetect = TRUE;
    float peakSmoothing = 20, sceneLow = 1, sceneHigh = 3, peakPercentile = 100;
    float contrastRecovery = 0, contrastSmoothness = 3.5f;
    int gamutMapping = 0;
    float kneeAdaptation = .4f, splineContrast = .5f, kneeMinimum = .1f, kneeMaximum = .8f;
    float blackCutoff = 1, perceptualStrength = .8f, softclipKnee = .7f;
    BOOL showClipping = FALSE;
    float hue = 0, temperature = 0;
    BOOL colorAdjustment = TRUE;
    BOOL allowDelayedPeak = FALSE;
    int hdrMode = 2; // 1=HDR passthrough, 2=GPU tone mapping to SDR; legacy 0 is migrated
    ScalingSettings scaling;
    int maximumFramesInFlight = 0;   // 0=retain the device's original DXGI latency
    wchar_t displayProfile[65] = {}; // Editing context; never included in INI values.
    BOOL rtxSuperResolution = FALSE;
    BOOL imageAnalysis = FALSE;
};

// Preserve old backups while removing the automatic output mode and OS toggle.
inline void NormalizeLegacyHdrSettings(PlaceboSettings& s)
{
    if (s.hdrMode == 0)
        s.hdrMode = s.hdrPassthrough ? 1 : 2;
    s.hdrPassthrough = s.hdrMode == 1;
    s.autoHdr = FALSE;
}

struct PlaceboStatus
{
    uint64_t rendered = 0, hardwareFrames = 0;
    int width = 0, height = 0;
    BOOL hdrSource = FALSE;
    HRESULT lastResult = S_OK;
    float masteringMaxNits = 0, masteringMinNits = 0, maxCLL = 0, maxFALL = 0;
    uint64_t deinterlacedFields = 0, mixedFrames = 0;
};

// New IID because the settings structure grew; old clients must fail QI
// rather than receive a larger structure into their smaller buffer.
interface __declspec(uuid("7ADBF525-A6F1-41E1-AC45-278B3ACCEB9E")) IPlaceboSettings : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetSettings(PlaceboSettings*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSettings(const PlaceboSettings*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(PlaceboStatus*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadPixels(int width, int height, BYTE* rgba, SIZE_T bytes) = 0;
};
PlaceboSettings LoadSettings(const std::wstring& display = {});
HRESULT SaveSettings(const PlaceboSettings&);
HRESULT EnsureDisplayProfiles(const std::vector<DisplayProfiles::Device>&, const PlaceboSettings& legacy);
HRESULT ValidateSettings(const PlaceboSettings&);
RenderSettings BackendSettings(const PlaceboSettings&);
