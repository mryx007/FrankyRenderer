#include "Renderer/NguScaler.h"
#include "Renderer/UserShaderSource.h"
#include <cstring>

namespace
{
struct StageConfig
{
    const char* file;
    int width, height;
};

const StageConfig* FindStage(NguScalerStage stage)
{
    static constexpr StageConfig Chroma = {"ngu_sharp_veryhigh_chroma_full.glsl", 640, 360};
    static constexpr StageConfig Luma = {"ngu_sharp_veryhigh_luma_1080_core.glsl", 1920, 1080};
    switch (stage)
    {
    case NguScalerStage::VeryHighChromaReference:
        return &Chroma;
    case NguScalerStage::VeryHighLuma1080Core:
        return &Luma;
    default:
        return nullptr;
    }
}

int ShaderId(const char* file)
{
    for (int id = 0; id < ShaderCount; ++id)
        if (!std::strcmp(ShaderCatalog[id].file, file))
            return id;
    return -1;
}
} // namespace

const char* NguScalerSupportText(NguScalerSupport support)
{
    switch (support)
    {
    case NguScalerSupport::Ready:
        return "ready";
    case NguScalerSupport::ExperimentalUnimplemented:
        return "Low/Medium/High NGU are experimental and not implemented";
    case NguScalerSupport::UnsupportedStage:
        return "NGU stage is not evidence-backed";
    case NguScalerSupport::UnsupportedGeometry:
        return "NGU stage is not verified for this geometry";
    }
    return "unknown NGU support state";
}

NguScalerSupport ProbeNguScaler(const NguScalerRequest& request)
{
    if (request.quality != NguQuality::VeryHigh)
        return NguScalerSupport::ExperimentalUnimplemented;
    const StageConfig* stage = FindStage(request.stage);
    if (!stage)
        return NguScalerSupport::UnsupportedStage;
    if (request.sourceWidth != stage->width || request.sourceHeight != stage->height)
        return NguScalerSupport::UnsupportedGeometry;
    return NguScalerSupport::Ready;
}

NguScaler::~NguScaler()
{
    Reset();
}

bool NguScaler::PrepareValidation(pl_gpu gpu, const NguScalerRequest& request)
{
    if (!gpu)
    {
        error_ = "NGU scaler requires a GPU";
        return false;
    }
    if (hasCached_ && gpu == gpu_ && cached_.sourceWidth == request.sourceWidth &&
        cached_.sourceHeight == request.sourceHeight && cached_.quality == request.quality &&
        cached_.stage == request.stage)
        return true;

    Reset();
    const NguScalerSupport support = ProbeNguScaler(request);
    if (support != NguScalerSupport::Ready)
    {
        error_ = NguScalerSupportText(support);
        return false;
    }
    const int id = ShaderId(FindStage(request.stage)->file);
    const std::string source = LoadUserShaderSource(id);
    if (source.empty())
    {
        error_ = "NGU validation shader source is unavailable";
        return false;
    }
    hook_ = pl_mpv_user_shader_parse(gpu, source.data(), source.size());
    if (!hook_)
    {
        error_ = "NGU validation shader failed to parse";
        return false;
    }
    gpu_ = gpu;
    cached_ = request;
    hasCached_ = true;
    error_ = "";
    return true;
}

void NguScaler::Reset()
{
    if (hook_)
        pl_mpv_user_shader_destroy(&hook_);
    gpu_ = nullptr;
    hasCached_ = false;
    cached_ = {};
    error_ = "";
}

const pl_hook* NguScaler::Hook() const
{
    return hook_;
}

NguScalerStage NguScaler::Stage() const
{
    return hasCached_ ? cached_.stage : NguScalerStage::None;
}

const char* NguScaler::LastError() const
{
    return error_;
}
