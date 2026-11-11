#pragma once
#include "Renderer/NguDither.h"
#include <libplacebo/shaders/custom.h>
#include <memory>
#include <string>

struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;

// NGU quality names mirror the target settings. Only VeryHigh has
// evidence-backed validation stages; the other levels stay fail-closed until
// their complete graphs are recovered and verified.
enum class NguQuality
{
    Low,
    Medium,
    High,
    VeryHigh,
};

enum class NguScalerStage
{
    None,
    VeryHighChromaReference,
    VeryHighLumaInitial1080,
    VeryHighLumaLayers1080,
    VeryHighLumaDense1080,
    VeryHighLumaScaleX1080,
    VeryHighLumaScaleY1080,
    VeryHighLumaAdd1080,
    VeryHighLumaUpX1080,
    VeryHighLumaUpY1080,
    VeryHighLumaMerge1080,
    VeryHighLumaFinal1080,
    VeryHighSuperResDownX,
    VeryHighSuperResDownY,
    VeryHighSuperResDiff,
    VeryHighSuperResToRgbMain,
    VeryHighSuperResToRgbBase,
    VeryHighSuperResStrength3,
    VeryHighLuma4x720LowCore,
    VeryHighLuma4x720HighCore,
    VeryHighLuma4x720DenseAdd,
    VeryHighLuma4x720TailYcbcr,
    VeryHighLuma4x720TailDownX,
    VeryHighLuma4x720TailDownY,
    VeryHighLuma4x720Full,
    VeryHighLuma1080Core,
};

enum class NguScalerSupport
{
    Ready,
    ExperimentalUnimplemented,
    UnsupportedStage,
    UnsupportedGeometry,
};

struct NguScalerRequest
{
    int sourceWidth = 0, sourceHeight = 0;
    NguQuality quality = NguQuality::VeryHigh;
    NguScalerStage stage = NguScalerStage::None;
};

const char* NguScalerSupportText(NguScalerSupport support);

// Validation-only capability boundary. A request is Ready only for the exact
// target-observed stage and geometry; it never becomes a guessed fallback.
NguScalerSupport ProbeNguScaler(const NguScalerRequest& request);

// Owns parsed NGU hook state so playback, quality measurement and later native
// scheduling can share one lifecycle without leaking algorithm-specific state
// into unrelated scaler paths.
class NguScaler
{
  public:
    NguScaler();
    ~NguScaler();
    NguScaler(const NguScaler&) = delete;
    NguScaler& operator=(const NguScaler&) = delete;

    bool PrepareValidation(pl_gpu gpu, const NguScalerRequest& request);
    bool PrepareNativeLumaInitial(pl_gpu gpu);
    bool PrepareNativeLumaSplit(pl_gpu gpu);
    bool RunNativeLumaSplit(ID3D11ShaderResourceView* source,
                            int width,
                            int height,
                            ID3D11RenderTargetView* luma,
                            ID3D11RenderTargetView* cbcr);
    bool RunNativeLumaInitial(ID3D11ShaderResourceView* source,
                              int width,
                              int height,
                              ID3D11RenderTargetView* const outputs[3]);
    bool PrepareNativeLumaLayers(pl_gpu gpu);
    bool RunNativeLumaLayer(ID3D11ShaderResourceView* const inputs[3],
                            int layer,
                            ID3D11RenderTargetView* output);
    bool PrepareNativeLumaDense(pl_gpu gpu);
    bool RunNativeLumaDense(ID3D11ShaderResourceView* const inputs[3],
                            ID3D11RenderTargetView* output);
    bool PrepareNativeLumaScaleAdd(pl_gpu gpu);
    bool RunNativeLumaScaleX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output);
    bool RunNativeLumaScaleY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output);
    bool RunNativeLumaAdd(ID3D11ShaderResourceView* scale,
                          ID3D11ShaderResourceView* dense,
                          ID3D11RenderTargetView* output);
    bool PrepareNativeLumaFinish(pl_gpu gpu);
    bool RunNativeLumaUpX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output);
    bool RunNativeLumaUpY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output);
    bool RunNativeLumaMerge(ID3D11ShaderResourceView* luma,
                            ID3D11ShaderResourceView* chroma,
                            ID3D11RenderTargetView* output);
    bool PrepareNativeLumaFinal(pl_gpu gpu);
    bool RunNativeLumaFinal(ID3D11ShaderResourceView* input,
                            ID3D11ShaderResourceView* dither,
                            ID3D11RenderTargetView* output);
    void ResetDitherPhase();
    bool RunNativeLumaFinalNextPhase(ID3D11ShaderResourceView* input,
                                     ID3D11ShaderResourceView* dither,
                                     ID3D11RenderTargetView* output);
    bool RunNativeLumaFinalOwnedDither(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* output);
    bool PrepareNativeLumaVeryHigh1080(pl_gpu gpu);
    bool RunNativeLumaVeryHigh1080(ID3D11ShaderResourceView* sourceRgb,
                                   int sourceWidth,
                                   int sourceHeight,
                                   ID3D11RenderTargetView* finalOutput,
                                   bool advanceDither = true);
    bool CopyNativeLumaVeryHighAdd(ID3D11RenderTargetView* output);
    bool CopyNativeLumaVeryHighUpX(ID3D11RenderTargetView* output);
    bool CopyNativeLumaVeryHighUpY(ID3D11RenderTargetView* output);
    bool CopyNativeLumaVeryHighMerge(ID3D11RenderTargetView* output);
    bool PrepareNativeSuperResPrimitives(pl_gpu gpu);
    bool RunNativeSuperResDownX(ID3D11ShaderResourceView* input,
                                ID3D11RenderTargetView* output);
    bool RunNativeSuperResDownY(ID3D11ShaderResourceView* input,
                                ID3D11RenderTargetView* output);
    bool RunNativeSuperResDiff(ID3D11ShaderResourceView* original,
                               ID3D11ShaderResourceView* downsample,
                               ID3D11RenderTargetView* output);
    bool RunNativeSuperResToRgbMain(ID3D11ShaderResourceView* original,
                                    ID3D11ShaderResourceView* work,
                                    ID3D11ShaderResourceView* difference,
                                    ID3D11RenderTargetView* output);
    bool RunNativeSuperResToRgbBase(ID3D11ShaderResourceView* original,
                                    ID3D11ShaderResourceView* work,
                                    ID3D11ShaderResourceView* difference,
                                    ID3D11RenderTargetView* output);
    bool PrepareNativeSuperResStrength3(pl_gpu gpu);
    bool RunNativeSuperResStrength3(ID3D11ShaderResourceView* source,
                                    ID3D11ShaderResourceView* diffOriginal,
                                    ID3D11ShaderResourceView* mainOriginal,
                                    ID3D11ShaderResourceView* baseOriginal,
                                    ID3D11RenderTargetView* output);
    bool PrepareNativeLuma4x720Low(pl_gpu gpu);
    bool RunNativeLuma4x720Low(ID3D11ShaderResourceView* sourceRgb,
                               int sourceWidth,
                               int sourceHeight);
    bool CopyNativeLuma4x720SplitY(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720SplitCbCr(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720ScaleX(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720ScaleY(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720Initial(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720LowLayer(ID3D11RenderTargetView* output);
    bool PrepareNativeLuma4x720High(pl_gpu gpu);
    bool RunNativeLuma4x720High(ID3D11ShaderResourceView* sourceRgb,
                                int sourceWidth,
                                int sourceHeight);
    bool CopyNativeLuma4x720Upsample(int feature,
                                     int part,
                                     ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720Convert(int feature, ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720HighLayer(int feature, ID3D11RenderTargetView* output);
    bool PrepareNativeLuma4x720DenseAdd(pl_gpu gpu);
    bool RunNativeLuma4x720DenseAdd(ID3D11ShaderResourceView* sourceRgb,
                                    int sourceWidth,
                                    int sourceHeight);
    bool CopyNativeLuma4x720Dense(ID3D11RenderTargetView* output);
    bool CopyNativeLuma4x720Add(ID3D11RenderTargetView* output);
    bool PrepareNativeLuma4x720Tail(pl_gpu gpu);
    bool RunNativeLuma4x720TailYcbcr(ID3D11ShaderResourceView* input,
                                     ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailDownX(ID3D11ShaderResourceView* input,
                                     ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailDownY(ID3D11ShaderResourceView* input,
                                     ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailSsimRt(ID3D11ShaderResourceView* input,
                                      ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailSsimFinal(ID3D11ShaderResourceView* rt,
                                         ID3D11ShaderResourceView* source,
                                         ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailSharpenAr(ID3D11ShaderResourceView* input,
                                         ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailChromaX(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailChromaY(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* output);
    bool RunNativeLuma4x720TailMerge(ID3D11ShaderResourceView* luma,
                                     ID3D11ShaderResourceView* chroma,
                                     ID3D11RenderTargetView* output);
    bool PrepareNativeLuma4x720Full(pl_gpu gpu);
    bool RunNativeLuma4x720Full(ID3D11ShaderResourceView* sourceRgb,
                                int sourceWidth,
                                int sourceHeight,
                                ID3D11RenderTargetView* finalOutput);
    void Reset();

    const pl_hook* Hook() const;
    NguScalerStage Stage() const;
    const char* LastError() const;

  private:
    bool PrepareOwnedDither();
    struct NativeLumaInitial;
    struct NativeLumaSplit;
    struct NativeLumaLayers;
    struct NativeLumaDense;
    struct NativeLumaScaleAdd;
    struct NativeLumaFinish;
    struct NativeLumaFinal;
    struct NativeLumaVeryHigh1080;
    struct NativeSuperResPrimitives;
    struct NativeSuperResStrength3;
    struct NativeLuma4x720Low;
    struct NativeLuma4x720High;
    struct NativeLuma4x720DenseAdd;
    struct NativeLuma4x720Tail;
    pl_gpu gpu_ = nullptr;
    const pl_hook* hook_ = nullptr;
    NguScalerRequest cached_ = {};
    std::string error_;
    bool hasCached_ = false;
    NguDither dither_;
    std::unique_ptr<NativeLumaInitial> nativeLumaInitial_;
    std::unique_ptr<NativeLumaSplit> nativeLumaSplit_;
    std::unique_ptr<NativeLumaLayers> nativeLumaLayers_;
    std::unique_ptr<NativeLumaDense> nativeLumaDense_;
    std::unique_ptr<NativeLumaScaleAdd> nativeLumaScaleAdd_;
    std::unique_ptr<NativeLumaFinish> nativeLumaFinish_;
    std::unique_ptr<NativeLumaFinal> nativeLumaFinal_;
    std::unique_ptr<NativeLumaVeryHigh1080> nativeLumaVeryHigh1080_;
    std::unique_ptr<NativeSuperResPrimitives> nativeSuperRes_;
    std::unique_ptr<NativeSuperResStrength3> nativeSuperResStrength3_;
    std::unique_ptr<NativeLuma4x720Low> nativeLuma4x720Low_;
    std::unique_ptr<NativeLuma4x720High> nativeLuma4x720High_;
    std::unique_ptr<NativeLuma4x720DenseAdd> nativeLuma4x720DenseAdd_;
    std::unique_ptr<NativeLuma4x720Tail> nativeLuma4x720Tail_;
};
