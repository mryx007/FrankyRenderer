#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include "Renderer/Backend.h"
#include "Settings/DisplayProfiles.h"
inline constexpr CLSID CLSID_PlaceboRenderer = {
    0xb864e92a, 0x1bdf, 0x4da1, {0x90, 0x04, 0xcb, 0xf4, 0x67, 0xdf, 0x23, 0x9a}};
inline constexpr CLSID CLSID_RegisteredRenderer = CLSID_PlaceboRenderer;
inline constexpr wchar_t RendererFilterName[] = L"FrankyRenderer";
inline constexpr CLSID CLSID_FrankyDirect2DPage = {
    0x19a4f3e2, 0xf52e, 0x4dbe, {0x91, 0x67, 0x33, 0x2e, 0x7c, 0x38, 0xe8, 0x0c}};

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
    // 1=HDR passthrough, 2=GPU tone mapping to SDR, 3=HDR output with highlights
    // rolled off to the display peak; legacy 0 is migrated
    int hdrMode = 2;
    ScalingSettings scaling;
    int maximumFramesInFlight = 0; // 0=retain the device's original DXGI latency
    // Smooth Motion refreshes presented in advance; 0=automatic, 1-4 fixed.
    int presentAhead = 0;
    // How the display decodes the signal: 0=auto (sRGB), 1=BT.1886 with the
    // display's contrast (0 = infinite, e.g. OLED), 180-260 = pure power x100.
    int displayGammaCurve = 0;
    int displayContrast = 0;
    // The display's gamut: 0=auto (BT.709), 1=SMPTE C, 2=EBU/PAL, 3=BT.2020, 4=DCI-P3, 5=Display P3.
    int displayPrimaries = 0;
    // SDR gamma processing: 0=off, 180-260 = look like this pure power x100.
    int sdrGammaCurve = 0;
    // Smooth Motion oversample mixing: a frame showing for less than this share
    // of a refresh is left out instead of blended (0 = always blend, max .5).
    float motionThreshold = 0;
    wchar_t displayProfile[65] = {}; // Editing context; never included in INI values.
    BOOL rtxSuperResolution = FALSE;
    BOOL rtxHdr = FALSE; // RTX Video HDR for SDR sources with Windows HDR on
    BOOL imageAnalysis = FALSE;
    // Ctrl+J statistics and image analysis follow the picture, not the window.
    BOOL statisticsInVideo = FALSE;
    // Display mode switching in fullscreen, e.g. L"2160p23, 2160p24, 2160p60".
    BOOL displayModeSwitch = FALSE;
    wchar_t displayModes[256] = {};
    float displayModeDelay = 2; // seconds paused after a switch
    // Windows HDR switching for playback (Renderer/WindowsHdrSwitch.h); 0 leaves it alone.
    int windowsHdrToggle = 0;
    // Where the quality values came from (Settings/QualityPresets.h): 0 none,
    // MeasuredFlag | upgrades = measured, PresetSource + i = preset i.
    // qualityStamp fingerprints all settings at that moment.
    int measuredQuality = 0;
    uint32_t qualityStamp = 0;
};

// Preserve old backups while removing the automatic output mode and OS toggle.
inline void NormalizeLegacyHdrSettings(PlaceboSettings& s)
{
    if (s.hdrMode == 0)
        s.hdrMode = s.hdrPassthrough ? 1 : 2;
    s.hdrPassthrough = s.hdrMode != 2;
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
interface __declspec(uuid("322DB0D4-840D-4D31-AFA9-2BFCCA325A35")) IPlaceboSettings : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetSettings(PlaceboSettings*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSettings(const PlaceboSettings*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(PlaceboStatus*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadPixels(int width, int height, BYTE* rgba, SIZE_T bytes) = 0;
};
PlaceboSettings LoadSettings(const std::wstring& display = {});
HRESULT SaveSettings(const PlaceboSettings&);
HRESULT EnsureDisplayProfiles(const std::vector<DisplayProfiles::Device>&, const PlaceboSettings& legacy);
HRESULT PrepareDisplaySettings(PlaceboSettings&, const std::vector<DisplayProfiles::Device>&, HMONITOR preferred);
HRESULT ValidateSettings(const PlaceboSettings&);
RenderSettings BackendSettings(const PlaceboSettings&);
