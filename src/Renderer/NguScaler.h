#pragma once
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
    void Reset();

    const pl_hook* Hook() const;
    NguScalerStage Stage() const;
    const char* LastError() const;

  private:
    struct NativeLumaInitial;
    struct NativeLumaLayers;
    struct NativeLumaDense;
    struct NativeLumaScaleAdd;
    struct NativeLumaFinish;
    pl_gpu gpu_ = nullptr;
    const pl_hook* hook_ = nullptr;
    NguScalerRequest cached_ = {};
    std::string error_;
    bool hasCached_ = false;
    std::unique_ptr<NativeLumaInitial> nativeLumaInitial_;
    std::unique_ptr<NativeLumaLayers> nativeLumaLayers_;
    std::unique_ptr<NativeLumaDense> nativeLumaDense_;
    std::unique_ptr<NativeLumaScaleAdd> nativeLumaScaleAdd_;
    std::unique_ptr<NativeLumaFinish> nativeLumaFinish_;
};
