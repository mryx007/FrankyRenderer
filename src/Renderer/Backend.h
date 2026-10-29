#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/shaders/deinterlacing.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "Renderer/PixelLayout.h"
#include "Settings/ScalingSettings.h"
#include "Settings/ColorMappingSettings.h"
#include "Renderer/RendererExtras.h"
#include "Settings/BlackBarSettings.h"
#include "Settings/ShaderCatalog.h"
#include "Settings/TonalSettings.h"
#include "DolbyVision/DolbyVisionStatus.h"
#include <optional>

struct VideoFrame
{
    int width = 0, height = 0, stride = 0;
    int aspectX = 0, aspectY = 0;
    PixelFormat format = PixelFormat::NV12;
    pl_color_space color = {};
    std::shared_ptr<const pl_dovi_metadata> dovi;
    DolbyVisionSource dvSource;
    bool dvLayered = false;
    pl_color_system matrix = PL_COLOR_SYSTEM_BT_709;
    pl_color_levels levels = PL_COLOR_LEVELS_LIMITED;
    pl_chroma_location chroma = PL_CHROMA_LEFT;
    bool bottomUp = false;
};

struct TemporalFrame
{
    uint64_t id = 0;
    int64_t start = 0, stop = 0;
    pl_field first = PL_FIELD_NONE;
};

struct RenderSettings
{
    TonalSettings tonal;
    UserShaderSettings userShaders;
    BlackBarSettings blackBars;
    RendererExtras extras;
    ColorMappingSettings colorMapping;
    float saturation = 1.f, contrast = 1.f, brightness = 0.f, gamma = 1.f;
    float hue = 0, temperature = 0;
    bool colorAdjustment = true;
    float sdrNits = 203.f;
    float hdrNits = 0, antiringing = 0, upscalerBlur = 0, downscalerBlur = 0;
    std::string toneMapping = "auto";
    int debandIterations = 1, ditherMethod = 0;
    float debandThreshold = 3, debandRadius = 16, debandGrain = 4;
    bool ditherTemporal = false;
    bool deband = false, hdrPassthrough = false, autoHdr = false;
    bool clippingProtection = false;
    int deinterlacing = 1; // 0=off, 1=Bwdif, 2=Yadif
    std::string upscaler = "ewa_lanczos", downscaler = "mitchell";
    std::string chromaUpscaler = "auto";
    int outputFormat = 0;
    int outputLevels = 0, displayDepth = 0; // levels: auto/full/limited; depth: 0=auto
    int hdrMode = 2;
    bool peakDetect = true, allowDelayedPeak = false;
    float peakSmoothing = 20, sceneLow = 1, sceneHigh = 3, peakPercentile = 100;
    float contrastRecovery = 0, contrastSmoothness = 3.5f;
    int gamutMapping = 0;
    float kneeAdaptation = .4f, splineContrast = .5f, kneeMinimum = .1f, kneeMaximum = .8f;
    float blackCutoff = 1, perceptualStrength = .8f, softclipKnee = .7f;
    bool showClipping = false;
    int maximumFramesInFlight = 0;
    bool adaptiveSharpen = false;
    float sharpenStrength = .5f;
    bool smoothMotion = false;
    bool rtxSuperResolution = false;
    bool imageAnalysis = false;
    std::optional<ScalingSettings> scaling;
    float motionRadius = 0;
    bool iccEnabled = false, lutEnabled = false;
    int iccIntent = 0;
    std::wstring iccPath, lutPath;
};

struct MeasurementSnapshot
{
    bool feedback = false, timing = false, countersAvailable = false;
    uint32_t pending = 0;
    uint64_t drops = 0;
    uint64_t submissions = 0, confirmed = 0, repeats = 0, glitches = 0, resets = 0;
    uint64_t renderCpuSamples = 0, renderGpuSamples = 0, presentCpuSamples = 0;
    double renderCpuAverage = 0, renderCpuMax = 0, renderGpuAverage = 0, renderGpuMax = 0, presentCpuAverage = 0,
           presentCpuMax = 0;
};

struct IDvLayerSample;

class PlaceboBackend
{
    struct State;
    std::unique_ptr<State> state;

  public:
    PlaceboBackend();
    ~PlaceboBackend();
    HRESULT SetWindow(HWND window);
    HRESULT SetDevice(ID3D11Device* device, HANDLE decoderMutex = nullptr);
    HRESULT SetSettings(const RenderSettings& settings);
    using OverlayPainter = std::function<HRESULT(ID3D11Device*, ID3D11DeviceContext*, int, int, const RECT&)>;
    void SetOverlayPainter(OverlayPainter painter, bool inverseAlpha = false);
    HRESULT Upload(const VideoFrame& frame, const uint8_t* data, size_t bytes);
    HRESULT Import(const VideoFrame& frame, ID3D11Texture2D* texture, UINT slice);
    HRESULT ImportDolbyVision(const VideoFrame& frame, IDvLayerSample* sample);
    HRESULT CaptureUpload(const VideoFrame&, const uint8_t*, size_t, uint64_t&);
    HRESULT CaptureImport(const VideoFrame&, ID3D11Texture2D*, UINT, uint64_t&);
    HRESULT CaptureDolbyVision(const VideoFrame&, IDvLayerSample*, uint64_t&);
    HRESULT SelectField(uint64_t current, uint64_t previous, uint64_t next, pl_field field, pl_field first);
    HRESULT SelectMix(uint64_t current,
                      uint64_t previous,
                      uint64_t next,
                      pl_field field,
                      pl_field first,
                      pl_field following,
                      float position,
                      float duration,
                      uint64_t afterNext = 0);
    HRESULT SelectTemporalMix(const std::vector<TemporalFrame>& frames,
                              uint64_t current,
                              uint64_t previous,
                              uint64_t next,
                              pl_field field,
                              pl_field first,
                              int64_t timestamp,
                              float duration);
    HRESULT Present();
    void HoldForResize(bool hold);
    HRESULT TryPresent(bool blackWhenEmpty = false);
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    void SetPresentProbeForTest(std::function<void(ID3D11Texture2D*)>);
    void SetPresentColorProbeForTest(std::function<void(const pl_color_space&)>);
#endif
    bool PaintLoadingBackground(HDC dc);
    void SetGeometry(const RECT& source, const RECT& target);
    HRESULT SetRotation(int degrees);
    int Rotation() const;
    void SetFlip(bool mirror);
    bool Flip() const;
    HRESULT
    RenderOffscreen(int width, int height, std::vector<uint8_t>& rgba, bool hdrTarget = false, int textureDepth = 8);
    void Flush(bool preserveBlackBars = true);
    ID3D11Device* Device() const;
    std::string LastError() const;
    int OutputTextureFormat() const; // positive=integer depth, negative=float depth
    int SharpenState() const;
    int CacheLoadedObjects() const;
    int CacheObjects() const;
    void ShowStatistics(bool enabled);
    bool StatisticsVisible() const;
    uint64_t RtxFrames() const;
    void SetSourceCodec(const std::string& codec);
    void SetStatisticsInput(double fps, bool directGpu, int dropped = -1);
    std::wstring StatisticsText() const;
    void SetPresentationTiming(int64_t dueQpc, bool running, int64_t mediaTime = -1);
    void ResetPresentationMeasurement(bool counters = false);
    void RestartDropMeasurement(int rawBeforeReset);
    MeasurementSnapshot Measurements() const;
    BlackBarStatus BlackBars() const;
    int OutputLevels() const;
    int OutputDitherDepth() const;
};
