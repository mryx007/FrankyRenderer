#include "Renderer/NguScaler.h"
#include "Renderer/UserShaderSource.h"
#include <d3d11.h>
#include <d3d11_3.h>
#include <d3dcompiler.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
thread_local uint64_t gDrawCalls = 0;

struct DrawCountScope
{
    bool enabled = std::getenv("FRANKY_NGU_COUNT_DRAWS") != nullptr;

    DrawCountScope()
    {
        gDrawCalls = 0;
    }

    ~DrawCountScope()
    {
        if (enabled)
            std::fprintf(stderr, "NGU_DRAW_CALLS %llu\n", static_cast<unsigned long long>(gDrawCalls));
    }
};

// E-158: source-bound 1920x1080 target FinalStep PS_1352 constants. The
// captured dither phase in U2.yz is replaced by NguDither per frame.
constexpr std::array<float, 16> NguTarget1080FinalConstants = {0.00390625f,
                                                               0.00125125889f,
                                                               0.563585341f,
                                                               0.03125f,
                                                               1.16438353f,
                                                               -0.0730593577f,
                                                               1920.0f,
                                                               1080.0f,
                                                               255.0f,
                                                               1.0f,
                                                               1.0f,
                                                               255.0f,
                                                               0.00392156886f,
                                                               0.00392156886f,
                                                               0.0f,
                                                               960.0f};
constexpr std::array<float, 16> NguTarget720FinalConstants = {0.00390625f,
                                                              0.350291461f,
                                                              0.895962417f,
                                                              0.03125f,
                                                              1.16438353f,
                                                              -0.0730593577f,
                                                              2560.0f,
                                                              1440.0f,
                                                              255.0f,
                                                              1.0f,
                                                              1.0f,
                                                              255.0f,
                                                              0.00392156886f,
                                                              0.00392156886f,
                                                              0.0f,
                                                              1280.0f};

struct StageConfig
{
    const char* hookFile;
    int width, height;
};

const StageConfig* FindStage(NguScalerStage stage)
{
    static constexpr StageConfig Chroma = {"ngu_sharp_veryhigh_chroma_full.glsl", 640, 360};
    static constexpr StageConfig LumaInitial = {nullptr, 1920, 1080};
    static constexpr StageConfig LumaLayers = {nullptr, 1920, 1080};
    static constexpr StageConfig LumaDense = {nullptr, 1920, 1080};
    static constexpr StageConfig LumaScaleX = {nullptr, 1920, 1080};
    static constexpr StageConfig LumaScaleY = {nullptr, 3840, 1080};
    static constexpr StageConfig LumaAdd = {nullptr, 3840, 2160};
    static constexpr StageConfig LumaUpX = {nullptr, 1920, 1080};
    static constexpr StageConfig LumaUpY = {nullptr, 3840, 1080};
    static constexpr StageConfig LumaMerge = {nullptr, 3840, 2160};
    static constexpr StageConfig LumaFinal = {nullptr, 3840, 2160};
    static constexpr StageConfig SuperResDownX = {nullptr, 1280, 720};
    static constexpr StageConfig SuperResDownY = {nullptr, 640, 720};
    static constexpr StageConfig SuperResDiff = {nullptr, 640, 360};
    static constexpr StageConfig SuperResToRgb = {nullptr, 1280, 720};
    static constexpr StageConfig SuperResStrength3 = {nullptr, 1280, 720};
    static constexpr StageConfig Luma1080SuperResFull = {nullptr, 1920, 1080};
    static constexpr StageConfig Luma4x720LowCore = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720HighCore = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720DenseAdd = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720TailYcbcr = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720TailDownX = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720TailDownY = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720Full = {nullptr, 1280, 720};
    static constexpr StageConfig Luma4x720SuperResFull = {nullptr, 1280, 720};
    static constexpr StageConfig Class3ChromaTail720 = {nullptr, 1280, 720};
    static constexpr StageConfig ChromaCoreClass3_720 = {nullptr, 640, 720};
    static constexpr StageConfig NguClass3_720 = {nullptr, 1280, 720};
    static constexpr StageConfig ChromaCoreClass3_1080 = {nullptr, 960, 1080};
    static constexpr StageConfig NguClass3_1080 = {nullptr, 1920, 1080};
    static constexpr StageConfig Luma = {"ngu_sharp_veryhigh_luma_1080_core.glsl", 1920, 1080};
    switch (stage)
    {
    case NguScalerStage::VeryHighChromaReference:
        return &Chroma;
    case NguScalerStage::VeryHighLumaInitial1080:
        return &LumaInitial;
    case NguScalerStage::VeryHighLumaLayers1080:
        return &LumaLayers;
    case NguScalerStage::VeryHighLumaDense1080:
        return &LumaDense;
    case NguScalerStage::VeryHighLumaScaleX1080:
        return &LumaScaleX;
    case NguScalerStage::VeryHighLumaScaleY1080:
        return &LumaScaleY;
    case NguScalerStage::VeryHighLumaAdd1080:
        return &LumaAdd;
    case NguScalerStage::VeryHighLumaUpX1080:
        return &LumaUpX;
    case NguScalerStage::VeryHighLumaUpY1080:
        return &LumaUpY;
    case NguScalerStage::VeryHighLumaMerge1080:
        return &LumaMerge;
    case NguScalerStage::VeryHighLumaFinal1080:
        return &LumaFinal;
    case NguScalerStage::VeryHighSuperResDownX:
        return &SuperResDownX;
    case NguScalerStage::VeryHighSuperResDownY:
        return &SuperResDownY;
    case NguScalerStage::VeryHighSuperResDiff:
        return &SuperResDiff;
    case NguScalerStage::VeryHighSuperResToRgbMain:
    case NguScalerStage::VeryHighSuperResToRgbBase:
        return &SuperResToRgb;
    case NguScalerStage::VeryHighSuperResStrength3:
        return &SuperResStrength3;
    case NguScalerStage::VeryHighLuma1080SuperResFull:
        return &Luma1080SuperResFull;
    case NguScalerStage::VeryHighLuma4x720LowCore:
        return &Luma4x720LowCore;
    case NguScalerStage::VeryHighLuma4x720HighCore:
        return &Luma4x720HighCore;
    case NguScalerStage::VeryHighLuma4x720DenseAdd:
        return &Luma4x720DenseAdd;
    case NguScalerStage::VeryHighLuma4x720TailYcbcr:
        return &Luma4x720TailYcbcr;
    case NguScalerStage::VeryHighLuma4x720TailDownX:
        return &Luma4x720TailDownX;
    case NguScalerStage::VeryHighLuma4x720TailDownY:
        return &Luma4x720TailDownY;
    case NguScalerStage::VeryHighLuma4x720Full:
        return &Luma4x720Full;
    case NguScalerStage::VeryHighLuma4x720SuperResFull:
        return &Luma4x720SuperResFull;
    case NguScalerStage::VeryHighClass3ChromaTail720:
        return &Class3ChromaTail720;
    case NguScalerStage::VeryHighChromaCoreClass3_720:
        return &ChromaCoreClass3_720;
    case NguScalerStage::VeryHighNguClass3_720:
        return &NguClass3_720;
    case NguScalerStage::VeryHighChromaCoreClass3_1080:
        return &ChromaCoreClass3_1080;
    case NguScalerStage::VeryHighNguClass3_1080:
        return &NguClass3_1080;
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

std::vector<uint8_t> LoadBundledBytes(const wchar_t* relative, size_t expected)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadBundledBytes),
                            &module))
        return {};
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768)
        return {};
    std::ifstream file(std::filesystem::path(path).parent_path() / relative, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size <= 0 || (expected && size != static_cast<std::streamoff>(expected)))
        return {};
    const size_t byteCount = static_cast<size_t>(size);
    file.seekg(0);
    std::vector<uint8_t> bytes(byteCount);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return {};
    return bytes;
}

std::string LoadBundledText(const wchar_t* relative, size_t maximum)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadBundledText),
                            &module))
        return {};
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768)
        return {};
    std::ifstream file(std::filesystem::path(path).parent_path() / relative, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size <= 0 || static_cast<size_t>(size) > maximum)
        return {};
    file.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size())))
        return {};
    return text;
}

void DrawD3D11Pass(ID3D11Device* device,
                   ID3D11VertexShader* vertex,
                   ID3D11PixelShader* pixel,
                   ID3D11Buffer* constants,
                   ID3D11RasterizerState* rasterizer,
                   ID3D11ShaderResourceView* const* inputs,
                   UINT inputCount,
                   ID3D11RenderTargetView* const* outputs,
                   UINT outputCount,
                   UINT width,
                   UINT height,
                   ID3D11SamplerState* sampler0 = nullptr,
                   ID3D11SamplerState* sampler5 = nullptr,
                   ID3D11SamplerState* sampler7 = nullptr,
                   ID3D11SamplerState* sampler2 = nullptr,
                   ID3D11SamplerState* sampler3 = nullptr,
                   ID3D11SamplerState* sampler1 = nullptr)
{
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    if (!context)
        return;
    ++gDrawCalls;

    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDepth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);
    ID3D11RasterizerState* oldRasterizer = nullptr;
    context->RSGetState(&oldRasterizer);
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> oldViewports = {};
    context->RSGetViewports(&viewportCount, oldViewports.data());
    ID3D11VertexShader* oldVertex = nullptr;
    ID3D11PixelShader* oldPixel = nullptr;
    ID3D11ShaderResourceView* oldInputs[4] = {};
    ID3D11SamplerState* oldSampler0 = nullptr;
    ID3D11SamplerState* oldSampler1 = nullptr;
    ID3D11SamplerState* oldSampler2 = nullptr;
    ID3D11SamplerState* oldSampler3 = nullptr;
    ID3D11SamplerState* oldSampler5 = nullptr;
    ID3D11SamplerState* oldSampler7 = nullptr;
    ID3D11Buffer* oldConstant = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->VSGetShader(&oldVertex, nullptr, nullptr);
    context->PSGetShader(&oldPixel, nullptr, nullptr);
    context->PSGetShaderResources(0, inputCount, oldInputs);
    context->PSGetSamplers(0, 1, &oldSampler0);
    context->PSGetSamplers(1, 1, &oldSampler1);
    context->PSGetSamplers(2, 1, &oldSampler2);
    context->PSGetSamplers(3, 1, &oldSampler3);
    context->PSGetSamplers(5, 1, &oldSampler5);
    context->PSGetSamplers(7, 1, &oldSampler7);
    context->PSGetConstantBuffers(0, 1, &oldConstant);
    context->IAGetPrimitiveTopology(&oldTopology);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(rasterizer);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(outputCount, outputs, nullptr);
    context->VSSetShader(vertex, nullptr, 0);
    context->PSSetShader(pixel, nullptr, 0);
    context->PSSetShaderResources(0, inputCount, inputs);
    context->PSSetSamplers(0, 1, &sampler0);
    context->PSSetSamplers(1, 1, &sampler1);
    context->PSSetSamplers(2, 1, &sampler2);
    context->PSSetSamplers(3, 1, &sampler3);
    context->PSSetSamplers(5, 1, &sampler5);
    context->PSSetSamplers(7, 1, &sampler7);
    context->PSSetConstantBuffers(0, 1, &constants);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullInputs[4] = {};
    ID3D11Buffer* nullConstant = nullptr;
    context->PSSetShaderResources(0, inputCount, nullInputs);
    ID3D11SamplerState* nullSampler = nullptr;
    context->PSSetSamplers(0, 1, &nullSampler);
    context->PSSetSamplers(1, 1, &nullSampler);
    context->PSSetSamplers(2, 1, &nullSampler);
    context->PSSetSamplers(3, 1, &nullSampler);
    context->PSSetSamplers(5, 1, &nullSampler);
    context->PSSetSamplers(7, 1, &nullSampler);
    context->PSSetConstantBuffers(0, 1, &nullConstant);

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
    context->RSSetState(oldRasterizer);
    if (viewportCount)
        context->RSSetViewports(viewportCount, oldViewports.data());
    context->VSSetShader(oldVertex, nullptr, 0);
    context->PSSetShader(oldPixel, nullptr, 0);
    context->PSSetShaderResources(0, inputCount, oldInputs);
    context->PSSetSamplers(0, 1, &oldSampler0);
    context->PSSetSamplers(1, 1, &oldSampler1);
    context->PSSetSamplers(2, 1, &oldSampler2);
    context->PSSetSamplers(3, 1, &oldSampler3);
    context->PSSetSamplers(5, 1, &oldSampler5);
    context->PSSetSamplers(7, 1, &oldSampler7);
    context->PSSetConstantBuffers(0, 1, &oldConstant);
    context->IASetPrimitiveTopology(oldTopology);
    for (auto*& target : oldTargets)
        if (target)
            target->Release();
    if (oldDepth)
        oldDepth->Release();
    if (oldRasterizer)
        oldRasterizer->Release();
    if (oldVertex)
        oldVertex->Release();
    if (oldPixel)
        oldPixel->Release();
    for (UINT i = 0; i < inputCount; ++i)
        if (oldInputs[i])
            oldInputs[i]->Release();
    if (oldSampler0)
        oldSampler0->Release();
    if (oldSampler1)
        oldSampler1->Release();
    if (oldSampler2)
        oldSampler2->Release();
    if (oldSampler3)
        oldSampler3->Release();
    if (oldSampler5)
        oldSampler5->Release();
    if (oldSampler7)
        oldSampler7->Release();
    if (oldConstant)
        oldConstant->Release();
}

bool ViewMatchesTexture(ID3D11View* view, DXGI_FORMAT format, UINT width, UINT height)
{
    if (!view)
        return false;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (!resource || FAILED(resource.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D11ShaderResourceView> shaderView;
    if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shaderView))))
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        shaderView->GetDesc(&viewDesc);
        if (viewDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)
            viewFormat = viewDesc.Format;
    }
    else
    {
        ComPtr<ID3D11RenderTargetView> renderTargetView;
        if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&renderTargetView))))
        {
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
            renderTargetView->GetDesc(&viewDesc);
            if (viewDesc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D)
                viewFormat = viewDesc.Format;
        }
    }
    const bool formatMatches = desc.Format == format ||
                               (desc.Format == DXGI_FORMAT_R16_TYPELESS && viewFormat == format &&
                                (format == DXGI_FORMAT_R16_UNORM || format == DXGI_FORMAT_R16_FLOAT)) ||
                               (desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS && viewFormat == format &&
                                (format == DXGI_FORMAT_R16G16B16A16_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT));
    return formatMatches && desc.Width == width && desc.Height == height && desc.ArraySize == 1 &&
           desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
}

bool ViewMatchesRgb8(ID3D11View* view, UINT width, UINT height)
{
    return ViewMatchesTexture(view, DXGI_FORMAT_B8G8R8A8_UNORM, width, height) ||
           ViewMatchesTexture(view, DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
}

bool CopyTextureToTarget(pl_gpu gpu, ID3D11Texture2D* source, ID3D11RenderTargetView* output)
{
    if (!gpu || !source || !output)
        return false;
    ComPtr<ID3D11Resource> destination;
    output->GetResource(&destination);
    if (!destination)
        return false;
    ComPtr<ID3D11DeviceContext> context;
    pl_d3d11_get(gpu)->device->GetImmediateContext(context.GetAddressOf());
    if (!context)
        return false;
    context->CopyResource(destination.Get(), source);
    return true;
}

void FillClass3PostConstants(std::array<float, 16>& constants, UINT width, UINT height, bool swapAxes)
{
    static constexpr float coefficients[12] = {
        -0.12963299453258514f,
        0.6296330094337463f,
        -0.17506800591945648f,
        0.6750680208206177f,
        -0.19444948434829712f,
        0.6944494843482971f,
        -0.2626020014286041f,
        0.7626020312309265f,
        -0.09722474217414856f,
        0.597224771976471f,
        -0.13130100071430206f,
        0.6313009858131409f,
    };
    constants[0] = static_cast<float>(width);
    constants[1] = static_cast<float>(height);
    constants[2] = 1.0f / static_cast<float>(width);
    constants[3] = 1.0f / static_cast<float>(height);
    if (!swapAxes)
    {
        for (int index = 0; index < 12; ++index)
            constants[4 + index] = coefficients[index];
        return;
    }
    constexpr int order[6] = {1, 0, 3, 2, 5, 4};
    for (int pair = 0; pair < 6; ++pair)
    {
        const int sourcePair = order[pair];
        constants[4 + pair * 2] = coefficients[sourcePair * 2];
        constants[5 + pair * 2] = coefficients[sourcePair * 2 + 1];
    }
}
} // namespace

void NguDither::SeedColorFromClock()
{
    SYSTEMTIME systemTime = {};
    FILETIME fileTime = {};
    GetSystemTime(&systemTime);
    SystemTimeToFileTime(&systemTime, &fileTime);
    SeedColor(static_cast<uint32_t>(GetTickCount()), fileTime.dwLowDateTime, fileTime.dwHighDateTime);
}

struct NguScaler::NativeLumaInitial
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaSplit
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeDecoderSource
{
    ComPtr<ID3D11Device3> device3;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaSplit
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaScale
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> scaleX;
    ComPtr<ID3D11PixelShader> scaleXClass3;
    ComPtr<ID3D11PixelShader> scaleY;
    ComPtr<ID3D11PixelShader> scaleYClass3;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Buffer> scaleYConstants;
    ComPtr<ID3D11Buffer> scaleXConstants1080;
    ComPtr<ID3D11Buffer> scaleYConstants1080;
    ComPtr<ID3D11Buffer> scaleXConstantsClass3;
    ComPtr<ID3D11Buffer> scaleYConstantsClass3;
    ComPtr<ID3D11Buffer> scaleXConstantsClass3_1080;
    ComPtr<ID3D11Buffer> scaleYConstantsClass3_1080;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguChromaQualityProfile
{
    ComPtr<ID3D11PixelShader> initial;
    ComPtr<ID3D11PixelShader> mixer;
    ComPtr<ID3D11PixelShader> layer;
    ComPtr<ID3D11PixelShader> dense;
    std::array<ComPtr<ID3D11Buffer>, 4> initialConstants;
    std::array<ComPtr<ID3D11Buffer>, 4> mixerConstants;
    std::array<std::array<ComPtr<ID3D11Buffer>, 28>, 4> layerConstants;
    std::array<ComPtr<ID3D11Buffer>, 4> denseConstants;
    int initialOutputs = 0;
    int layerInputs = 0;
    int layerOutputs = 0;
    int layerCount = 0;
    int layersPerGroup = 0;
    const char* name = "";
};

struct NguScaler::NativeChromaNetwork
{
    ComPtr<ID3D11VertexShader> vertex;
    std::array<NguChromaQualityProfile, 4> qualities;
    // Retained for the validation-only VeryHigh pass entry points. Product
    // scheduling uses qualities[].
    ComPtr<ID3D11PixelShader> cubici2;
    ComPtr<ID3D11PixelShader> layer;
    ComPtr<ID3D11PixelShader> dense;
    ComPtr<ID3D11Buffer> cubici2Constants;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstants;
    ComPtr<ID3D11Buffer> denseConstants;
    ComPtr<ID3D11Buffer> cubici2Constants1080;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstants1080;
    ComPtr<ID3D11Buffer> denseConstants1080;
    ComPtr<ID3D11Buffer> cubici2ConstantsClass3;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstantsClass3;
    ComPtr<ID3D11Buffer> denseConstantsClass3;
    ComPtr<ID3D11Buffer> cubici2ConstantsClass3_1080;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstantsClass3_1080;
    ComPtr<ID3D11Buffer> denseConstantsClass3_1080;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaVeryHighResources
{
    UINT width = 0;
    UINT height = 0;
    NguQuality quality = NguQuality::VeryHigh;
    std::array<ComPtr<ID3D11Texture2D>, 4> initialTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 4> initialViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> initialShaderViews;
    std::array<std::array<ComPtr<ID3D11Texture2D>, 4>, 2> groupTextures;
    std::array<std::array<ComPtr<ID3D11RenderTargetView>, 4>, 2> groupViews;
    std::array<std::array<ComPtr<ID3D11ShaderResourceView>, 4>, 2> groupShaderViews;
};

struct NguScaler::NativeChromaCombines
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> combine1;
    ComPtr<ID3D11PixelShader> combine2;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> constants1080;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaRealign
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> constants1080;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaYCbCr
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11PixelShader> class3Pixel;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaFullResources
{
    UINT width = 0;
    UINT height = 0;
    NguQuality quality = NguQuality::VeryHigh;
    ComPtr<ID3D11Texture2D> cbTexture;
    ComPtr<ID3D11RenderTargetView> cbView;
    ComPtr<ID3D11ShaderResourceView> cbShaderView;
    ComPtr<ID3D11Texture2D> crTexture;
    ComPtr<ID3D11RenderTargetView> crView;
    ComPtr<ID3D11ShaderResourceView> crShaderView;
    ComPtr<ID3D11Texture2D> cbScaleXTexture;
    ComPtr<ID3D11RenderTargetView> cbScaleXView;
    ComPtr<ID3D11ShaderResourceView> cbScaleXShaderView;
    ComPtr<ID3D11Texture2D> cbScaleYTexture;
    ComPtr<ID3D11RenderTargetView> cbScaleYView;
    ComPtr<ID3D11ShaderResourceView> cbScaleYShaderView;
    ComPtr<ID3D11Texture2D> crScaleXTexture;
    ComPtr<ID3D11RenderTargetView> crScaleXView;
    ComPtr<ID3D11ShaderResourceView> crScaleXShaderView;
    ComPtr<ID3D11Texture2D> crScaleYTexture;
    ComPtr<ID3D11RenderTargetView> crScaleYView;
    ComPtr<ID3D11ShaderResourceView> crScaleYShaderView;
    ComPtr<ID3D11Texture2D> denseCbTexture;
    ComPtr<ID3D11RenderTargetView> denseCbView;
    ComPtr<ID3D11ShaderResourceView> denseCbShaderView;
    ComPtr<ID3D11Texture2D> denseCrTexture;
    ComPtr<ID3D11RenderTargetView> denseCrView;
    ComPtr<ID3D11ShaderResourceView> denseCrShaderView;
    ComPtr<ID3D11Texture2D> combine1Texture;
    ComPtr<ID3D11RenderTargetView> combine1View;
    ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
    ComPtr<ID3D11Texture2D> combine2Texture;
    ComPtr<ID3D11RenderTargetView> combine2View;
    ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
    ComPtr<ID3D11Texture2D> realignTexture;
    ComPtr<ID3D11RenderTargetView> realignView;
    ComPtr<ID3D11ShaderResourceView> realignShaderView;
    ComPtr<ID3D11Texture2D> ycbcrTexture;
    ComPtr<ID3D11RenderTargetView> ycbcrView;
    ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
};

struct NguScaler::NativeNguFromPlanesResources
{
    UINT width = 0;
    UINT height = 0;
    NguQuality quality = NguQuality::VeryHigh;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> expandPixel;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11Texture2D> yTexture;
    ComPtr<ID3D11RenderTargetView> yView;
    ComPtr<ID3D11ShaderResourceView> yShaderView;
    ComPtr<ID3D11Texture2D> chromaTexture;
    ComPtr<ID3D11RenderTargetView> chromaView;
    ComPtr<ID3D11ShaderResourceView> chromaShaderView;
};

struct NguScaler::NativeLumaLayers
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    std::array<ComPtr<ID3D11Buffer>, 15> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaDense
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaScaleAdd
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> scaleX;
    ComPtr<ID3D11PixelShader> scaleY;
    ComPtr<ID3D11PixelShader> add;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Buffer> scaleYConstants;
    ComPtr<ID3D11Buffer> addConstants;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaFinish
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> upX;
    ComPtr<ID3D11PixelShader> upY;
    ComPtr<ID3D11PixelShader> merge;
    ComPtr<ID3D11Buffer> upXConstants;
    ComPtr<ID3D11Buffer> upYConstants;
    ComPtr<ID3D11Buffer> mergeConstants;
    ComPtr<ID3D11Texture2D> upXTable;
    ComPtr<ID3D11Texture2D> upYTable;
    ComPtr<ID3D11ShaderResourceView> upXTableView;
    ComPtr<ID3D11ShaderResourceView> upYTableView;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaFinal
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> constants1080;
    ComPtr<ID3D11Buffer> constants720;
    std::array<float, 16> constantsData{};
    std::array<float, 16> constantsData1080 = NguTarget1080FinalConstants;
    std::array<float, 16> constantsData720 = NguTarget720FinalConstants;
    ComPtr<ID3D11Texture2D> weightTexture;
    ComPtr<ID3D11ShaderResourceView> weightView;
    ComPtr<ID3D11Texture2D> ditherTexture;
    ComPtr<ID3D11ShaderResourceView> ditherView;
    ComPtr<ID3D11Texture2D> ditherTexture1080;
    ComPtr<ID3D11ShaderResourceView> ditherView1080;
    ComPtr<ID3D11Texture2D> ditherTexture720;
    ComPtr<ID3D11ShaderResourceView> ditherView720;
    ComPtr<ID3D11SamplerState> clampSampler;
    ComPtr<ID3D11SamplerState> wrapSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeHookOutput
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeLumaVeryHigh1080
{
    ComPtr<ID3D11Texture2D> splitLumaTexture;
    ComPtr<ID3D11RenderTargetView> splitLumaView;
    ComPtr<ID3D11ShaderResourceView> splitLumaShaderView;
    ComPtr<ID3D11Texture2D> splitCbCrTexture;
    ComPtr<ID3D11RenderTargetView> splitCbCrView;
    ComPtr<ID3D11ShaderResourceView> splitCbCrShaderView;
    std::array<ComPtr<ID3D11Texture2D>, 3> initialTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> initialViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> initialShaderViews;
    std::array<std::array<ComPtr<ID3D11Texture2D>, 3>, 2> layerTextures;
    std::array<std::array<ComPtr<ID3D11RenderTargetView>, 3>, 2> layerViews;
    std::array<std::array<ComPtr<ID3D11ShaderResourceView>, 3>, 2> layerShaderViews;
    ComPtr<ID3D11Texture2D> denseTexture;
    ComPtr<ID3D11RenderTargetView> denseView;
    ComPtr<ID3D11ShaderResourceView> denseShaderView;
    ComPtr<ID3D11Texture2D> scaleXTexture;
    ComPtr<ID3D11RenderTargetView> scaleXView;
    ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
    ComPtr<ID3D11Texture2D> scaleYTexture;
    ComPtr<ID3D11RenderTargetView> scaleYView;
    ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
    ComPtr<ID3D11Texture2D> addTexture;
    ComPtr<ID3D11RenderTargetView> addView;
    ComPtr<ID3D11ShaderResourceView> addShaderView;
    ComPtr<ID3D11Texture2D> upXTexture;
    ComPtr<ID3D11RenderTargetView> upXView;
    ComPtr<ID3D11ShaderResourceView> upXShaderView;
    ComPtr<ID3D11Texture2D> upYTexture;
    ComPtr<ID3D11RenderTargetView> upYView;
    ComPtr<ID3D11ShaderResourceView> upYShaderView;
    ComPtr<ID3D11Texture2D> mergeTexture;
    ComPtr<ID3D11RenderTargetView> mergeView;
    ComPtr<ID3D11ShaderResourceView> mergeShaderView;
};

struct NguScaler::NativeLuma720Double
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> split;
    ComPtr<ID3D11PixelShader> initial;
    ComPtr<ID3D11PixelShader> layer;
    ComPtr<ID3D11PixelShader> dense;
    ComPtr<ID3D11PixelShader> scaleX;
    ComPtr<ID3D11PixelShader> scaleY;
    ComPtr<ID3D11PixelShader> cbcrScaleX;
    ComPtr<ID3D11PixelShader> cbcrScaleY;
    ComPtr<ID3D11PixelShader> add;
    ComPtr<ID3D11PixelShader> merge;
    ComPtr<ID3D11Buffer> initialConstants;
    std::array<ComPtr<ID3D11Buffer>, 15> layerConstants;
    ComPtr<ID3D11Buffer> denseConstants;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Buffer> scaleYConstants;
    ComPtr<ID3D11Buffer> chromaXConstants;
    ComPtr<ID3D11Buffer> chromaYConstants;
    ComPtr<ID3D11Buffer> addConstants;
    ComPtr<ID3D11Buffer> mergeConstants;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11Texture2D> splitLumaTexture;
    ComPtr<ID3D11RenderTargetView> splitLumaView;
    ComPtr<ID3D11ShaderResourceView> splitLumaShaderView;
    ComPtr<ID3D11Texture2D> splitCbCrTexture;
    ComPtr<ID3D11RenderTargetView> splitCbCrView;
    ComPtr<ID3D11ShaderResourceView> splitCbCrShaderView;
    std::array<ComPtr<ID3D11Texture2D>, 3> initialTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> initialViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> initialShaderViews;
    std::array<std::array<ComPtr<ID3D11Texture2D>, 3>, 2> layerTextures;
    std::array<std::array<ComPtr<ID3D11RenderTargetView>, 3>, 2> layerViews;
    std::array<std::array<ComPtr<ID3D11ShaderResourceView>, 3>, 2> layerShaderViews;
    ComPtr<ID3D11Texture2D> denseTexture;
    ComPtr<ID3D11RenderTargetView> denseView;
    ComPtr<ID3D11ShaderResourceView> denseShaderView;
    ComPtr<ID3D11Texture2D> scaleXTexture;
    ComPtr<ID3D11RenderTargetView> scaleXView;
    ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
    ComPtr<ID3D11Texture2D> scaleYTexture;
    ComPtr<ID3D11RenderTargetView> scaleYView;
    ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
    ComPtr<ID3D11Texture2D> chromaXTexture;
    ComPtr<ID3D11RenderTargetView> chromaXView;
    ComPtr<ID3D11ShaderResourceView> chromaXShaderView;
    ComPtr<ID3D11Texture2D> chromaYTexture;
    ComPtr<ID3D11RenderTargetView> chromaYView;
    ComPtr<ID3D11ShaderResourceView> chromaYShaderView;
    ComPtr<ID3D11Texture2D> cbcrXTable;
    ComPtr<ID3D11Texture2D> cbcrYTable;
    ComPtr<ID3D11ShaderResourceView> cbcrXTableView;
    ComPtr<ID3D11ShaderResourceView> cbcrYTableView;
    ComPtr<ID3D11Texture2D> addTexture;
    ComPtr<ID3D11RenderTargetView> addView;
    ComPtr<ID3D11ShaderResourceView> addShaderView;
    ComPtr<ID3D11Texture2D> mergeTexture;
    ComPtr<ID3D11RenderTargetView> mergeView;
    ComPtr<ID3D11ShaderResourceView> mergeShaderView;
};

struct NguScaler::NativeLuma720DoubleHighBit
{
    struct Surface
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11ShaderResourceView> shaderView;
    };

    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> split;
    ComPtr<ID3D11PixelShader> initial;
    ComPtr<ID3D11PixelShader> layer6140;
    ComPtr<ID3D11PixelShader> layer4280;
    ComPtr<ID3D11PixelShader> dense1528;
    ComPtr<ID3D11PixelShader> toRgb;
    std::array<ComPtr<ID3D11Buffer>, 2> initialConstants;
    ComPtr<ID3D11Buffer> constants6140;
    std::array<ComPtr<ID3D11Buffer>, 3> constants4280;
    ComPtr<ID3D11Buffer> constants1528;
    ComPtr<ID3D11Buffer> constantsToRgb;
    ComPtr<ID3D11Buffer> constantsAdd;
    ComPtr<ID3D11Buffer> constants1672;
    std::array<ComPtr<ID3D11Buffer>, 28> constantsLumaLayer;
    ComPtr<ID3D11Buffer> constantsLumaDense;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;

    std::array<Surface, 3> splitSource;
    std::array<Surface, 3> upX;
    std::array<Surface, 3> upY;
    std::array<Surface, 4> lumaInitial;
    std::array<std::array<Surface, 4>, 2> lumaGroups;
    Surface lumaDense;
    std::array<Surface, 3> chromaInitial;
    std::array<Surface, 2> chromaA;
    std::array<Surface, 2> chromaB;
    std::array<Surface, 2> chromaResult;
    std::array<Surface, 2> chromaDense;
    Surface rgb;
};

struct NguScaler::NativeSuperResPrimitives
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> downX;
    ComPtr<ID3D11PixelShader> downY;
    ComPtr<ID3D11PixelShader> diff;
    ComPtr<ID3D11PixelShader> toRgbMain;
    ComPtr<ID3D11PixelShader> toRgbBase;
    ComPtr<ID3D11Buffer> downXConstants;
    ComPtr<ID3D11Buffer> downYConstants;
    ComPtr<ID3D11Buffer> toRgbMainConstants;
    ComPtr<ID3D11Buffer> toRgbBaseConstants;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsP010;
    ComPtr<ID3D11Buffer> toRgbMainConstantsHdr;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsHdr;
    ComPtr<ID3D11Buffer> toRgbMainConstantsHlg;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsHlg;
    ComPtr<ID3D11Buffer> toRgbMainConstantsBt709Pq;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsBt709Pq;
    ComPtr<ID3D11Buffer> toRgbMainConstantsClass3;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsClass3P010;
    ComPtr<ID3D11Buffer> toRgbMainConstantsClass3_1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsClass3P010_1080;
    ComPtr<ID3D11Texture2D> downXTable;
    ComPtr<ID3D11Texture2D> downYTable;
    ComPtr<ID3D11ShaderResourceView> downXTableView;
    ComPtr<ID3D11ShaderResourceView> downYTableView;
    ComPtr<ID3D11Texture2D> uvLut;
    ComPtr<ID3D11ShaderResourceView> uvLutView;
    ComPtr<ID3D11Texture2D> downXUvLut;
    ComPtr<ID3D11ShaderResourceView> downXUvLutView;
    ComPtr<ID3D11Texture2D> downYUvLut;
    ComPtr<ID3D11ShaderResourceView> downYUvLutView;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;

    ComPtr<ID3D11PixelShader> downX1080;
    ComPtr<ID3D11PixelShader> downY1080;
    ComPtr<ID3D11PixelShader> diff1080;
    ComPtr<ID3D11PixelShader> toRgbMain1080;
    ComPtr<ID3D11PixelShader> toRgbBase1080;
    ComPtr<ID3D11Buffer> downXConstants1080;
    ComPtr<ID3D11Buffer> downYConstants1080;
    ComPtr<ID3D11Buffer> toRgbMainConstants1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstants1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsP0101080;
    ComPtr<ID3D11Buffer> toRgbMainConstantsHdr1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsHdr1080;
    ComPtr<ID3D11Buffer> toRgbMainConstantsHlg1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsHlg1080;
    ComPtr<ID3D11Buffer> toRgbMainConstantsBt709Pq1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstantsBt709Pq1080;
    ComPtr<ID3D11Texture2D> downXTable1080;
    ComPtr<ID3D11Texture2D> downYTable1080;
    ComPtr<ID3D11ShaderResourceView> downXTableView1080;
    ComPtr<ID3D11ShaderResourceView> downYTableView1080;
    ComPtr<ID3D11Texture2D> uvLut1080;
    ComPtr<ID3D11ShaderResourceView> uvLutView1080;
    ComPtr<ID3D11Texture2D> downXUvLut1080;
    ComPtr<ID3D11ShaderResourceView> downXUvLutView1080;
    ComPtr<ID3D11Texture2D> downYUvLut1080;
    ComPtr<ID3D11ShaderResourceView> downYUvLutView1080;
};

struct NguScaler::NativeSuperResStrength3
{
    ComPtr<ID3D11Texture2D> downXTexture;
    ComPtr<ID3D11RenderTargetView> downXView;
    ComPtr<ID3D11ShaderResourceView> downXShaderView;
    ComPtr<ID3D11Texture2D> downYTexture;
    ComPtr<ID3D11RenderTargetView> downYView;
    ComPtr<ID3D11ShaderResourceView> downYShaderView;
    ComPtr<ID3D11Texture2D> diffTexture;
    ComPtr<ID3D11RenderTargetView> diffView;
    ComPtr<ID3D11ShaderResourceView> diffShaderView;
    ComPtr<ID3D11Texture2D> mainTexture;
    ComPtr<ID3D11RenderTargetView> mainView;
    ComPtr<ID3D11ShaderResourceView> mainShaderView;
    ComPtr<ID3D11Texture2D> outputTexture;
    ComPtr<ID3D11RenderTargetView> outputView;
    ComPtr<ID3D11ShaderResourceView> outputShaderView;
};

struct NguScaler::NativeSuperResStrength3_1080
{
    ComPtr<ID3D11Texture2D> downXTexture;
    ComPtr<ID3D11RenderTargetView> downXView;
    ComPtr<ID3D11ShaderResourceView> downXShaderView;
    ComPtr<ID3D11Texture2D> downYTexture;
    ComPtr<ID3D11RenderTargetView> downYView;
    ComPtr<ID3D11ShaderResourceView> downYShaderView;
    ComPtr<ID3D11Texture2D> diffTexture;
    ComPtr<ID3D11RenderTargetView> diffView;
    ComPtr<ID3D11ShaderResourceView> diffShaderView;
    ComPtr<ID3D11Texture2D> mainTexture;
    ComPtr<ID3D11RenderTargetView> mainView;
    ComPtr<ID3D11ShaderResourceView> mainShaderView;
    ComPtr<ID3D11Texture2D> outputTexture;
    ComPtr<ID3D11RenderTargetView> outputView;
    ComPtr<ID3D11ShaderResourceView> outputShaderView;
};

struct NguScaler::NativeLuma4x720Low
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> scaleX;
    ComPtr<ID3D11PixelShader> scaleY;
    ComPtr<ID3D11PixelShader> initial;
    ComPtr<ID3D11PixelShader> layer;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Buffer> scaleYConstants;
    ComPtr<ID3D11Buffer> initialConstants;
    std::array<ComPtr<ID3D11Buffer>, 15> layerConstants;
    ComPtr<ID3D11Texture2D> scaleXTable;
    ComPtr<ID3D11Texture2D> scaleYTable;
    ComPtr<ID3D11ShaderResourceView> scaleXTableView;
    ComPtr<ID3D11ShaderResourceView> scaleYTableView;
    ComPtr<ID3D11RasterizerState> rasterizer;

    ComPtr<ID3D11Texture2D> splitLumaTexture;
    ComPtr<ID3D11RenderTargetView> splitLumaView;
    ComPtr<ID3D11ShaderResourceView> splitLumaShaderView;
    ComPtr<ID3D11Texture2D> splitCbCrTexture;
    ComPtr<ID3D11RenderTargetView> splitCbCrView;
    ComPtr<ID3D11ShaderResourceView> splitCbCrShaderView;
    ComPtr<ID3D11Texture2D> scaleXTexture;
    ComPtr<ID3D11RenderTargetView> scaleXView;
    ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
    ComPtr<ID3D11Texture2D> scaleYTexture;
    ComPtr<ID3D11RenderTargetView> scaleYView;
    ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
    std::array<ComPtr<ID3D11Texture2D>, 3> featureATextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> featureAViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> featureAShaderViews;
    std::array<ComPtr<ID3D11Texture2D>, 3> featureBTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> featureBViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> featureBShaderViews;
};

struct NguScaler::NativeLuma4x720High
{
    ComPtr<ID3D11PixelShader> upsample;
    ComPtr<ID3D11PixelShader> convert;
    std::array<ComPtr<ID3D11Buffer>, 3> upsampleConstants;
    std::array<ComPtr<ID3D11Buffer>, 3> convertConstants;
    std::array<ComPtr<ID3D11Buffer>, 6> highLayerConstants;

    std::array<std::array<ComPtr<ID3D11Texture2D>, 4>, 3> upFeatureTextures;
    std::array<std::array<ComPtr<ID3D11RenderTargetView>, 4>, 3> upFeatureViews;
    std::array<std::array<ComPtr<ID3D11ShaderResourceView>, 4>, 3> upFeatureShaderViews;
    std::array<ComPtr<ID3D11Texture2D>, 3> convertTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> convertViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> convertShaderViews;
    std::array<ComPtr<ID3D11Texture2D>, 3> highScratchTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> highScratchViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> highScratchShaderViews;
};

struct NguScaler::NativeLuma4x720DenseAdd
{
    ComPtr<ID3D11PixelShader> dense;
    ComPtr<ID3D11PixelShader> add;
    ComPtr<ID3D11Buffer> denseConstants;
    ComPtr<ID3D11Buffer> addConstants;
    ComPtr<ID3D11Texture2D> denseTexture;
    ComPtr<ID3D11RenderTargetView> denseView;
    ComPtr<ID3D11ShaderResourceView> denseShaderView;
    ComPtr<ID3D11Texture2D> addTexture;
    ComPtr<ID3D11RenderTargetView> addView;
    ComPtr<ID3D11ShaderResourceView> addShaderView;
};

struct NguScaler::NativeLuma4x720Tail
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> ycbcr;
    ComPtr<ID3D11PixelShader> downX;
    ComPtr<ID3D11Buffer> downXConstants;
    ComPtr<ID3D11Texture2D> downXTable;
    ComPtr<ID3D11ShaderResourceView> downXTableView;
    ComPtr<ID3D11Texture2D> downXUvLut;
    ComPtr<ID3D11ShaderResourceView> downXUvLutView;
    ComPtr<ID3D11PixelShader> downY;
    ComPtr<ID3D11Buffer> downYConstants;
    ComPtr<ID3D11Texture2D> downYTable;
    ComPtr<ID3D11ShaderResourceView> downYTableView;
    ComPtr<ID3D11Texture2D> downYUvLut;
    ComPtr<ID3D11ShaderResourceView> downYUvLutView;
    ComPtr<ID3D11PixelShader> ssimRt;
    ComPtr<ID3D11Buffer> ssimRtConstants;
    ComPtr<ID3D11PixelShader> ssimFinal;
    ComPtr<ID3D11Buffer> ssimFinalConstants;
    ComPtr<ID3D11PixelShader> sharpenAr;
    ComPtr<ID3D11Buffer> sharpenArConstants;
    ComPtr<ID3D11PixelShader> chromaX;
    ComPtr<ID3D11PixelShader> chromaY;
    ComPtr<ID3D11PixelShader> merge;
    ComPtr<ID3D11Buffer> chromaXConstants;
    ComPtr<ID3D11Buffer> chromaYConstants;
    ComPtr<ID3D11Buffer> mergeConstants;
    ComPtr<ID3D11Texture2D> chromaXTable;
    ComPtr<ID3D11Texture2D> chromaYTable;
    ComPtr<ID3D11ShaderResourceView> chromaXTableView;
    ComPtr<ID3D11ShaderResourceView> chromaYTableView;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;

    ComPtr<ID3D11Texture2D> ycbcrTexture;
    ComPtr<ID3D11RenderTargetView> ycbcrView;
    ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
    ComPtr<ID3D11Texture2D> downXTexture;
    ComPtr<ID3D11RenderTargetView> downXView;
    ComPtr<ID3D11ShaderResourceView> downXShaderView;
    ComPtr<ID3D11Texture2D> downYTexture;
    ComPtr<ID3D11RenderTargetView> downYView;
    ComPtr<ID3D11ShaderResourceView> downYShaderView;
    ComPtr<ID3D11Texture2D> ssimRtTexture;
    ComPtr<ID3D11RenderTargetView> ssimRtView;
    ComPtr<ID3D11ShaderResourceView> ssimRtShaderView;
    ComPtr<ID3D11Texture2D> ssimFinalTexture;
    ComPtr<ID3D11RenderTargetView> ssimFinalView;
    ComPtr<ID3D11ShaderResourceView> ssimFinalShaderView;
    ComPtr<ID3D11Texture2D> sharpenArTexture;
    ComPtr<ID3D11RenderTargetView> sharpenArView;
    ComPtr<ID3D11ShaderResourceView> sharpenArShaderView;
    ComPtr<ID3D11Texture2D> chromaXTexture;
    ComPtr<ID3D11RenderTargetView> chromaXView;
    ComPtr<ID3D11ShaderResourceView> chromaXShaderView;
    ComPtr<ID3D11Texture2D> chromaYTexture;
    ComPtr<ID3D11RenderTargetView> chromaYView;
    ComPtr<ID3D11ShaderResourceView> chromaYShaderView;
    ComPtr<ID3D11Texture2D> mergeTexture;
    ComPtr<ID3D11RenderTargetView> mergeView;
    ComPtr<ID3D11ShaderResourceView> mergeShaderView;
};

struct NguScaler::NativeClass3ChromaTail720
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> superXbr0;
    ComPtr<ID3D11PixelShader> superXbr1;
    ComPtr<ID3D11PixelShader> superXbr2;
    ComPtr<ID3D11PixelShader> ycbcr;
    std::array<ComPtr<ID3D11Buffer>, 6> constants;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;

    ComPtr<ID3D11Texture2D> post6392Texture;
    ComPtr<ID3D11RenderTargetView> post6392View;
    ComPtr<ID3D11ShaderResourceView> post6392ShaderView;
    ComPtr<ID3D11Texture2D> post5140Texture;
    ComPtr<ID3D11RenderTargetView> post5140View;
    ComPtr<ID3D11ShaderResourceView> post5140ShaderView;
    ComPtr<ID3D11Texture2D> post4804Texture;
    ComPtr<ID3D11RenderTargetView> post4804View;
    ComPtr<ID3D11ShaderResourceView> post4804ShaderView;
    ComPtr<ID3D11Texture2D> post6392SecondTexture;
    ComPtr<ID3D11RenderTargetView> post6392SecondView;
    ComPtr<ID3D11ShaderResourceView> post6392SecondShaderView;
    ComPtr<ID3D11Texture2D> post5140SecondTexture;
    ComPtr<ID3D11RenderTargetView> post5140SecondView;
    ComPtr<ID3D11ShaderResourceView> post5140SecondShaderView;
    ComPtr<ID3D11Texture2D> post4804SecondTexture;
    ComPtr<ID3D11RenderTargetView> post4804SecondView;
    ComPtr<ID3D11ShaderResourceView> post4804SecondShaderView;
};

struct NguScaler::NativeChromaCoreClass3_720
{
    UINT chromaWidth = 0;
    UINT chromaHeight = 0;
    NguQuality quality = NguQuality::VeryHigh;
    ComPtr<ID3D11VertexShader> expandVertex;
    ComPtr<ID3D11PixelShader> expandPixel;
    ComPtr<ID3D11VertexShader> scaleXVertex;
    ComPtr<ID3D11PixelShader> scaleXPixel;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Texture2D> scaleXTable;
    ComPtr<ID3D11ShaderResourceView> scaleXTableView;
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> scaleXRasterizer;

    ComPtr<ID3D11Texture2D> cbTexture;
    ComPtr<ID3D11RenderTargetView> cbView;
    ComPtr<ID3D11ShaderResourceView> cbShaderView;
    ComPtr<ID3D11Texture2D> crTexture;
    ComPtr<ID3D11RenderTargetView> crView;
    ComPtr<ID3D11ShaderResourceView> crShaderView;

    ComPtr<ID3D11Texture2D> cbScaleXTexture;
    ComPtr<ID3D11RenderTargetView> cbScaleXView;
    ComPtr<ID3D11ShaderResourceView> cbScaleXShaderView;
    ComPtr<ID3D11Texture2D> cbScaleYTexture;
    ComPtr<ID3D11RenderTargetView> cbScaleYView;
    ComPtr<ID3D11ShaderResourceView> cbScaleYShaderView;
    ComPtr<ID3D11Texture2D> crScaleXTexture;
    ComPtr<ID3D11RenderTargetView> crScaleXView;
    ComPtr<ID3D11ShaderResourceView> crScaleXShaderView;
    ComPtr<ID3D11Texture2D> crScaleYTexture;
    ComPtr<ID3D11RenderTargetView> crScaleYView;
    ComPtr<ID3D11ShaderResourceView> crScaleYShaderView;

    ComPtr<ID3D11Texture2D> denseCbTexture;
    ComPtr<ID3D11RenderTargetView> denseCbView;
    ComPtr<ID3D11ShaderResourceView> denseCbShaderView;
    ComPtr<ID3D11Texture2D> denseCrTexture;
    ComPtr<ID3D11RenderTargetView> denseCrView;
    ComPtr<ID3D11ShaderResourceView> denseCrShaderView;
    ComPtr<ID3D11Texture2D> combine1Texture;
    ComPtr<ID3D11RenderTargetView> combine1View;
    ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
    ComPtr<ID3D11Texture2D> combine2Texture;
    ComPtr<ID3D11RenderTargetView> combine2View;
    ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
    ComPtr<ID3D11Texture2D> realignTexture;
    ComPtr<ID3D11RenderTargetView> realignView;
    ComPtr<ID3D11ShaderResourceView> realignShaderView;
    ComPtr<ID3D11Texture2D> rgbTexture;
    ComPtr<ID3D11RenderTargetView> rgbView;
    ComPtr<ID3D11ShaderResourceView> rgbShaderView;
    ComPtr<ID3D11Texture2D> yExpandedTexture;
    ComPtr<ID3D11RenderTargetView> yExpandedView;
    ComPtr<ID3D11ShaderResourceView> yExpandedShaderView;

    ComPtr<ID3D11Texture2D> downXTexture;
    ComPtr<ID3D11RenderTargetView> downXView;
    ComPtr<ID3D11ShaderResourceView> downXShaderView;
    ComPtr<ID3D11Texture2D> firstDownXTexture;
    ComPtr<ID3D11RenderTargetView> firstDownXView;
    ComPtr<ID3D11ShaderResourceView> firstDownXShaderView;
    ComPtr<ID3D11Texture2D> diffTexture;
    ComPtr<ID3D11RenderTargetView> diffView;
    ComPtr<ID3D11ShaderResourceView> diffShaderView;
    ComPtr<ID3D11Texture2D> firstDiffTexture;
    ComPtr<ID3D11RenderTargetView> firstDiffView;
    ComPtr<ID3D11ShaderResourceView> firstDiffShaderView;
    ComPtr<ID3D11Texture2D> mainTexture;
    ComPtr<ID3D11RenderTargetView> mainView;
    ComPtr<ID3D11ShaderResourceView> mainShaderView;
    ComPtr<ID3D11Texture2D> ps4576Texture;
    ComPtr<ID3D11RenderTargetView> ps4576View;
    ComPtr<ID3D11ShaderResourceView> ps4576ShaderView;
};

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

NguScaler::NguScaler() = default;

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
    const char* hookFile = FindStage(request.stage)->hookFile;
    if (!hookFile)
    {
        error_ = "NGU native stage has no user-shader validation hook";
        return false;
    }
    const int id = ShaderId(hookFile);
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

bool NguScaler::PrepareNativeLumaSplit(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU luma split requires a GPU";
        return false;
    }
    if (nativeLumaSplit_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU luma split requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_1080_split.hlsl", 64 * 1024);
    const auto pixelCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_split.cso", 0);
    if (shaderText.empty() || pixelCso.empty())
    {
        error_ = "NGU luma split assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_Luma1080_Split",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU luma split vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaSplit>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU luma split shader objects could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU luma split sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU luma split rasterizer could not be created";
        return false;
    }

    nativeLumaSplit_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaSplit(
    ID3D11ShaderResourceView* source, int width, int height, ID3D11RenderTargetView* luma, ID3D11RenderTargetView* cbcr)
{
    if (!nativeLumaSplit_ || !source || width <= 0 || height <= 0 || !luma || !cbcr)
    {
        error_ = "NGU luma split request is invalid";
        return false;
    }
    ID3D11RenderTargetView* outputs[2] = {luma, cbcr};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaSplit_->vertex.Get(),
                  nativeLumaSplit_->pixel.Get(),
                  nullptr,
                  nativeLumaSplit_->rasterizer.Get(),
                  &source,
                  1,
                  outputs,
                  2,
                  static_cast<UINT>(width),
                  static_cast<UINT>(height),
                  nativeLumaSplit_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeDecoderSource(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU decoder source import requires a GPU";
        return false;
    }
    if (nativeDecoderSource_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU decoder source import requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_source_copy.hlsl", 16 * 1024);
    const auto pixelCso = LoadBundledBytes(L"shaders\\Ngu\\ngu_source_copy.cso", 0);
    if (shaderText.empty() || pixelCso.empty())
    {
        error_ = "NGU decoder source import assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_DecoderSourceCopy",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU decoder source vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeDecoderSource>();
    if (FAILED(
            device->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(state->device3.GetAddressOf()))))
    {
        error_ = "NGU decoder source import requires D3D11.3 plane views";
        return false;
    }
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU decoder source shader objects could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU decoder source sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU decoder source rasterizer could not be created";
        return false;
    }

    nativeDecoderSource_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeDecoderSourceCopy(ID3D11Texture2D* source,
                                           unsigned int arraySlice,
                                           int width,
                                           int height,
                                           ID3D11RenderTargetView* luma,
                                           ID3D11RenderTargetView* chroma)
{
    if (!nativeDecoderSource_ || !source || width <= 0 || height <= 0 || (width & 1) || (height & 1) || !luma ||
        !chroma)
    {
        error_ = "NGU decoder source request is invalid";
        return false;
    }
    D3D11_TEXTURE2D_DESC sourceDesc = {};
    source->GetDesc(&sourceDesc);
    const bool nv12 = sourceDesc.Format == DXGI_FORMAT_NV12;
    if ((!nv12 && sourceDesc.Format != DXGI_FORMAT_P010 && sourceDesc.Format != DXGI_FORMAT_P016) ||
        sourceDesc.MipLevels != 1 || sourceDesc.SampleDesc.Count != 1 || arraySlice >= sourceDesc.ArraySize ||
        static_cast<UINT>(width) > sourceDesc.Width || static_cast<UINT>(height) > sourceDesc.Height)
    {
        error_ = "NGU decoder source format or geometry is unsupported";
        return false;
    }
    if (!ViewMatchesTexture(luma, DXGI_FORMAT_R16_UNORM, width, height) ||
        !ViewMatchesTexture(chroma, DXGI_FORMAT_R16G16B16A16_UNORM, width / 2, height / 2))
    {
        error_ = "NGU decoder source output format or geometry is unsupported";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC1 lumaViewDesc = {};
    lumaViewDesc.Format = nv12 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R16_UNORM;
    lumaViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    lumaViewDesc.Texture2DArray.MipLevels = 1;
    lumaViewDesc.Texture2DArray.FirstArraySlice = arraySlice;
    lumaViewDesc.Texture2DArray.ArraySize = 1;
    lumaViewDesc.Texture2DArray.PlaneSlice = 0;
    ComPtr<ID3D11ShaderResourceView1> lumaView;
    if (FAILED(nativeDecoderSource_->device3->CreateShaderResourceView1(source, &lumaViewDesc, &lumaView)))
    {
        error_ = "NGU decoder luma plane view could not be created";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC1 chromaViewDesc = {};
    chromaViewDesc.Format = nv12 ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R16G16_UNORM;
    chromaViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    chromaViewDesc.Texture2DArray.MipLevels = 1;
    chromaViewDesc.Texture2DArray.FirstArraySlice = arraySlice;
    chromaViewDesc.Texture2DArray.ArraySize = 1;
    chromaViewDesc.Texture2DArray.PlaneSlice = 1;
    ComPtr<ID3D11ShaderResourceView1> chromaView;
    if (FAILED(nativeDecoderSource_->device3->CreateShaderResourceView1(source, &chromaViewDesc, &chromaView)))
    {
        error_ = "NGU decoder chroma plane view could not be created";
        return false;
    }

    ID3D11ShaderResourceView* input = lumaView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeDecoderSource_->vertex.Get(),
                  nativeDecoderSource_->pixel.Get(),
                  nullptr,
                  nativeDecoderSource_->rasterizer.Get(),
                  &input,
                  1,
                  &luma,
                  1,
                  static_cast<UINT>(width),
                  static_cast<UINT>(height),
                  nativeDecoderSource_->pointSampler.Get());
    input = chromaView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeDecoderSource_->vertex.Get(),
                  nativeDecoderSource_->pixel.Get(),
                  nullptr,
                  nativeDecoderSource_->rasterizer.Get(),
                  &input,
                  1,
                  &chroma,
                  1,
                  static_cast<UINT>(width / 2),
                  static_cast<UINT>(height / 2),
                  nativeDecoderSource_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaSplit(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma split requires a GPU";
        return false;
    }
    if (nativeChromaSplit_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma split requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_split.hlsl", 16 * 1024);
    const auto pixelCso = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_split.cso", 0);
    if (shaderText.empty() || pixelCso.empty())
    {
        error_ = "NGU chroma split assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaSplit",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma split vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaSplit>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU chroma split shader objects could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU chroma split sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma split rasterizer could not be created";
        return false;
    }

    nativeChromaSplit_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaSplit(
    ID3D11ShaderResourceView* packedUv, int width, int height, ID3D11RenderTargetView* cb, ID3D11RenderTargetView* cr)
{
    if (!nativeChromaSplit_ || !packedUv || width <= 0 || height <= 0 || !cb || !cr ||
        !ViewMatchesTexture(cb, DXGI_FORMAT_R16_UNORM, width, height) ||
        !ViewMatchesTexture(cr, DXGI_FORMAT_R16_UNORM, width, height))
    {
        error_ = "NGU chroma split request is invalid";
        return false;
    }
    ID3D11RenderTargetView* outputs[2] = {cb, cr};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaSplit_->vertex.Get(),
                  nativeChromaSplit_->pixel.Get(),
                  nullptr,
                  nativeChromaSplit_->rasterizer.Get(),
                  &packedUv,
                  1,
                  outputs,
                  2,
                  static_cast<UINT>(width),
                  static_cast<UINT>(height),
                  nativeChromaSplit_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaScale(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma scale requires a GPU";
        return false;
    }
    if (nativeChromaScale_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma scale requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_scale.hlsl", 16 * 1024);
    const auto scaleXCso = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_x.cso", 0);
    const auto scaleXClass3Cso = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_x_class3.cso", 0);
    const auto scaleYCso = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_y.cso", 0);
    const auto scaleYClass3Cso = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_scale_y.cso", 0);
    const auto scaleXConstants1080 = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_x_constants_1080.bin", 48);
    const auto scaleYConstants1080 = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_y_constants_1080.bin", 48);
    if (shaderText.empty() || scaleXCso.empty() || scaleXClass3Cso.empty() || scaleYCso.empty() ||
        scaleYClass3Cso.empty() || scaleXConstants1080.empty() || scaleYConstants1080.empty())
    {
        error_ = "NGU chroma scale assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaScale",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma scale vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaScale>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            scaleXCso.data(), static_cast<SIZE_T>(scaleXCso.size()), nullptr, &state->scaleX)) ||
        FAILED(device->CreatePixelShader(
            scaleXClass3Cso.data(), static_cast<SIZE_T>(scaleXClass3Cso.size()), nullptr, &state->scaleXClass3)) ||
        FAILED(device->CreatePixelShader(
            scaleYCso.data(), static_cast<SIZE_T>(scaleYCso.size()), nullptr, &state->scaleY)) ||
        FAILED(device->CreatePixelShader(
            scaleYClass3Cso.data(), static_cast<SIZE_T>(scaleYClass3Cso.size()), nullptr, &state->scaleYClass3)))
    {
        error_ = "NGU chroma scale shader objects could not be created";
        return false;
    }

    const float scaleXConstants[12] = {640.0f,
                                       0.00078125f,
                                       0.0015625f,
                                       0.0f,
                                       0.871875f,
                                       0.240625f,
                                       -0.084375f,
                                       -0.028125f,
                                       640.0f,
                                       360.0f,
                                       0.0f,
                                       0.0f};
    const float scaleYConstants[12] = {720.0f,
                                       0.0013888889f,
                                       0.0027777778f,
                                       0.0f,
                                       0.871875f,
                                       0.240625f,
                                       -0.084375f,
                                       -0.028125f,
                                       1280.0f,
                                       720.0f,
                                       0.0f,
                                       0.0f};
    const float scaleXConstantsClass3[12] = {1280.0f,
                                             0.00078125f,
                                             0.0013888889f,
                                             0.0f,
                                             0.871875f,
                                             0.240625f,
                                             -0.084375f,
                                             -0.028125f,
                                             1280.0f,
                                             720.0f,
                                             0.0f,
                                             0.0f};
    const float scaleYConstantsClass3[12] = {1440.0f,
                                             0.00069444446f,
                                             0.0013888889f,
                                             0.0f,
                                             0.871875f,
                                             0.240625f,
                                             -0.084375f,
                                             -0.028125f,
                                             1280.0f,
                                             1440.0f,
                                             0.0f,
                                             0.0f};
    const float scaleXConstantsClass3_1080[12] = {1920.0f,
                                                  0.0005208334f,
                                                  0.001041667f,
                                                  0.0f,
                                                  0.871875f,
                                                  0.240625f,
                                                  -0.084375f,
                                                  -0.028125f,
                                                  1920.0f,
                                                  1080.0f,
                                                  0.0f,
                                                  0.0f};
    const float scaleYConstantsClass3_1080[12] = {2160.0f,
                                                  0.000462963f,
                                                  0.0009259259f,
                                                  0.0f,
                                                  0.871875f,
                                                  0.240625f,
                                                  -0.084375f,
                                                  -0.028125f,
                                                  1920.0f,
                                                  2160.0f,
                                                  0.0f,
                                                  0.0f};
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(scaleXConstants);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = scaleXConstants;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleXConstants)))
    {
        error_ = "NGU chroma X scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleYConstants;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleYConstants)))
    {
        error_ = "NGU chroma Y scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleXConstants1080.data();
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleXConstants1080)))
    {
        error_ = "NGU 1080p chroma X scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleYConstants1080.data();
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleYConstants1080)))
    {
        error_ = "NGU 1080p chroma Y scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleXConstantsClass3;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleXConstantsClass3)))
    {
        error_ = "NGU class-3 chroma X scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleYConstantsClass3;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleYConstantsClass3)))
    {
        error_ = "NGU class-3 chroma Y scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleXConstantsClass3_1080;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleXConstantsClass3_1080)))
    {
        error_ = "NGU 1080p class-3 chroma X scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleYConstantsClass3_1080;
    if (FAILED(device->CreateBuffer(&bufferDesc, &initial, &state->scaleYConstantsClass3_1080)))
    {
        error_ = "NGU 1080p class-3 chroma Y scale constants could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma scale rasterizer could not be created";
        return false;
    }
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->sampler)))
    {
        error_ = "NGU chroma scale sampler could not be created";
        return false;
    }

    nativeChromaScale_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaScaleX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    const bool is720 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 360) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 360);
    const bool is1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 540) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 540);
    const bool isClass3 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 720) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 720);
    const bool isClass3_1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 1080) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 1080);
    if (!nativeChromaScale_ || !input || !output || (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma X scale request has unsupported geometry";
        return false;
    }
    DrawD3D11Pass(
        pl_d3d11_get(gpu_)->device,
        nativeChromaScale_->vertex.Get(),
        ((isClass3 || isClass3_1080) ? nativeChromaScale_->scaleXClass3.Get() : nativeChromaScale_->scaleX.Get()),
        (isClass3_1080 ? nativeChromaScale_->scaleXConstantsClass3_1080.Get()
         : isClass3    ? nativeChromaScale_->scaleXConstantsClass3.Get()
         : is1080      ? nativeChromaScale_->scaleXConstants1080.Get()
                       : nativeChromaScale_->scaleXConstants.Get()),
        nativeChromaScale_->rasterizer.Get(),
        &input,
        1,
        &output,
        1,
        (is1080 || isClass3_1080) ? 1920 : 1280,
        isClass3_1080 ? 1080
        : isClass3    ? 720
        : is1080      ? 540
                      : 360,
        nativeChromaScale_->sampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaScaleY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    const bool is720 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1280, 360) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 720);
    const bool is1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1920, 540) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 1080);
    const bool isClass3 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 1440);
    const bool isClass3_1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 2160);
    if (!nativeChromaScale_ || !input || !output || (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma Y scale request has unsupported geometry";
        return false;
    }
    DrawD3D11Pass(
        pl_d3d11_get(gpu_)->device,
        nativeChromaScale_->vertex.Get(),
        ((isClass3 || isClass3_1080) ? nativeChromaScale_->scaleYClass3.Get() : nativeChromaScale_->scaleY.Get()),
        (isClass3_1080 ? nativeChromaScale_->scaleYConstantsClass3_1080.Get()
         : isClass3    ? nativeChromaScale_->scaleYConstantsClass3.Get()
         : is1080      ? nativeChromaScale_->scaleYConstants1080.Get()
                       : nativeChromaScale_->scaleYConstants.Get()),
        nativeChromaScale_->rasterizer.Get(),
        &input,
        1,
        &output,
        1,
        (is1080 || isClass3_1080) ? 1920 : 1280,
        isClass3_1080 ? 2160
        : isClass3    ? 1440
        : is1080      ? 1080
                      : 720,
        nativeChromaScale_->sampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaNetworkLegacy(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma network requires a GPU";
        return false;
    }
    if (nativeChromaNetwork_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma network requires the D3D11 backend";
        return false;
    }

    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_cubici2.hlsl", 64 * 1024);
    const auto cubici2Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2.cso", 0);
    const auto layerBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer.cso", 0);
    const auto denseBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense.cso", 0);
    const auto cubici2Constants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2_constants.bin", 42 * sizeof(float) * 4);
    const auto layerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer_constants.bin", 28 * 147 * sizeof(float) * 4);
    const auto denseConstants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense_constants.bin", 67 * sizeof(float) * 4);
    const auto cubici2Constants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2_constants_1080.bin", 42 * sizeof(float) * 4);
    const auto layerConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer_constants_1080.bin", 28 * 147 * sizeof(float) * 4);
    const auto denseConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense_constants_1080.bin", 67 * sizeof(float) * 4);
    if (shaderText.empty() || cubici2Bytecode.empty() || layerBytecode.empty() || denseBytecode.empty() ||
        cubici2Constants.empty() || layerConstants.empty() || denseConstants.empty() || cubici2Constants1080.empty() ||
        layerConstants1080.empty() || denseConstants1080.empty())
    {
        error_ = "NGU chroma network assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaNetwork",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma network vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaNetwork>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            cubici2Bytecode.data(), static_cast<SIZE_T>(cubici2Bytecode.size()), nullptr, &state->cubici2)) ||
        FAILED(device->CreatePixelShader(
            layerBytecode.data(), static_cast<SIZE_T>(layerBytecode.size()), nullptr, &state->layer)) ||
        FAILED(device->CreatePixelShader(
            denseBytecode.data(), static_cast<SIZE_T>(denseBytecode.size()), nullptr, &state->dense)))
    {
        error_ = "NGU chroma network shader objects could not be created";
        return false;
    }

    auto createConstants =
        [&](const std::vector<uint8_t>& bytes, size_t offset, UINT size, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = bytes.data() + offset;
        return SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer));
    };
    if (!createConstants(cubici2Constants, 0, UINT(cubici2Constants.size()), state->cubici2Constants))
    {
        error_ = "NGU chroma Cubici2 constants could not be created";
        return false;
    }
    constexpr size_t layerConstantBytes = 147 * sizeof(float) * 4;
    for (size_t layer = 0; layer < state->layerConstants.size(); ++layer)
    {
        if (!createConstants(
                layerConstants, layer * layerConstantBytes, UINT(layerConstantBytes), state->layerConstants[layer]))
        {
            error_ = "NGU chroma layer constants could not be created";
            return false;
        }
    }
    if (!createConstants(denseConstants, 0, UINT(denseConstants.size()), state->denseConstants))
    {
        error_ = "NGU chroma dense constants could not be created";
        return false;
    }
    if (!createConstants(cubici2Constants1080, 0, UINT(cubici2Constants1080.size()), state->cubici2Constants1080))
    {
        error_ = "NGU 1080p chroma Cubici2 constants could not be created";
        return false;
    }
    for (size_t layer = 0; layer < state->layerConstants1080.size(); ++layer)
    {
        if (!createConstants(layerConstants1080,
                             layer * layerConstantBytes,
                             UINT(layerConstantBytes),
                             state->layerConstants1080[layer]))
        {
            error_ = "NGU 1080p chroma layer constants could not be created";
            return false;
        }
    }
    if (!createConstants(denseConstants1080, 0, UINT(denseConstants1080.size()), state->denseConstants1080))
    {
        error_ = "NGU 1080p chroma dense constants could not be created";
        return false;
    }

    auto createClass3Constants = [&](const std::vector<uint8_t>& bytes,
                                     size_t offset,
                                     int vectorCount,
                                     bool dense,
                                     int scaleWidth,
                                     int scaleHeight,
                                     int denseWidth,
                                     int denseHeight,
                                     ComPtr<ID3D11Buffer>& buffer)
    {
        const size_t byteCount = size_t(vectorCount) * 4 * sizeof(float);
        std::vector<uint8_t> adjusted(bytes.begin() + offset, bytes.begin() + offset + byteCount);
        float* values = reinterpret_cast<float*>(adjusted.data());
        const int scaleOffset = vectorCount * 4 - (dense ? 8 : 4);
        values[scaleOffset] = 1.0f / float(scaleWidth);
        values[scaleOffset + 1] = 1.0f / float(scaleHeight);
        if (dense)
        {
            values[vectorCount * 4 - 4] = float(denseWidth);
            values[vectorCount * 4 - 3] = float(denseHeight);
        }
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = UINT(byteCount);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = adjusted.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer));
    };
    if (!createClass3Constants(cubici2Constants, 0, 42, false, 640, 720, 642, 722, state->cubici2ConstantsClass3))
    {
        error_ = "NGU class-3 chroma Cubici2 constants could not be created";
        return false;
    }
    for (size_t layer = 0; layer < state->layerConstantsClass3.size(); ++layer)
    {
        if (!createClass3Constants(layerConstants,
                                   layer * layerConstantBytes,
                                   147,
                                   false,
                                   640,
                                   720,
                                   642,
                                   722,
                                   state->layerConstantsClass3[layer]))
        {
            error_ = "NGU class-3 chroma layer constants could not be created";
            return false;
        }
    }
    if (!createClass3Constants(denseConstants, 0, 67, true, 640, 720, 642, 722, state->denseConstantsClass3))
    {
        error_ = "NGU class-3 chroma dense constants could not be created";
        return false;
    }
    if (!createClass3Constants(
            cubici2Constants, 0, 42, false, 960, 1080, 962, 1082, state->cubici2ConstantsClass3_1080))
    {
        error_ = "NGU 1080p class-3 chroma Cubici2 constants could not be created";
        return false;
    }
    for (size_t layer = 0; layer < state->layerConstantsClass3_1080.size(); ++layer)
    {
        if (!createClass3Constants(layerConstants,
                                   layer * layerConstantBytes,
                                   147,
                                   false,
                                   960,
                                   1080,
                                   962,
                                   1082,
                                   state->layerConstantsClass3_1080[layer]))
        {
            error_ = "NGU 1080p class-3 chroma layer constants could not be created";
            return false;
        }
    }
    if (!createClass3Constants(denseConstants, 0, 67, true, 960, 1080, 962, 1082, state->denseConstantsClass3_1080))
    {
        error_ = "NGU 1080p class-3 chroma dense constants could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma network rasterizer could not be created";
        return false;
    }
    nativeChromaNetwork_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCubici2Legacy(ID3D11ShaderResourceView* input,
                                             ID3D11RenderTargetView* const outputs[4])
{
    const bool is720 = input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 360);
    const bool is1080 = input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 540);
    const bool isClass3 = input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 720);
    const bool isClass3_1080 = input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 1080);
    const UINT width = (is1080 || isClass3_1080) ? 960 : 640;
    const UINT height = isClass3_1080 ? 1080 : isClass3 ? 720 : is1080 ? 540 : 360;
    if (!nativeChromaNetwork_ || !input || !outputs || !outputs[0] || !outputs[1] || !outputs[2] || !outputs[3] ||
        (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma Cubici2 request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(outputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, width, height))
        {
            error_ = "NGU chroma Cubici2 output is invalid";
            return false;
        }
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->cubici2.Get(),
                  (isClass3_1080 ? nativeChromaNetwork_->cubici2ConstantsClass3_1080.Get()
                   : isClass3    ? nativeChromaNetwork_->cubici2ConstantsClass3.Get()
                   : is1080      ? nativeChromaNetwork_->cubici2Constants1080.Get()
                                 : nativeChromaNetwork_->cubici2Constants.Get()),
                  nativeChromaNetwork_->rasterizer.Get(),
                  &input,
                  1,
                  outputs,
                  4,
                  width,
                  height);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaLayerLegacy(ID3D11ShaderResourceView* const inputs[4],
                                           int layer,
                                           ID3D11RenderTargetView* output)
{
    const bool is720 = inputs && inputs[0] && ViewMatchesTexture(inputs[0], DXGI_FORMAT_R16G16B16A16_FLOAT, 640, 360);
    const bool is1080 = inputs && inputs[0] && ViewMatchesTexture(inputs[0], DXGI_FORMAT_R16G16B16A16_FLOAT, 960, 540);
    const bool isClass3 =
        inputs && inputs[0] && ViewMatchesTexture(inputs[0], DXGI_FORMAT_R16G16B16A16_FLOAT, 640, 720);
    const bool isClass3_1080 =
        inputs && inputs[0] && ViewMatchesTexture(inputs[0], DXGI_FORMAT_R16G16B16A16_FLOAT, 960, 1080);
    const UINT width = (is1080 || isClass3_1080) ? 960 : 640;
    const UINT height = isClass3_1080 ? 1080 : isClass3 ? 720 : is1080 ? 540 : 360;
    if (!nativeChromaNetwork_ || !inputs || !inputs[0] || !inputs[1] || !inputs[2] || !inputs[3] || !output ||
        layer < 0 || layer >= int(nativeChromaNetwork_->layerConstants.size()) ||
        (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma layer request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(inputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, width, height))
        {
            error_ = "NGU chroma layer input is invalid";
            return false;
        }
    }
    if (!ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height))
    {
        error_ = "NGU chroma layer output is invalid";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->layer.Get(),
                  (isClass3_1080 ? nativeChromaNetwork_->layerConstantsClass3_1080[layer].Get()
                   : isClass3    ? nativeChromaNetwork_->layerConstantsClass3[layer].Get()
                   : is1080      ? nativeChromaNetwork_->layerConstants1080[layer].Get()
                                 : nativeChromaNetwork_->layerConstants[layer].Get()),
                  nativeChromaNetwork_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  width,
                  height);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaDenseLegacy(ID3D11ShaderResourceView* const inputs[4],
                                           ID3D11RenderTargetView* output)
{
    const bool output720 = output && ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362);
    const bool output1080 = output && ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542);
    const bool outputClass3 = output && ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 722);
    const bool outputClass3_1080 = output && ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 1082);
    const UINT inputWidth = (output1080 || outputClass3_1080) ? 960 : 640;
    const UINT inputHeight = outputClass3_1080 ? 1080 : outputClass3 ? 720 : output1080 ? 540 : 360;
    const UINT outputWidth = (output1080 || outputClass3_1080) ? 962 : 642;
    const UINT outputHeight = outputClass3_1080 ? 1082 : outputClass3 ? 722 : output1080 ? 542 : 362;
    if (!nativeChromaNetwork_ || !inputs || !inputs[0] || !inputs[1] || !inputs[2] || !inputs[3] || !output ||
        (!output720 && !output1080 && !outputClass3 && !outputClass3_1080))
    {
        error_ = "NGU chroma dense request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(inputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, inputWidth, inputHeight))
        {
            error_ = "NGU chroma dense input is invalid";
            return false;
        }
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->dense.Get(),
                  (outputClass3_1080 ? nativeChromaNetwork_->denseConstantsClass3_1080.Get()
                   : outputClass3    ? nativeChromaNetwork_->denseConstantsClass3.Get()
                   : output1080      ? nativeChromaNetwork_->denseConstants1080.Get()
                                     : nativeChromaNetwork_->denseConstants.Get()),
                  nativeChromaNetwork_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  outputWidth,
                  outputHeight);
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaNetwork(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma network requires a GPU";
        return false;
    }
    if (nativeChromaNetwork_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma network requires the D3D11 backend";
        return false;
    }

    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_cubici2.hlsl", 64 * 1024);
    const auto cubici0Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_cubici0.cso", 0);
    const auto cubici1Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_cubici1.cso", 0);
    const auto cubici2Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2.cso", 0);
    const auto mixer6140Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_6140.cso", 0);
    const auto layer4280Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_4280.cso", 0);
    const auto layer3588Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer.cso", 0);
    const auto layer4620Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer.cso", 0);
    const auto dense1528Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_1528.cso", 0);
    const auto dense2100Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense.cso", 0);
    const auto dense2672Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense.cso", 0);

    const auto lowInitial =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_low_initial_constants.bin", 22 * sizeof(float) * 4);
    const auto lowLayer0 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_low_layer_00_constants.bin", 148 * sizeof(float) * 4);
    const auto lowLayer1 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_low_layer_01_constants.bin", 148 * sizeof(float) * 4);
    const auto lowDense =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_low_dense_constants.bin", 35 * sizeof(float) * 4);
    const auto medInitial =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_initial_constants.bin", 32 * sizeof(float) * 4);
    const auto medMixer =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_mixer_constants.bin", 220 * sizeof(float) * 4);
    const auto medLayer0 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_layer_00_constants.bin", 148 * sizeof(float) * 4);
    const auto medLayer1 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_layer_01_constants.bin", 148 * sizeof(float) * 4);
    const auto medLayer2 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_layer_02_constants.bin", 148 * sizeof(float) * 4);
    const auto medDense =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_med_dense_constants.bin", 35 * sizeof(float) * 4);
    const auto highInitial =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_high_initial_constants.bin", 32 * sizeof(float) * 4);
    const auto highDense =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_quality_high_dense_constants.bin", 51 * sizeof(float) * 4);
    std::array<std::vector<uint8_t>, 15> highLayers;
    for (size_t index = 0; index < highLayers.size(); ++index)
    {
        const std::wstring name = L"shaders\\Ngu\\ngu_quality_high_layer_" + std::wstring(index < 10 ? L"0" : L"") +
                                  std::to_wstring(index) + L"_constants.bin";
        highLayers[index] = LoadBundledBytes(name.c_str(), 111 * sizeof(float) * 4);
    }
    const auto veryHighInitial =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2_constants.bin", 42 * sizeof(float) * 4);
    const auto veryHighLayerFile =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer_constants.bin", 28 * 147 * sizeof(float) * 4);
    const auto veryHighDense =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense_constants.bin", 67 * sizeof(float) * 4);
    std::array<std::vector<uint8_t>, 28> veryHighLayers;
    constexpr size_t veryHighLayerBytes = 147 * sizeof(float) * 4;
    if (veryHighLayerFile.size() == veryHighLayers.size() * veryHighLayerBytes)
    {
        for (size_t index = 0; index < veryHighLayers.size(); ++index)
        {
            veryHighLayers[index].assign(veryHighLayerFile.begin() + index * veryHighLayerBytes,
                                         veryHighLayerFile.begin() + (index + 1) * veryHighLayerBytes);
        }
    }

    const bool shaderAssets =
        !shaderText.empty() && !cubici0Bytecode.empty() && !cubici1Bytecode.empty() && !cubici2Bytecode.empty() &&
        !mixer6140Bytecode.empty() && !layer4280Bytecode.empty() && !layer3588Bytecode.empty() &&
        !layer4620Bytecode.empty() && !dense1528Bytecode.empty() && !dense2100Bytecode.empty() &&
        !dense2672Bytecode.empty();
    const bool constantAssets =
        !lowInitial.empty() && !lowLayer0.empty() && !lowLayer1.empty() && !lowDense.empty() && !medInitial.empty() &&
        !medMixer.empty() && !medLayer0.empty() && !medLayer1.empty() && !medLayer2.empty() && !medDense.empty() &&
        !highInitial.empty() && !highDense.empty() &&
        std::all_of(highLayers.begin(), highLayers.end(), [](const auto& value) { return !value.empty(); }) &&
        !veryHighInitial.empty() && !veryHighDense.empty() && !veryHighLayers.back().empty();
    if (!shaderAssets || !constantAssets)
    {
        error_ = "NGU chroma quality assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaQualityNetwork",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma quality vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaNetwork>();
    auto createPixel = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11PixelShader>& shader)
    {
        return SUCCEEDED(
            device->CreatePixelShader(bytes.data(), static_cast<SIZE_T>(bytes.size()), nullptr, &shader));
    };
    ComPtr<ID3D11PixelShader> cubici0, cubici1, cubici2, mixer6140, layer4280, layer3588, layer4620;
    ComPtr<ID3D11PixelShader> dense1528, dense2100, dense2672;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        !createPixel(cubici0Bytecode, cubici0) || !createPixel(cubici1Bytecode, cubici1) ||
        !createPixel(cubici2Bytecode, cubici2) || !createPixel(mixer6140Bytecode, mixer6140) ||
        !createPixel(layer4280Bytecode, layer4280) || !createPixel(layer3588Bytecode, layer3588) ||
        !createPixel(layer4620Bytecode, layer4620) || !createPixel(dense1528Bytecode, dense1528) ||
        !createPixel(dense2100Bytecode, dense2100) || !createPixel(dense2672Bytecode, dense2672))
    {
        error_ = "NGU chroma quality shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        if (bytes.empty())
            return false;
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = UINT(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer));
    };

    constexpr std::array<std::pair<UINT, UINT>, 4> geometry = {
        std::pair<UINT, UINT>{640, 360},
        std::pair<UINT, UINT>{960, 540},
        std::pair<UINT, UINT>{640, 720},
        std::pair<UINT, UINT>{960, 1080},
    };
    auto adjustedConstants = [&](const std::vector<uint8_t>& bytes,
                                 int vectorCount,
                                 bool dense,
                                 UINT width,
                                 UINT height)
    {
        const size_t byteCount = size_t(vectorCount) * sizeof(float) * 4;
        if (bytes.size() != byteCount)
            return std::vector<uint8_t>{};
        std::vector<uint8_t> adjusted(bytes);
        float* values = reinterpret_cast<float*>(adjusted.data());
        const int scaleOffset = vectorCount * 4 - (dense ? 8 : 4);
        values[scaleOffset] = 1.0f / float(width);
        values[scaleOffset + 1] = 1.0f / float(height);
        if (dense)
        {
            values[vectorCount * 4 - 4] = float(width + 2);
            values[vectorCount * 4 - 3] = float(height + 2);
        }
        return adjusted;
    };

    auto buildConstants = [&](NguChromaQualityProfile& profile,
                              const std::vector<uint8_t>& initial,
                              int initialVectors,
                              const std::vector<uint8_t>& mixer,
                              int mixerVectors,
                              const auto& layers,
                              int layerVectors,
                              const std::vector<uint8_t>& dense,
                              int denseVectors)
    {
        for (size_t index = 0; index < geometry.size(); ++index)
        {
            const auto [width, height] = geometry[index];
            const auto initialAdjusted = adjustedConstants(initial, initialVectors, false, width, height);
            if (!createConstants(initialAdjusted, profile.initialConstants[index]))
                return false;
            if (!mixer.empty())
            {
                const auto mixerAdjusted = adjustedConstants(mixer, mixerVectors, false, width, height);
                if (!createConstants(mixerAdjusted, profile.mixerConstants[index]))
                    return false;
            }
            for (int layer = 0; layer < profile.layerCount; ++layer)
            {
                const auto adjusted = adjustedConstants(layers[size_t(layer)], layerVectors, false, width, height);
                if (!createConstants(adjusted, profile.layerConstants[index][size_t(layer)]))
                    return false;
            }
            const auto denseAdjusted = adjustedConstants(dense, denseVectors, true, width, height);
            if (!createConstants(denseAdjusted, profile.denseConstants[index]))
                return false;
        }
        return true;
    };

    auto& low = state->qualities[size_t(NguQuality::Low)];
    low.name = "Low";
    low.initial = cubici0;
    low.layer = layer4280;
    low.dense = dense1528;
    low.initialOutputs = 2;
    low.layerInputs = 2;
    low.layerOutputs = 2;
    low.layerCount = 2;
    low.layersPerGroup = 1;
    const std::array<std::vector<uint8_t>, 2> lowLayers = {lowLayer0, lowLayer1};
    if (!buildConstants(low, lowInitial, 22, {}, 0, lowLayers, 148, lowDense, 35))
    {
        error_ = "NGU Low chroma constants could not be created";
        return false;
    }

    auto& medium = state->qualities[size_t(NguQuality::Medium)];
    medium.name = "Medium";
    medium.initial = cubici1;
    medium.mixer = mixer6140;
    medium.layer = layer4280;
    medium.dense = dense1528;
    medium.initialOutputs = 3;
    medium.layerInputs = 2;
    medium.layerOutputs = 2;
    medium.layerCount = 3;
    medium.layersPerGroup = 1;
    const std::array<std::vector<uint8_t>, 3> mediumLayers = {medLayer0, medLayer1, medLayer2};
    if (!buildConstants(medium, medInitial, 32, medMixer, 220, mediumLayers, 148, medDense, 35))
    {
        error_ = "NGU Medium chroma constants could not be created";
        return false;
    }

    auto& high = state->qualities[size_t(NguQuality::High)];
    high.name = "High";
    high.initial = cubici1;
    high.layer = layer3588;
    high.dense = dense2100;
    high.initialOutputs = 3;
    high.layerInputs = 3;
    high.layerOutputs = 1;
    high.layerCount = 15;
    high.layersPerGroup = 3;
    if (!buildConstants(high, highInitial, 32, {}, 0, highLayers, 111, highDense, 51))
    {
        error_ = "NGU High chroma constants could not be created";
        return false;
    }

    auto& veryHigh = state->qualities[size_t(NguQuality::VeryHigh)];
    veryHigh.name = "VeryHigh";
    veryHigh.initial = cubici2;
    veryHigh.layer = layer4620;
    veryHigh.dense = dense2672;
    veryHigh.initialOutputs = 4;
    veryHigh.layerInputs = 4;
    veryHigh.layerOutputs = 1;
    veryHigh.layerCount = 28;
    veryHigh.layersPerGroup = 4;
    if (!buildConstants(veryHigh, veryHighInitial, 42, {}, 0, veryHighLayers, 147, veryHighDense, 67))
    {
        error_ = "NGU VeryHigh chroma constants could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma quality rasterizer could not be created";
        return false;
    }
    nativeChromaNetwork_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

static bool NguChromaGeometry(ID3D11View* view, bool denseOutput, int& geometryIndex)
{
    if (!view)
        return false;
    if (denseOutput)
    {
        if (ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362))
            geometryIndex = 0;
        else if (ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542))
            geometryIndex = 1;
        else if (ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 722))
            geometryIndex = 2;
        else if (ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 1082))
            geometryIndex = 3;
        else
            return false;
        return true;
    }
    if (ViewMatchesTexture(view, DXGI_FORMAT_R16_UNORM, 640, 360) ||
        ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 640, 360))
        geometryIndex = 0;
    else if (ViewMatchesTexture(view, DXGI_FORMAT_R16_UNORM, 960, 540) ||
             ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 960, 540))
        geometryIndex = 1;
    else if (ViewMatchesTexture(view, DXGI_FORMAT_R16_UNORM, 640, 720) ||
             ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 640, 720))
        geometryIndex = 2;
    else if (ViewMatchesTexture(view, DXGI_FORMAT_R16_UNORM, 960, 1080) ||
             ViewMatchesTexture(view, DXGI_FORMAT_R16G16B16A16_FLOAT, 960, 1080))
        geometryIndex = 3;
    else
        return false;
    return true;
}

bool NguScaler::RunNativeChromaInitial(NguQuality quality,
                                       ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* const* outputs,
                                       int outputCount)
{
    int geometry = 0;
    if (!nativeChromaNetwork_ || !outputs || !NguChromaGeometry(input, false, geometry))
    {
        error_ = "NGU chroma initializer request is invalid";
        return false;
    }
    auto& profile = nativeChromaNetwork_->qualities[size_t(quality)];
    if (!profile.initial || outputCount != profile.initialOutputs)
    {
        error_ = "NGU chroma initializer output count is invalid";
        return false;
    }
    static constexpr UINT widths[] = {640, 960, 640, 960};
    static constexpr UINT heights[] = {360, 540, 720, 1080};
    for (int index = 0; index < outputCount; ++index)
        if (!outputs[index] ||
            !ViewMatchesTexture(outputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma initializer output is invalid";
            return false;
        }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  profile.initial.Get(),
                  profile.initialConstants[size_t(geometry)].Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  &input,
                  1,
                  outputs,
                  outputCount,
                  widths[geometry],
                  heights[geometry]);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaMixer(NguQuality quality,
                                     ID3D11ShaderResourceView* const* inputs,
                                     ID3D11RenderTargetView* const* outputs,
                                     int outputCount)
{
    int geometry = 0;
    if (!nativeChromaNetwork_ || !inputs || !outputs || !NguChromaGeometry(inputs[0], false, geometry))
    {
        error_ = "NGU chroma mixer request is invalid";
        return false;
    }
    auto& profile = nativeChromaNetwork_->qualities[size_t(quality)];
    if (!profile.mixer || outputCount != 2)
    {
        error_ = "NGU chroma mixer is not defined for this quality";
        return false;
    }
    static constexpr UINT widths[] = {640, 960, 640, 960};
    static constexpr UINT heights[] = {360, 540, 720, 1080};
    for (int index = 0; index < 3; ++index)
        if (!inputs[index] ||
            !ViewMatchesTexture(inputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma mixer input is invalid";
            return false;
        }
    for (int index = 0; index < outputCount; ++index)
        if (!outputs[index] ||
            !ViewMatchesTexture(outputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma mixer output is invalid";
            return false;
        }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  profile.mixer.Get(),
                  profile.mixerConstants[size_t(geometry)].Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  inputs,
                  3,
                  outputs,
                  outputCount,
                  widths[geometry],
                  heights[geometry]);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaQualityLayer(NguQuality quality,
                                            ID3D11ShaderResourceView* const* inputs,
                                            int inputCount,
                                            int layer,
                                            ID3D11RenderTargetView* const* outputs,
                                            int outputCount)
{
    int geometry = 0;
    if (!nativeChromaNetwork_ || !inputs || !outputs || !NguChromaGeometry(inputs[0], false, geometry))
    {
        error_ = "NGU chroma layer request is invalid";
        return false;
    }
    auto& profile = nativeChromaNetwork_->qualities[size_t(quality)];
    if (!profile.layer || inputCount != profile.layerInputs || outputCount != profile.layerOutputs || layer < 0 ||
        layer >= profile.layerCount)
    {
        error_ = "NGU chroma layer shape is not evidence-backed";
        return false;
    }
    static constexpr UINT widths[] = {640, 960, 640, 960};
    static constexpr UINT heights[] = {360, 540, 720, 1080};
    for (int index = 0; index < inputCount; ++index)
        if (!inputs[index] ||
            !ViewMatchesTexture(inputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma layer input is invalid";
            return false;
        }
    for (int index = 0; index < outputCount; ++index)
        if (!outputs[index] ||
            !ViewMatchesTexture(outputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma layer output is invalid";
            return false;
        }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  profile.layer.Get(),
                  profile.layerConstants[size_t(geometry)][size_t(layer)].Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  inputs,
                  inputCount,
                  outputs,
                  outputCount,
                  widths[geometry],
                  heights[geometry]);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaQualityDense(NguQuality quality,
                                            ID3D11ShaderResourceView* const* inputs,
                                            int inputCount,
                                            ID3D11RenderTargetView* output)
{
    int geometry = 0;
    if (!nativeChromaNetwork_ || !inputs || !output || !NguChromaGeometry(output, true, geometry))
    {
        error_ = "NGU chroma dense request is invalid";
        return false;
    }
    auto& profile = nativeChromaNetwork_->qualities[size_t(quality)];
    if (!profile.dense || inputCount != profile.layerInputs)
    {
        error_ = "NGU chroma dense input count is invalid";
        return false;
    }
    static constexpr UINT widths[] = {640, 960, 640, 960};
    static constexpr UINT heights[] = {360, 540, 720, 1080};
    static constexpr UINT denseWidths[] = {642, 962, 642, 962};
    static constexpr UINT denseHeights[] = {362, 542, 722, 1082};
    for (int index = 0; index < inputCount; ++index)
        if (!inputs[index] ||
            !ViewMatchesTexture(inputs[index], DXGI_FORMAT_R16G16B16A16_FLOAT, widths[geometry], heights[geometry]))
        {
            error_ = "NGU chroma dense input is invalid";
            return false;
        }
    if (!ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_FLOAT, denseWidths[geometry], denseHeights[geometry]))
    {
        error_ = "NGU chroma dense output is invalid";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  profile.dense.Get(),
                  profile.denseConstants[size_t(geometry)].Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  inputs,
                  inputCount,
                  &output,
                  1,
                  denseWidths[geometry],
                  denseHeights[geometry]);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCubici2(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* const outputs[4])
{
    return RunNativeChromaInitial(NguQuality::VeryHigh, input, outputs, 4);
}

bool NguScaler::RunNativeChromaLayer(ID3D11ShaderResourceView* const inputs[4],
                                     int layer,
                                     ID3D11RenderTargetView* output)
{
    ID3D11RenderTargetView* outputs[1] = {output};
    return RunNativeChromaQualityLayer(NguQuality::VeryHigh, inputs, 4, layer, outputs, 1);
}

bool NguScaler::RunNativeChromaDense(ID3D11ShaderResourceView* const inputs[4],
                                     ID3D11RenderTargetView* output)
{
    return RunNativeChromaQualityDense(NguQuality::VeryHigh, inputs, 4, output);
}

bool NguScaler::RunNativeChromaQualityLayerForTest(NguQuality quality,
                                                    ID3D11ShaderResourceView* const* inputs,
                                                    int inputCount,
                                                    int layer,
                                                    ID3D11RenderTargetView* output)
{
    ID3D11RenderTargetView* outputs[1] = {output};
    return RunNativeChromaQualityLayer(quality, inputs, inputCount, layer, outputs, 1);
}

bool NguScaler::RunNativeChromaQualityLayerMultiForTest(NguQuality quality,
                                                        ID3D11ShaderResourceView* const* inputs,
                                                        int inputCount,
                                                        int layer,
                                                        ID3D11RenderTargetView* const* outputs,
                                                        int outputCount)
{
    return RunNativeChromaQualityLayer(quality, inputs, inputCount, layer, outputs, outputCount);
}

bool NguScaler::RunNativeChromaMixerForTest(NguQuality quality,
                                            ID3D11ShaderResourceView* const* inputs,
                                            ID3D11RenderTargetView* const* outputs,
                                            int outputCount)
{
    return RunNativeChromaMixer(quality, inputs, outputs, outputCount);
}

bool NguScaler::RunNativeChromaQualityDenseForTest(NguQuality quality,
                                                    ID3D11ShaderResourceView* const* inputs,
                                                    int inputCount,
                                                    ID3D11RenderTargetView* output)
{
    return RunNativeChromaQualityDense(quality, inputs, inputCount, output);
}

bool NguScaler::RunNativeChromaInitialForTest(NguQuality quality,
                                              ID3D11ShaderResourceView* input,
                                              ID3D11RenderTargetView* const* outputs,
                                              int outputCount)
{
    return RunNativeChromaInitial(quality, input, outputs, outputCount);
}

bool NguScaler::PrepareNativeChromaVeryHigh(
    pl_gpu gpu,
    int width,
    int height,
    NguQuality quality,
    std::unique_ptr<NativeChromaVeryHighResources>& state,
    const char* geometry)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU VeryHigh chroma requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) && state->height == UINT(height) &&
        state->quality == quality)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device || !PrepareNativeChromaNetwork(gpu))
    {
        if (error_.empty())
            error_ = "NGU VeryHigh chroma requires the D3D11 backend";
        return false;
    }

    auto next = std::make_unique<NativeChromaVeryHighResources>();
    next->width = UINT(width);
    next->height = UINT(height);
    next->quality = quality;
    ID3D11Device* device = d3d->device;
    auto create = [&](ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& view,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = next->width;
        desc.Height = next->height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    for (size_t index = 0; index < next->initialTextures.size(); ++index)
    {
        if (!create(next->initialTextures[index], next->initialViews[index], next->initialShaderViews[index]))
        {
            error_ = std::string("NGU ") + geometry + " VeryHigh chroma initializer surfaces failed";
            return false;
        }
    }
    for (size_t set = 0; set < next->groupTextures.size(); ++set)
    {
        for (size_t feature = 0; feature < next->groupTextures[set].size(); ++feature)
        {
            if (!create(next->groupTextures[set][feature],
                        next->groupViews[set][feature],
                        next->groupShaderViews[set][feature]))
            {
                error_ = std::string("NGU ") + geometry + " VeryHigh chroma layer surfaces failed";
                return false;
            }
        }
    }

    state = std::move(next);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaVeryHigh(ID3D11ShaderResourceView* cb,
                                        ID3D11RenderTargetView* denseOutput,
                                        int width,
                                        int height,
                                        NativeChromaVeryHighResources& state)
{
    if (state.width != UINT(width) || state.height != UINT(height) || !cb || !denseOutput ||
        !ViewMatchesTexture(cb, DXGI_FORMAT_R16_UNORM, UINT(width), UINT(height)) ||
        !ViewMatchesTexture(denseOutput, DXGI_FORMAT_R16G16B16A16_FLOAT, UINT(width + 2), UINT(height + 2)))
    {
        error_ = "NGU VeryHigh chroma request is invalid";
        return false;
    }

    auto& profile = nativeChromaNetwork_->qualities[size_t(state.quality)];
    std::array<ID3D11RenderTargetView*, 4> initialTargets = {};
    for (int index = 0; index < profile.initialOutputs; ++index)
        initialTargets[size_t(index)] = state.initialViews[size_t(index)].Get();
    if (!RunNativeChromaInitial(state.quality, cb, initialTargets.data(), profile.initialOutputs))
        return false;

    std::array<ID3D11ShaderResourceView*, 4> current = {};
    for (int index = 0; index < profile.initialOutputs; ++index)
        current[size_t(index)] = state.initialShaderViews[size_t(index)].Get();

    if (profile.mixer)
    {
        std::array<ID3D11RenderTargetView*, 2> mixerTargets = {
            state.groupViews[0][0].Get(), state.groupViews[0][1].Get()};
        if (!RunNativeChromaMixer(state.quality, current.data(), mixerTargets.data(), 2))
            return false;
        current[0] = state.groupShaderViews[0][0].Get();
        current[1] = state.groupShaderViews[0][1].Get();
    }

    for (int base = 0; base < profile.layerCount; base += profile.layersPerGroup)
    {
        // Mixer outputs already occupy group 0. Start the first layer in group
        // 1 so a texture is never rebound as both SRV input and RTV output.
        const size_t set =
            size_t(((base / profile.layersPerGroup) + (profile.mixer ? 1 : 0)) % 2);
        if (profile.layerOutputs == 1)
        {
            for (int pass = 0; pass < profile.layersPerGroup; ++pass)
            {
                ID3D11RenderTargetView* target = state.groupViews[set][size_t(pass)].Get();
                if (!RunNativeChromaQualityLayer(
                        state.quality, current.data(), profile.layerInputs, base + pass, &target, 1))
                    return false;
            }
            for (int input = 0; input < profile.layerInputs; ++input)
                current[size_t(input)] = state.groupShaderViews[set][size_t(input)].Get();
        }
        else
        {
            std::array<ID3D11RenderTargetView*, 4> targets = {};
            for (int output = 0; output < profile.layerOutputs; ++output)
                targets[size_t(output)] = state.groupViews[set][size_t(output)].Get();
            if (!RunNativeChromaQualityLayer(
                    state.quality, current.data(), profile.layerInputs, base, targets.data(), profile.layerOutputs))
                return false;
            for (int input = 0; input < profile.layerOutputs; ++input)
                current[size_t(input)] = state.groupShaderViews[set][size_t(input)].Get();
        }
    }
    if (!RunNativeChromaQualityDense(state.quality, current.data(), profile.layerInputs, denseOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaVeryHigh720(pl_gpu gpu)
{
    return PrepareNativeChromaVeryHigh(
        gpu, 640, 360, NguQuality::VeryHigh, nativeChromaVeryHigh720_, "720p");
}

bool NguScaler::RunNativeChromaVeryHigh720(ID3D11ShaderResourceView* cb, ID3D11RenderTargetView* denseOutput)
{
    if (!nativeChromaVeryHigh720_)
    {
        error_ = "NGU 720p VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaVeryHigh(cb, denseOutput, 640, 360, *nativeChromaVeryHigh720_);
}

bool NguScaler::PrepareNativeChromaVeryHigh1080(pl_gpu gpu)
{
    return PrepareNativeChromaVeryHigh(
        gpu, 960, 540, NguQuality::VeryHigh, nativeChromaVeryHigh1080_, "1080p");
}

bool NguScaler::RunNativeChromaVeryHigh1080(ID3D11ShaderResourceView* cb, ID3D11RenderTargetView* denseOutput)
{
    if (!nativeChromaVeryHigh1080_)
    {
        error_ = "NGU 1080p VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaVeryHigh(cb, denseOutput, 960, 540, *nativeChromaVeryHigh1080_);
}

bool NguScaler::PrepareNativeChromaCombines(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma combines require a GPU";
        return false;
    }
    if (nativeChromaCombines_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma combines require the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_combine.hlsl", 32 * 1024);
    const auto combine1Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine1.cso", 0);
    const auto combine2Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine2.cso", 0);
    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine_constants.bin", 2 * sizeof(float) * 4);
    const auto constantBytes1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine_constants_1080.bin", 2 * sizeof(float) * 4);
    if (shaderText.empty() || combine1Bytecode.empty() || combine2Bytecode.empty() || constantBytes.empty() ||
        constantBytes1080.empty())
    {
        error_ = "NGU chroma combine assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaCombine",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma combine vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaCombines>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            combine1Bytecode.data(), static_cast<SIZE_T>(combine1Bytecode.size()), nullptr, &state->combine1)) ||
        FAILED(device->CreatePixelShader(
            combine2Bytecode.data(), static_cast<SIZE_T>(combine2Bytecode.size()), nullptr, &state->combine2)))
    {
        error_ = "NGU chroma combine shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(constantBytes.size());
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = constantBytes.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU chroma combine constants could not be created";
        return false;
    }
    constantData.pSysMem = constantBytes1080.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants1080)))
    {
        error_ = "NGU 1080p chroma combine constants could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma combine rasterizer could not be created";
        return false;
    }
    nativeChromaCombines_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCombine1(ID3D11ShaderResourceView* addInput,
                                        ID3D11ShaderResourceView* nnInput,
                                        ID3D11RenderTargetView* output)
{
    const bool is720 = addInput && nnInput && output &&
                       ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
                       ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 720);
    const bool is1080 = addInput && nnInput && output &&
                        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
                        ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 1080);
    const bool isClass3 = addInput && nnInput && output &&
                          ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 1440) &&
                          ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 722) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 1440);
    const bool isClass3_1080 = addInput && nnInput && output &&
                               ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 2160) &&
                               ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 1082) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 2160);
    if (!nativeChromaCombines_ || !addInput || !nnInput || !output ||
        (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma combine 1 request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {addInput, nnInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaCombines_->vertex.Get(),
                  nativeChromaCombines_->combine1.Get(),
                  ((is1080 || isClass3_1080) ? nativeChromaCombines_->constants1080.Get()
                                             : nativeChromaCombines_->constants.Get()),
                  nativeChromaCombines_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  (is1080 || isClass3_1080) ? 1920 : 1280,
                  isClass3_1080 ? 2160
                  : isClass3    ? 1440
                  : is1080      ? 1080
                                : 720);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCombine2(ID3D11ShaderResourceView* addInput,
                                        ID3D11ShaderResourceView* cbInput,
                                        ID3D11ShaderResourceView* nnInput,
                                        ID3D11RenderTargetView* output)
{
    const bool is720 = addInput && cbInput && nnInput && output &&
                       ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
                       ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
                       ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1280, 720);
    const bool is1080 = addInput && cbInput && nnInput && output &&
                        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
                        ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
                        ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1920, 1080);
    const bool isClass3 = addInput && cbInput && nnInput && output &&
                          ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 1440) &&
                          ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1280, 1440) &&
                          ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 722) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1280, 1440);
    const bool isClass3_1080 = addInput && cbInput && nnInput && output &&
                               ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 2160) &&
                               ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1920, 2160) &&
                               ViewMatchesTexture(nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 1082) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1920, 2160);
    if (!nativeChromaCombines_ || !addInput || !cbInput || !nnInput || !output ||
        (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma combine 2 request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {addInput, nnInput, cbInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaCombines_->vertex.Get(),
                  nativeChromaCombines_->combine2.Get(),
                  ((is1080 || isClass3_1080) ? nativeChromaCombines_->constants1080.Get()
                                             : nativeChromaCombines_->constants.Get()),
                  nativeChromaCombines_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  (is1080 || isClass3_1080) ? 1920 : 1280,
                  isClass3_1080 ? 2160
                  : isClass3    ? 1440
                  : is1080      ? 1080
                                : 720);
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaRealign(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma Realign requires a GPU";
        return false;
    }
    if (nativeChromaRealign_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma Realign requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_realign.hlsl", 96 * 1024);
    const auto pixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign.cso", 0);
    const auto constantBytes = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign_constants.bin", sizeof(float) * 4);
    const auto constantBytes1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign_constants_1080.bin", sizeof(float) * 4);
    if (shaderText.empty() || pixelBytecode.empty() || constantBytes.empty() || constantBytes1080.empty())
    {
        error_ = "NGU chroma Realign assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaRealign",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma Realign vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaRealign>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(), static_cast<SIZE_T>(pixelBytecode.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU chroma Realign shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(constantBytes.size());
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = constantBytes.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU chroma Realign constants could not be created";
        return false;
    }
    constantData.pSysMem = constantBytes1080.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants1080)))
    {
        error_ = "NGU 1080p chroma Realign constants could not be created";
        return false;
    }
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU chroma Realign sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma Realign rasterizer could not be created";
        return false;
    }
    nativeChromaRealign_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaRealign(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    const bool is720 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1280, 720) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool is1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1920, 1080) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080);
    const bool isClass3 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1280, 1440) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 1440);
    const bool isClass3_1080 = input && output && ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1920, 2160) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 2160);
    if (!nativeChromaRealign_ || !input || !output || (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma Realign request is invalid";
        return false;
    }
    DrawD3D11Pass(
        pl_d3d11_get(gpu_)->device,
        nativeChromaRealign_->vertex.Get(),
        nativeChromaRealign_->pixel.Get(),
        ((is1080 || isClass3_1080) ? nativeChromaRealign_->constants1080.Get() : nativeChromaRealign_->constants.Get()),
        nativeChromaRealign_->rasterizer.Get(),
        &input,
        1,
        &output,
        1,
        (is1080 || isClass3_1080) ? 1920 : 1280,
        isClass3_1080 ? 2160
        : isClass3    ? 1440
        : is1080      ? 1080
                      : 720,
        nativeChromaRealign_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaYCbCr(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU chroma YCbCr requires a GPU";
        return false;
    }
    if (nativeChromaYCbCr_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU chroma YCbCr requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_chroma_ycbcr.hlsl", 16 * 1024);
    const auto pixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_ycbcr.cso", 0);
    const auto class3PixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_ycbcr_class3.cso", 0);
    if (shaderText.empty() || pixelBytecode.empty() || class3PixelBytecode.empty())
    {
        error_ = "NGU chroma YCbCr assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_ChromaYCbCr",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU chroma YCbCr vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaYCbCr>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(), static_cast<SIZE_T>(pixelBytecode.size()), nullptr, &state->pixel)) ||
        FAILED(device->CreatePixelShader(
            class3PixelBytecode.data(), static_cast<SIZE_T>(class3PixelBytecode.size()), nullptr, &state->class3Pixel)))
    {
        error_ = "NGU chroma YCbCr shader objects could not be created";
        return false;
    }
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU chroma YCbCr sampler could not be created";
        return false;
    }
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->linearSampler)))
    {
        error_ = "NGU class-3 chroma YCbCr linear sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU chroma YCbCr rasterizer could not be created";
        return false;
    }
    nativeChromaYCbCr_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaYCbCr(ID3D11ShaderResourceView* yInput,
                                     ID3D11ShaderResourceView* cbcrInput,
                                     ID3D11RenderTargetView* output)
{
    const bool is720 = yInput && cbcrInput && output &&
                       (ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1280, 720) ||
                        ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1280, 720)) &&
                       ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) &&
                       (ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16_UNORM, 1280, 720) ||
                        ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720));
    const bool is1080 = yInput && cbcrInput && output &&
                        (ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1920, 1080) ||
                         ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1920, 1080)) &&
                        ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) &&
                        (ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16_UNORM, 1920, 1080) ||
                         ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080));
    const bool isClass3 = yInput && cbcrInput && output &&
                          (ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1280, 720) ||
                           ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1280, 720)) &&
                          ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 1440) &&
                          ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool isClass3_1080 = yInput && cbcrInput && output &&
                               (ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1920, 1080) ||
                                ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1920, 1080)) &&
                               ViewMatchesTexture(cbcrInput, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 2160) &&
                               ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080);
    if (!nativeChromaYCbCr_ || !yInput || !cbcrInput || !output || (!is720 && !is1080 && !isClass3 && !isClass3_1080))
    {
        error_ = "NGU chroma YCbCr request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {yInput, cbcrInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaYCbCr_->vertex.Get(),
                  (isClass3 || isClass3_1080) ? nativeChromaYCbCr_->class3Pixel.Get() : nativeChromaYCbCr_->pixel.Get(),
                  nullptr,
                  nativeChromaYCbCr_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  (is1080 || isClass3_1080) ? 1920 : 1280,
                  (is1080 || isClass3_1080) ? 1080 : 720,
                  nativeChromaYCbCr_->pointSampler.Get(),
                  nullptr,
                  nullptr,
                  nullptr,
                  nullptr,
                  (isClass3 || isClass3_1080) ? nativeChromaYCbCr_->linearSampler.Get() : nullptr);
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaFull(
    pl_gpu gpu,
    int width,
    int height,
    NguQuality quality,
    std::unique_ptr<NativeChromaFullResources>& state,
    const char* geometry)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU full VeryHigh chroma requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) && state->height == UINT(height) &&
        state->quality == quality)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    const bool is1080 = width == 960 && height == 540;
    if (!PrepareNativeChromaSplit(gpu) || !PrepareNativeChromaScale(gpu) ||
        !PrepareNativeChromaVeryHigh(
            gpu,
            width,
            height,
            quality,
            is1080 ? nativeChromaVeryHigh1080_ : nativeChromaVeryHigh720_,
            geometry) ||
        !PrepareNativeChromaCombines(gpu) || !PrepareNativeChromaRealign(gpu) || !PrepareNativeChromaYCbCr(gpu) ||
        (is1080 ? !PrepareNativeSuperResStrength3_1080(gpu) : !PrepareNativeSuperResStrength3(gpu)))
        return false;

    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto next = std::make_unique<NativeChromaFullResources>();
    next->width = UINT(width);
    next->height = UINT(height);
    next->quality = quality;
    const UINT outputWidth = UINT(width * 2);
    const UINT outputHeight = UINT(height * 2);
    const UINT denseWidth = UINT(width + 2);
    const UINT denseHeight = UINT(height + 2);
    auto create = [&](UINT width,
                      UINT height,
                      DXGI_FORMAT format,
                      ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& renderTarget,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!create(next->width, next->height, DXGI_FORMAT_R16_UNORM, next->cbTexture, next->cbView, next->cbShaderView) ||
        !create(next->width, next->height, DXGI_FORMAT_R16_UNORM, next->crTexture, next->crView, next->crShaderView) ||
        !create(outputWidth,
                next->height,
                DXGI_FORMAT_R16_UNORM,
                next->cbScaleXTexture,
                next->cbScaleXView,
                next->cbScaleXShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16_UNORM,
                next->cbScaleYTexture,
                next->cbScaleYView,
                next->cbScaleYShaderView) ||
        !create(outputWidth,
                next->height,
                DXGI_FORMAT_R16_UNORM,
                next->crScaleXTexture,
                next->crScaleXView,
                next->crScaleXShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16_UNORM,
                next->crScaleYTexture,
                next->crScaleYView,
                next->crScaleYShaderView) ||
        !create(denseWidth,
                denseHeight,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                next->denseCbTexture,
                next->denseCbView,
                next->denseCbShaderView) ||
        !create(denseWidth,
                denseHeight,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                next->denseCrTexture,
                next->denseCrView,
                next->denseCrShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16_UNORM,
                next->combine1Texture,
                next->combine1View,
                next->combine1ShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16G16_UNORM,
                next->combine2Texture,
                next->combine2View,
                next->combine2ShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                next->realignTexture,
                next->realignView,
                next->realignShaderView) ||
        !create(outputWidth,
                outputHeight,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                next->ycbcrTexture,
                next->ycbcrView,
                next->ycbcrShaderView))
    {
        error_ = std::string("NGU ") + geometry + " full VeryHigh chroma surfaces could not be created";
        return false;
    }
    state = std::move(next);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaFull(ID3D11ShaderResourceView* packedUv,
                                    ID3D11ShaderResourceView* yInput,
                                    ID3D11ShaderResourceView* diffOriginal,
                                    ID3D11ShaderResourceView* mainOriginal,
                                    ID3D11ShaderResourceView* baseOriginal,
                                    ID3D11RenderTargetView* output,
                                    int width,
                                    int height,
                                    bool is1080,
                                    bool p010,
                                    NativeChromaFullResources& state)
{
    const UINT outputWidth = UINT(width * 2);
    const UINT outputHeight = UINT(height * 2);
    const bool packedUvIsRgba16 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_R16G16B16A16_UNORM, UINT(width), UINT(height));
    const bool packedUvIsRg16 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_R16G16_UNORM, UINT(width), UINT(height));
    const bool packedUvIsRg8 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_R8G8_UNORM, UINT(width), UINT(height));
    const bool packedUvIsBgra8 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_B8G8R8A8_UNORM, UINT(width), UINT(height));
    const bool diffOriginalIsRg16 =
        diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16_UNORM, UINT(width), UINT(height));
    const bool diffOriginalIsRg8 =
        diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R8G8_UNORM, UINT(width), UINT(height));
    const bool mainIsRgba16 =
        mainOriginal && ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, outputWidth, outputHeight);
    const bool mainIsR8 =
        mainOriginal && ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R8_UNORM, outputWidth, outputHeight);
    const bool baseIsRgba16 =
        baseOriginal && ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, outputWidth, outputHeight);
    const bool baseIsR8 =
        baseOriginal && ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R8_UNORM, outputWidth, outputHeight);
    if (state.width != UINT(width) || state.height != UINT(height) || !packedUv || !yInput || !diffOriginal ||
        !mainOriginal || !baseOriginal || !output ||
        (!packedUvIsRgba16 && !packedUvIsRg16 && !packedUvIsRg8 && !packedUvIsBgra8) ||
        (!ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, outputWidth, outputHeight) &&
         !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, outputWidth, outputHeight)) ||
        (!ViewMatchesTexture(diffOriginal, DXGI_FORMAT_B8G8R8A8_UNORM, UINT(width), UINT(height)) &&
         !ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, UINT(width), UINT(height)) &&
         !diffOriginalIsRg16 && !diffOriginalIsRg8) ||
        (!mainIsRgba16 && !mainIsR8) || (!baseIsRgba16 && !baseIsR8) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, outputWidth, outputHeight))
    {
        error_ = "NGU full VeryHigh chroma request is invalid";
        return false;
    }

    if (!RunNativeChromaSplit(packedUv, width, height, state.cbView.Get(), state.crView.Get()) ||
        !RunNativeChromaVeryHigh(
            state.cbShaderView.Get(), state.denseCbView.Get(), width, height, *(
                is1080 ? nativeChromaVeryHigh1080_.get() : nativeChromaVeryHigh720_.get())) ||
        !RunNativeChromaScaleX(state.cbShaderView.Get(), state.cbScaleXView.Get()) ||
        !RunNativeChromaScaleY(state.cbScaleXShaderView.Get(), state.cbScaleYView.Get()) ||
        !RunNativeChromaCombine1(
            state.cbScaleYShaderView.Get(), state.denseCbShaderView.Get(), state.combine1View.Get()) ||
        !RunNativeChromaVeryHigh(
            state.crShaderView.Get(), state.denseCrView.Get(), width, height, *(
                is1080 ? nativeChromaVeryHigh1080_.get() : nativeChromaVeryHigh720_.get())) ||
        !RunNativeChromaScaleX(state.crShaderView.Get(), state.crScaleXView.Get()) ||
        !RunNativeChromaScaleY(state.crScaleXShaderView.Get(), state.crScaleYView.Get()) ||
        !RunNativeChromaCombine2(state.crScaleYShaderView.Get(),
                                 state.combine1ShaderView.Get(),
                                 state.denseCrShaderView.Get(),
                                 state.combine2View.Get()) ||
        !RunNativeChromaRealign(state.combine2ShaderView.Get(), state.realignView.Get()) ||
        !RunNativeChromaYCbCr(yInput, state.realignShaderView.Get(), state.ycbcrView.Get()) ||
        (is1080 ? !RunNativeSuperResStrength3_1080(
                      state.ycbcrShaderView.Get(), diffOriginal, mainOriginal, baseOriginal, output, p010)
                : !RunNativeSuperResStrength3(
                      state.ycbcrShaderView.Get(), diffOriginal, mainOriginal, baseOriginal, output, p010)))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaVeryHigh720Full(pl_gpu gpu)
{
    return PrepareNativeChromaFull(
        gpu, 640, 360, NguQuality::VeryHigh, nativeChromaVeryHigh720Full_, "720p");
}

bool NguScaler::RunNativeChromaVeryHigh720Full(ID3D11ShaderResourceView* packedUv,
                                               ID3D11ShaderResourceView* yInput,
                                               ID3D11ShaderResourceView* diffOriginal,
                                               ID3D11ShaderResourceView* mainOriginal,
                                               ID3D11ShaderResourceView* baseOriginal,
                                               ID3D11RenderTargetView* output,
                                               bool p010)
{
    if (!nativeChromaVeryHigh720Full_)
    {
        error_ = "NGU 720p full VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaFull(packedUv,
                               yInput,
                               diffOriginal,
                               mainOriginal,
                               baseOriginal,
                               output,
                               640,
                               360,
                               false,
                               p010,
                               *nativeChromaVeryHigh720Full_);
}

bool NguScaler::CopyNativeChromaFullStage720(int stage, ID3D11RenderTargetView* output)
{
    if (!nativeChromaVeryHigh720Full_ || !output)
    {
        error_ = "NGU 720p full chroma stage copy is unavailable";
        return false;
    }
    auto& state = *nativeChromaVeryHigh720Full_;
    ID3D11Texture2D* source = nullptr;
    switch (stage)
    {
    case 0:
        source = state.cbTexture.Get();
        break;
    case 1:
        source = state.crTexture.Get();
        break;
    case 2:
        source = state.denseCbTexture.Get();
        break;
    case 3:
        source = state.denseCrTexture.Get();
        break;
    case 4:
        source = state.cbScaleXTexture.Get();
        break;
    case 5:
        source = state.cbScaleYTexture.Get();
        break;
    case 6:
        source = state.combine1Texture.Get();
        break;
    case 7:
        source = state.crScaleXTexture.Get();
        break;
    case 8:
        source = state.crScaleYTexture.Get();
        break;
    case 9:
        source = state.combine2Texture.Get();
        break;
    case 10:
        source = state.realignTexture.Get();
        break;
    case 11:
        source = state.ycbcrTexture.Get();
        break;
    }
    if (!source || !CopyTextureToTarget(gpu_, source, output))
    {
        error_ = "NGU 720p full chroma stage copy failed";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaVeryHigh1080Full(pl_gpu gpu)
{
    return PrepareNativeChromaFull(
        gpu, 960, 540, NguQuality::VeryHigh, nativeChromaVeryHigh1080Full_, "1080p");
}

bool NguScaler::RunNativeChromaVeryHigh1080Full(ID3D11ShaderResourceView* packedUv,
                                                ID3D11ShaderResourceView* yInput,
                                                ID3D11ShaderResourceView* diffOriginal,
                                                ID3D11ShaderResourceView* mainOriginal,
                                                ID3D11ShaderResourceView* baseOriginal,
                                                ID3D11RenderTargetView* output,
                                                bool p010)
{
    if (!nativeChromaVeryHigh1080Full_)
    {
        error_ = "NGU 1080p full VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaFull(packedUv,
                               yInput,
                               diffOriginal,
                               mainOriginal,
                               baseOriginal,
                               output,
                               960,
                               540,
                               true,
                               p010,
                               *nativeChromaVeryHigh1080Full_);
}

bool NguScaler::PrepareNativeChromaCoreClass3(pl_gpu gpu,
                                              int chromaWidth,
                                              int chromaHeight,
                                              NguQuality quality,
                                              std::unique_ptr<NativeChromaCoreClass3_720>& stateRef,
                                              const char* geometry)
{
    if (!gpu || chromaWidth <= 0 || chromaHeight <= 0)
    {
        error_ = "NGU class-3 chroma core requires a GPU";
        return false;
    }
    if (stateRef && gpu == gpu_ && stateRef->chromaWidth == UINT(chromaWidth) &&
        stateRef->chromaHeight == UINT(chromaHeight) && stateRef->quality == quality)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    const bool is1080 = chromaWidth == 960 && chromaHeight == 1080;
    auto& veryHighState = is1080 ? nativeChromaVeryHighClass3_1080_ : nativeChromaVeryHighClass3_720_;
    if (!PrepareNativeChromaSplit(gpu) || !PrepareNativeChromaScale(gpu) ||
        !PrepareNativeChromaVeryHigh(gpu, chromaWidth, chromaHeight, quality, veryHighState, geometry) ||
        !PrepareNativeChromaCombines(gpu) || !PrepareNativeChromaRealign(gpu) || !PrepareNativeChromaYCbCr(gpu) ||
        !PrepareNativeSuperResPrimitives(gpu))
        return false;

    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto state = std::make_unique<NativeChromaCoreClass3_720>();
    state->chromaWidth = UINT(chromaWidth);
    state->chromaHeight = UINT(chromaHeight);
    state->quality = quality;
    const std::string expandText = LoadBundledText(L"shaders\\Ngu\\ngu_l8_expand.hlsl", 16 * 1024);
    const auto expandBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_l8_expand.cso", 0);
    const std::string scaleXText = LoadBundledText(L"shaders\\Ngu\\ngu_class3_scale_x.hlsl", 16 * 1024);
    const auto scaleXBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_scale_x.cso", 0);
    const auto scaleXTable = LoadBundledBytes(is1080 ? L"shaders\\Ngu\\superres_1080_class3_down_x_table.bin"
                                                     : L"shaders\\Ngu\\superres_down_x_table.bin",
                                              size_t(4) * chromaWidth * 8);
    if (expandText.empty() || expandBytecode.empty() || scaleXText.empty() || scaleXBytecode.empty() ||
        scaleXTable.empty())
    {
        error_ = "NGU class-3 expansion or scale-X assets are unavailable";
        return false;
    }
    ComPtr<ID3DBlob> expandVertexBlob, expandErrors;
    ComPtr<ID3DBlob> scaleXVertexBlob, scaleXErrors;
    const UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(expandText.data(),
                          expandText.size(),
                          "NGU_Class3_YExpand",
                          nullptr,
                          nullptr,
                          "VSMain",
                          "vs_5_0",
                          compileFlags,
                          0,
                          &expandVertexBlob,
                          &expandErrors)) ||
        FAILED(device->CreateVertexShader(
            expandVertexBlob->GetBufferPointer(), expandVertexBlob->GetBufferSize(), nullptr, &state->expandVertex)) ||
        FAILED(device->CreatePixelShader(
            expandBytecode.data(), static_cast<SIZE_T>(expandBytecode.size()), nullptr, &state->expandPixel)) ||
        FAILED(D3DCompile(scaleXText.data(),
                          scaleXText.size(),
                          "NGU_Class3_ScaleX",
                          nullptr,
                          nullptr,
                          "VSMain",
                          "vs_5_0",
                          compileFlags,
                          0,
                          &scaleXVertexBlob,
                          &scaleXErrors)) ||
        FAILED(device->CreateVertexShader(
            scaleXVertexBlob->GetBufferPointer(), scaleXVertexBlob->GetBufferSize(), nullptr, &state->scaleXVertex)) ||
        FAILED(device->CreatePixelShader(
            scaleXBytecode.data(), static_cast<SIZE_T>(scaleXBytecode.size()), nullptr, &state->scaleXPixel)))
    {
        error_ = "NGU class-3 expansion or scale-X shaders could not be created";
        return false;
    }
    const float scaleXConstants[8] = {
        float(chromaWidth * 2), float(chromaWidth), 0.125f, 0.25f, float(chromaWidth), float(chromaHeight), 0.0f, 0.0f};
    D3D11_BUFFER_DESC scaleXConstantDesc = {};
    scaleXConstantDesc.ByteWidth = sizeof(scaleXConstants);
    scaleXConstantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    scaleXConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA scaleXConstantData = {};
    scaleXConstantData.pSysMem = scaleXConstants;
    if (FAILED(device->CreateBuffer(&scaleXConstantDesc, &scaleXConstantData, &state->scaleXConstants)))
    {
        error_ = "NGU class-3 scale-X constants could not be created";
        return false;
    }
    D3D11_TEXTURE2D_DESC scaleXTableDesc = {};
    scaleXTableDesc.Width = 4;
    scaleXTableDesc.Height = UINT(chromaWidth);
    scaleXTableDesc.MipLevels = 1;
    scaleXTableDesc.ArraySize = 1;
    scaleXTableDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    scaleXTableDesc.SampleDesc.Count = 1;
    scaleXTableDesc.Usage = D3D11_USAGE_IMMUTABLE;
    scaleXTableDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA scaleXTableData = {};
    scaleXTableData.pSysMem = scaleXTable.data();
    scaleXTableData.SysMemPitch = 4 * 8;
    if (FAILED(device->CreateTexture2D(&scaleXTableDesc, &scaleXTableData, &state->scaleXTable)) ||
        FAILED(device->CreateShaderResourceView(state->scaleXTable.Get(), nullptr, &state->scaleXTableView)))
    {
        error_ = "NGU class-3 scale-X table could not be created";
        return false;
    }
    D3D11_SAMPLER_DESC scaleXSamplerDesc = {};
    scaleXSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    scaleXSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    scaleXSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    scaleXSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    scaleXSamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&scaleXSamplerDesc, &state->pointSampler)))
    {
        error_ = "NGU class-3 scale-X sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC scaleXRasterizerDesc = {};
    scaleXRasterizerDesc.FillMode = D3D11_FILL_SOLID;
    scaleXRasterizerDesc.CullMode = D3D11_CULL_NONE;
    scaleXRasterizerDesc.ScissorEnable = FALSE;
    scaleXRasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&scaleXRasterizerDesc, &state->scaleXRasterizer)))
    {
        error_ = "NGU class-3 scale-X rasterizer could not be created";
        return false;
    }
    auto create = [&](UINT width,
                      UINT height,
                      DXGI_FORMAT format,
                      ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& view,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16_UNORM,
                state->cbTexture,
                state->cbView,
                state->cbShaderView) ||
        !create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16_UNORM,
                state->crTexture,
                state->crView,
                state->crShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16_UNORM,
                state->cbScaleXTexture,
                state->cbScaleXView,
                state->cbScaleXShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight * 2),
                DXGI_FORMAT_R16_UNORM,
                state->cbScaleYTexture,
                state->cbScaleYView,
                state->cbScaleYShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16_UNORM,
                state->crScaleXTexture,
                state->crScaleXView,
                state->crScaleXShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight * 2),
                DXGI_FORMAT_R16_UNORM,
                state->crScaleYTexture,
                state->crScaleYView,
                state->crScaleYShaderView) ||
        !create(UINT(chromaWidth + 2),
                UINT(chromaHeight + 2),
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                state->denseCbTexture,
                state->denseCbView,
                state->denseCbShaderView) ||
        !create(UINT(chromaWidth + 2),
                UINT(chromaHeight + 2),
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                state->denseCrTexture,
                state->denseCrView,
                state->denseCrShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight * 2),
                DXGI_FORMAT_R16_UNORM,
                state->combine1Texture,
                state->combine1View,
                state->combine1ShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight * 2),
                DXGI_FORMAT_R16G16_UNORM,
                state->combine2Texture,
                state->combine2View,
                state->combine2ShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight * 2),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->realignTexture,
                state->realignView,
                state->realignShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->rgbTexture,
                state->rgbView,
                state->rgbShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->yExpandedTexture,
                state->yExpandedView,
                state->yExpandedShaderView) ||
        !create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->downXTexture,
                state->downXView,
                state->downXShaderView) ||
        !create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->firstDownXTexture,
                state->firstDownXView,
                state->firstDownXShaderView) ||
        !create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16_FLOAT,
                state->diffTexture,
                state->diffView,
                state->diffShaderView) ||
        !create(UINT(chromaWidth),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16_FLOAT,
                state->firstDiffTexture,
                state->firstDiffView,
                state->firstDiffShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->mainTexture,
                state->mainView,
                state->mainShaderView) ||
        !create(UINT(chromaWidth * 2),
                UINT(chromaHeight),
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->ps4576Texture,
                state->ps4576View,
                state->ps4576ShaderView))
    {
        error_ = "NGU class-3 core surfaces could not be created";
        return false;
    }

    stateRef = std::move(state);
    cached_.quality = quality;
    cached_.stage = NguScalerStage::VeryHighChromaCoreClass3_720;
    cached_.sourceWidth = UINT(chromaWidth);
    cached_.sourceHeight = UINT(chromaHeight);
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaCoreClass3_720(pl_gpu gpu, NguQuality quality)
{
    return PrepareNativeChromaCoreClass3(gpu, 640, 720, quality, nativeChromaCoreClass3_720_, "class-3 720p");
}

bool NguScaler::PrepareNativeChromaCoreClass3_1080(pl_gpu gpu, NguQuality quality)
{
    return PrepareNativeChromaCoreClass3(gpu, 960, 1080, quality, nativeChromaCoreClass3_1080_, "class-3 1080p");
}

bool NguScaler::RunNativeChromaCoreClass3(ID3D11ShaderResourceView* packedUv,
                                          ID3D11ShaderResourceView* yInput,
                                          ID3D11RenderTargetView* ps4576Output,
                                          int chromaWidth,
                                          int chromaHeight,
                                          NativeChromaCoreClass3_720& state)
{
    const bool is1080 = chromaWidth == 960 && chromaHeight == 1080;
    auto* veryHighState = is1080 ? nativeChromaVeryHighClass3_1080_.get() : nativeChromaVeryHighClass3_720_.get();
    if (!veryHighState || !packedUv || !yInput || !ps4576Output ||
        !ViewMatchesTexture(packedUv, DXGI_FORMAT_R16G16_UNORM, chromaWidth, chromaHeight) ||
        (!ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, chromaWidth * 2, chromaHeight) &&
         !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, chromaWidth * 2, chromaHeight)) ||
        !ViewMatchesTexture(ps4576Output, DXGI_FORMAT_R16G16B16A16_UNORM, chromaWidth * 2, chromaHeight))
    {
        error_ = "NGU class-3 core request is invalid";
        return false;
    }
    auto runExpandY = [&](ID3D11ShaderResourceView* input)
    {
        ID3D11RenderTargetView* output = state.yExpandedView.Get();
        DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                      state.expandVertex.Get(),
                      state.expandPixel.Get(),
                      nullptr,
                      state.scaleXRasterizer.Get(),
                      &input,
                      1,
                      &output,
                      1,
                      UINT(chromaWidth * 2),
                      UINT(chromaHeight),
                      state.pointSampler.Get());
    };
    auto runScaleX = [&](ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
    {
        ID3D11ShaderResourceView* inputs[2] = {input, state.scaleXTableView.Get()};
        DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                      state.scaleXVertex.Get(),
                      state.scaleXPixel.Get(),
                      state.scaleXConstants.Get(),
                      state.scaleXRasterizer.Get(),
                      inputs,
                      2,
                      &output,
                      1,
                      UINT(chromaWidth),
                      UINT(chromaHeight),
                      state.pointSampler.Get());
    };
    auto runSuperResMain = [&](ID3D11ShaderResourceView* original,
                               ID3D11ShaderResourceView* work,
                               ID3D11ShaderResourceView* difference,
                               ID3D11RenderTargetView* output)
    {
        ID3D11ShaderResourceView* inputs[4] = {
            original,
            work,
            difference,
            (is1080 ? nativeSuperRes_->uvLutView1080.Get() : nativeSuperRes_->uvLutView.Get())};
        DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                      nativeSuperRes_->vertex.Get(),
                      (is1080 ? nativeSuperRes_->toRgbMain1080.Get() : nativeSuperRes_->toRgbMain.Get()),
                      (is1080 ? nativeSuperRes_->toRgbMainConstantsClass3_1080.Get()
                              : nativeSuperRes_->toRgbMainConstantsClass3.Get()),
                      nativeSuperRes_->rasterizer.Get(),
                      inputs,
                      4,
                      &output,
                      1,
                      UINT(chromaWidth * 2),
                      UINT(chromaHeight),
                      nativeSuperRes_->linearSampler.Get(),
                      nullptr,
                      nullptr,
                      nativeSuperRes_->pointSampler.Get(),
                      nativeSuperRes_->pointSampler.Get());
    };
    auto runSuperResBase = [&](ID3D11ShaderResourceView* original,
                               ID3D11ShaderResourceView* work,
                               ID3D11ShaderResourceView* difference,
                               ID3D11RenderTargetView* output)
    {
        ID3D11ShaderResourceView* inputs[4] = {
            original,
            work,
            difference,
            (is1080 ? nativeSuperRes_->uvLutView1080.Get() : nativeSuperRes_->uvLutView.Get())};
        DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                      nativeSuperRes_->vertex.Get(),
                      (is1080 ? nativeSuperRes_->toRgbBase1080.Get() : nativeSuperRes_->toRgbBase.Get()),
                      (is1080 ? nativeSuperRes_->toRgbBaseConstantsClass3P010_1080.Get()
                              : nativeSuperRes_->toRgbBaseConstantsClass3P010.Get()),
                      nativeSuperRes_->rasterizer.Get(),
                      inputs,
                      4,
                      &output,
                      1,
                      UINT(chromaWidth * 2),
                      UINT(chromaHeight),
                      nativeSuperRes_->linearSampler.Get(),
                      nullptr,
                      nullptr,
                      nativeSuperRes_->pointSampler.Get(),
                      nativeSuperRes_->pointSampler.Get());
    };
    runExpandY(yInput);
    if (!RunNativeChromaSplit(packedUv, chromaWidth, chromaHeight, state.cbView.Get(), state.crView.Get()) ||
        !RunNativeChromaVeryHigh(
            state.cbShaderView.Get(), state.denseCbView.Get(), chromaWidth, chromaHeight, *veryHighState) ||
        !RunNativeChromaScaleX(state.cbShaderView.Get(), state.cbScaleXView.Get()) ||
        !RunNativeChromaScaleY(state.cbScaleXShaderView.Get(), state.cbScaleYView.Get()) ||
        !RunNativeChromaCombine1(
            state.cbScaleYShaderView.Get(), state.denseCbShaderView.Get(), state.combine1View.Get()) ||
        !RunNativeChromaVeryHigh(
            state.crShaderView.Get(), state.denseCrView.Get(), chromaWidth, chromaHeight, *veryHighState) ||
        !RunNativeChromaScaleX(state.crShaderView.Get(), state.crScaleXView.Get()) ||
        !RunNativeChromaScaleY(state.crScaleXShaderView.Get(), state.crScaleYView.Get()) ||
        !RunNativeChromaCombine2(state.crScaleYShaderView.Get(),
                                 state.combine1ShaderView.Get(),
                                 state.denseCrShaderView.Get(),
                                 state.combine2View.Get()) ||
        !RunNativeChromaRealign(state.combine2ShaderView.Get(), state.realignView.Get()) ||
        !RunNativeChromaYCbCr(yInput, state.realignShaderView.Get(), state.rgbView.Get()))
        return false;
    runScaleX(state.rgbShaderView.Get(), state.firstDownXView.Get());
    if (!(is1080
              ? RunNativeSuperResDiffClass3_1080(packedUv, state.firstDownXShaderView.Get(), state.firstDiffView.Get())
              : RunNativeSuperResDiffClass3_720(packedUv, state.firstDownXShaderView.Get(), state.firstDiffView.Get())))
        return false;
    runSuperResMain(state.yExpandedShaderView.Get(),
                    state.rgbShaderView.Get(),
                    state.firstDiffShaderView.Get(),
                    state.mainView.Get());
    runScaleX(state.mainShaderView.Get(), state.downXView.Get());
    if (!(is1080 ? RunNativeSuperResDiffClass3_1080(packedUv, state.downXShaderView.Get(), state.diffView.Get())
                 : RunNativeSuperResDiffClass3_720(packedUv, state.downXShaderView.Get(), state.diffView.Get())))
        return false;
    runSuperResBase(
        state.yExpandedShaderView.Get(), state.mainShaderView.Get(), state.diffShaderView.Get(), ps4576Output);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCoreClass3_720(ID3D11ShaderResourceView* packedUv,
                                              ID3D11ShaderResourceView* yInput,
                                              ID3D11RenderTargetView* ps4576Output)
{
    if (!nativeChromaCoreClass3_720_)
    {
        error_ = "NGU class-3 720p core is not prepared";
        return false;
    }
    return RunNativeChromaCoreClass3(packedUv, yInput, ps4576Output, 640, 720, *nativeChromaCoreClass3_720_);
}

bool NguScaler::RunNativeChromaCoreClass3_1080(ID3D11ShaderResourceView* packedUv,
                                               ID3D11ShaderResourceView* yInput,
                                               ID3D11RenderTargetView* ps4576Output)
{
    if (!nativeChromaCoreClass3_1080_)
    {
        error_ = "NGU class-3 1080p core is not prepared";
        return false;
    }
    return RunNativeChromaCoreClass3(packedUv, yInput, ps4576Output, 960, 1080, *nativeChromaCoreClass3_1080_);
}

bool NguScaler::PrepareNativeNguClass3_720(pl_gpu gpu, NguQuality quality)
{
    if (!gpu)
    {
        error_ = "NGU class-3 combined path requires a GPU";
        return false;
    }
    if (nativeNguClass3_720_ && gpu == gpu_ && nativeNguClass3_720_->quality == quality)
        return true;
    if (!PrepareNativeChromaCoreClass3_720(gpu, quality) || !PrepareNativeLuma4x720Full(gpu))
        return false;
    auto state = std::make_unique<NativeNguFromPlanesResources>();
    state->width = 1280;
    state->height = 720;
    state->quality = quality;
    nativeNguClass3_720_ = std::move(state);
    cached_.quality = quality;
    cached_.stage = NguScalerStage::VeryHighNguClass3_720;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNguClass3_720(ID3D11ShaderResourceView* packedUv,
                                       ID3D11ShaderResourceView* yInput,
                                       ID3D11RenderTargetView* finalOutput)
{
    if (!nativeNguClass3_720_ || !nativeChromaCoreClass3_720_ || !finalOutput ||
        !ViewMatchesRgb8(finalOutput, 3840, 2160))
    {
        error_ = "NGU combined class-3 request is invalid";
        return false;
    }
    if (!RunNativeChromaCoreClass3_720(packedUv, yInput, nativeChromaCoreClass3_720_->ps4576View.Get()) ||
        !RunNativeLuma4x720FromPacked(nativeChromaCoreClass3_720_->ps4576ShaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeNguClass3_1080(pl_gpu gpu, NguQuality quality)
{
    if (!gpu)
    {
        error_ = "NGU 1080p class-3 combined path requires a GPU";
        return false;
    }
    if (nativeNguClass3_1080_ && gpu == gpu_ && nativeNguClass3_1080_->quality == quality)
        return true;
    if (!PrepareNativeChromaCoreClass3_1080(gpu, quality) || !PrepareNativeLumaVeryHigh1080(gpu))
        return false;
    auto state = std::make_unique<NativeNguFromPlanesResources>();
    state->width = 1920;
    state->height = 1080;
    state->quality = quality;
    nativeNguClass3_1080_ = std::move(state);
    cached_.quality = quality;
    cached_.stage = NguScalerStage::VeryHighNguClass3_1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNguClass3_1080(ID3D11ShaderResourceView* packedUv,
                                        ID3D11ShaderResourceView* yInput,
                                        ID3D11RenderTargetView* finalOutput)
{
    if (!nativeNguClass3_1080_ || !nativeChromaCoreClass3_1080_ || !finalOutput ||
        !ViewMatchesRgb8(finalOutput, 3840, 2160))
    {
        error_ = "NGU 1080p combined class-3 request is invalid";
        return false;
    }
    if (!RunNativeChromaCoreClass3_1080(packedUv, yInput, nativeChromaCoreClass3_1080_->ps4576View.Get()) ||
        !RunNativeLumaVeryHigh1080(nativeChromaCoreClass3_1080_->ps4576ShaderView.Get(), 1920, 1080, finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeChromaCoreClass3Stage720(int stage, ID3D11RenderTargetView* output)
{
    if (!nativeChromaCoreClass3_720_ || !output || stage < 0 || stage > 13)
    {
        error_ = "NGU class-3 core stage copy request is invalid";
        return false;
    }
    auto& state = *nativeChromaCoreClass3_720_;
    ID3D11Texture2D* source = nullptr;
    switch (stage)
    {
    case 0:
        source = state.cbTexture.Get();
        break;
    case 1:
        source = state.denseCbTexture.Get();
        break;
    case 2:
        source = state.cbScaleYTexture.Get();
        break;
    case 3:
        source = state.combine1Texture.Get();
        break;
    case 4:
        source = state.denseCrTexture.Get();
        break;
    case 5:
        source = state.combine2Texture.Get();
        break;
    case 6:
        source = state.realignTexture.Get();
        break;
    case 7:
        source = state.rgbTexture.Get();
        break;
    case 8:
        source = state.firstDownXTexture.Get();
        break;
    case 9:
        source = state.firstDiffTexture.Get();
        break;
    case 10:
        source = state.mainTexture.Get();
        break;
    case 11:
        source = state.ps4576Texture.Get();
        break;
    case 12:
        source = state.cbScaleXTexture.Get();
        break;
    case 13:
        source = state.yExpandedTexture.Get();
        break;
    }
    if (!CopyTextureToTarget(gpu_, source, output))
    {
        error_ = "NGU class-3 core stage copy failed";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeChromaCoreClass3Stage1080(int stage, ID3D11RenderTargetView* output)
{
    if (!nativeChromaCoreClass3_1080_ || !output || stage < 0 || stage > 13)
    {
        error_ = "NGU 1080p class-3 core stage copy request is invalid";
        return false;
    }
    auto& state = *nativeChromaCoreClass3_1080_;
    ID3D11Texture2D* source = nullptr;
    switch (stage)
    {
    case 0:
        source = state.cbTexture.Get();
        break;
    case 1:
        source = state.denseCbTexture.Get();
        break;
    case 2:
        source = state.cbScaleYTexture.Get();
        break;
    case 3:
        source = state.combine1Texture.Get();
        break;
    case 4:
        source = state.denseCrTexture.Get();
        break;
    case 5:
        source = state.combine2Texture.Get();
        break;
    case 6:
        source = state.realignTexture.Get();
        break;
    case 7:
        source = state.rgbTexture.Get();
        break;
    case 8:
        source = state.firstDownXTexture.Get();
        break;
    case 9:
        source = state.firstDiffTexture.Get();
        break;
    case 10:
        source = state.mainTexture.Get();
        break;
    case 11:
        source = state.ps4576Texture.Get();
        break;
    case 12:
        source = state.cbScaleXTexture.Get();
        break;
    case 13:
        source = state.yExpandedTexture.Get();
        break;
    }
    if (!CopyTextureToTarget(gpu_, source, output))
    {
        error_ = "NGU 1080p class-3 core stage copy failed";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeNguFromPlanes(pl_gpu gpu,
                                           int width,
                                           int height,
                                           NguQuality quality,
                                           std::unique_ptr<NativeNguFromPlanesResources>& state,
                                           const char* geometry,
                                           bool includeFullLuma)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU full plane entry requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) && state->height == UINT(height) &&
        state->quality == quality)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    const bool is1080 = width == 1920 && height == 1080;
    if (!PrepareNativeChromaFull(
            gpu,
            width / 2,
            height / 2,
            quality,
            is1080 ? nativeChromaVeryHigh1080Full_ : nativeChromaVeryHigh720Full_,
            geometry) ||
        (includeFullLuma && (is1080 ? !PrepareNativeLumaVeryHigh1080(gpu) : !PrepareNativeLuma4x720Full(gpu))))
        return false;
    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto next = std::make_unique<NativeNguFromPlanesResources>();
    next->width = UINT(width);
    next->height = UINT(height);
    next->quality = quality;
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_l8_expand.hlsl", 16 * 1024);
    const auto expandBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_l8_expand.cso", 0);
    if (shaderText.empty() || expandBytecode.empty())
    {
        error_ = std::string("NGU ") + geometry + " L8 expansion assets are unavailable";
        return false;
    }
    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_L8Expand",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr) ||
        FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &next->vertex)) ||
        FAILED(device->CreatePixelShader(
            expandBytecode.data(), static_cast<SIZE_T>(expandBytecode.size()), nullptr, &next->expandPixel)))
    {
        error_ = std::string("NGU ") + geometry + " L8 expansion shaders could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &next->rasterizer)))
    {
        error_ = std::string("NGU ") + geometry + " L8 expansion rasterizer could not be created";
        return false;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = next->width;
    desc.Height = next->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &next->yTexture)) ||
        FAILED(device->CreateRenderTargetView(next->yTexture.Get(), nullptr, &next->yView)) ||
        FAILED(device->CreateShaderResourceView(next->yTexture.Get(), nullptr, &next->yShaderView)))
    {
        error_ = std::string("NGU ") + geometry + " Y surface could not be created";
        return false;
    }
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &next->chromaTexture)) ||
        FAILED(device->CreateRenderTargetView(next->chromaTexture.Get(), nullptr, &next->chromaView)) ||
        FAILED(device->CreateShaderResourceView(next->chromaTexture.Get(), nullptr, &next->chromaShaderView)))
    {
        error_ = std::string("NGU ") + geometry + " chroma surface could not be created";
        return false;
    }
    state = std::move(next);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNguFromPlanes(ID3D11ShaderResourceView* packedUv,
                                       ID3D11ShaderResourceView* yInput,
                                       ID3D11RenderTargetView* finalOutput,
                                       int width,
                                       int height,
                                       bool is1080,
                                       bool p010,
                                       NativeNguFromPlanesResources& state)
{
    if (state.width != UINT(width) || state.height != UINT(height) || !packedUv || !yInput || !finalOutput ||
        (!ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, UINT(width), UINT(height)) &&
         !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, UINT(width), UINT(height))) ||
        !ViewMatchesRgb8(finalOutput, 3840, 2160))
    {
        error_ = "NGU full 720p plane request is invalid";
        return false;
    }
    ID3D11RenderTargetView* yTarget = state.yView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.expandPixel.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &yInput,
                  1,
                  &yTarget,
                  1,
                  UINT(width),
                  UINT(height));
    const bool chromaReady =
        is1080 ? RunNativeChromaVeryHigh1080Full(packedUv,
                                                yInput,
                                                packedUv,
                                                state.yShaderView.Get(),
                                                state.yShaderView.Get(),
                                                state.chromaView.Get(),
                                                p010)
               : RunNativeChromaVeryHigh720Full(packedUv,
                                                yInput,
                                                packedUv,
                                                state.yShaderView.Get(),
                                                state.yShaderView.Get(),
                                                state.chromaView.Get(),
                                                p010);
    if (!chromaReady)
        return false;
    if (is1080 ? !RunNativeLumaVeryHigh1080(state.chromaShaderView.Get(), UINT(width), UINT(height), finalOutput)
               : !RunNativeLuma4x720Full(state.chromaShaderView.Get(), UINT(width), UINT(height), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNguHdrChromaOnlyFromPlanes(ID3D11ShaderResourceView* packedUv,
                                                    ID3D11ShaderResourceView* yInput,
                                                    ID3D11RenderTargetView* output,
                                                    int width,
                                                    int height,
                                                    NativeNguFromPlanesResources& state)
{
    const bool is1080 = width == 1920 && height == 1080;
    const UINT chromaWidth = UINT(width / 2);
    const UINT chromaHeight = UINT(height / 2);
    const bool packedIsRgba16 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_R16G16B16A16_UNORM, chromaWidth, chromaHeight);
    const bool packedIsRg16 =
        packedUv && ViewMatchesTexture(packedUv, DXGI_FORMAT_R16G16_UNORM, chromaWidth, chromaHeight);
    if (!hdr_ || state.width != UINT(width) || state.height != UINT(height) || !packedUv || !yInput || !output ||
        (!packedIsRgba16 && !packedIsRg16) ||
        !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, UINT(width), UINT(height)) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, UINT(width), UINT(height)))
    {
        error_ = "NGU HDR chroma-only request is invalid";
        return false;
    }

    ID3D11RenderTargetView* yTarget = state.yView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.expandPixel.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &yInput,
                  1,
                  &yTarget,
                  1,
                  UINT(width),
                  UINT(height));
    if (is1080)
    {
        if (!RunNativeChromaVeryHigh1080Full(
                packedUv, yInput, packedUv, state.yShaderView.Get(), state.yShaderView.Get(), output, true))
            return false;
    }
    else if (!RunNativeChromaVeryHigh720Full(
                 packedUv, yInput, packedUv, state.yShaderView.Get(), state.yShaderView.Get(), output, true))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeNgu720FromPlanes(pl_gpu gpu, NguQuality quality)
{
    return PrepareNativeNguFromPlanes(gpu, 1280, 720, quality, nativeNgu720FromPlanes_, "720p");
}

bool NguScaler::RunNativeNgu720FromPlanes(ID3D11ShaderResourceView* packedUv,
                                          ID3D11ShaderResourceView* yInput,
                                          ID3D11RenderTargetView* finalOutput,
                                          bool p010)
{
    if (!nativeNgu720FromPlanes_)
    {
        error_ = "NGU 720p full plane scheduler is not prepared";
        return false;
    }
    return RunNativeNguFromPlanes(packedUv, yInput, finalOutput, 1280, 720, false, p010, *nativeNgu720FromPlanes_);
}

bool NguScaler::PrepareNativeNgu720DoubleFromPlanes(pl_gpu gpu, NguQuality quality)
{
    if (!PrepareNativeNgu720FromPlanes(gpu, quality) || !PrepareNativeLuma720Double(gpu) ||
        !PrepareNativeLuma720DoubleHighBit(gpu))
        return false;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma720Double;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNgu720DoubleFromPlanes(ID3D11ShaderResourceView* packedUv,
                                                ID3D11ShaderResourceView* yInput,
                                                ID3D11RenderTargetView* finalOutput,
                                                bool p010)
{
    const bool preFinalRgb16 =
        finalOutput && ViewMatchesTexture(finalOutput, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440);
    if (!nativeNgu720FromPlanes_ || !packedUv || !yInput || !finalOutput ||
        (!ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1280, 720) &&
         !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1280, 720)) ||
        (!preFinalRgb16 && !ViewMatchesRgb8(finalOutput, 2560, 1440)) || (preFinalRgb16 && !p010))
    {
        error_ = "NGU 720 Double plane request is invalid";
        return false;
    }
    auto& state = *nativeNgu720FromPlanes_;
    ID3D11RenderTargetView* yTarget = state.yView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.expandPixel.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &yInput,
                  1,
                  &yTarget,
                  1,
                  1280,
                  720);
    if (!RunNativeChromaVeryHigh720Full(packedUv,
                                        yInput,
                                        packedUv,
                                        state.yShaderView.Get(),
                                        state.yShaderView.Get(),
                                        state.chromaView.Get(),
                                        p010) ||
        (p010 ? !RunNativeLuma720DoubleHighBit(state.chromaShaderView.Get(), finalOutput)
              : !RunNativeLuma720Double(state.chromaShaderView.Get(), 1280, 720, finalOutput)))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeNgu1080FromPlanes(pl_gpu gpu, NguQuality quality)
{
    return PrepareNativeNguFromPlanes(gpu, 1920, 1080, quality, nativeNgu1080FromPlanes_, "1080p");
}

bool NguScaler::RunNativeNgu1080FromPlanes(ID3D11ShaderResourceView* packedUv,
                                           ID3D11ShaderResourceView* yInput,
                                           ID3D11RenderTargetView* finalOutput,
                                           bool p010)
{
    if (!nativeNgu1080FromPlanes_)
    {
        error_ = "NGU 1080p full plane scheduler is not prepared";
        return false;
    }
    return RunNativeNguFromPlanes(packedUv, yInput, finalOutput, 1920, 1080, true, p010, *nativeNgu1080FromPlanes_);
}

bool NguScaler::RunNativeNgu1080ChromaOnlyFromPlanes(ID3D11ShaderResourceView* packedUv,
                                                     ID3D11ShaderResourceView* yInput,
                                                     ID3D11RenderTargetView* finalOutput,
                                                     bool p010)
{
    if (!nativeNgu1080FromPlanes_ || !packedUv || !yInput || !finalOutput ||
        (!ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1920, 1080) &&
         !ViewMatchesTexture(yInput, DXGI_FORMAT_R16_UNORM, 1920, 1080)) ||
        !ViewMatchesRgb8(finalOutput, 1920, 1080))
    {
        error_ = "NGU 1080 chroma-only request is invalid";
        return false;
    }
    auto& state = *nativeNgu1080FromPlanes_;
    ID3D11RenderTargetView* yTarget = state.yView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.expandPixel.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &yInput,
                  1,
                  &yTarget,
                  1,
                  1920,
                  1080);
    if (!RunNativeChromaVeryHigh1080Full(packedUv,
                                         yInput,
                                         packedUv,
                                         state.yShaderView.Get(),
                                         state.yShaderView.Get(),
                                         state.chromaView.Get(),
                                         p010) ||
        !RunNativeLumaFinal1080(state.chromaShaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

NguTargetPlan NguScaler::PlanNativeVeryHigh(const NguSourceFrame& source, int targetWidth, int targetHeight) const
{
    NguTargetPlan plan;
    plan.hdr = pl_color_space_is_hdr(&source.color);
    if (plan.hdr)
    {
        // E-160 [observed]: both captured HDR NGU graphs stop at PS_4576
        // with one output pixel per source pixel. Franky owns every scaling
        // and HDR presentation operation after that boundary.
        plan.outputWidth = source.width;
        plan.outputHeight = source.height;
        plan.outputHook = false;
        return plan;
    }
    plan.noLuma = source.width == 1920 && source.height == 1080 && targetWidth == 1920 && targetHeight == 1080;
    plan.double720 = source.width == 1280 && source.height == 720 &&
                     ((targetWidth == 1920 && targetHeight == 1080) || (targetWidth == 2560 && targetHeight == 1440));
    plan.outputWidth = plan.noLuma ? 1920 : plan.double720 ? 2560 : 3840;
    plan.outputHeight = plan.noLuma ? 1080 : plan.double720 ? 1440 : 2160;
    // E-170: the correlated P016 Double source reaches an exact 2560x1440
    // RGBA16 PS_908 composition but selects the target presentation shader
    // PS_1368. Hand the evidence-backed pre-presentation surface to Franky.
    plan.preFinalRgb16 = plan.double720 && source.chromaLayout == NguChromaLayout::SemiPlanar420 &&
                         source.lumaBits == 16 && source.chromaBits == 16;
    const bool class3 =
        source.chromaLayout == NguChromaLayout::SemiPlanar422 || source.chromaLayout == NguChromaLayout::Planar422;
    plan.outputHook =
        !plan.preFinalRgb16 && (class3 || (targetWidth == plan.outputWidth && targetHeight == plan.outputHeight));
    return plan;
}

bool NguScaler::PrepareNativeVeryHigh(pl_gpu gpu, const NguSourceFrame& source, const NguTargetPlan& plan)
{
    hdr_ = plan.hdr;
    colorDomain_ = ColorDomainProfile::Sdr;
    if (source.lumaQuality != NguQuality::VeryHigh)
    {
        error_ = "NGU luma quality other than VeryHigh is not implemented";
        return false;
    }
    if (hdr_)
    {
        // E-171 [observed]: these are the only target-captured HDR
        // source-domain profiles. All other transfer/primaries pairs fail
        // closed rather than selecting SDR or a guessed profile.
        if (source.color.transfer == PL_COLOR_TRC_PQ && source.color.primaries == PL_COLOR_PRIM_BT_2020)
            colorDomain_ = ColorDomainProfile::PqBt2020;
        else if (source.color.transfer == PL_COLOR_TRC_HLG && source.color.primaries == PL_COLOR_PRIM_BT_2020)
            colorDomain_ = ColorDomainProfile::HlgBt2020;
        else if (source.color.transfer == PL_COLOR_TRC_PQ && source.color.primaries == PL_COLOR_PRIM_BT_709)
            colorDomain_ = ColorDomainProfile::PqBt709;
        else
        {
            error_ = "ngu_sharp HDR color domain is not target-observed";
            return false;
        }
    }
    if (!gpu || source.width <= 0 || source.height <= 0 || source.lumaBits <= 0 || source.chromaBits <= 0 ||
        !source.topDown)
    {
        error_ = "NGU canonical source request is invalid";
        return false;
    }
    const bool hdrGeometry =
        (source.width == 1280 && source.height == 720) || (source.width == 1920 && source.height == 1080);
    const bool matrixMatchesColorDomain =
        (colorDomain_ == ColorDomainProfile::PqBt2020 || colorDomain_ == ColorDomainProfile::HlgBt2020)
            ? (source.repr.sys == PL_COLOR_SYSTEM_BT_2020_NC || source.repr.sys == PL_COLOR_SYSTEM_BT_2020_C)
            : colorDomain_ == ColorDomainProfile::PqBt709 && source.repr.sys == PL_COLOR_SYSTEM_BT_709;
    if (hdr_ && !(hdrGeometry && plan.outputWidth == source.width && plan.outputHeight == source.height &&
                  source.chromaLayout == NguChromaLayout::SemiPlanar420 && source.lumaBits == 10 &&
                  source.chromaBits == 10 && !source.fullRange && matrixMatchesColorDomain))
    {
        error_ = "ngu_sharp HDR requires an observed limited-range P010 720p or 1080p color domain";
        return false;
    }
    switch (source.chromaLayout)
    {
    case NguChromaLayout::None:
        if (source.width == 1280 && source.height == 720)
            return PrepareNativeLuma4x720Full(gpu);
        if (source.width == 1920 && source.height == 1080)
            return PrepareNativeLumaVeryHigh1080(gpu);
        break;
    case NguChromaLayout::SemiPlanar420:
        if (!PrepareNativeDecoderSource(gpu))
            return false;
        if (hdr_)
        {
            if (source.width == 1280 && source.height == 720)
                return PrepareNativeNguFromPlanes(
                    gpu, 1280, 720, source.chromaQuality, nativeNgu720FromPlanes_, "HDR 720p", false);
            if (source.width == 1920 && source.height == 1080)
                return PrepareNativeNguFromPlanes(
                    gpu, 1920, 1080, source.chromaQuality, nativeNgu1080FromPlanes_, "HDR 1080p", false);
        }
        if (source.width == 1280 && source.height == 720)
        {
            return plan.double720 ? PrepareNativeNgu720DoubleFromPlanes(gpu, source.chromaQuality)
                                  : PrepareNativeNgu720FromPlanes(gpu, source.chromaQuality);
        }
        if (source.width == 1920 && source.height == 1080)
            return PrepareNativeNgu1080FromPlanes(gpu, source.chromaQuality);
        break;
    case NguChromaLayout::SemiPlanar422:
        if (source.width == 1280 && source.height == 720)
            return PrepareNativeNguClass3_720(gpu, source.chromaQuality);
        if (source.width == 1920 && source.height == 1080)
            return PrepareNativeNguClass3_1080(gpu, source.chromaQuality);
        break;
    case NguChromaLayout::Planar420:
        error_ = "NGU canonical Planar420 input requires the common I420 interleave adapter";
        return false;
    case NguChromaLayout::Planar422:
        error_ = "NGU canonical Planar422 input requires the common 4:2:2 interleave adapter";
        return false;
    }
    error_ = "NGU canonical source geometry or chroma layout is unsupported";
    return false;
}

bool NguScaler::RunNativeVeryHigh(const NguSourceFrame& source,
                                  const NguTargetPlan& plan,
                                  ID3D11RenderTargetView* finalOutput)
{
    DrawCountScope drawCount;
    if (!finalOutput || plan.outputWidth <= 0 || plan.outputHeight <= 0 || source.width <= 0 || source.height <= 0 ||
        !source.topDown || source.lumaBits <= 0 || source.chromaBits <= 0)
    {
        error_ = "NGU canonical run request is invalid";
        return false;
    }
    const bool highBitDepth = source.lumaBits > 8 || source.chromaBits > 8;
    switch (source.chromaLayout)
    {
    case NguChromaLayout::None:
        if (!source.rgb || plan.noLuma || plan.double720)
        {
            error_ = "NGU canonical RGB input requires the observed luma-only geometry";
            return false;
        }
        if (source.width == 1280 && source.height == 720)
            return RunNativeLuma4x720Full(source.rgb, 1280, 720, finalOutput);
        if (source.width == 1920 && source.height == 1080)
            return RunNativeLumaVeryHigh1080(source.rgb, 1920, 1080, finalOutput);
        break;
    case NguChromaLayout::SemiPlanar420:
        if (!source.luma || !source.chroma)
        {
            error_ = "NGU canonical 4:2:0 planes are unavailable";
            return false;
        }
        if (plan.hdr)
        {
            if (source.width == 1280 && source.height == 720 && nativeNgu720FromPlanes_)
            {
                return RunNativeNguHdrChromaOnlyFromPlanes(
                    source.chroma, source.luma, finalOutput, 1280, 720, *nativeNgu720FromPlanes_);
            }
            if (source.width == 1920 && source.height == 1080 && nativeNgu1080FromPlanes_)
            {
                return RunNativeNguHdrChromaOnlyFromPlanes(
                    source.chroma, source.luma, finalOutput, 1920, 1080, *nativeNgu1080FromPlanes_);
            }
            error_ = "NGU HDR chroma-only resource is unavailable";
            return false;
        }
        if (plan.noLuma)
            return RunNativeNgu1080ChromaOnlyFromPlanes(source.chroma, source.luma, finalOutput, highBitDepth);
        if (source.width == 1280 && source.height == 720)
            return plan.double720
                       ? RunNativeNgu720DoubleFromPlanes(source.chroma, source.luma, finalOutput, highBitDepth)
                       : RunNativeNgu720FromPlanes(source.chroma, source.luma, finalOutput, highBitDepth);
        if (source.width == 1920 && source.height == 1080)
            return RunNativeNgu1080FromPlanes(source.chroma, source.luma, finalOutput, highBitDepth);
        break;
    case NguChromaLayout::SemiPlanar422:
        if (!source.luma || !source.chroma)
        {
            error_ = "NGU canonical class-3 planes are unavailable";
            return false;
        }
        if (source.width == 1280 && source.height == 720)
            return RunNativeNguClass3_720(source.chroma, source.luma, finalOutput);
        if (source.width == 1920 && source.height == 1080)
            return RunNativeNguClass3_1080(source.chroma, source.luma, finalOutput);
        break;
    case NguChromaLayout::Planar420:
    case NguChromaLayout::Planar422:
        error_ = "NGU canonical planar chroma must be interleaved by the common adapter";
        return false;
    }
    error_ = "NGU canonical source geometry or chroma layout is unsupported";
    return false;
}

bool NguScaler::PrepareNativeLumaInitial(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native scaler requires a GPU";
        return false;
    }
    if (nativeLumaInitial_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native scaler requires the D3D11 backend";
        return false;
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadBundledBytes),
                            &module))
    {
        error_ = "NGU native module path is unavailable";
        return false;
    }
    wchar_t modulePath[32768];
    const DWORD modulePathLength = GetModuleFileNameW(module, modulePath, 32768);
    if (!modulePathLength || modulePathLength >= 32768)
    {
        error_ = "NGU native module path is invalid";
        return false;
    }
    const auto shaderPath =
        std::filesystem::path(modulePath).parent_path() / L"shaders" / L"Ngu" / L"luma_1080_cubici1.hlsl";
    std::ifstream shaderFile(shaderPath, std::ios::binary | std::ios::ate);
    std::streamoff shaderSize = -1;
    if (shaderFile)
        shaderSize = static_cast<std::streamoff>(shaderFile.tellg());
    if (!shaderFile || shaderSize <= 0 || shaderSize > 256 * 1024)
    {
        error_ = "NGU native Cubici1 shader source is unavailable";
        return false;
    }
    shaderFile.seekg(0);
    std::string shaderText(static_cast<size_t>(shaderSize), '\0');
    if (!shaderFile.read(shaderText.data(), shaderSize))
    {
        error_ = "NGU native Cubici1 shader source could not be read";
        return false;
    }

    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_cubici1_constants.bin", 32 * sizeof(float) * 4);
    if (constantBytes.empty())
    {
        error_ = "NGU native Cubici1 constants are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, pixelBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(shaderText.data(),
                            shaderText.size(),
                            "NGU_Luma1080_Cubici1",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native vertex shader failed";
        return false;
    }
    errors.Reset();
    hr = D3DCompile(shaderText.data(),
                    shaderText.size(),
                    "NGU_Luma1080_Cubici1",
                    nullptr,
                    nullptr,
                    "PSMain",
                    "ps_5_0",
                    flags,
                    0,
                    &pixelBlob,
                    &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native pixel shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaInitial>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &state->pixel)))
    {
        error_ = "NGU native shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(constantBytes.size());
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = constantBytes.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU native Cubici1 constant buffer could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native Cubici1 rasterizer state could not be created";
        return false;
    }

    nativeLumaInitial_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaInitial1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaInitial(ID3D11ShaderResourceView* source,
                                     int width,
                                     int height,
                                     ID3D11RenderTargetView* const outputs[3])
{
    if (!nativeLumaInitial_ || !source || width != 1920 || height != 1080 || !outputs[0] || !outputs[1] || !outputs[2])
    {
        error_ = "NGU native Cubici1 request is not prepared or has unsupported inputs";
        return false;
    }
    pl_d3d11 d3d = pl_d3d11_get(gpu_);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native D3D11 device is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    d3d->device->GetImmediateContext(context.GetAddressOf());
    if (!context)
    {
        error_ = "NGU native D3D11 context is unavailable";
        return false;
    }

    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDepth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);

    ID3D11RasterizerState* oldRasterizer = nullptr;
    context->RSGetState(&oldRasterizer);
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> oldViewports = {};
    context->RSGetViewports(&viewportCount, oldViewports.data());

    ID3D11VertexShader* oldVertex = nullptr;
    ID3D11PixelShader* oldPixel = nullptr;
    ID3D11ShaderResourceView* oldSource = nullptr;
    ID3D11Buffer* oldConstants = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->VSGetShader(&oldVertex, nullptr, nullptr);
    context->PSGetShader(&oldPixel, nullptr, nullptr);
    context->PSGetShaderResources(0, 1, &oldSource);
    context->PSGetConstantBuffers(0, 1, &oldConstants);
    context->IAGetPrimitiveTopology(&oldTopology);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(nativeLumaInitial_->rasterizer.Get());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(3, outputs, nullptr);
    context->VSSetShader(nativeLumaInitial_->vertex.Get(), nullptr, 0);
    context->PSSetShader(nativeLumaInitial_->pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &source);
    context->PSSetConstantBuffers(0, 1, nativeLumaInitial_->constants.GetAddressOf());
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSource = nullptr;
    ID3D11Buffer* nullConstant = nullptr;
    context->PSSetShaderResources(0, 1, &nullSource);
    context->PSSetConstantBuffers(0, 1, &nullConstant);

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
    context->RSSetState(oldRasterizer);
    if (viewportCount)
        context->RSSetViewports(viewportCount, oldViewports.data());
    context->VSSetShader(oldVertex, nullptr, 0);
    context->PSSetShader(oldPixel, nullptr, 0);
    context->PSSetShaderResources(0, 1, &oldSource);
    context->PSSetConstantBuffers(0, 1, &oldConstants);
    context->IASetPrimitiveTopology(oldTopology);
    for (auto*& target : oldTargets)
        if (target)
            target->Release();
    if (oldDepth)
        oldDepth->Release();
    if (oldRasterizer)
        oldRasterizer->Release();
    if (oldVertex)
        oldVertex->Release();
    if (oldPixel)
        oldPixel->Release();
    if (oldSource)
        oldSource->Release();
    if (oldConstants)
        oldConstants->Release();
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLumaLayers(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native layer scaler requires a GPU";
        return false;
    }
    if (nativeLumaLayers_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native layer scaler requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_1080_layer.hlsl", 256 * 1024);
    if (shaderText.empty())
    {
        error_ = "NGU native layer shader source is unavailable";
        return false;
    }
    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer_constants.bin", 15 * 111 * sizeof(float) * 4);
    if (constantBytes.empty())
    {
        error_ = "NGU native layer constants are unavailable";
        return false;
    }

    const auto pixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer.cso", 0);
    if (pixelBytecode.empty())
    {
        error_ = "NGU native layer pixel bytecode is unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(shaderText.data(),
                            shaderText.size(),
                            "NGU_Luma1080_Layer",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native layer vertex shader failed";
        return false;
    }
    auto state = std::make_unique<NativeLumaLayers>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(), static_cast<SIZE_T>(pixelBytecode.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU native layer shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = 111 * sizeof(float) * 4;
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    for (size_t layer = 0; layer < state->constants.size(); ++layer)
    {
        D3D11_SUBRESOURCE_DATA constantData = {};
        constantData.pSysMem = constantBytes.data() + layer * constantDesc.ByteWidth;
        if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants[layer])))
        {
            error_ = "NGU native layer constant buffer could not be created";
            return false;
        }
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native layer rasterizer state could not be created";
        return false;
    }

    nativeLumaLayers_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaLayers1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaLayer(ID3D11ShaderResourceView* const inputs[3], int layer, ID3D11RenderTargetView* output)
{
    if (!nativeLumaLayers_ || !inputs || !inputs[0] || !inputs[1] || !inputs[2] || !output || layer < 0 ||
        layer >= int(nativeLumaLayers_->constants.size()))
    {
        error_ = "NGU native layer request is not prepared or has unsupported inputs";
        return false;
    }
    pl_d3d11 d3d = pl_d3d11_get(gpu_);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native layer D3D11 device is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    d3d->device->GetImmediateContext(context.GetAddressOf());
    if (!context)
    {
        error_ = "NGU native layer D3D11 context is unavailable";
        return false;
    }

    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDepth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);
    ID3D11RasterizerState* oldRasterizer = nullptr;
    context->RSGetState(&oldRasterizer);
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> oldViewports = {};
    context->RSGetViewports(&viewportCount, oldViewports.data());
    ID3D11VertexShader* oldVertex = nullptr;
    ID3D11PixelShader* oldPixel = nullptr;
    ID3D11ShaderResourceView* oldInputs[3] = {};
    ID3D11Buffer* oldConstant = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->VSGetShader(&oldVertex, nullptr, nullptr);
    context->PSGetShader(&oldPixel, nullptr, nullptr);
    context->PSGetShaderResources(0, 3, oldInputs);
    context->PSGetConstantBuffers(0, 1, &oldConstant);
    context->IAGetPrimitiveTopology(&oldTopology);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(nativeLumaLayers_->rasterizer.Get());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 1920.0f;
    viewport.Height = 1080.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &output, nullptr);
    context->VSSetShader(nativeLumaLayers_->vertex.Get(), nullptr, 0);
    context->PSSetShader(nativeLumaLayers_->pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 3, inputs);
    context->PSSetConstantBuffers(0, 1, nativeLumaLayers_->constants[layer].GetAddressOf());
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullInputs[3] = {};
    ID3D11Buffer* nullConstant = nullptr;
    context->PSSetShaderResources(0, 3, nullInputs);
    context->PSSetConstantBuffers(0, 1, &nullConstant);

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
    context->RSSetState(oldRasterizer);
    if (viewportCount)
        context->RSSetViewports(viewportCount, oldViewports.data());
    context->VSSetShader(oldVertex, nullptr, 0);
    context->PSSetShader(oldPixel, nullptr, 0);
    context->PSSetShaderResources(0, 3, oldInputs);
    context->PSSetConstantBuffers(0, 1, &oldConstant);
    context->IASetPrimitiveTopology(oldTopology);
    for (auto*& target : oldTargets)
        if (target)
            target->Release();
    if (oldDepth)
        oldDepth->Release();
    if (oldRasterizer)
        oldRasterizer->Release();
    if (oldVertex)
        oldVertex->Release();
    if (oldPixel)
        oldPixel->Release();
    for (auto*& input : oldInputs)
        if (input)
            input->Release();
    if (oldConstant)
        oldConstant->Release();
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLumaDense(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native dense scaler requires a GPU";
        return false;
    }
    if (nativeLumaDense_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native dense scaler requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_1080_dense.hlsl", 256 * 1024);
    const auto pixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense.cso", 0);
    const auto constantBytes = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense_constants.bin", 51 * sizeof(float) * 4);
    if (shaderText.empty() || pixelBytecode.empty() || constantBytes.empty())
    {
        error_ = "NGU native dense assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(shaderText.data(),
                            shaderText.size(),
                            "NGU_Luma1080_Dense",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native dense vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaDense>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(), static_cast<SIZE_T>(pixelBytecode.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU native dense shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(constantBytes.size());
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = constantBytes.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU native dense constant buffer could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native dense rasterizer state could not be created";
        return false;
    }

    nativeLumaDense_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaDense1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaDense(ID3D11ShaderResourceView* const inputs[3], ID3D11RenderTargetView* output)
{
    if (!nativeLumaDense_ || !inputs || !inputs[0] || !inputs[1] || !inputs[2] || !output)
    {
        error_ = "NGU native dense request is not prepared or has unsupported inputs";
        return false;
    }
    pl_d3d11 d3d = pl_d3d11_get(gpu_);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native dense D3D11 device is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    d3d->device->GetImmediateContext(context.GetAddressOf());
    if (!context)
    {
        error_ = "NGU native dense D3D11 context is unavailable";
        return false;
    }

    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDepth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &oldDepth);
    ID3D11RasterizerState* oldRasterizer = nullptr;
    context->RSGetState(&oldRasterizer);
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> oldViewports = {};
    context->RSGetViewports(&viewportCount, oldViewports.data());
    ID3D11VertexShader* oldVertex = nullptr;
    ID3D11PixelShader* oldPixel = nullptr;
    ID3D11ShaderResourceView* oldInputs[3] = {};
    ID3D11Buffer* oldConstant = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->VSGetShader(&oldVertex, nullptr, nullptr);
    context->PSGetShader(&oldPixel, nullptr, nullptr);
    context->PSGetShaderResources(0, 3, oldInputs);
    context->PSGetConstantBuffers(0, 1, &oldConstant);
    context->IAGetPrimitiveTopology(&oldTopology);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(nativeLumaDense_->rasterizer.Get());
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 1922.0f;
    viewport.Height = 1082.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &output, nullptr);
    context->VSSetShader(nativeLumaDense_->vertex.Get(), nullptr, 0);
    context->PSSetShader(nativeLumaDense_->pixel.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 3, inputs);
    context->PSSetConstantBuffers(0, 1, nativeLumaDense_->constants.GetAddressOf());
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullInputs[3] = {};
    ID3D11Buffer* nullConstant = nullptr;
    context->PSSetShaderResources(0, 3, nullInputs);
    context->PSSetConstantBuffers(0, 1, &nullConstant);

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, oldDepth);
    context->RSSetState(oldRasterizer);
    if (viewportCount)
        context->RSSetViewports(viewportCount, oldViewports.data());
    context->VSSetShader(oldVertex, nullptr, 0);
    context->PSSetShader(oldPixel, nullptr, 0);
    context->PSSetShaderResources(0, 3, oldInputs);
    context->PSSetConstantBuffers(0, 1, &oldConstant);
    context->IASetPrimitiveTopology(oldTopology);
    for (auto*& target : oldTargets)
        if (target)
            target->Release();
    if (oldDepth)
        oldDepth->Release();
    if (oldRasterizer)
        oldRasterizer->Release();
    if (oldVertex)
        oldVertex->Release();
    if (oldPixel)
        oldPixel->Release();
    for (auto*& input : oldInputs)
        if (input)
            input->Release();
    if (oldConstant)
        oldConstant->Release();
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLumaScaleAdd(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native scale/add requires a GPU";
        return false;
    }
    if (nativeLumaScaleAdd_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native scale/add requires the D3D11 backend";
        return false;
    }
    const std::string scaleText = LoadBundledText(L"shaders\\Ngu\\luma_1080_scale_x.hlsl", 256 * 1024);
    const auto scaleXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_scale_x.cso", 0);
    const auto scaleYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_scale_y.cso", 0);
    const auto addCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_add.cso", 0);
    const auto scaleXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_scale_x_constants.bin", 2 * sizeof(float) * 4);
    const auto scaleYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_scale_y_constants.bin", 2 * sizeof(float) * 4);
    const auto addConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_add_constants.bin", 2 * sizeof(float) * 4);
    if (scaleText.empty() || scaleXCso.empty() || scaleYCso.empty() || addCso.empty() || scaleXConstants.empty() ||
        scaleYConstants.empty() || addConstants.empty())
    {
        error_ = "NGU native scale/add assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(scaleText.data(),
                            scaleText.size(),
                            "NGU_Luma1080_Scale",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native scale vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaScaleAdd>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            scaleXCso.data(), static_cast<SIZE_T>(scaleXCso.size()), nullptr, &state->scaleX)) ||
        FAILED(device->CreatePixelShader(
            scaleYCso.data(), static_cast<SIZE_T>(scaleYCso.size()), nullptr, &state->scaleY)) ||
        FAILED(device->CreatePixelShader(addCso.data(), static_cast<SIZE_T>(addCso.size()), nullptr, &state->add)))
    {
        error_ = "NGU native scale/add shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(scaleXConstants, state->scaleXConstants) ||
        !createConstants(scaleYConstants, state->scaleYConstants) ||
        !createConstants(addConstants, state->addConstants))
    {
        error_ = "NGU native scale/add constants could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native scale/add rasterizer state could not be created";
        return false;
    }

    nativeLumaScaleAdd_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaScaleX1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaScaleX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaScaleAdd_ || !input || !output)
    {
        error_ = "NGU native X scale request is invalid";
        return false;
    }
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    DrawD3D11Pass(device,
                  nativeLumaScaleAdd_->vertex.Get(),
                  nativeLumaScaleAdd_->scaleX.Get(),
                  nativeLumaScaleAdd_->scaleXConstants.Get(),
                  nativeLumaScaleAdd_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  3840,
                  1080);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaScaleY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaScaleAdd_ || !input || !output)
    {
        error_ = "NGU native Y scale request is invalid";
        return false;
    }
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    DrawD3D11Pass(device,
                  nativeLumaScaleAdd_->vertex.Get(),
                  nativeLumaScaleAdd_->scaleY.Get(),
                  nativeLumaScaleAdd_->scaleYConstants.Get(),
                  nativeLumaScaleAdd_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  3840,
                  2160);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaAdd(ID3D11ShaderResourceView* scale,
                                 ID3D11ShaderResourceView* dense,
                                 ID3D11RenderTargetView* output)
{
    if (!nativeLumaScaleAdd_ || !scale || !dense || !output)
    {
        error_ = "NGU native add request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {scale, dense};
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    DrawD3D11Pass(device,
                  nativeLumaScaleAdd_->vertex.Get(),
                  nativeLumaScaleAdd_->add.Get(),
                  nativeLumaScaleAdd_->addConstants.Get(),
                  nativeLumaScaleAdd_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  3840,
                  2160);
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLumaFinish(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native finish requires a GPU";
        return false;
    }
    if (nativeLumaFinish_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native finish requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_2160_up_x.hlsl", 256 * 1024);
    const auto upXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_x.cso", 0);
    const auto upYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_y.cso", 0);
    const auto mergeCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_merge.cso", 0);
    const auto upXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_x_constants.bin", 2 * sizeof(float) * 4);
    const auto upYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_y_constants.bin", 2 * sizeof(float) * 4);
    const auto mergeConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_merge_constants.bin", 2 * sizeof(float) * 4);
    const auto upXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_x_table.bin", 4 * 3840 * 8);
    const auto upYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_y_table.bin", 4 * 2160 * 8);
    if (shaderText.empty() || upXCso.empty() || upYCso.empty() || mergeCso.empty() || upXConstants.empty() ||
        upYConstants.empty() || mergeConstants.empty() || upXTable.empty() || upYTable.empty())
    {
        error_ = "NGU native finish assets are unavailable";
        return false;
    }
    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(shaderText.data(),
                            shaderText.size(),
                            "NGU_Luma2160_Upscale",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native finish vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaFinish>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(upXCso.data(), static_cast<SIZE_T>(upXCso.size()), nullptr, &state->upX)) ||
        FAILED(device->CreatePixelShader(upYCso.data(), static_cast<SIZE_T>(upYCso.size()), nullptr, &state->upY)) ||
        FAILED(
            device->CreatePixelShader(mergeCso.data(), static_cast<SIZE_T>(mergeCso.size()), nullptr, &state->merge)))
    {
        error_ = "NGU native finish shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(upXConstants, state->upXConstants) || !createConstants(upYConstants, state->upYConstants) ||
        !createConstants(mergeConstants, state->mergeConstants))
    {
        error_ = "NGU native finish constants could not be created";
        return false;
    }

    auto createTable = [&](const std::vector<uint8_t>& bytes,
                           int height,
                           ComPtr<ID3D11Texture2D>& texture,
                           ComPtr<ID3D11ShaderResourceView>& view)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 4;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        data.SysMemPitch = 4 * 8;
        return SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
    };
    if (!createTable(upXTable, 3840, state->upXTable, state->upXTableView) ||
        !createTable(upYTable, 2160, state->upYTable, state->upYTableView))
    {
        error_ = "NGU native finish scale tables could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU native finish sampler state could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native finish rasterizer state could not be created";
        return false;
    }

    nativeLumaFinish_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaUpX1080;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaUpX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinish_ || !input || !output)
    {
        error_ = "NGU native 4x X reconstruction request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {input, nativeLumaFinish_->upXTableView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinish_->vertex.Get(),
                  nativeLumaFinish_->upX.Get(),
                  nativeLumaFinish_->upXConstants.Get(),
                  nativeLumaFinish_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  3840,
                  1080,
                  nativeLumaFinish_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaUpY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinish_ || !input || !output)
    {
        error_ = "NGU native 4x Y reconstruction request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {input, nativeLumaFinish_->upYTableView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinish_->vertex.Get(),
                  nativeLumaFinish_->upY.Get(),
                  nativeLumaFinish_->upYConstants.Get(),
                  nativeLumaFinish_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  3840,
                  2160,
                  nativeLumaFinish_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaMerge(ID3D11ShaderResourceView* luma,
                                   ID3D11ShaderResourceView* chroma,
                                   ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinish_ || !luma || !chroma || !output)
    {
        error_ = "NGU native merge request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {luma, chroma};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinish_->vertex.Get(),
                  nativeLumaFinish_->merge.Get(),
                  nativeLumaFinish_->mergeConstants.Get(),
                  nativeLumaFinish_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  3840,
                  2160,
                  nativeLumaFinish_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLumaFinal(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native final requires a GPU";
        return false;
    }
    if (nativeLumaFinal_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native final requires the D3D11 backend";
        return false;
    }
    const std::string vertexText = LoadBundledText(L"shaders\\Ngu\\luma_2160_up_x.hlsl", 256 * 1024);
    const auto finalCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final.cso", 0);
    const auto finalConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final_constants.bin", 4 * sizeof(float) * 4);
    const auto finalWeights = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final_weights.bin", 32 * 32 * sizeof(float));
    if (vertexText.empty() || finalCso.empty() || finalConstants.empty() || finalWeights.empty())
    {
        error_ = "NGU native final assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(vertexText.data(),
                            vertexText.size(),
                            "NGU_Luma2160_FinalVertex",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native final vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaFinal>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(finalCso.data(), static_cast<SIZE_T>(finalCso.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU native final shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(finalConstants.size());
    constantDesc.Usage = D3D11_USAGE_DEFAULT;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = finalConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU native final constants could not be created";
        return false;
    }
    std::memcpy(state->constantsData.data(), finalConstants.data(), sizeof(state->constantsData));
    constantData.pSysMem = state->constantsData1080.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants1080)))
    {
        error_ = "NGU native 1080 final constants could not be created";
        return false;
    }
    constantData.pSysMem = state->constantsData720.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->constants720)))
    {
        error_ = "NGU native 720 final constants could not be created";
        return false;
    }

    D3D11_TEXTURE2D_DESC weightDesc = {};
    weightDesc.Width = 32;
    weightDesc.Height = 32;
    weightDesc.MipLevels = 1;
    weightDesc.ArraySize = 1;
    weightDesc.Format = DXGI_FORMAT_R32_FLOAT;
    weightDesc.SampleDesc.Count = 1;
    weightDesc.Usage = D3D11_USAGE_IMMUTABLE;
    weightDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA weightData = {};
    weightData.pSysMem = finalWeights.data();
    weightData.SysMemPitch = 32 * sizeof(float);
    if (FAILED(device->CreateTexture2D(&weightDesc, &weightData, &state->weightTexture)) ||
        FAILED(device->CreateShaderResourceView(state->weightTexture.Get(), nullptr, &state->weightView)))
    {
        error_ = "NGU native final weight texture could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->clampSampler)))
    {
        error_ = "NGU native final clamp sampler could not be created";
        return false;
    }
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->wrapSampler)))
    {
        error_ = "NGU native final wrap sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU native final rasterizer state could not be created";
        return false;
    }

    nativeLumaFinal_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLumaFinal1080;
    cached_.sourceWidth = 3840;
    cached_.sourceHeight = 2160;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::PrepareOwnedDither()
{
    if (!nativeLumaFinal_ || !gpu_)
    {
        error_ = "NGU native final dither state is unavailable";
        return false;
    }
    if (nativeLumaFinal_->ditherView)
        return true;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    std::vector<uint32_t> ditherWords(size_t(3840) * 2160);
    dither_.SeedColorFromClock();
    dither_.FillColor(ditherWords);
    D3D11_TEXTURE2D_DESC ditherDesc = {};
    ditherDesc.Width = 3840;
    ditherDesc.Height = 2160;
    ditherDesc.MipLevels = 1;
    ditherDesc.ArraySize = 1;
    ditherDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ditherDesc.SampleDesc.Count = 1;
    ditherDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ditherDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA ditherData = {};
    ditherData.pSysMem = ditherWords.data();
    ditherData.SysMemPitch = 3840 * sizeof(uint32_t);
    if (FAILED(device->CreateTexture2D(&ditherDesc, &ditherData, &nativeLumaFinal_->ditherTexture)) ||
        FAILED(device->CreateShaderResourceView(
            nativeLumaFinal_->ditherTexture.Get(), nullptr, &nativeLumaFinal_->ditherView)))
    {
        error_ = "NGU native final dither texture could not be created";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareOwnedDither1080()
{
    if (!nativeLumaFinal_ || !gpu_)
    {
        error_ = "NGU native 1080 final dither state is unavailable";
        return false;
    }
    if (nativeLumaFinal_->ditherView1080)
        return true;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    std::vector<uint32_t> ditherWords(size_t(1920) * 1080);
    dither1080_.SeedColorFromClock();
    dither1080_.FillColor(ditherWords);
    D3D11_TEXTURE2D_DESC ditherDesc = {};
    ditherDesc.Width = 1920;
    ditherDesc.Height = 1080;
    ditherDesc.MipLevels = 1;
    ditherDesc.ArraySize = 1;
    ditherDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ditherDesc.SampleDesc.Count = 1;
    ditherDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ditherDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA ditherData = {};
    ditherData.pSysMem = ditherWords.data();
    ditherData.SysMemPitch = 1920 * sizeof(uint32_t);
    if (FAILED(device->CreateTexture2D(&ditherDesc, &ditherData, &nativeLumaFinal_->ditherTexture1080)) ||
        FAILED(device->CreateShaderResourceView(
            nativeLumaFinal_->ditherTexture1080.Get(), nullptr, &nativeLumaFinal_->ditherView1080)))
    {
        error_ = "NGU native 1080 final dither texture could not be created";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareOwnedDither720()
{
    if (!nativeLumaFinal_ || !gpu_)
    {
        error_ = "NGU native 720 final dither state is unavailable";
        return false;
    }
    if (nativeLumaFinal_->ditherView720)
        return true;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    std::vector<uint32_t> ditherWords(size_t(2560) * 1440);
    dither720_.SeedColorFromClock();
    dither720_.FillColor(ditherWords);
    D3D11_TEXTURE2D_DESC ditherDesc = {};
    ditherDesc.Width = 2560;
    ditherDesc.Height = 1440;
    ditherDesc.MipLevels = 1;
    ditherDesc.ArraySize = 1;
    ditherDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ditherDesc.SampleDesc.Count = 1;
    ditherDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ditherDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA ditherData = {};
    ditherData.pSysMem = ditherWords.data();
    ditherData.SysMemPitch = 2560 * sizeof(uint32_t);
    if (FAILED(device->CreateTexture2D(&ditherDesc, &ditherData, &nativeLumaFinal_->ditherTexture720)) ||
        FAILED(device->CreateShaderResourceView(
            nativeLumaFinal_->ditherTexture720.Get(), nullptr, &nativeLumaFinal_->ditherView720)))
    {
        error_ = "NGU native 720 final dither texture could not be created";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeHookOutput(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU hook output requires a GPU";
        return false;
    }
    if (nativeHookOutput_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU hook output requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\ngu_hook_output.hlsl", 64 * 1024);
    const auto pixelCso = LoadBundledBytes(L"shaders\\Ngu\\ngu_hook_output.cso", 0);
    if (shaderText.empty() || pixelCso.empty())
    {
        error_ = "NGU hook output assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(shaderText.data(),
                            shaderText.size(),
                            "NGU_HookOutput",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU hook output vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeHookOutput>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
    {
        error_ = "NGU hook output shader objects could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU hook output rasterizer state could not be created";
        return false;
    }

    nativeHookOutput_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeHookOutput(ID3D11ShaderResourceView* input,
                                    ID3D11RenderTargetView* output,
                                    int width,
                                    int height)
{
    if (!nativeHookOutput_ || !input || !output || width <= 0 || height <= 0)
    {
        error_ = "NGU hook output request is invalid";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeHookOutput_->vertex.Get(),
                  nativeHookOutput_->pixel.Get(),
                  nullptr,
                  nativeHookOutput_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  UINT(width),
                  UINT(height));
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaFinal(ID3D11ShaderResourceView* input,
                                   ID3D11ShaderResourceView* dither,
                                   ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinal_ || !input || !dither || !output)
    {
        error_ = "NGU native final request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {input, dither, nativeLumaFinal_->weightView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinal_->vertex.Get(),
                  nativeLumaFinal_->pixel.Get(),
                  nativeLumaFinal_->constants.Get(),
                  nativeLumaFinal_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  3840,
                  2160,
                  nativeLumaFinal_->clampSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaFinal1080(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinal_ || !input || !output ||
        !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesRgb8(output, 1920, 1080) || !PrepareOwnedDither1080())
    {
        error_ = "NGU native 1080 final request is invalid";
        return false;
    }
    const NguDither::Phase phase = dither1080_.NextPhase();
    nativeLumaFinal_->constantsData1080[1] = phase.y;
    nativeLumaFinal_->constantsData1080[2] = phase.z;
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->UpdateSubresource(
        nativeLumaFinal_->constants1080.Get(), 0, nullptr, nativeLumaFinal_->constantsData1080.data(), 0, 0);
    context->Release();
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeLumaFinal_->ditherView1080.Get(), nativeLumaFinal_->weightView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinal_->vertex.Get(),
                  nativeLumaFinal_->pixel.Get(),
                  nativeLumaFinal_->constants1080.Get(),
                  nativeLumaFinal_->rasterizer.Get(),
                  inputs,
                  3,
                  targets,
                  1,
                  1920,
                  1080,
                  nativeLumaFinal_->clampSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaFinal720(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinal_ || !input || !output ||
        !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440) ||
        !ViewMatchesRgb8(output, 2560, 1440) || !PrepareOwnedDither720())
    {
        error_ = "NGU native 720 final request is invalid";
        return false;
    }
    const NguDither::Phase phase = dither720_.NextPhase();
    nativeLumaFinal_->constantsData720[1] = phase.y;
    nativeLumaFinal_->constantsData720[2] = phase.z;
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->UpdateSubresource(
        nativeLumaFinal_->constants720.Get(), 0, nullptr, nativeLumaFinal_->constantsData720.data(), 0, 0);
    context->Release();
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeLumaFinal_->ditherView720.Get(), nativeLumaFinal_->weightView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinal_->vertex.Get(),
                  nativeLumaFinal_->pixel.Get(),
                  nativeLumaFinal_->constants720.Get(),
                  nativeLumaFinal_->rasterizer.Get(),
                  inputs,
                  3,
                  targets,
                  1,
                  2560,
                  1440,
                  nativeLumaFinal_->clampSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get());
    error_ = "";
    return true;
}

void NguScaler::ResetDitherPhase()
{
    dither_.Reset();
}

bool NguScaler::RunNativeLumaFinalNextPhase(ID3D11ShaderResourceView* input,
                                            ID3D11ShaderResourceView* dither,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinal_ || !input || !dither || !output)
    {
        error_ = "NGU native final request is invalid";
        return false;
    }
    const NguDither::Phase phase = dither_.NextPhase();
    nativeLumaFinal_->constantsData[1] = phase.y;
    nativeLumaFinal_->constantsData[2] = phase.z;
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->UpdateSubresource(
        nativeLumaFinal_->constants.Get(), 0, nullptr, nativeLumaFinal_->constantsData.data(), 0, 0);
    context->Release();
    ID3D11ShaderResourceView* inputs[3] = {input, dither, nativeLumaFinal_->weightView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeLumaFinal_->vertex.Get(),
                  nativeLumaFinal_->pixel.Get(),
                  nativeLumaFinal_->constants.Get(),
                  nativeLumaFinal_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  3840,
                  2160,
                  nativeLumaFinal_->clampSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get(),
                  nativeLumaFinal_->wrapSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaFinalOwnedDither(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLumaFinal_ || !input || !output || !PrepareOwnedDither())
    {
        error_ = "NGU native final request is invalid";
        return false;
    }
    return RunNativeLumaFinalNextPhase(input, nativeLumaFinal_->ditherView.Get(), output);
}

bool NguScaler::PrepareNativeLumaVeryHigh1080(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU VeryHigh luma requires a GPU";
        return false;
    }
    if (nativeLumaVeryHigh1080_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU VeryHigh luma requires the D3D11 backend";
        return false;
    }
    if (!PrepareNativeLumaSplit(gpu) || !PrepareNativeLumaInitial(gpu) || !PrepareNativeLumaLayers(gpu) ||
        !PrepareNativeLumaDense(gpu) || !PrepareNativeLumaScaleAdd(gpu) || !PrepareNativeLumaFinish(gpu) ||
        !PrepareNativeLumaFinal(gpu))
    {
        return false;
    }

    auto state = std::make_unique<NativeLumaVeryHigh1080>();
    ID3D11Device* device = d3d->device;
    auto create = [&](UINT width,
                      UINT height,
                      DXGI_FORMAT format,
                      ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& view,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };

    if (!create(1920,
                1080,
                DXGI_FORMAT_R16_UNORM,
                state->splitLumaTexture,
                state->splitLumaView,
                state->splitLumaShaderView) ||
        !create(1920,
                1080,
                DXGI_FORMAT_R16G16_UNORM,
                state->splitCbCrTexture,
                state->splitCbCrView,
                state->splitCbCrShaderView))
    {
        error_ = "NGU VeryHigh luma split textures could not be created";
        return false;
    }

    for (size_t i = 0; i < state->initialTextures.size(); ++i)
    {
        if (!create(1920,
                    1080,
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    state->initialTextures[i],
                    state->initialViews[i],
                    state->initialShaderViews[i]))
        {
            error_ = "NGU VeryHigh luma initial textures could not be created";
            return false;
        }
    }
    for (size_t setIndex = 0; setIndex < state->layerTextures.size(); ++setIndex)
    {
        auto& set = state->layerTextures[setIndex];
        for (size_t feature = 0; feature < set.size(); ++feature)
        {
            if (!create(1920,
                        1080,
                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                        set[feature],
                        state->layerViews[setIndex][feature],
                        state->layerShaderViews[setIndex][feature]))
            {
                error_ = "NGU VeryHigh luma layer textures could not be created";
                return false;
            }
        }
    }
    if (!create(1922,
                1082,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                state->denseTexture,
                state->denseView,
                state->denseShaderView) ||
        !create(3840, 1080, DXGI_FORMAT_R16_UNORM, state->scaleXTexture, state->scaleXView, state->scaleXShaderView) ||
        !create(3840, 2160, DXGI_FORMAT_R16_UNORM, state->scaleYTexture, state->scaleYView, state->scaleYShaderView) ||
        !create(3840, 2160, DXGI_FORMAT_R16_UNORM, state->addTexture, state->addView, state->addShaderView) ||
        !create(3840, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, state->upXTexture, state->upXView, state->upXShaderView) ||
        !create(3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM, state->upYTexture, state->upYView, state->upYShaderView) ||
        !create(
            3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM, state->mergeTexture, state->mergeView, state->mergeShaderView))
    {
        error_ = "NGU VeryHigh luma reconstruction textures could not be created";
        return false;
    }

    nativeLumaVeryHigh1080_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaVeryHigh1080(ID3D11ShaderResourceView* sourceRgb,
                                          int sourceWidth,
                                          int sourceHeight,
                                          ID3D11RenderTargetView* finalOutput,
                                          bool advanceDither)
{
    if (!nativeLumaVeryHigh1080_ || !sourceRgb || !finalOutput || sourceWidth != 1920 || sourceHeight != 1080)
    {
        error_ = "NGU VeryHigh luma request is outside the verified 1080p path";
        return false;
    }

    if (!RunNativeLumaSplit(sourceRgb,
                            sourceWidth,
                            sourceHeight,
                            nativeLumaVeryHigh1080_->splitLumaView.Get(),
                            nativeLumaVeryHigh1080_->splitCbCrView.Get()))
    {
        return false;
    }

    std::array<ID3D11RenderTargetView*, 3> initialViews = {};
    for (size_t i = 0; i < initialViews.size(); ++i)
        initialViews[i] = nativeLumaVeryHigh1080_->initialViews[i].Get();
    if (!RunNativeLumaInitial(
            nativeLumaVeryHigh1080_->splitLumaShaderView.Get(), sourceWidth, sourceHeight, initialViews.data()))
        return false;

    std::array<ID3D11ShaderResourceView*, 3> current = {};
    for (size_t i = 0; i < current.size(); ++i)
        current[i] = nativeLumaVeryHigh1080_->initialShaderViews[i].Get();
    for (int layer = 0; layer < 15; ++layer)
    {
        const size_t set = size_t((layer / 3) % 2);
        const size_t feature = size_t(layer % 3);
        if (!RunNativeLumaLayer(current.data(), layer, nativeLumaVeryHigh1080_->layerViews[set][feature].Get()))
        {
            return false;
        }
        if (feature == 2)
            for (size_t i = 0; i < current.size(); ++i)
                current[i] = nativeLumaVeryHigh1080_->layerShaderViews[set][i].Get();
    }
    if (!RunNativeLumaDense(current.data(), nativeLumaVeryHigh1080_->denseView.Get()) ||
        !RunNativeLumaScaleX(nativeLumaVeryHigh1080_->splitLumaShaderView.Get(),
                             nativeLumaVeryHigh1080_->scaleXView.Get()) ||
        !RunNativeLumaScaleY(nativeLumaVeryHigh1080_->scaleXShaderView.Get(),
                             nativeLumaVeryHigh1080_->scaleYView.Get()) ||
        !RunNativeLumaAdd(nativeLumaVeryHigh1080_->scaleYShaderView.Get(),
                          nativeLumaVeryHigh1080_->denseShaderView.Get(),
                          nativeLumaVeryHigh1080_->addView.Get()) ||
        !RunNativeLumaUpX(nativeLumaVeryHigh1080_->splitCbCrShaderView.Get(), nativeLumaVeryHigh1080_->upXView.Get()) ||
        !RunNativeLumaUpY(nativeLumaVeryHigh1080_->upXShaderView.Get(), nativeLumaVeryHigh1080_->upYView.Get()) ||
        !RunNativeLumaMerge(nativeLumaVeryHigh1080_->addShaderView.Get(),
                            nativeLumaVeryHigh1080_->upYShaderView.Get(),
                            nativeLumaVeryHigh1080_->mergeView.Get()))
    {
        return false;
    }
    if (!PrepareOwnedDither())
        return false;
    if (advanceDither)
    {
        if (!RunNativeLumaFinalOwnedDither(nativeLumaVeryHigh1080_->mergeShaderView.Get(), finalOutput))
            return false;
    }
    else if (!RunNativeLumaFinal(
                 nativeLumaVeryHigh1080_->mergeShaderView.Get(), nativeLumaFinal_->ditherView.Get(), finalOutput))
    {
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma720Double(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720 Double luma requires a GPU";
        return false;
    }
    if (nativeLuma720Double_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    if (!PrepareNativeLumaSplit(gpu) || !PrepareNativeLumaFinal(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU 720 Double luma requires the D3D11 backend";
        return false;
    }
    const std::string vertexText = LoadBundledText(L"shaders\\Ngu\\luma_1080_split.hlsl", 64 * 1024);
    const std::string initialText = LoadBundledText(L"shaders\\Ngu\\luma_1080_cubici1.hlsl", 256 * 1024);
    const auto splitCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_split.cso", 0);
    const auto layerCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer.cso", 0);
    const auto denseCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense.cso", 0);
    const auto scaleXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_720_scale_x.cso", 0);
    const auto scaleYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_720_scale_y.cso", 0);
    const auto cbcrScaleXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_x.cso", 0);
    const auto cbcrScaleYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_y.cso", 0);
    const auto addCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_add.cso", 0);
    const auto mergeCso = LoadBundledBytes(L"shaders\\Ngu\\luma_720_merge.cso", 0);
    const auto initialConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cubici1_constants.bin", 32 * 16);
    const auto layerConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_layer_constants.bin", 15 * 111 * 16);
    const auto denseConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_dense_constants.bin", 51 * 16);
    const auto scaleXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_scale_x_constants.bin", 2 * 16);
    const auto scaleYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_scale_y_constants.bin", 2 * 16);
    const auto cbcrXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_x_constants.bin", 2 * 16);
    const auto cbcrYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_y_constants.bin", 2 * 16);
    const auto cbcrXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_x_table.bin", 81920);
    const auto cbcrYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_720_cbcr_y_table.bin", 46080);
    const auto addConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_add_constants.bin", 2 * 16);
    const auto mergeConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_720_merge_constants.bin", 2 * 16);
    if (vertexText.empty() || initialText.empty() || splitCso.empty() || layerCso.empty() || denseCso.empty() ||
        scaleXCso.empty() || scaleYCso.empty() || cbcrScaleXCso.empty() || cbcrScaleYCso.empty() || addCso.empty() ||
        mergeCso.empty() || initialConstants.empty() || layerConstants.empty() || denseConstants.empty() ||
        scaleXConstants.empty() || scaleYConstants.empty() || cbcrXConstants.empty() || cbcrYConstants.empty() ||
        cbcrXTable.empty() || cbcrYTable.empty() || addConstants.empty() || mergeConstants.empty())
    {
        error_ = "NGU 720 Double luma assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, initialBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(vertexText.data(),
                            vertexText.size(),
                            "NGU_Luma720_Vertex",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ = "NGU 720 Double vertex shader failed";
        return false;
    }
    errors.Reset();
    hr = D3DCompile(initialText.data(),
                    initialText.size(),
                    "NGU_Luma720_Cubici1",
                    nullptr,
                    nullptr,
                    "PSMain",
                    "ps_5_0",
                    flags,
                    0,
                    &initialBlob,
                    &errors);
    if (FAILED(hr))
    {
        error_ = "NGU 720 Double initial shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeLuma720Double>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(splitCso.data(), splitCso.size(), nullptr, &state->split)) ||
        FAILED(device->CreatePixelShader(
            initialBlob->GetBufferPointer(), initialBlob->GetBufferSize(), nullptr, &state->initial)) ||
        FAILED(device->CreatePixelShader(layerCso.data(), layerCso.size(), nullptr, &state->layer)) ||
        FAILED(device->CreatePixelShader(denseCso.data(), denseCso.size(), nullptr, &state->dense)) ||
        FAILED(device->CreatePixelShader(scaleXCso.data(), scaleXCso.size(), nullptr, &state->scaleX)) ||
        FAILED(device->CreatePixelShader(scaleYCso.data(), scaleYCso.size(), nullptr, &state->scaleY)) ||
        FAILED(device->CreatePixelShader(cbcrScaleXCso.data(), cbcrScaleXCso.size(), nullptr, &state->cbcrScaleX)) ||
        FAILED(device->CreatePixelShader(cbcrScaleYCso.data(), cbcrScaleYCso.size(), nullptr, &state->cbcrScaleY)) ||
        FAILED(device->CreatePixelShader(addCso.data(), addCso.size(), nullptr, &state->add)) ||
        FAILED(device->CreatePixelShader(mergeCso.data(), mergeCso.size(), nullptr, &state->merge)))
    {
        error_ = "NGU 720 Double shader objects could not be created";
        return false;
    }
    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(initialConstants, state->initialConstants) ||
        !createConstants(denseConstants, state->denseConstants) ||
        !createConstants(scaleXConstants, state->scaleXConstants) ||
        !createConstants(scaleYConstants, state->scaleYConstants) ||
        !createConstants(cbcrXConstants, state->chromaXConstants) ||
        !createConstants(cbcrYConstants, state->chromaYConstants) ||
        !createConstants(addConstants, state->addConstants) || !createConstants(mergeConstants, state->mergeConstants))
    {
        error_ = "NGU 720 Double constants could not be created";
        return false;
    }
    for (size_t layer = 0; layer < state->layerConstants.size(); ++layer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 111 * 16;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = layerConstants.data() + layer * desc.ByteWidth;
        if (FAILED(device->CreateBuffer(&desc, &data, &state->layerConstants[layer])))
        {
            error_ = "NGU 720 Double layer constants could not be created";
            return false;
        }
    }

    auto createTexture = [&](UINT width,
                             UINT height,
                             DXGI_FORMAT format,
                             ComPtr<ID3D11Texture2D>& texture,
                             ComPtr<ID3D11RenderTargetView>& view,
                             ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createTexture(1280,
                       720,
                       DXGI_FORMAT_R16_UNORM,
                       state->splitLumaTexture,
                       state->splitLumaView,
                       state->splitLumaShaderView) ||
        !createTexture(1280,
                       720,
                       DXGI_FORMAT_R16G16_UNORM,
                       state->splitCbCrTexture,
                       state->splitCbCrView,
                       state->splitCbCrShaderView))
    {
        error_ = "NGU 720 Double split surfaces could not be created";
        return false;
    }
    for (size_t index = 0; index < 3; ++index)
    {
        if (!createTexture(1280,
                           720,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           state->initialTextures[index],
                           state->initialViews[index],
                           state->initialShaderViews[index]))
        {
            error_ = "NGU 720 Double initial surfaces could not be created";
            return false;
        }
    }
    for (size_t set = 0; set < 2; ++set)
        for (size_t feature = 0; feature < 3; ++feature)
            if (!createTexture(1280,
                               720,
                               DXGI_FORMAT_R16G16B16A16_FLOAT,
                               state->layerTextures[set][feature],
                               state->layerViews[set][feature],
                               state->layerShaderViews[set][feature]))
            {
                error_ = "NGU 720 Double layer surfaces could not be created";
                return false;
            }
    if (!createTexture(
            1282, 722, DXGI_FORMAT_R16G16B16A16_FLOAT, state->denseTexture, state->denseView, state->denseShaderView) ||
        !createTexture(
            2560, 720, DXGI_FORMAT_R16_UNORM, state->scaleXTexture, state->scaleXView, state->scaleXShaderView) ||
        !createTexture(
            2560, 1440, DXGI_FORMAT_R16_UNORM, state->scaleYTexture, state->scaleYView, state->scaleYShaderView) ||
        !createTexture(2560,
                       720,
                       DXGI_FORMAT_R16G16B16A16_UNORM,
                       state->chromaXTexture,
                       state->chromaXView,
                       state->chromaXShaderView) ||
        !createTexture(2560,
                       1440,
                       DXGI_FORMAT_R16G16B16A16_UNORM,
                       state->chromaYTexture,
                       state->chromaYView,
                       state->chromaYShaderView) ||
        !createTexture(2560, 1440, DXGI_FORMAT_R16_UNORM, state->addTexture, state->addView, state->addShaderView) ||
        !createTexture(
            2560, 1440, DXGI_FORMAT_R16G16B16A16_UNORM, state->mergeTexture, state->mergeView, state->mergeShaderView))
    {
        error_ = "NGU 720 Double output surfaces could not be created";
        return false;
    }

    auto createTable = [&](const std::vector<uint8_t>& bytes,
                           int height,
                           ComPtr<ID3D11Texture2D>& texture,
                           ComPtr<ID3D11ShaderResourceView>& view)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 4;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        data.SysMemPitch = 4 * sizeof(float) * 2;
        return SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
    };
    if (!createTable(cbcrXTable, 2560, state->cbcrXTable, state->cbcrXTableView) ||
        !createTable(cbcrYTable, 1440, state->cbcrYTable, state->cbcrYTableView))
    {
        error_ = "NGU 720 Double CbCr scale tables could not be created";
        return false;
    }
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU 720 Double sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU 720 Double rasterizer could not be created";
        return false;
    }

    nativeLuma720Double_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma720Double;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma720DoubleHighBit(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU P010 720p Double requires a GPU";
        return false;
    }
    if (nativeLuma720DoubleHighBit_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    if (!PrepareNativeChromaNetwork(gpu) || !PrepareNativeLuma720Double(gpu) || !PrepareNativeLumaFinal(gpu))
        return false;

    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device || !nativeChromaNetwork_ || !nativeLuma720Double_)
    {
        error_ = "NGU P010 720p Double requires the D3D11 backend";
        return false;
    }

    const std::string vertexText = LoadBundledText(L"shaders\\Ngu\\luma_720_highbit_split.hlsl", 64 * 1024);
    const auto splitBytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_split.cso", 0);
    const auto layer6140Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_6140.cso", 0);
    const auto layer4280Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_4280.cso", 0);
    const auto dense1528Bytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_1528.cso", 0);
    const auto toRgbBytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_to_rgb.cso", 0);
    const auto constants6140 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_6140_constants.bin", 220 * 16);
    const auto constants4280_0 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_4280_0_constants.bin", 148 * 16);
    const auto constants4280_1 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_4280_1_constants.bin", 148 * 16);
    const auto constants4280_2 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_4280_2_constants.bin", 148 * 16);
    const auto constants1528 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_1528_constants.bin", 35 * 16);
    const auto constantsToRgb = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_to_rgb_constants.bin", 2 * 16);
    const auto constantsAdd = LoadBundledBytes(L"shaders\\Ngu\\ngu_highbit_720_add_0_constants.bin", 2 * 16);
    const auto constants1672 = LoadBundledBytes(L"shaders\\Ngu\\ngu_highbit_720_1672_constants.bin", 42 * 16);
    const auto constantsInitial0 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_1412_0_constants.bin", 32 * 16);
    const auto constantsInitial1 = LoadBundledBytes(L"shaders\\Ngu\\luma_720_highbit_1412_1_constants.bin", 32 * 16);
    const auto constantsLumaDense = LoadBundledBytes(L"shaders\\Ngu\\ngu_highbit_720_dense_constants.bin", 67 * 16);
    if (vertexText.empty() || splitBytecode.empty() || layer6140Bytecode.empty() || layer4280Bytecode.empty() ||
        dense1528Bytecode.empty() || toRgbBytecode.empty() || constants6140.empty() || constants4280_0.empty() ||
        constants4280_1.empty() || constants4280_2.empty() || constants1528.empty() || constantsToRgb.empty() ||
        constantsAdd.empty() || constants1672.empty() || constantsInitial0.empty() || constantsInitial1.empty() ||
        constantsLumaDense.empty())
    {
        error_ = "NGU P010 720p Double assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(vertexText.data(),
                          vertexText.size(),
                          "NGU_HighBit720_Vertex",
                          nullptr,
                          nullptr,
                          "VSMain",
                          "vs_5_0",
                          flags,
                          0,
                          &vertexBlob,
                          &errors)))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU P010 720p Double vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeLuma720DoubleHighBit>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            splitBytecode.data(), static_cast<SIZE_T>(splitBytecode.size()), nullptr, &state->split)) ||
        FAILED(device->CreatePixelShader(
            layer6140Bytecode.data(), static_cast<SIZE_T>(layer6140Bytecode.size()), nullptr, &state->layer6140)) ||
        FAILED(device->CreatePixelShader(
            layer4280Bytecode.data(), static_cast<SIZE_T>(layer4280Bytecode.size()), nullptr, &state->layer4280)) ||
        FAILED(device->CreatePixelShader(
            dense1528Bytecode.data(), static_cast<SIZE_T>(dense1528Bytecode.size()), nullptr, &state->dense1528)) ||
        FAILED(device->CreatePixelShader(
            toRgbBytecode.data(), static_cast<SIZE_T>(toRgbBytecode.size()), nullptr, &state->toRgb)))
    {
        error_ = "NGU P010 720p Double shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(constants6140, state->constants6140) ||
        !createConstants(constants4280_0, state->constants4280[0]) ||
        !createConstants(constants4280_1, state->constants4280[1]) ||
        !createConstants(constants4280_2, state->constants4280[2]) ||
        !createConstants(constants1528, state->constants1528) ||
        !createConstants(constantsToRgb, state->constantsToRgb) ||
        !createConstants(constantsAdd, state->constantsAdd) || !createConstants(constants1672, state->constants1672) ||
        !createConstants(constantsInitial0, state->initialConstants[0]) ||
        !createConstants(constantsInitial1, state->initialConstants[1]) ||
        !createConstants(constantsLumaDense, state->constantsLumaDense))
    {
        error_ = "NGU P010 720p Double constants could not be created";
        return false;
    }
    for (int layer = 0; layer < 28; ++layer)
    {
        wchar_t filename[96] = {};
        swprintf_s(filename, L"shaders\\Ngu\\ngu_highbit_720_layer_%02d_constants.bin", layer);
        const auto bytes = LoadBundledBytes(filename, 147 * 16);
        if (bytes.empty() || !createConstants(bytes, state->constantsLumaLayer[size_t(layer)]))
        {
            error_ = "NGU P010 720p Double layer constants could not be created";
            return false;
        }
    }

    auto createSurface = [&](UINT width, UINT height, DXGI_FORMAT format, NativeLuma720DoubleHighBit::Surface& surface)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &surface.texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(surface.texture.Get(), nullptr, &surface.view)) &&
               SUCCEEDED(device->CreateShaderResourceView(surface.texture.Get(), nullptr, &surface.shaderView));
    };
    for (size_t index = 0; index < 3; ++index)
    {
        if (!createSurface(1280, 720, DXGI_FORMAT_R16_UNORM, state->splitSource[index]) ||
            !createSurface(2560, 720, DXGI_FORMAT_R16_UNORM, state->upX[index]) ||
            !createSurface(2560, 1440, DXGI_FORMAT_R16_UNORM, state->upY[index]))
        {
            error_ = "NGU P010 720p Double plane surfaces could not be created";
            return false;
        }
    }
    for (size_t index = 0; index < 2; ++index)
    {
        if (!createSurface(2560, 1440, DXGI_FORMAT_R16_UNORM, state->chromaResult[index]))
        {
            error_ = "NGU P010 720p Double chroma result surfaces could not be created";
            return false;
        }
    }
    for (size_t index = 0; index < 4; ++index)
    {
        if (!createSurface(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, state->lumaInitial[index]))
        {
            error_ = "NGU P010 720p Double luma initial surfaces could not be created";
            return false;
        }
        for (size_t set = 0; set < 2; ++set)
        {
            if (!createSurface(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, state->lumaGroups[set][index]))
            {
                error_ = "NGU P010 720p Double luma group surfaces could not be created";
                return false;
            }
        }
    }
    for (size_t index = 0; index < 3; ++index)
    {
        if (!createSurface(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, state->chromaInitial[index]))
        {
            error_ = "NGU P010 720p Double chroma initial surfaces could not be created";
            return false;
        }
    }
    for (size_t index = 0; index < 2; ++index)
    {
        if (!createSurface(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, state->chromaA[index]) ||
            !createSurface(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, state->chromaB[index]))
        {
            error_ = "NGU P010 720p Double chroma work surfaces could not be created";
            return false;
        }
    }
    if (!createSurface(1282, 722, DXGI_FORMAT_R16G16B16A16_FLOAT, state->lumaDense) ||
        !createSurface(1282, 722, DXGI_FORMAT_R16G16B16A16_FLOAT, state->chromaDense[0]) ||
        !createSurface(1282, 722, DXGI_FORMAT_R16G16B16A16_FLOAT, state->chromaDense[1]) ||
        !createSurface(2560, 1440, DXGI_FORMAT_R16G16B16A16_UNORM, state->rgb))
    {
        error_ = "NGU P010 720p Double dense or RGB surfaces could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU P010 720p Double sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU P010 720p Double rasterizer could not be created";
        return false;
    }

    nativeLuma720DoubleHighBit_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma720Double;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma720DoubleHighBit(ID3D11ShaderResourceView* sourceRgb, ID3D11RenderTargetView* finalOutput)
{
    const bool preFinalRgb16 =
        finalOutput && ViewMatchesTexture(finalOutput, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440);
    if (!nativeLuma720DoubleHighBit_ || !nativeLuma720Double_ || !nativeChromaNetwork_ || !sourceRgb || !finalOutput ||
        !ViewMatchesTexture(sourceRgb, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) ||
        (!preFinalRgb16 && !ViewMatchesRgb8(finalOutput, 2560, 1440)))
    {
        error_ = "NGU P010 720p Double request is outside the captured path";
        return false;
    }
    auto& state = *nativeLuma720DoubleHighBit_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;

    ID3D11RenderTargetView* splitTargets[3] = {
        state.splitSource[0].view.Get(), state.splitSource[1].view.Get(), state.splitSource[2].view.Get()};
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.split.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &sourceRgb,
                  1,
                  splitTargets,
                  3,
                  1280,
                  720,
                  state.pointSampler.Get());

    auto upscale = [&](size_t channel)
    {
        ID3D11ShaderResourceView* input = state.splitSource[channel].shaderView.Get();
        ID3D11RenderTargetView* target = state.upX[channel].view.Get();
        DrawD3D11Pass(device,
                      nativeLuma720Double_->vertex.Get(),
                      nativeLuma720Double_->scaleX.Get(),
                      nativeLuma720Double_->scaleXConstants.Get(),
                      nativeLuma720Double_->rasterizer.Get(),
                      &input,
                      1,
                      &target,
                      1,
                      2560,
                      720,
                      nativeLuma720Double_->pointSampler.Get());
        input = state.upX[channel].shaderView.Get();
        target = state.upY[channel].view.Get();
        DrawD3D11Pass(device,
                      nativeLuma720Double_->vertex.Get(),
                      nativeLuma720Double_->scaleY.Get(),
                      nativeLuma720Double_->scaleYConstants.Get(),
                      nativeLuma720Double_->rasterizer.Get(),
                      &input,
                      1,
                      &target,
                      1,
                      2560,
                      1440,
                      nativeLuma720Double_->pointSampler.Get());
    };
    upscale(0);
    upscale(1);
    upscale(2);

    ID3D11RenderTargetView* lumaInitialTargets[4] = {state.lumaInitial[0].view.Get(),
                                                     state.lumaInitial[1].view.Get(),
                                                     state.lumaInitial[2].view.Get(),
                                                     state.lumaInitial[3].view.Get()};
    ID3D11ShaderResourceView* splitY = state.splitSource[0].shaderView.Get();
    DrawD3D11Pass(device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->cubici2.Get(),
                  state.constants1672.Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  &splitY,
                  1,
                  lumaInitialTargets,
                  4,
                  1280,
                  720);

    std::array<ID3D11ShaderResourceView*, 4> current = {state.lumaInitial[0].shaderView.Get(),
                                                        state.lumaInitial[1].shaderView.Get(),
                                                        state.lumaInitial[2].shaderView.Get(),
                                                        state.lumaInitial[3].shaderView.Get()};
    for (int layer = 0; layer < 28; ++layer)
    {
        const size_t set = size_t((layer / 4) % 2);
        const size_t feature = size_t(layer % 4);
        ID3D11RenderTargetView* target = state.lumaGroups[set][feature].view.Get();
        DrawD3D11Pass(device,
                      nativeChromaNetwork_->vertex.Get(),
                      nativeChromaNetwork_->layer.Get(),
                      state.constantsLumaLayer[size_t(layer)].Get(),
                      nativeChromaNetwork_->rasterizer.Get(),
                      current.data(),
                      4,
                      &target,
                      1,
                      1280,
                      720);
        if (feature == 3)
        {
            for (size_t index = 0; index < current.size(); ++index)
                current[index] = state.lumaGroups[set][index].shaderView.Get();
        }
    }
    ID3D11RenderTargetView* lumaDenseTarget = state.lumaDense.view.Get();
    DrawD3D11Pass(device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->dense.Get(),
                  state.constantsLumaDense.Get(),
                  nativeChromaNetwork_->rasterizer.Get(),
                  current.data(),
                  4,
                  &lumaDenseTarget,
                  1,
                  1282,
                  722);

    for (size_t outputChannel = 0; outputChannel < 2; ++outputChannel)
    {
        const size_t inputChannel = outputChannel + 1;
        ID3D11RenderTargetView* initialTargets[3] = {
            state.chromaInitial[0].view.Get(), state.chromaInitial[1].view.Get(), state.chromaInitial[2].view.Get()};
        ID3D11ShaderResourceView* channelInput = state.splitSource[inputChannel].shaderView.Get();
        DrawD3D11Pass(device,
                      nativeLuma720Double_->vertex.Get(),
                      nativeLuma720Double_->initial.Get(),
                      state.initialConstants[outputChannel].Get(),
                      nativeLuma720Double_->rasterizer.Get(),
                      &channelInput,
                      1,
                      initialTargets,
                      3,
                      1280,
                      720);

        ID3D11ShaderResourceView* layerInputs[3] = {state.chromaInitial[0].shaderView.Get(),
                                                    state.chromaInitial[1].shaderView.Get(),
                                                    state.chromaInitial[2].shaderView.Get()};
        ID3D11RenderTargetView* workTargets[2] = {state.chromaA[0].view.Get(), state.chromaA[1].view.Get()};
        DrawD3D11Pass(device,
                      state.vertex.Get(),
                      state.layer6140.Get(),
                      state.constants6140.Get(),
                      state.rasterizer.Get(),
                      layerInputs,
                      3,
                      workTargets,
                      2,
                      1280,
                      720);

        ID3D11ShaderResourceView* pair[2] = {state.chromaA[0].shaderView.Get(), state.chromaA[1].shaderView.Get()};
        for (int pass = 0; pass < 3; ++pass)
        {
            auto& targetState = (pass == 1) ? state.chromaA : state.chromaB;
            ID3D11RenderTargetView* targets[2] = {targetState[0].view.Get(), targetState[1].view.Get()};
            DrawD3D11Pass(device,
                          state.vertex.Get(),
                          state.layer4280.Get(),
                          state.constants4280[size_t(pass)].Get(),
                          state.rasterizer.Get(),
                          pair,
                          2,
                          targets,
                          2,
                          1280,
                          720);
            pair[0] = targetState[0].shaderView.Get();
            pair[1] = targetState[1].shaderView.Get();
        }

        const size_t chromaIndex = outputChannel;
        ID3D11RenderTargetView* denseChromaTarget = state.chromaDense[chromaIndex].view.Get();
        DrawD3D11Pass(device,
                      state.vertex.Get(),
                      state.dense1528.Get(),
                      state.constants1528.Get(),
                      state.rasterizer.Get(),
                      pair,
                      2,
                      &denseChromaTarget,
                      1,
                      1282,
                      722,
                      state.pointSampler.Get());

        ID3D11ShaderResourceView* addInputs[2] = {state.upY[inputChannel].shaderView.Get(),
                                                  state.chromaDense[chromaIndex].shaderView.Get()};
        ID3D11RenderTargetView* addTarget = state.chromaResult[outputChannel].view.Get();
        DrawD3D11Pass(device,
                      nativeLuma720Double_->vertex.Get(),
                      nativeLuma720Double_->add.Get(),
                      state.constantsAdd.Get(),
                      nativeLuma720Double_->rasterizer.Get(),
                      addInputs,
                      2,
                      &addTarget,
                      1,
                      2560,
                      1440,
                      nativeLuma720Double_->pointSampler.Get());
    }

    ID3D11ShaderResourceView* toRgbInputs[4] = {state.upY[0].shaderView.Get(),
                                                state.chromaResult[0].shaderView.Get(),
                                                state.chromaResult[1].shaderView.Get(),
                                                state.lumaDense.shaderView.Get()};
    ID3D11RenderTargetView* rgbTarget = state.rgb.view.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.toRgb.Get(),
                  state.constantsToRgb.Get(),
                  state.rasterizer.Get(),
                  toRgbInputs,
                  4,
                  &rgbTarget,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    if (preFinalRgb16 ? !CopyTextureToTarget(gpu_, state.rgb.texture.Get(), finalOutput)
                      : !RunNativeLumaFinal720(state.rgb.shaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma720Double(ID3D11ShaderResourceView* sourceRgb,
                                       int sourceWidth,
                                       int sourceHeight,
                                       ID3D11RenderTargetView* finalOutput)
{
    if (!nativeLuma720Double_ || !sourceRgb || !finalOutput || sourceWidth != 1280 || sourceHeight != 720 ||
        !ViewMatchesRgb8(finalOutput, 2560, 1440))
    {
        error_ = "NGU 720 Double request is outside the verified path";
        return false;
    }
    auto& state = *nativeLuma720Double_;
    if (!RunNativeLuma720DoubleIntermediate(sourceRgb, sourceWidth, sourceHeight, state.mergeView.Get()))
        return false;
    if (!RunNativeLumaFinal720(state.mergeShaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma720DoubleIntermediate(ID3D11ShaderResourceView* sourceRgb,
                                                   int sourceWidth,
                                                   int sourceHeight,
                                                   ID3D11RenderTargetView* mergeOutput)
{
    if (!nativeLuma720Double_ || !sourceRgb || !mergeOutput || sourceWidth != 1280 || sourceHeight != 720 ||
        !ViewMatchesTexture(mergeOutput, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440))
    {
        error_ = "NGU 720 Double intermediate request is outside the captured path";
        return false;
    }
    auto& state = *nativeLuma720Double_;
    if (!RunNativeLumaSplit(sourceRgb, sourceWidth, sourceHeight, state.splitLumaView.Get(), state.splitCbCrView.Get()))
        return false;

    ID3D11ShaderResourceView* initialInput = state.splitLumaShaderView.Get();
    ID3D11RenderTargetView* initialTargets[3] = {
        state.initialViews[0].Get(), state.initialViews[1].Get(), state.initialViews[2].Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.initial.Get(),
                  state.initialConstants.Get(),
                  state.rasterizer.Get(),
                  &initialInput,
                  1,
                  initialTargets,
                  3,
                  1280,
                  720);

    std::array<ID3D11ShaderResourceView*, 3> current = {
        state.initialShaderViews[0].Get(), state.initialShaderViews[1].Get(), state.initialShaderViews[2].Get()};
    for (int layer = 0; layer < 15; ++layer)
    {
        const size_t set = size_t((layer / 3) % 2);
        const size_t feature = size_t(layer % 3);
        ID3D11RenderTargetView* layerTarget = state.layerViews[set][feature].Get();
        DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                      state.vertex.Get(),
                      state.layer.Get(),
                      state.layerConstants[layer].Get(),
                      state.rasterizer.Get(),
                      current.data(),
                      3,
                      &layerTarget,
                      1,
                      1280,
                      720);
        if (feature == 2)
            for (size_t index = 0; index < 3; ++index)
                current[index] = state.layerShaderViews[set][index].Get();
    }
    ID3D11RenderTargetView* denseTarget = state.denseView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.dense.Get(),
                  state.denseConstants.Get(),
                  state.rasterizer.Get(),
                  current.data(),
                  3,
                  &denseTarget,
                  1,
                  1282,
                  722);
    ID3D11ShaderResourceView* scaleInput = state.splitLumaShaderView.Get();
    ID3D11RenderTargetView* scaleTarget = state.scaleXView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.scaleX.Get(),
                  state.scaleXConstants.Get(),
                  state.rasterizer.Get(),
                  &scaleInput,
                  1,
                  &scaleTarget,
                  1,
                  2560,
                  720,
                  state.pointSampler.Get());
    scaleInput = state.scaleXShaderView.Get();
    scaleTarget = state.scaleYView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.scaleY.Get(),
                  state.scaleYConstants.Get(),
                  state.rasterizer.Get(),
                  &scaleInput,
                  1,
                  &scaleTarget,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    ID3D11ShaderResourceView* chromaXInputs[2] = {state.splitCbCrShaderView.Get(), state.cbcrXTableView.Get()};
    scaleTarget = state.chromaXView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.cbcrScaleX.Get(),
                  state.chromaXConstants.Get(),
                  state.rasterizer.Get(),
                  chromaXInputs,
                  2,
                  &scaleTarget,
                  1,
                  2560,
                  720,
                  state.pointSampler.Get());
    ID3D11ShaderResourceView* chromaYInputs[2] = {state.chromaXShaderView.Get(), state.cbcrYTableView.Get()};
    scaleTarget = state.chromaYView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.cbcrScaleY.Get(),
                  state.chromaYConstants.Get(),
                  state.rasterizer.Get(),
                  chromaYInputs,
                  2,
                  &scaleTarget,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    ID3D11ShaderResourceView* addInputs[2] = {state.scaleYShaderView.Get(), state.denseShaderView.Get()};
    ID3D11RenderTargetView* addTarget = state.addView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.add.Get(),
                  state.addConstants.Get(),
                  state.rasterizer.Get(),
                  addInputs,
                  2,
                  &addTarget,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    ID3D11ShaderResourceView* mergeInputs[2] = {state.addShaderView.Get(), state.chromaYShaderView.Get()};
    ID3D11RenderTargetView* mergeTarget = mergeOutput;
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.merge.Get(),
                  state.mergeConstants.Get(),
                  state.rasterizer.Get(),
                  mergeInputs,
                  2,
                  &mergeTarget,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma720Merge(ID3D11ShaderResourceView* luma,
                                      ID3D11ShaderResourceView* cbcr,
                                      ID3D11RenderTargetView* output)
{
    if (!nativeLuma720Double_ || !luma || !cbcr || !output ||
        !ViewMatchesTexture(luma, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440) ||
        !ViewMatchesTexture(cbcr, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440))
    {
        error_ = "NGU 720 Double merge request is outside the captured path";
        return false;
    }
    auto& state = *nativeLuma720Double_;
    ID3D11ShaderResourceView* inputs[2] = {luma, cbcr};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.merge.Get(),
                  state.mergeConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma720DoubleSurface(Ngu720DoubleSurface surface, ID3D11RenderTargetView* output)
{
    if (!nativeLuma720Double_ || !output)
    {
        error_ = "NGU 720 Double surface copy is unavailable";
        return false;
    }
    auto& state = *nativeLuma720Double_;
    ID3D11Texture2D* source = nullptr;
    switch (surface)
    {
    case Ngu720DoubleSurface::SplitLuma:
        source = state.splitLumaTexture.Get();
        break;
    case Ngu720DoubleSurface::SplitCbCr:
        source = state.splitCbCrTexture.Get();
        break;
    case Ngu720DoubleSurface::ScaleX:
        source = state.scaleXTexture.Get();
        break;
    case Ngu720DoubleSurface::ScaleY:
        source = state.scaleYTexture.Get();
        break;
    case Ngu720DoubleSurface::Initial0:
        source = state.initialTextures[0].Get();
        break;
    case Ngu720DoubleSurface::Initial1:
        source = state.initialTextures[1].Get();
        break;
    case Ngu720DoubleSurface::Initial2:
        source = state.initialTextures[2].Get();
        break;
    case Ngu720DoubleSurface::FinalLayer0:
        source = state.layerTextures[0][0].Get();
        break;
    case Ngu720DoubleSurface::FinalLayer1:
        source = state.layerTextures[0][1].Get();
        break;
    case Ngu720DoubleSurface::FinalLayer2:
        source = state.layerTextures[0][2].Get();
        break;
    case Ngu720DoubleSurface::Dense:
        source = state.denseTexture.Get();
        break;
    case Ngu720DoubleSurface::Add:
        source = state.addTexture.Get();
        break;
    case Ngu720DoubleSurface::ChromaX:
        source = state.chromaXTexture.Get();
        break;
    case Ngu720DoubleSurface::ChromaY:
        source = state.chromaYTexture.Get();
        break;
    case Ngu720DoubleSurface::Merge:
        source = state.mergeTexture.Get();
        break;
    }
    if (!source)
    {
        error_ = "NGU 720 Double surface does not exist";
        return false;
    }
    ID3D11Resource* destination = nullptr;
    output->GetResource(&destination);
    if (!destination)
    {
        error_ = "NGU 720 Double destination resource is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(context.GetAddressOf());
    context->CopyResource(destination, source);
    destination->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma720DoubleHighBitRgb(ID3D11RenderTargetView* output)
{
    if (!nativeLuma720DoubleHighBit_ || !output || !nativeLuma720DoubleHighBit_->rgb.texture ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 2560, 1440))
    {
        error_ = "NGU P010 720p Double RGB surface copy is unavailable";
        return false;
    }
    ID3D11Resource* destination = nullptr;
    output->GetResource(&destination);
    if (!destination)
    {
        error_ = "NGU P010 720p Double RGB destination is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(context.GetAddressOf());
    context->CopyResource(destination, nativeLuma720DoubleHighBit_->rgb.texture.Get());
    destination->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma720DoubleHighBitSurface(NguHighBit720Surface surface, ID3D11RenderTargetView* output)
{
    if (!nativeLuma720DoubleHighBit_ || !output)
    {
        error_ = "NGU P010 720p Double surface copy is unavailable";
        return false;
    }
    auto& state = *nativeLuma720DoubleHighBit_;
    ID3D11Texture2D* source = nullptr;
    switch (surface)
    {
    case NguHighBit720Surface::SplitY:
        source = state.splitSource[0].texture.Get();
        break;
    case NguHighBit720Surface::SplitCb:
        source = state.splitSource[1].texture.Get();
        break;
    case NguHighBit720Surface::SplitCr:
        source = state.splitSource[2].texture.Get();
        break;
    case NguHighBit720Surface::UpscaledY:
        source = state.upY[0].texture.Get();
        break;
    case NguHighBit720Surface::UpscaledCb:
        source = state.upY[1].texture.Get();
        break;
    case NguHighBit720Surface::UpscaledCr:
        source = state.upY[2].texture.Get();
        break;
    case NguHighBit720Surface::LumaResidual:
        source = state.lumaDense.texture.Get();
        break;
    case NguHighBit720Surface::ChromaResidualCb:
        source = state.chromaDense[0].texture.Get();
        break;
    case NguHighBit720Surface::ChromaResidualCr:
        source = state.chromaDense[1].texture.Get();
        break;
    case NguHighBit720Surface::ChromaCb:
        source = state.chromaResult[0].texture.Get();
        break;
    case NguHighBit720Surface::ChromaCr:
        source = state.chromaResult[1].texture.Get();
        break;
    case NguHighBit720Surface::Rgb:
        source = state.rgb.texture.Get();
        break;
    }
    if (!source)
    {
        error_ = "NGU P010 720p Double surface does not exist";
        return false;
    }
    ID3D11Resource* destination = nullptr;
    output->GetResource(&destination);
    if (!destination)
    {
        error_ = "NGU P010 720p Double surface destination is unavailable";
        return false;
    }
    ComPtr<ID3D11DeviceContext> context;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(context.GetAddressOf());
    context->CopyResource(destination, source);
    destination->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLumaVeryHighAdd(ID3D11RenderTargetView* output)
{
    if (!nativeLumaVeryHigh1080_ || !nativeLumaVeryHigh1080_->addTexture || !output)
    {
        error_ = "NGU VeryHigh luma add output is unavailable";
        return false;
    }
    ComPtr<ID3D11Resource> destination;
    output->GetResource(&destination);
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->CopyResource(destination.Get(), nativeLumaVeryHigh1080_->addTexture.Get());
    context->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLumaVeryHighUpX(ID3D11RenderTargetView* output)
{
    if (!nativeLumaVeryHigh1080_ || !nativeLumaVeryHigh1080_->upXTexture || !output)
    {
        error_ = "NGU VeryHigh luma UpX output is unavailable";
        return false;
    }
    ComPtr<ID3D11Resource> destination;
    output->GetResource(&destination);
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->CopyResource(destination.Get(), nativeLumaVeryHigh1080_->upXTexture.Get());
    context->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLumaVeryHighUpY(ID3D11RenderTargetView* output)
{
    if (!nativeLumaVeryHigh1080_ || !nativeLumaVeryHigh1080_->upYTexture || !output)
    {
        error_ = "NGU VeryHigh luma UpY output is unavailable";
        return false;
    }
    ComPtr<ID3D11Resource> destination;
    output->GetResource(&destination);
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->CopyResource(destination.Get(), nativeLumaVeryHigh1080_->upYTexture.Get());
    context->Release();
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLumaVeryHighMerge(ID3D11RenderTargetView* output)
{
    if (!nativeLumaVeryHigh1080_ || !nativeLumaVeryHigh1080_->mergeTexture || !output)
    {
        error_ = "NGU VeryHigh luma merge output is unavailable";
        return false;
    }
    ComPtr<ID3D11Resource> destination;
    output->GetResource(&destination);
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->CopyResource(destination.Get(), nativeLumaVeryHigh1080_->mergeTexture.Get());
    context->Release();
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeSuperResPrimitives(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU SuperChromaRes requires a GPU";
        return false;
    }
    if (nativeSuperRes_ && gpu == gpu_)
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU SuperChromaRes requires the D3D11 backend";
        return false;
    }
    const std::string vertexText = LoadBundledText(L"shaders\\Ngu\\superres_down_x.hlsl", 64 * 1024);
    const auto downXCso = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x.cso", 0);
    const auto downYCso = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y.cso", 0);
    const auto diffCso = LoadBundledBytes(L"shaders\\Ngu\\superres_diff.cso", 0);
    const auto toRgbMainCso = LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main.cso", 0);
    const auto toRgbBaseCso = LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base.cso", 0);
    const auto downXConstants = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_constants.bin", 2 * sizeof(float) * 4);
    const auto downYConstants = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbMainConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants.bin", 6 * sizeof(float) * 4);
    const auto toRgbBaseConstantsP010 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants_p010.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsHdr =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_constants_hdr.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsHdr =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants_hdr.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsHlg =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_constants_hlg.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsHlg =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants_hlg.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsBt709Pq =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_constants_bt709pq.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsBt709Pq =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants_bt709pq.bin", 6 * sizeof(float) * 4);
    const auto downXTable = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_table.bin", 4 * 640 * 8);
    const auto downYTable = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_table.bin", 4 * 360 * 8);
    const auto uvLut = LoadBundledBytes(L"shaders\\Ngu\\superres_uv_lut.bin", 2048 * 8);
    const auto downXUvLut = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_uv_lut.bin", (640 + 720) * 8);
    const auto downYUvLut = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_uv_lut.bin", (640 + 360) * 8);
    const auto downXCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_1080.cso", 0);
    const auto downYCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_1080.cso", 0);
    const auto diffCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_diff_1080.cso", 0);
    const auto toRgbMainCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_1080.cso", 0);
    const auto toRgbBaseCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_1080.cso", 0);
    const auto downXConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_constants.bin", 2 * sizeof(float) * 4);
    const auto downYConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbMainConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_main_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants.bin", 6 * sizeof(float) * 4);
    const auto toRgbBaseConstantsP0101080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants_p010.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsHdr1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_main_constants_hdr.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsHdr1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants_hdr.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsHlg1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_main_constants_hlg.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsHlg1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants_hlg.bin", 6 * sizeof(float) * 4);
    const auto toRgbMainConstantsBt709Pq1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_main_constants_bt709pq.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstantsBt709Pq1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants_bt709pq.bin", 6 * sizeof(float) * 4);
    const auto downXTable1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_table.bin", 4 * 960 * 8);
    const auto downYTable1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_table.bin", 4 * 540 * 8);
    const auto uvLut1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_uv_lut.bin", 4096 * 8);
    const auto downXUvLut1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_uv_lut.bin", (960 + 1080) * 8);
    const auto downYUvLut1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_uv_lut.bin", (960 + 540) * 8);
    if (vertexText.empty() || downXCso.empty() || downYCso.empty() || diffCso.empty() || toRgbMainCso.empty() ||
        toRgbBaseCso.empty() || downXConstants.empty() || downYConstants.empty() || toRgbMainConstants.empty() ||
        toRgbBaseConstants.empty() || toRgbBaseConstantsP010.empty() || toRgbMainConstantsHdr.empty() ||
        toRgbBaseConstantsHdr.empty() || toRgbMainConstantsHlg.empty() || toRgbBaseConstantsHlg.empty() ||
        toRgbMainConstantsBt709Pq.empty() || toRgbBaseConstantsBt709Pq.empty() || downXTable.empty() ||
        downYTable.empty() || uvLut.empty() || downXUvLut.empty() || downYUvLut.empty() || downXCso1080.empty() ||
        downYCso1080.empty() || diffCso1080.empty() || toRgbMainCso1080.empty() || toRgbBaseCso1080.empty() ||
        downXConstants1080.empty() || downYConstants1080.empty() || toRgbMainConstants1080.empty() ||
        toRgbBaseConstants1080.empty() || downXTable1080.empty() || toRgbBaseConstantsP0101080.empty() ||
        toRgbMainConstantsHdr1080.empty() || toRgbBaseConstantsHdr1080.empty() || toRgbMainConstantsHlg1080.empty() ||
        toRgbBaseConstantsHlg1080.empty() || toRgbMainConstantsBt709Pq1080.empty() ||
        toRgbBaseConstantsBt709Pq1080.empty() || downYTable1080.empty() || uvLut1080.empty() ||
        downXUvLut1080.empty() || downYUvLut1080.empty())
    {
        error_ = "NGU SuperChromaRes primitive assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(vertexText.data(),
                            vertexText.size(),
                            "NGU_SuperResVertex",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU SuperChromaRes vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeSuperResPrimitives>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(downXCso.data(), static_cast<SIZE_T>(downXCso.size()), nullptr, &state->downX)) ||
        FAILED(
            device->CreatePixelShader(downYCso.data(), static_cast<SIZE_T>(downYCso.size()), nullptr, &state->downY)) ||
        FAILED(device->CreatePixelShader(diffCso.data(), static_cast<SIZE_T>(diffCso.size()), nullptr, &state->diff)) ||
        FAILED(device->CreatePixelShader(
            toRgbMainCso.data(), static_cast<SIZE_T>(toRgbMainCso.size()), nullptr, &state->toRgbMain)) ||
        FAILED(device->CreatePixelShader(
            toRgbBaseCso.data(), static_cast<SIZE_T>(toRgbBaseCso.size()), nullptr, &state->toRgbBase)) ||
        FAILED(device->CreatePixelShader(
            downXCso1080.data(), static_cast<SIZE_T>(downXCso1080.size()), nullptr, &state->downX1080)) ||
        FAILED(device->CreatePixelShader(
            downYCso1080.data(), static_cast<SIZE_T>(downYCso1080.size()), nullptr, &state->downY1080)) ||
        FAILED(device->CreatePixelShader(
            diffCso1080.data(), static_cast<SIZE_T>(diffCso1080.size()), nullptr, &state->diff1080)) ||
        FAILED(device->CreatePixelShader(
            toRgbMainCso1080.data(), static_cast<SIZE_T>(toRgbMainCso1080.size()), nullptr, &state->toRgbMain1080)) ||
        FAILED(device->CreatePixelShader(
            toRgbBaseCso1080.data(), static_cast<SIZE_T>(toRgbBaseCso1080.size()), nullptr, &state->toRgbBase1080)))
    {
        error_ = "NGU SuperChromaRes shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(downXConstants, state->downXConstants) ||
        !createConstants(downYConstants, state->downYConstants) ||
        !createConstants(toRgbMainConstants, state->toRgbMainConstants) ||
        !createConstants(toRgbBaseConstants, state->toRgbBaseConstants) ||
        !createConstants(toRgbBaseConstantsP010, state->toRgbBaseConstantsP010) ||
        !createConstants(toRgbMainConstantsHdr, state->toRgbMainConstantsHdr) ||
        !createConstants(toRgbBaseConstantsHdr, state->toRgbBaseConstantsHdr) ||
        !createConstants(toRgbMainConstantsHlg, state->toRgbMainConstantsHlg) ||
        !createConstants(toRgbBaseConstantsHlg, state->toRgbBaseConstantsHlg) ||
        !createConstants(toRgbMainConstantsBt709Pq, state->toRgbMainConstantsBt709Pq) ||
        !createConstants(toRgbBaseConstantsBt709Pq, state->toRgbBaseConstantsBt709Pq) ||
        !createConstants(downXConstants1080, state->downXConstants1080) ||
        !createConstants(downYConstants1080, state->downYConstants1080) ||
        !createConstants(toRgbMainConstants1080, state->toRgbMainConstants1080) ||
        !createConstants(toRgbBaseConstants1080, state->toRgbBaseConstants1080) ||
        !createConstants(toRgbBaseConstantsP0101080, state->toRgbBaseConstantsP0101080) ||
        !createConstants(toRgbMainConstantsHdr1080, state->toRgbMainConstantsHdr1080) ||
        !createConstants(toRgbBaseConstantsHdr1080, state->toRgbBaseConstantsHdr1080) ||
        !createConstants(toRgbMainConstantsHlg1080, state->toRgbMainConstantsHlg1080) ||
        !createConstants(toRgbBaseConstantsHlg1080, state->toRgbBaseConstantsHlg1080) ||
        !createConstants(toRgbMainConstantsBt709Pq1080, state->toRgbMainConstantsBt709Pq1080) ||
        !createConstants(toRgbBaseConstantsBt709Pq1080, state->toRgbBaseConstantsBt709Pq1080))
    {
        error_ = "NGU SuperChromaRes constants could not be created";
        return false;
    }
    std::vector<uint8_t> class3MainConstants = toRgbMainConstants;
    float* class3Main = reinterpret_cast<float*>(class3MainConstants.data());
    class3Main[1] = 720.0f;
    class3Main[3] = 1.0f / 720.0f;
    if (!createConstants(class3MainConstants, state->toRgbMainConstantsClass3))
    {
        error_ = "NGU class-3 SuperChromaRes main constants could not be created";
        return false;
    }
    std::vector<uint8_t> class3BaseConstants = toRgbBaseConstantsP010;
    float* class3Base = reinterpret_cast<float*>(class3BaseConstants.data());
    class3Base[1] = 720.0f;
    class3Base[3] = 1.0f / 720.0f;
    if (!createConstants(class3BaseConstants, state->toRgbBaseConstantsClass3P010))
    {
        error_ = "NGU class-3 SuperChromaRes base constants could not be created";
        return false;
    }
    std::vector<uint8_t> class3MainConstants1080 = toRgbMainConstants1080;
    float* class3Main1080 = reinterpret_cast<float*>(class3MainConstants1080.data());
    class3Main1080[1] = 1080.0f;
    class3Main1080[3] = 1.0f / 1080.0f;
    if (!createConstants(class3MainConstants1080, state->toRgbMainConstantsClass3_1080))
    {
        error_ = "NGU 1080p class-3 SuperChromaRes main constants could not be created";
        return false;
    }
    std::vector<uint8_t> class3BaseConstants1080 = toRgbBaseConstantsP0101080;
    float* class3Base1080 = reinterpret_cast<float*>(class3BaseConstants1080.data());
    class3Base1080[1] = 1080.0f;
    class3Base1080[3] = 1.0f / 1080.0f;
    if (!createConstants(class3BaseConstants1080, state->toRgbBaseConstantsClass3P010_1080))
    {
        error_ = "NGU 1080p class-3 SuperChromaRes base constants could not be created";
        return false;
    }

    auto createTable = [&](const std::vector<uint8_t>& bytes,
                           int height,
                           ComPtr<ID3D11Texture2D>& texture,
                           ComPtr<ID3D11ShaderResourceView>& view)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 4;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        data.SysMemPitch = 4 * 8;
        return SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
    };
    if (!createTable(downXTable, 640, state->downXTable, state->downXTableView) ||
        !createTable(downYTable, 360, state->downYTable, state->downYTableView) ||
        !createTable(downXTable1080, 960, state->downXTable1080, state->downXTableView1080) ||
        !createTable(downYTable1080, 540, state->downYTable1080, state->downYTableView1080))
    {
        error_ = "NGU SuperChromaRes tables could not be created";
        return false;
    }
    D3D11_TEXTURE2D_DESC uvLutDesc = {};
    uvLutDesc.Width = 2048;
    uvLutDesc.Height = 1;
    uvLutDesc.MipLevels = 1;
    uvLutDesc.ArraySize = 1;
    uvLutDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    uvLutDesc.SampleDesc.Count = 1;
    uvLutDesc.Usage = D3D11_USAGE_IMMUTABLE;
    uvLutDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA uvLutData = {};
    uvLutData.pSysMem = uvLut.data();
    uvLutData.SysMemPitch = 2048 * 8;
    if (FAILED(device->CreateTexture2D(&uvLutDesc, &uvLutData, &state->uvLut)) ||
        FAILED(device->CreateShaderResourceView(state->uvLut.Get(), nullptr, &state->uvLutView)))
    {
        error_ = "NGU SuperChromaRes UV LUT could not be created";
        return false;
    }
    auto createUvLut = [&](const std::vector<uint8_t>& bytes,
                           UINT width,
                           ComPtr<ID3D11Texture2D>& texture,
                           ComPtr<ID3D11ShaderResourceView>& view)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        data.SysMemPitch = width * 8;
        return SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view));
    };
    if (!createUvLut(downXUvLut, 640 + 720, state->downXUvLut, state->downXUvLutView) ||
        !createUvLut(downYUvLut, 640 + 360, state->downYUvLut, state->downYUvLutView) ||
        !createUvLut(uvLut1080, 4096, state->uvLut1080, state->uvLutView1080) ||
        !createUvLut(downXUvLut1080, 960 + 1080, state->downXUvLut1080, state->downXUvLutView1080) ||
        !createUvLut(downYUvLut1080, 960 + 540, state->downYUvLut1080, state->downYUvLutView1080))
    {
        error_ = "NGU SuperChromaRes downsample UV LUTs could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU SuperChromaRes sampler could not be created";
        return false;
    }
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->linearSampler)))
    {
        error_ = "NGU SuperChromaRes linear sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU SuperChromaRes rasterizer could not be created";
        return false;
    }

    nativeSuperRes_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighSuperResDownX;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDownX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !input || !output)
    {
        error_ = "NGU SuperChromaRes X downsample request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeSuperRes_->downXTableView.Get(), nativeSuperRes_->downXUvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->downX.Get(),
                  nativeSuperRes_->downXConstants.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  640,
                  720,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDownY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !input || !output)
    {
        error_ = "NGU SuperChromaRes Y downsample request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeSuperRes_->downYTableView.Get(), nativeSuperRes_->downYUvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->downY.Get(),
                  nativeSuperRes_->downYConstants.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  640,
                  360,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDiff(ID3D11ShaderResourceView* original,
                                      ID3D11ShaderResourceView* downsample,
                                      ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !downsample || !output)
    {
        error_ = "NGU SuperChromaRes difference request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {original, downsample, nativeSuperRes_->downYUvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->diff.Get(),
                  nullptr,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  640,
                  360,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDiffClass3_720(ID3D11ShaderResourceView* original,
                                                ID3D11ShaderResourceView* downsample,
                                                ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !downsample || !output)
    {
        error_ = "NGU class-3 SuperChromaRes difference request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {original, downsample, nativeSuperRes_->downXUvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->diff.Get(),
                  nullptr,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  640,
                  720,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDiffClass3_1080(ID3D11ShaderResourceView* original,
                                                 ID3D11ShaderResourceView* downsample,
                                                 ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !downsample || !output)
    {
        error_ = "NGU 1080p class-3 SuperChromaRes difference request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {original, downsample, nativeSuperRes_->downXUvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->diff1080.Get(),
                  nullptr,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  960,
                  1080,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbMain(ID3D11ShaderResourceView* original,
                                           ID3D11ShaderResourceView* work,
                                           ID3D11ShaderResourceView* difference,
                                           ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU SuperChromaRes main ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView.Get()};
    ID3D11Buffer* constants = nativeSuperRes_->toRgbMainConstants.Get();
    if (colorDomain_ == ColorDomainProfile::PqBt2020)
        constants = nativeSuperRes_->toRgbMainConstantsHdr.Get();
    else if (colorDomain_ == ColorDomainProfile::HlgBt2020)
        constants = nativeSuperRes_->toRgbMainConstantsHlg.Get();
    else if (colorDomain_ == ColorDomainProfile::PqBt709)
        constants = nativeSuperRes_->toRgbMainConstantsBt709Pq.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbMain.Get(),
                  constants,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1280,
                  720,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbBase(ID3D11ShaderResourceView* original,
                                           ID3D11ShaderResourceView* work,
                                           ID3D11ShaderResourceView* difference,
                                           ID3D11RenderTargetView* output,
                                           bool p010)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU SuperChromaRes base ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView.Get()};
    ID3D11Buffer* constants =
        p010 ? nativeSuperRes_->toRgbBaseConstantsP010.Get() : nativeSuperRes_->toRgbBaseConstants.Get();
    if (colorDomain_ == ColorDomainProfile::PqBt2020)
        constants = nativeSuperRes_->toRgbBaseConstantsHdr.Get();
    else if (colorDomain_ == ColorDomainProfile::HlgBt2020)
        constants = nativeSuperRes_->toRgbBaseConstantsHlg.Get();
    else if (colorDomain_ == ColorDomainProfile::PqBt709)
        constants = nativeSuperRes_->toRgbBaseConstantsBt709Pq.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbBase.Get(),
                  constants,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1280,
                  720,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbMainClass3(ID3D11ShaderResourceView* original,
                                                 ID3D11ShaderResourceView* work,
                                                 ID3D11ShaderResourceView* difference,
                                                 ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU class-3 main ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbMain.Get(),
                  nativeSuperRes_->toRgbMainConstantsClass3.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1280,
                  720,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbBaseClass3(ID3D11ShaderResourceView* original,
                                                 ID3D11ShaderResourceView* work,
                                                 ID3D11ShaderResourceView* difference,
                                                 ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU class-3 base ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbBase.Get(),
                  nativeSuperRes_->toRgbBaseConstantsClass3P010.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1280,
                  720,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeSuperResStrength3(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler requires a GPU";
        return false;
    }
    if (nativeSuperResStrength3_ && gpu == gpu_)
        return true;
    if (!PrepareNativeSuperResPrimitives(gpu))
        return false;

    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto state = std::make_unique<NativeSuperResStrength3>();
    auto create = [&](UINT width,
                      UINT height,
                      DXGI_FORMAT format,
                      ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& renderTarget,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!create(
            640, 720, DXGI_FORMAT_R16G16B16A16_UNORM, state->downXTexture, state->downXView, state->downXShaderView) ||
        !create(
            640, 360, DXGI_FORMAT_R16G16B16A16_UNORM, state->downYTexture, state->downYView, state->downYShaderView) ||
        !create(640, 360, DXGI_FORMAT_R16G16_FLOAT, state->diffTexture, state->diffView, state->diffShaderView) ||
        !create(
            1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, state->mainTexture, state->mainView, state->mainShaderView) ||
        !create(1280,
                720,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->outputTexture,
                state->outputView,
                state->outputShaderView))
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler surfaces could not be created";
        return false;
    }

    nativeSuperResStrength3_ = std::move(state);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResStrength3(ID3D11ShaderResourceView* source,
                                           ID3D11ShaderResourceView* diffOriginal,
                                           ID3D11ShaderResourceView* mainOriginal,
                                           ID3D11ShaderResourceView* baseOriginal,
                                           ID3D11RenderTargetView* output,
                                           bool p010)
{
    const bool mainIsRgba16 =
        mainOriginal && ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool mainIsR8 = mainOriginal && ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R8_UNORM, 1280, 720);
    const bool baseIsRgba16 =
        baseOriginal && ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool baseIsR8 = baseOriginal && ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R8_UNORM, 1280, 720);
    const bool diffIsBgra8 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_B8G8R8A8_UNORM, 640, 360);
    const bool diffIsRgba16 =
        diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 640, 360);
    const bool diffIsRg16 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16_UNORM, 640, 360);
    const bool diffIsRg8 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R8G8_UNORM, 640, 360);
    if (!nativeSuperRes_ || !nativeSuperResStrength3_ || !source || !diffOriginal || !mainOriginal || !baseOriginal ||
        !output)
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler request is invalid";
        return false;
    }
    if (!ViewMatchesTexture(source, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) || (!mainIsRgba16 && !mainIsR8) ||
        (!baseIsRgba16 && !baseIsR8) || (!diffIsBgra8 && !diffIsRgba16 && !diffIsRg16 && !diffIsRg8) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720))
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler input format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeSuperResStrength3_;
    if (!RunNativeSuperResDownX(source, state.downXView.Get()) ||
        !RunNativeSuperResDownY(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbMain(mainOriginal, source, state.diffShaderView.Get(), state.mainView.Get()) ||
        !RunNativeSuperResDownX(state.mainShaderView.Get(), state.downXView.Get()) ||
        !RunNativeSuperResDownY(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbBase(baseOriginal, state.mainShaderView.Get(), state.diffShaderView.Get(), output, p010))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDownX1080(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !input || !output)
    {
        error_ = "NGU 1080p SuperChromaRes X downsample request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeSuperRes_->downXTableView1080.Get(), nativeSuperRes_->downXUvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->downX1080.Get(),
                  nativeSuperRes_->downXConstants1080.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  960,
                  1080,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDownY1080(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !input || !output)
    {
        error_ = "NGU 1080p SuperChromaRes Y downsample request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {
        input, nativeSuperRes_->downYTableView1080.Get(), nativeSuperRes_->downYUvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->downY1080.Get(),
                  nativeSuperRes_->downYConstants1080.Get(),
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  960,
                  540,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDiff1080(ID3D11ShaderResourceView* original,
                                          ID3D11ShaderResourceView* downsample,
                                          ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !downsample || !output)
    {
        error_ = "NGU 1080p SuperChromaRes difference request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {original, downsample, nativeSuperRes_->downYUvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->diff1080.Get(),
                  nullptr,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  960,
                  540,
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbMain1080(ID3D11ShaderResourceView* original,
                                               ID3D11ShaderResourceView* work,
                                               ID3D11ShaderResourceView* difference,
                                               ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU 1080p SuperChromaRes main ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView1080.Get()};
    ID3D11Buffer* constants = nativeSuperRes_->toRgbMainConstants1080.Get();
    if (colorDomain_ == ColorDomainProfile::PqBt2020)
        constants = nativeSuperRes_->toRgbMainConstantsHdr1080.Get();
    else if (colorDomain_ == ColorDomainProfile::HlgBt2020)
        constants = nativeSuperRes_->toRgbMainConstantsHlg1080.Get();
    else if (colorDomain_ == ColorDomainProfile::PqBt709)
        constants = nativeSuperRes_->toRgbMainConstantsBt709Pq1080.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbMain1080.Get(),
                  constants,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1920,
                  1080,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResToRgbBase1080(ID3D11ShaderResourceView* original,
                                               ID3D11ShaderResourceView* work,
                                               ID3D11ShaderResourceView* difference,
                                               ID3D11RenderTargetView* output,
                                               bool p010)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU 1080p SuperChromaRes base ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {original, work, difference, nativeSuperRes_->uvLutView1080.Get()};
    ID3D11Buffer* constants =
        p010 ? nativeSuperRes_->toRgbBaseConstantsP0101080.Get() : nativeSuperRes_->toRgbBaseConstants1080.Get();
    if (colorDomain_ == ColorDomainProfile::PqBt2020)
        constants = nativeSuperRes_->toRgbBaseConstantsHdr1080.Get();
    else if (colorDomain_ == ColorDomainProfile::HlgBt2020)
        constants = nativeSuperRes_->toRgbBaseConstantsHlg1080.Get();
    else if (colorDomain_ == ColorDomainProfile::PqBt709)
        constants = nativeSuperRes_->toRgbBaseConstantsBt709Pq1080.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbBase1080.Get(),
                  constants,
                  nativeSuperRes_->rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  1920,
                  1080,
                  nativeSuperRes_->linearSampler.Get(),
                  nullptr,
                  nullptr,
                  nativeSuperRes_->pointSampler.Get(),
                  nativeSuperRes_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeSuperResStrength3_1080(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler requires a GPU";
        return false;
    }
    if (nativeSuperResStrength3_1080_ && gpu == gpu_)
        return true;
    if (!PrepareNativeSuperResPrimitives(gpu))
        return false;

    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto state = std::make_unique<NativeSuperResStrength3_1080>();
    auto create = [&](UINT width,
                      UINT height,
                      DXGI_FORMAT format,
                      ComPtr<ID3D11Texture2D>& texture,
                      ComPtr<ID3D11RenderTargetView>& renderTarget,
                      ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!create(
            960, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, state->downXTexture, state->downXView, state->downXShaderView) ||
        !create(
            960, 540, DXGI_FORMAT_R16G16B16A16_UNORM, state->downYTexture, state->downYView, state->downYShaderView) ||
        !create(960, 540, DXGI_FORMAT_R16G16_FLOAT, state->diffTexture, state->diffView, state->diffShaderView) ||
        !create(
            1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, state->mainTexture, state->mainView, state->mainShaderView) ||
        !create(1920,
                1080,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->outputTexture,
                state->outputView,
                state->outputShaderView))
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler surfaces could not be created";
        return false;
    }

    nativeSuperResStrength3_1080_ = std::move(state);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResStrength3_1080(ID3D11ShaderResourceView* source,
                                                ID3D11ShaderResourceView* diffOriginal,
                                                ID3D11ShaderResourceView* mainOriginal,
                                                ID3D11ShaderResourceView* baseOriginal,
                                                ID3D11RenderTargetView* output,
                                                bool p010)
{
    const bool diffIsBgra8 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_B8G8R8A8_UNORM, 960, 540);
    const bool diffIsRgba16 =
        diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 960, 540);
    const bool diffIsRg16 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R16G16_UNORM, 960, 540);
    const bool diffIsRg8 = diffOriginal && ViewMatchesTexture(diffOriginal, DXGI_FORMAT_R8G8_UNORM, 960, 540);
    if (!nativeSuperRes_ || !nativeSuperResStrength3_1080_ || !source || !diffOriginal || !mainOriginal ||
        !baseOriginal || !output)
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler request is invalid";
        return false;
    }
    if (!ViewMatchesTexture(source, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        (!diffIsBgra8 && !diffIsRgba16 && !diffIsRg16 && !diffIsRg8) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080))
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler input format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeSuperResStrength3_1080_;
    if (!RunNativeSuperResDownX1080(source, state.downXView.Get()) ||
        !RunNativeSuperResDownY1080(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff1080(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbMain1080(mainOriginal, source, state.diffShaderView.Get(), state.mainView.Get()) ||
        !RunNativeSuperResDownX1080(state.mainShaderView.Get(), state.downXView.Get()) ||
        !RunNativeSuperResDownY1080(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff1080(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbBase1080(
            baseOriginal, state.mainShaderView.Get(), state.diffShaderView.Get(), output, p010))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720Low(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720p 4x luma core requires a GPU";
        return false;
    }
    if (nativeLuma4x720Low_ && gpu == gpu_)
        return true;
    if (!PrepareNativeLumaSplit(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU 720p 4x luma core requires the D3D11 backend";
        return false;
    }

    const std::string scaleXText = LoadBundledText(L"shaders\\Ngu\\luma_4x720_scale_x.hlsl", 64 * 1024);
    const std::string initialText = LoadBundledText(L"shaders\\Ngu\\luma_1080_cubici1.hlsl", 256 * 1024);
    const auto scaleXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_x.cso", 0);
    const auto scaleYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_y.cso", 0);
    const auto layerCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer.cso", 0);
    const auto scaleXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_x_constants.bin", 32);
    const auto scaleYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_y_constants.bin", 32);
    const auto initialConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_initial_constants.bin", 512);
    const auto layerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_low_layer_constants.bin", 15 * 111 * sizeof(float) * 4);
    const auto scaleXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_table_x.bin", 163840);
    const auto scaleYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_table_y.bin", 92160);
    if (scaleXText.empty() || initialText.empty() || scaleXCso.empty() || scaleYCso.empty() || layerCso.empty() ||
        scaleXConstants.empty() || scaleYConstants.empty() || initialConstants.empty() || layerConstants.empty() ||
        scaleXTable.empty() || scaleYTable.empty())
    {
        error_ = "NGU 720p 4x luma core assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, initialBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(scaleXText.data(),
                            scaleXText.size(),
                            "NGU_Luma4x720_Vertex",
                            nullptr,
                            nullptr,
                            "VSMain",
                            "vs_5_0",
                            flags,
                            0,
                            &vertexBlob,
                            &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU 720p 4x vertex shader failed";
        return false;
    }
    errors.Reset();
    hr = D3DCompile(initialText.data(),
                    initialText.size(),
                    "NGU_Luma4x720_Initial",
                    nullptr,
                    nullptr,
                    "PSMain",
                    "ps_5_0",
                    flags,
                    0,
                    &initialBlob,
                    &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU 720p 4x initializer shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720Low>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            scaleXCso.data(), static_cast<SIZE_T>(scaleXCso.size()), nullptr, &state->scaleX)) ||
        FAILED(device->CreatePixelShader(
            scaleYCso.data(), static_cast<SIZE_T>(scaleYCso.size()), nullptr, &state->scaleY)) ||
        FAILED(device->CreatePixelShader(
            initialBlob->GetBufferPointer(), initialBlob->GetBufferSize(), nullptr, &state->initial)) ||
        FAILED(
            device->CreatePixelShader(layerCso.data(), static_cast<SIZE_T>(layerCso.size()), nullptr, &state->layer)))
    {
        error_ = "NGU 720p 4x luma shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(scaleXConstants, state->scaleXConstants) ||
        !createConstants(scaleYConstants, state->scaleYConstants) ||
        !createConstants(initialConstants, state->initialConstants))
    {
        error_ = "NGU 720p 4x luma constants could not be created";
        return false;
    }
    for (size_t layer = 0; layer < state->layerConstants.size(); ++layer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = 111 * sizeof(float) * 4;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = layerConstants.data() + layer * desc.ByteWidth;
        if (FAILED(device->CreateBuffer(&desc, &data, &state->layerConstants[layer])))
        {
            error_ = "NGU 720p 4x luma layer constants could not be created";
            return false;
        }
    }

    auto createTable = [&](const std::vector<uint8_t>& bytes,
                           UINT height,
                           ComPtr<ID3D11Texture2D>& texture,
                           ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 4;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        data.SysMemPitch = 4 * 8;
        return SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createTable(scaleXTable, 5120, state->scaleXTable, state->scaleXTableView) ||
        !createTable(scaleYTable, 2880, state->scaleYTable, state->scaleYTableView))
    {
        error_ = "NGU 720p 4x luma scale tables could not be created";
        return false;
    }

    auto createTexture = [&](UINT width,
                             UINT height,
                             DXGI_FORMAT format,
                             ComPtr<ID3D11Texture2D>& texture,
                             ComPtr<ID3D11RenderTargetView>& renderTarget,
                             ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createTexture(1280,
                       720,
                       DXGI_FORMAT_R16_UNORM,
                       state->splitLumaTexture,
                       state->splitLumaView,
                       state->splitLumaShaderView) ||
        !createTexture(1280,
                       720,
                       DXGI_FORMAT_R16G16_UNORM,
                       state->splitCbCrTexture,
                       state->splitCbCrView,
                       state->splitCbCrShaderView) ||
        !createTexture(
            5120, 720, DXGI_FORMAT_R16_UNORM, state->scaleXTexture, state->scaleXView, state->scaleXShaderView) ||
        !createTexture(
            5120, 2880, DXGI_FORMAT_R16_UNORM, state->scaleYTexture, state->scaleYView, state->scaleYShaderView))
    {
        error_ = "NGU 720p 4x luma fixed surfaces could not be created";
        return false;
    }
    for (size_t i = 0; i < state->featureATextures.size(); ++i)
    {
        if (!createTexture(1280,
                           720,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           state->featureATextures[i],
                           state->featureAViews[i],
                           state->featureAShaderViews[i]) ||
            !createTexture(1280,
                           720,
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           state->featureBTextures[i],
                           state->featureBViews[i],
                           state->featureBShaderViews[i]))
        {
            error_ = "NGU 720p 4x luma feature surfaces could not be created";
            return false;
        }
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU 720p 4x luma rasterizer could not be created";
        return false;
    }

    nativeLuma4x720Low_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720LowCore;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720Low(ID3D11ShaderResourceView* sourceRgb,
                                      int sourceWidth,
                                      int sourceHeight,
                                      bool sourcePacked)
{
    if (!nativeLuma4x720Low_ || !nativeLumaSplit_ || !sourceRgb || sourceWidth != 1280 || sourceHeight != 720)
    {
        error_ = "NGU 720p 4x luma core request is outside the verified path";
        return false;
    }
    if (!ViewMatchesTexture(sourceRgb, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720))
    {
        error_ = "NGU 720p 4x luma source format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeLuma4x720Low_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    if (!sourcePacked &&
        !RunNativeLumaSplit(sourceRgb, sourceWidth, sourceHeight, state.splitLumaView.Get(), state.splitCbCrView.Get()))
        return false;

    ID3D11ShaderResourceView* scaleXInputs[2] = {state.splitLumaShaderView.Get(), state.scaleXTableView.Get()};
    ID3D11RenderTargetView* scaleXOutput = state.scaleXView.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.scaleX.Get(),
                  state.scaleXConstants.Get(),
                  state.rasterizer.Get(),
                  scaleXInputs,
                  2,
                  &scaleXOutput,
                  1,
                  5120,
                  720,
                  nativeLumaSplit_->pointSampler.Get());
    ID3D11ShaderResourceView* scaleYInputs[2] = {state.scaleXShaderView.Get(), state.scaleYTableView.Get()};
    ID3D11RenderTargetView* scaleYOutput = state.scaleYView.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.scaleY.Get(),
                  state.scaleYConstants.Get(),
                  state.rasterizer.Get(),
                  scaleYInputs,
                  2,
                  &scaleYOutput,
                  1,
                  5120,
                  2880,
                  nativeLumaSplit_->pointSampler.Get());

    ID3D11ShaderResourceView* initialInput = state.splitLumaShaderView.Get();
    ID3D11RenderTargetView* initialOutputs[3] = {
        state.featureAViews[0].Get(), state.featureAViews[1].Get(), state.featureAViews[2].Get()};
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.initial.Get(),
                  state.initialConstants.Get(),
                  state.rasterizer.Get(),
                  &initialInput,
                  1,
                  initialOutputs,
                  3,
                  1280,
                  720,
                  nativeLumaSplit_->pointSampler.Get());

    for (size_t stage = 0; stage < 5; ++stage)
    {
        ID3D11ShaderResourceView* inputs[3] = {
            state.featureAShaderViews[0].Get(), state.featureAShaderViews[1].Get(), state.featureAShaderViews[2].Get()};
        for (size_t feature = 0; feature < 3; ++feature)
        {
            const size_t layer = stage * 3 + feature;
            ID3D11RenderTargetView* layerOutput = state.featureBViews[feature].Get();
            DrawD3D11Pass(device,
                          state.vertex.Get(),
                          state.layer.Get(),
                          state.layerConstants[layer].Get(),
                          state.rasterizer.Get(),
                          inputs,
                          3,
                          &layerOutput,
                          1,
                          1280,
                          720,
                          nativeLumaSplit_->pointSampler.Get());
        }
        state.featureATextures.swap(state.featureBTextures);
        state.featureAViews.swap(state.featureBViews);
        state.featureAShaderViews.swap(state.featureBShaderViews);
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720SplitY(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->splitLumaTexture.Get(), output))
    {
        error_ = "NGU 720p 4x split output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720SplitCbCr(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->splitCbCrTexture.Get(), output))
    {
        error_ = "NGU 720p 4x split CbCr output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720ScaleX(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->scaleXTexture.Get(), output))
    {
        error_ = "NGU 720p 4x X-scale output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720ScaleY(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->scaleYTexture.Get(), output))
    {
        error_ = "NGU 720p 4x Y-scale output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Initial(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !output)
    {
        error_ = "NGU 720p 4x initializer output is unavailable";
        return false;
    }
    auto& state = *nativeLuma4x720Low_;
    ID3D11ShaderResourceView* input = state.splitLumaShaderView.Get();
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.initial.Get(),
                  state.initialConstants.Get(),
                  state.rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  1280,
                  720,
                  nativeLumaSplit_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720LowLayer(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ || !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->featureATextures[2].Get(), output))
    {
        error_ = "NGU 720p 4x low-layer output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720High(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720p 4x high core requires a GPU";
        return false;
    }
    if (nativeLuma4x720High_ && gpu == gpu_)
        return true;
    if (!PrepareNativeLuma4x720Low(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU 720p 4x high core requires the D3D11 backend";
        return false;
    }

    const auto upsampleCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_upsample.cso", 0);
    const auto convertCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_convert.cso", 0);
    const auto upsampleConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_upsample_constants.bin", 3 * 196 * sizeof(float) * 4);
    const auto convertConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_convert_constants.bin", 3 * 2 * sizeof(float) * 4);
    const auto highLayerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_high_layer_constants.bin", 6 * 111 * sizeof(float) * 4);
    if (upsampleCso.empty() || convertCso.empty() || upsampleConstants.empty() || convertConstants.empty() ||
        highLayerConstants.empty())
    {
        error_ = "NGU 720p 4x high-core assets are unavailable";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720High>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreatePixelShader(
            upsampleCso.data(), static_cast<SIZE_T>(upsampleCso.size()), nullptr, &state->upsample)) ||
        FAILED(device->CreatePixelShader(
            convertCso.data(), static_cast<SIZE_T>(convertCso.size()), nullptr, &state->convert)))
    {
        error_ = "NGU 720p 4x high-core shaders could not be created";
        return false;
    }

    auto createConstants = [&](const uint8_t* bytes, UINT size, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes;
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    for (size_t feature = 0; feature < 3; ++feature)
    {
        if (!createConstants(
                upsampleConstants.data() + feature * 196 * 16, 196 * 16, state->upsampleConstants[feature]) ||
            !createConstants(convertConstants.data() + feature * 2 * 16, 2 * 16, state->convertConstants[feature]))
        {
            error_ = "NGU 720p 4x upsampler/convert constants could not be created";
            return false;
        }
    }
    for (size_t layer = 0; layer < 6; ++layer)
    {
        if (!createConstants(highLayerConstants.data() + layer * 111 * 16, 111 * 16, state->highLayerConstants[layer]))
        {
            error_ = "NGU 720p 4x high-layer constants could not be created";
            return false;
        }
    }

    auto createTexture = [&](UINT width,
                             UINT height,
                             ComPtr<ID3D11Texture2D>& texture,
                             ComPtr<ID3D11RenderTargetView>& renderTarget,
                             ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    for (size_t feature = 0; feature < 3; ++feature)
    {
        for (size_t part = 0; part < 4; ++part)
            if (!createTexture(1282,
                               722,
                               state->upFeatureTextures[feature][part],
                               state->upFeatureViews[feature][part],
                               state->upFeatureShaderViews[feature][part]))
            {
                error_ = "NGU 720p 4x upsampler surfaces could not be created";
                return false;
            }
        if (!createTexture(2560,
                           1440,
                           state->convertTextures[feature],
                           state->convertViews[feature],
                           state->convertShaderViews[feature]) ||
            !createTexture(2560,
                           1440,
                           state->highScratchTextures[feature],
                           state->highScratchViews[feature],
                           state->highScratchShaderViews[feature]))
        {
            error_ = "NGU 720p 4x high-core surfaces could not be created";
            return false;
        }
    }

    nativeLuma4x720High_ = std::move(state);
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720HighCore;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720High(ID3D11ShaderResourceView* sourceRgb,
                                       int sourceWidth,
                                       int sourceHeight,
                                       bool sourcePacked)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ ||
        !RunNativeLuma4x720Low(sourceRgb, sourceWidth, sourceHeight, sourcePacked))
    {
        if (nativeLuma4x720High_ && error_.empty())
            error_ = "NGU 720p 4x high-core request is unavailable";
        return false;
    }

    auto& low = *nativeLuma4x720Low_;
    auto& high = *nativeLuma4x720High_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    for (size_t feature = 0; feature < 3; ++feature)
    {
        ID3D11ShaderResourceView* inputs[3] = {
            low.featureAShaderViews[0].Get(), low.featureAShaderViews[1].Get(), low.featureAShaderViews[2].Get()};
        ID3D11RenderTargetView* outputs[4] = {high.upFeatureViews[feature][0].Get(),
                                              high.upFeatureViews[feature][1].Get(),
                                              high.upFeatureViews[feature][2].Get(),
                                              high.upFeatureViews[feature][3].Get()};
        DrawD3D11Pass(device,
                      low.vertex.Get(),
                      high.upsample.Get(),
                      high.upsampleConstants[feature].Get(),
                      low.rasterizer.Get(),
                      inputs,
                      3,
                      outputs,
                      4,
                      1282,
                      722,
                      nativeLumaSplit_->pointSampler.Get());
    }
    for (size_t feature = 0; feature < 3; ++feature)
    {
        ID3D11ShaderResourceView* inputs[4] = {high.upFeatureShaderViews[feature][0].Get(),
                                               high.upFeatureShaderViews[feature][1].Get(),
                                               high.upFeatureShaderViews[feature][2].Get(),
                                               high.upFeatureShaderViews[feature][3].Get()};
        ID3D11RenderTargetView* convertOutput = high.convertViews[feature].Get();
        DrawD3D11Pass(device,
                      low.vertex.Get(),
                      high.convert.Get(),
                      high.convertConstants[feature].Get(),
                      low.rasterizer.Get(),
                      inputs,
                      4,
                      &convertOutput,
                      1,
                      2560,
                      1440,
                      nativeLumaSplit_->pointSampler.Get());
    }
    for (size_t stage = 0; stage < 2; ++stage)
    {
        auto& inputViews = stage == 0 ? high.convertShaderViews : high.highScratchShaderViews;
        auto& outputViews = stage == 0 ? high.highScratchViews : high.convertViews;
        ID3D11ShaderResourceView* inputs[3] = {inputViews[0].Get(), inputViews[1].Get(), inputViews[2].Get()};
        for (size_t feature = 0; feature < 3; ++feature)
        {
            const size_t layer = stage * 3 + feature;
            ID3D11RenderTargetView* output = outputViews[feature].Get();
            DrawD3D11Pass(device,
                          low.vertex.Get(),
                          low.layer.Get(),
                          high.highLayerConstants[layer].Get(),
                          low.rasterizer.Get(),
                          inputs,
                          3,
                          &output,
                          1,
                          2560,
                          1440,
                          nativeLumaSplit_->pointSampler.Get());
        }
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Upsample(int feature, int part, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ || !output || feature < 0 || feature >= 3 || part < 0 ||
        part >= 4)
    {
        error_ = "NGU 720p 4x upsampler output request is invalid";
        return false;
    }
    auto& high = *nativeLuma4x720High_;
    if (!CopyTextureToTarget(gpu_, high.upFeatureTextures[feature][part].Get(), output))
    {
        error_ = "NGU 720p 4x upsampler output copy failed";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Convert(int feature, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ || !output || feature < 0 || feature >= 3)
    {
        error_ = "NGU 720p 4x converter output request is invalid";
        return false;
    }
    auto& low = *nativeLuma4x720Low_;
    auto& high = *nativeLuma4x720High_;
    ID3D11ShaderResourceView* inputs[4] = {high.upFeatureShaderViews[feature][0].Get(),
                                           high.upFeatureShaderViews[feature][1].Get(),
                                           high.upFeatureShaderViews[feature][2].Get(),
                                           high.upFeatureShaderViews[feature][3].Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  low.vertex.Get(),
                  high.convert.Get(),
                  high.convertConstants[feature].Get(),
                  low.rasterizer.Get(),
                  inputs,
                  4,
                  &output,
                  1,
                  2560,
                  1440,
                  nativeLumaSplit_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720HighLayer(int feature, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !output || feature < 0 || feature >= 3 ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720High_->convertTextures[feature].Get(), output))
    {
        error_ = "NGU 720p 4x high-layer output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720DenseAdd(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720p 4x dense/add requires a GPU";
        return false;
    }
    if (nativeLuma4x720DenseAdd_ && gpu == gpu_)
        return true;
    if (!PrepareNativeLuma4x720High(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU 720p 4x dense/add requires the D3D11 backend";
        return false;
    }

    const auto denseCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense.cso", 0);
    const auto addCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_add.cso", 0);
    const auto denseConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_dense_constants.bin", 816);
    const auto addConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_add_constants.bin", 32);
    if (denseCso.empty() || addCso.empty() || denseConstants.empty() || addConstants.empty())
    {
        error_ = "NGU 720p 4x dense/add assets are unavailable";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720DenseAdd>();
    ID3D11Device* device = d3d->device;
    if (FAILED(
            device->CreatePixelShader(denseCso.data(), static_cast<SIZE_T>(denseCso.size()), nullptr, &state->dense)) ||
        FAILED(device->CreatePixelShader(addCso.data(), static_cast<SIZE_T>(addCso.size()), nullptr, &state->add)))
    {
        error_ = "NGU 720p 4x dense/add shaders could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes, ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>(bytes.size());
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = bytes.data();
        return SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer));
    };
    if (!createConstants(denseConstants, state->denseConstants) || !createConstants(addConstants, state->addConstants))
    {
        error_ = "NGU 720p 4x dense/add constants could not be created";
        return false;
    }

    auto createTexture = [&](UINT width,
                             UINT height,
                             DXGI_FORMAT format,
                             ComPtr<ID3D11Texture2D>& texture,
                             ComPtr<ID3D11RenderTargetView>& renderTarget,
                             ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createTexture(2562,
                       1442,
                       DXGI_FORMAT_R16G16B16A16_FLOAT,
                       state->denseTexture,
                       state->denseView,
                       state->denseShaderView) ||
        !createTexture(5120, 2880, DXGI_FORMAT_R16_UNORM, state->addTexture, state->addView, state->addShaderView))
    {
        error_ = "NGU 720p 4x dense/add surfaces could not be created";
        return false;
    }

    nativeLuma4x720DenseAdd_ = std::move(state);
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720DenseAdd;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720DenseAdd(ID3D11ShaderResourceView* sourceRgb,
                                           int sourceWidth,
                                           int sourceHeight,
                                           bool sourcePacked)
{
    if (!nativeLuma4x720DenseAdd_ || !nativeLuma4x720High_ || !nativeLuma4x720Low_ ||
        !RunNativeLuma4x720High(sourceRgb, sourceWidth, sourceHeight, sourcePacked))
        return false;

    auto& low = *nativeLuma4x720Low_;
    auto& high = *nativeLuma4x720High_;
    auto& state = *nativeLuma4x720DenseAdd_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    ID3D11ShaderResourceView* denseInputs[3] = {
        high.convertShaderViews[0].Get(), high.convertShaderViews[1].Get(), high.convertShaderViews[2].Get()};
    ID3D11RenderTargetView* denseOutput = state.denseView.Get();
    DrawD3D11Pass(device,
                  low.vertex.Get(),
                  state.dense.Get(),
                  state.denseConstants.Get(),
                  low.rasterizer.Get(),
                  denseInputs,
                  3,
                  &denseOutput,
                  1,
                  2562,
                  1442,
                  nativeLumaSplit_->pointSampler.Get());

    ID3D11ShaderResourceView* addInputs[2] = {low.scaleYShaderView.Get(), state.denseShaderView.Get()};
    ID3D11RenderTargetView* addOutput = state.addView.Get();
    DrawD3D11Pass(device,
                  low.vertex.Get(),
                  state.add.Get(),
                  state.addConstants.Get(),
                  low.rasterizer.Get(),
                  addInputs,
                  2,
                  &addOutput,
                  1,
                  5120,
                  2880,
                  nativeLumaSplit_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Dense(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720DenseAdd_ || !CopyTextureToTarget(gpu_, nativeLuma4x720DenseAdd_->denseTexture.Get(), output))
    {
        error_ = "NGU 720p 4x dense output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Add(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720DenseAdd_ || !CopyTextureToTarget(gpu_, nativeLuma4x720DenseAdd_->addTexture.Get(), output))
    {
        error_ = "NGU 720p 4x add output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720Tail(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720p 4x tail requires a GPU";
        return false;
    }
    if (nativeLuma4x720Tail_ && gpu == gpu_)
        return true;
    if (!PrepareNativeLuma4x720DenseAdd(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU 720p 4x tail requires the D3D11 backend";
        return false;
    }

    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_4x720_tail_ycbcr.hlsl", 32 * 1024);
    const auto ycbcrCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ycbcr.cso", 0);
    const auto downXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x.cso", 0);
    const auto downXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_constants.bin", 32);
    const auto downXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_table.bin", 184320);
    const auto downXUvLut = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_uv_lut.bin", 61440);
    const auto downYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y.cso", 0);
    const auto downYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_constants.bin", 32);
    const auto downYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_table.bin", 103680);
    const auto downYUvLut = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_uv_lut.bin", 61440);
    const auto ssimRtCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_rt.cso", 0);
    const auto ssimRtConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_rt_constants.bin", 32);
    const auto ssimFinalCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_final.cso", 0);
    const auto ssimFinalConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_final_constants.bin", 32);
    const auto sharpenArCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_sharpen_ar.cso", 0);
    const auto sharpenArConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_sharpen_ar_constants.bin", 4096);
    const auto chromaXCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x.cso", 0);
    const auto chromaYCso = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y.cso", 0);
    const auto mergeCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_merge.cso", 0);
    const auto chromaXConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x_constants.bin", 32);
    const auto chromaYConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y_constants.bin", 32);
    const auto mergeConstants = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_merge_constants.bin", 32);
    const auto chromaXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x_table.bin", 122880);
    const auto chromaYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y_table.bin", 69120);
    if (shaderText.empty() || ycbcrCso.empty() || downXCso.empty() || downXConstants.empty() || downXTable.empty() ||
        downXUvLut.empty() || downYCso.empty() || downYConstants.empty() || downYTable.empty() || downYUvLut.empty() ||
        ssimRtCso.empty() || ssimRtConstants.empty() || ssimFinalCso.empty() || ssimFinalConstants.empty() ||
        sharpenArCso.empty() || sharpenArConstants.empty() || chromaXCso.empty() || chromaYCso.empty() ||
        mergeCso.empty() || chromaXConstants.empty() || chromaYConstants.empty() || mergeConstants.empty() ||
        chromaXTable.empty() || chromaYTable.empty())
    {
        error_ = "NGU 720p 4x tail assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_Luma4x720_Ycbcr",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU 720p 4x tail vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720Tail>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(
            device->CreatePixelShader(ycbcrCso.data(), static_cast<SIZE_T>(ycbcrCso.size()), nullptr, &state->ycbcr)) ||
        FAILED(
            device->CreatePixelShader(downXCso.data(), static_cast<SIZE_T>(downXCso.size()), nullptr, &state->downX)) ||
        FAILED(
            device->CreatePixelShader(downYCso.data(), static_cast<SIZE_T>(downYCso.size()), nullptr, &state->downY)) ||
        FAILED(device->CreatePixelShader(
            ssimRtCso.data(), static_cast<SIZE_T>(ssimRtCso.size()), nullptr, &state->ssimRt)) ||
        FAILED(device->CreatePixelShader(
            ssimFinalCso.data(), static_cast<SIZE_T>(ssimFinalCso.size()), nullptr, &state->ssimFinal)) ||
        FAILED(device->CreatePixelShader(
            sharpenArCso.data(), static_cast<SIZE_T>(sharpenArCso.size()), nullptr, &state->sharpenAr)) ||
        FAILED(device->CreatePixelShader(
            chromaXCso.data(), static_cast<SIZE_T>(chromaXCso.size()), nullptr, &state->chromaX)) ||
        FAILED(device->CreatePixelShader(
            chromaYCso.data(), static_cast<SIZE_T>(chromaYCso.size()), nullptr, &state->chromaY)) ||
        FAILED(
            device->CreatePixelShader(mergeCso.data(), static_cast<SIZE_T>(mergeCso.size()), nullptr, &state->merge)))
    {
        error_ = "NGU 720p 4x tail shader objects could not be created";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc = {};
    constantDesc.ByteWidth = static_cast<UINT>(downXConstants.size());
    constantDesc.Usage = D3D11_USAGE_IMMUTABLE;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData = {};
    constantData.pSysMem = downXConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->downXConstants)))
    {
        error_ = "NGU 720p 4x tail down-X constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(downYConstants.size());
    constantData.pSysMem = downYConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->downYConstants)))
    {
        error_ = "NGU 720p 4x tail down-Y constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(ssimRtConstants.size());
    constantData.pSysMem = ssimRtConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->ssimRtConstants)))
    {
        error_ = "NGU 720p 4x tail SSIM RT constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(ssimFinalConstants.size());
    constantData.pSysMem = ssimFinalConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->ssimFinalConstants)))
    {
        error_ = "NGU 720p 4x tail SSIM final constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(sharpenArConstants.size());
    constantData.pSysMem = sharpenArConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->sharpenArConstants)))
    {
        error_ = "NGU 720p 4x tail SharpenAR constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(chromaXConstants.size());
    constantData.pSysMem = chromaXConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->chromaXConstants)))
    {
        error_ = "NGU 720p 4x tail chroma-X constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(chromaYConstants.size());
    constantData.pSysMem = chromaYConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->chromaYConstants)))
    {
        error_ = "NGU 720p 4x tail chroma-Y constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(mergeConstants.size());
    constantData.pSysMem = mergeConstants.data();
    if (FAILED(device->CreateBuffer(&constantDesc, &constantData, &state->mergeConstants)))
    {
        error_ = "NGU 720p 4x tail merge constants could not be created";
        return false;
    }

    D3D11_TEXTURE2D_DESC tableDesc = {};
    tableDesc.Width = 6;
    tableDesc.Height = 3840;
    tableDesc.MipLevels = 1;
    tableDesc.ArraySize = 1;
    tableDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    tableDesc.SampleDesc.Count = 1;
    tableDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tableDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA tableData = {};
    tableData.pSysMem = downXTable.data();
    tableData.SysMemPitch = 6 * 8;
    if (FAILED(device->CreateTexture2D(&tableDesc, &tableData, &state->downXTable)) ||
        FAILED(device->CreateShaderResourceView(state->downXTable.Get(), nullptr, &state->downXTableView)))
    {
        error_ = "NGU 720p 4x tail down-X table could not be created";
        return false;
    }
    tableDesc.Height = 2160;
    tableData.pSysMem = downYTable.data();
    if (FAILED(device->CreateTexture2D(&tableDesc, &tableData, &state->downYTable)) ||
        FAILED(device->CreateShaderResourceView(state->downYTable.Get(), nullptr, &state->downYTableView)))
    {
        error_ = "NGU 720p 4x tail down-Y table could not be created";
        return false;
    }
    tableDesc.Width = 4;
    tableDesc.Height = 3840;
    tableData.pSysMem = chromaXTable.data();
    tableData.SysMemPitch = 4 * 8;
    if (FAILED(device->CreateTexture2D(&tableDesc, &tableData, &state->chromaXTable)) ||
        FAILED(device->CreateShaderResourceView(state->chromaXTable.Get(), nullptr, &state->chromaXTableView)))
    {
        error_ = "NGU 720p 4x tail chroma-X table could not be created";
        return false;
    }
    tableDesc.Height = 2160;
    tableData.pSysMem = chromaYTable.data();
    if (FAILED(device->CreateTexture2D(&tableDesc, &tableData, &state->chromaYTable)) ||
        FAILED(device->CreateShaderResourceView(state->chromaYTable.Get(), nullptr, &state->chromaYTableView)))
    {
        error_ = "NGU 720p 4x tail chroma-Y table could not be created";
        return false;
    }

    D3D11_TEXTURE2D_DESC uvDesc = {};
    uvDesc.Width = 3840;
    uvDesc.Height = 2;
    uvDesc.MipLevels = 1;
    uvDesc.ArraySize = 1;
    uvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    uvDesc.SampleDesc.Count = 1;
    uvDesc.Usage = D3D11_USAGE_IMMUTABLE;
    uvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA uvData = {};
    uvData.pSysMem = downXUvLut.data();
    uvData.SysMemPitch = 3840 * 8;
    if (FAILED(device->CreateTexture2D(&uvDesc, &uvData, &state->downXUvLut)) ||
        FAILED(device->CreateShaderResourceView(state->downXUvLut.Get(), nullptr, &state->downXUvLutView)))
    {
        error_ = "NGU 720p 4x tail down-X UV LUT could not be created";
        return false;
    }
    uvData.pSysMem = downYUvLut.data();
    if (FAILED(device->CreateTexture2D(&uvDesc, &uvData, &state->downYUvLut)) ||
        FAILED(device->CreateShaderResourceView(state->downYUvLut.Get(), nullptr, &state->downYUvLutView)))
    {
        error_ = "NGU 720p 4x tail down-Y UV LUT could not be created";
        return false;
    }

    auto createIntermediate = [&](UINT width,
                                  UINT height,
                                  DXGI_FORMAT format,
                                  ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11RenderTargetView>& renderTarget,
                                  ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createIntermediate(5120,
                            2880,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->ycbcrTexture,
                            state->ycbcrView,
                            state->ycbcrShaderView) ||
        !createIntermediate(3840,
                            2880,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->downXTexture,
                            state->downXView,
                            state->downXShaderView) ||
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->downYTexture,
                            state->downYView,
                            state->downYShaderView) ||
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_FLOAT,
                            state->ssimRtTexture,
                            state->ssimRtView,
                            state->ssimRtShaderView) ||
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->ssimFinalTexture,
                            state->ssimFinalView,
                            state->ssimFinalShaderView) ||
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->sharpenArTexture,
                            state->sharpenArView,
                            state->sharpenArShaderView) ||
        !createIntermediate(3840,
                            720,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->chromaXTexture,
                            state->chromaXView,
                            state->chromaXShaderView) ||
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->chromaYTexture,
                            state->chromaYView,
                            state->chromaYShaderView) ||
        !createIntermediate(
            3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM, state->mergeTexture, state->mergeView, state->mergeShaderView))
    {
        error_ = "NGU 720p 4x tail intermediate surfaces could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU 720p 4x tail sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU 720p 4x tail rasterizer could not be created";
        return false;
    }

    nativeLuma4x720Tail_ = std::move(state);
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720TailYcbcr;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailYcbcr(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 5120, 2880) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 5120, 2880))
    {
        error_ = "NGU 720p 4x tail YCbCr requires exact 5120x2880 surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.ycbcr.Get(),
                  nullptr,
                  state.rasterizer.Get(),
                  &input,
                  1,
                  targets,
                  1,
                  5120,
                  2880,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailDownX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 5120, 2880) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2880))
    {
        error_ = "NGU 720p 4x tail down-X requires exact 5120/3840 surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {input, state.downXTableView.Get(), state.downXUvLutView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.downX.Get(),
                  state.downXConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  3,
                  targets,
                  1,
                  3840,
                  2880,
                  state.pointSampler.Get(),
                  nullptr,
                  nullptr,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailDownY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2880) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail down-Y requires exact 3840x2880/2160 surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {input, state.downYTableView.Get(), state.downYUvLutView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.downY.Get(),
                  state.downYConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  3,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get(),
                  nullptr,
                  nullptr,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailSsimRt(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_FLOAT, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SSIM RT requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.downYUvLutView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.ssimRt.Get(),
                  state.ssimRtConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailSsimFinal(ID3D11ShaderResourceView* rt,
                                                ID3D11ShaderResourceView* source,
                                                ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(rt, DXGI_FORMAT_R16G16B16A16_FLOAT, 3840, 2160) ||
        !ViewMatchesTexture(source, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SSIM final requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {rt, source, state.downYUvLutView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.ssimFinal.Get(),
                  state.ssimFinalConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  3,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailSharpenAr(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SharpenAR requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.downYUvLutView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.sharpenAr.Get(),
                  state.sharpenArConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailChromaX(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1280, 720) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 720))
    {
        error_ = "NGU 720p 4x tail chroma-X requires exact 1280x720/3840x720 surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.chromaXTableView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.chromaX.Get(),
                  state.chromaXConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  targets,
                  1,
                  3840,
                  720,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailChromaY(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 720) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail chroma-Y requires exact 3840x720/2160 surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.chromaYTableView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.chromaY.Get(),
                  state.chromaYConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailMerge(ID3D11ShaderResourceView* luma,
                                            ID3D11ShaderResourceView* chroma,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ || !ViewMatchesTexture(luma, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(chroma, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail merge requires exact 4K surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {luma, chroma};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(),
                  state.merge.Get(),
                  state.mergeConstants.Get(),
                  state.rasterizer.Get(),
                  inputs,
                  2,
                  targets,
                  1,
                  3840,
                  2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720Full(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU 720p 4x full path requires a GPU";
        return false;
    }
    if (nativeLuma4x720Tail_ && nativeLuma4x720DenseAdd_ && nativeLumaFinal_ && gpu == gpu_)
    {
        cached_.quality = NguQuality::VeryHigh;
        cached_.stage = NguScalerStage::VeryHighLuma4x720Full;
        cached_.sourceWidth = 1280;
        cached_.sourceHeight = 720;
        hasCached_ = true;
        return true;
    }
    if (!PrepareNativeLuma4x720DenseAdd(gpu) || !PrepareNativeLuma4x720Tail(gpu) || !PrepareNativeLumaFinal(gpu))
        return false;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720Full;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720Full(ID3D11ShaderResourceView* sourceRgb,
                                       int sourceWidth,
                                       int sourceHeight,
                                       ID3D11RenderTargetView* finalOutput)
{
    if (!nativeLuma4x720Low_ || !nativeLuma4x720High_ || !nativeLuma4x720DenseAdd_ || !nativeLuma4x720Tail_ ||
        !nativeLumaFinal_ || !sourceRgb || !finalOutput || sourceWidth != 1280 || sourceHeight != 720 ||
        !ViewMatchesRgb8(finalOutput, 3840, 2160))
    {
        error_ = "NGU 720p 4x full request is outside the verified path";
        return false;
    }

    auto& low = *nativeLuma4x720Low_;
    auto& denseAdd = *nativeLuma4x720DenseAdd_;
    auto& tail = *nativeLuma4x720Tail_;
    if (!RunNativeLuma4x720DenseAdd(sourceRgb, sourceWidth, sourceHeight) ||
        !RunNativeLuma4x720TailYcbcr(denseAdd.addShaderView.Get(), tail.ycbcrView.Get()) ||
        !RunNativeLuma4x720TailDownX(tail.ycbcrShaderView.Get(), tail.downXView.Get()) ||
        !RunNativeLuma4x720TailDownY(tail.downXShaderView.Get(), tail.downYView.Get()) ||
        !RunNativeLuma4x720TailSsimRt(tail.downYShaderView.Get(), tail.ssimRtView.Get()) ||
        !RunNativeLuma4x720TailSsimFinal(
            tail.ssimRtShaderView.Get(), tail.downYShaderView.Get(), tail.ssimFinalView.Get()) ||
        !RunNativeLuma4x720TailSharpenAr(tail.ssimFinalShaderView.Get(), tail.sharpenArView.Get()) ||
        !RunNativeLuma4x720TailChromaX(low.splitCbCrShaderView.Get(), tail.chromaXView.Get()) ||
        !RunNativeLuma4x720TailChromaY(tail.chromaXShaderView.Get(), tail.chromaYView.Get()) ||
        !RunNativeLuma4x720TailMerge(
            tail.sharpenArShaderView.Get(), tail.chromaYShaderView.Get(), tail.mergeView.Get()))
        return false;
    if (!RunNativeLumaFinalOwnedDither(tail.mergeShaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720FromPacked(ID3D11ShaderResourceView* sourcePacked,
                                             ID3D11RenderTargetView* finalOutput)
{
    if (!nativeLuma4x720Low_ || !nativeLuma4x720High_ || !nativeLuma4x720DenseAdd_ || !nativeLuma4x720Tail_ ||
        !nativeLumaFinal_ || !sourcePacked || !finalOutput ||
        !ViewMatchesTexture(sourcePacked, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) ||
        !ViewMatchesRgb8(finalOutput, 3840, 2160))
    {
        error_ = "NGU 720p packed luma request is outside the verified path";
        return false;
    }
    auto& low = *nativeLuma4x720Low_;
    auto& tail = *nativeLuma4x720Tail_;
    if (!RunNativeLumaSplit(sourcePacked, 1280, 720, low.splitLumaView.Get(), low.splitCbCrView.Get()) ||
        !RunNativeLuma4x720DenseAdd(sourcePacked, 1280, 720, true) ||
        !RunNativeLuma4x720TailYcbcr(nativeLuma4x720DenseAdd_->addShaderView.Get(), tail.ycbcrView.Get()) ||
        !RunNativeLuma4x720TailDownX(tail.ycbcrShaderView.Get(), tail.downXView.Get()) ||
        !RunNativeLuma4x720TailDownY(tail.downXShaderView.Get(), tail.downYView.Get()) ||
        !RunNativeLuma4x720TailSsimRt(tail.downYShaderView.Get(), tail.ssimRtView.Get()) ||
        !RunNativeLuma4x720TailSsimFinal(
            tail.ssimRtShaderView.Get(), tail.downYShaderView.Get(), tail.ssimFinalView.Get()) ||
        !RunNativeLuma4x720TailSharpenAr(tail.ssimFinalShaderView.Get(), tail.sharpenArView.Get()) ||
        !RunNativeLuma4x720TailChromaX(low.splitCbCrShaderView.Get(), tail.chromaXView.Get()) ||
        !RunNativeLuma4x720TailChromaY(tail.chromaXShaderView.Get(), tail.chromaYView.Get()) ||
        !RunNativeLuma4x720TailMerge(
            tail.sharpenArShaderView.Get(), tail.chromaYShaderView.Get(), tail.mergeView.Get()) ||
        !RunNativeLumaFinalOwnedDither(tail.mergeShaderView.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeLuma4x720SuperResFull(pl_gpu gpu)
{
    if (!PrepareNativeSuperResStrength3(gpu))
        return false;
    if (!PrepareNativeLuma4x720Full(gpu))
        return false;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma4x720SuperResFull;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720SuperResFull(ID3D11ShaderResourceView* source,
                                               ID3D11ShaderResourceView* diffOriginal,
                                               ID3D11ShaderResourceView* mainOriginal,
                                               ID3D11ShaderResourceView* baseOriginal,
                                               ID3D11RenderTargetView* finalOutput)
{
    if (!nativeSuperResStrength3_ || !source || !diffOriginal || !mainOriginal || !baseOriginal || !finalOutput)
    {
        error_ = "NGU 720p SuperRes full request is invalid";
        return false;
    }
    auto& superRes = *nativeSuperResStrength3_;
    if (!RunNativeSuperResStrength3(source, diffOriginal, mainOriginal, baseOriginal, superRes.outputView.Get()))
        return false;
    return RunNativeLuma4x720Full(superRes.outputShaderView.Get(), 1280, 720, finalOutput);
}

bool NguScaler::PrepareNativeLumaVeryHigh1080SuperResFull(pl_gpu gpu)
{
    if (!PrepareNativeSuperResStrength3_1080(gpu) || !PrepareNativeLumaVeryHigh1080(gpu))
        return false;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma1080SuperResFull;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaVeryHigh1080SuperResFull(ID3D11ShaderResourceView* source,
                                                      ID3D11ShaderResourceView* diffOriginal,
                                                      ID3D11ShaderResourceView* mainOriginal,
                                                      ID3D11ShaderResourceView* baseOriginal,
                                                      ID3D11RenderTargetView* finalOutput)
{
    if (!nativeSuperResStrength3_1080_ || !source || !diffOriginal || !mainOriginal || !baseOriginal || !finalOutput)
    {
        error_ = "NGU 1080p SuperRes full request is invalid";
        return false;
    }
    auto& superRes = *nativeSuperResStrength3_1080_;
    if (!RunNativeSuperResStrength3_1080(source, diffOriginal, mainOriginal, baseOriginal, superRes.outputView.Get()))
        return false;
    return RunNativeLumaVeryHigh1080(superRes.outputShaderView.Get(), 1920, 1080, finalOutput);
}

bool NguScaler::PrepareNativeClass3ChromaTail720(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU class-3 chroma tail requires a GPU";
        return false;
    }
    if (nativeClass3ChromaTail720_ && gpu == gpu_)
    {
        cached_.quality = NguQuality::VeryHigh;
        cached_.stage = NguScalerStage::VeryHighClass3ChromaTail720;
        cached_.sourceWidth = 1280;
        cached_.sourceHeight = 720;
        hasCached_ = true;
        return true;
    }
    if (gpu_ && gpu_ != gpu)
        Reset();
    if (!PrepareNativeLuma4x720Tail(gpu))
        return false;
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU class-3 chroma tail requires the D3D11 backend";
        return false;
    }

    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_4x720_tail_ycbcr.hlsl", 32 * 1024);
    const auto superXbr0Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_superxbr0.cso", 0);
    const auto superXbr1Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_superxbr1.cso", 0);
    const auto superXbr2Bytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_superxbr2.cso", 0);
    const auto ycbcrBytecode = LoadBundledBytes(L"shaders\\Ngu\\ngu_class3_tail_ycbcr.cso", 0);
    if (shaderText.empty() || superXbr0Bytecode.empty() || superXbr1Bytecode.empty() || superXbr2Bytecode.empty() ||
        ycbcrBytecode.empty())
    {
        error_ = "NGU class-3 SuperXbr shader assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(shaderText.data(),
                                  shaderText.size(),
                                  "NGU_Class3_Vertex",
                                  nullptr,
                                  nullptr,
                                  "VSMain",
                                  "vs_5_0",
                                  flags,
                                  0,
                                  &vertexBlob,
                                  &errors);
    if (FAILED(hr))
    {
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU class-3 vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeClass3ChromaTail720>();
    if (FAILED(device->CreateVertexShader(
            vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            superXbr0Bytecode.data(), static_cast<SIZE_T>(superXbr0Bytecode.size()), nullptr, &state->superXbr0)) ||
        FAILED(device->CreatePixelShader(
            superXbr1Bytecode.data(), static_cast<SIZE_T>(superXbr1Bytecode.size()), nullptr, &state->superXbr1)) ||
        FAILED(device->CreatePixelShader(
            superXbr2Bytecode.data(), static_cast<SIZE_T>(superXbr2Bytecode.size()), nullptr, &state->superXbr2)) ||
        FAILED(device->CreatePixelShader(
            ycbcrBytecode.data(), static_cast<SIZE_T>(ycbcrBytecode.size()), nullptr, &state->ycbcr)))
    {
        error_ = "NGU class-3 SuperXbr shader objects could not be created";
        return false;
    }

    const UINT dimensions[6][2] = {
        {1280, 720},
        {2560, 1440},
        {2560, 1440},
        {2560, 1440},
        {5120, 2880},
        {5120, 2880},
    };
    const bool swapAxes[6] = {false, true, false, false, true, false};
    for (size_t pass = 0; pass < state->constants.size(); ++pass)
    {
        std::array<float, 16> values = {};
        FillClass3PostConstants(values, dimensions[pass][0], dimensions[pass][1], swapAxes[pass]);
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(values);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = values.data();
        if (FAILED(device->CreateBuffer(&desc, &initial, &state->constants[pass])))
        {
            error_ = "NGU class-3 post constants could not be created";
            return false;
        }
    }

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &state->pointSampler)))
    {
        error_ = "NGU class-3 point sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc, &state->rasterizer)))
    {
        error_ = "NGU class-3 rasterizer could not be created";
        return false;
    }

    auto createSurface = [&](UINT width,
                             UINT height,
                             ComPtr<ID3D11Texture2D>& texture,
                             ComPtr<ID3D11RenderTargetView>& view,
                             ComPtr<ID3D11ShaderResourceView>& shaderView)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &shaderView));
    };
    if (!createSurface(1280, 720, state->post6392Texture, state->post6392View, state->post6392ShaderView) ||
        !createSurface(2560, 1440, state->post5140Texture, state->post5140View, state->post5140ShaderView) ||
        !createSurface(2560, 1440, state->post4804Texture, state->post4804View, state->post4804ShaderView) ||
        !createSurface(
            2560, 1440, state->post6392SecondTexture, state->post6392SecondView, state->post6392SecondShaderView) ||
        !createSurface(
            5120, 2880, state->post5140SecondTexture, state->post5140SecondView, state->post5140SecondShaderView) ||
        !createSurface(
            5120, 2880, state->post4804SecondTexture, state->post4804SecondView, state->post4804SecondShaderView))
    {
        error_ = "NGU class-3 post surfaces could not be created";
        return false;
    }

    nativeClass3ChromaTail720_ = std::move(state);
    gpu_ = gpu;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighClass3ChromaTail720;
    cached_.sourceWidth = 1280;
    cached_.sourceHeight = 720;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeClass3ChromaTail720(ID3D11ShaderResourceView* sourcePs4576,
                                             ID3D11RenderTargetView* finalOutput)
{
    if (!nativeClass3ChromaTail720_ || !nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(sourcePs4576, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) ||
        !ViewMatchesTexture(finalOutput, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU class-3 chroma tail requires 1280x720 input and 4K output";
        return false;
    }
    auto& state = *nativeClass3ChromaTail720_;
    auto& tail = *nativeLuma4x720Tail_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;

    ID3D11RenderTargetView* post6392 = state.post6392View.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr0.Get(),
                  state.constants[0].Get(),
                  state.rasterizer.Get(),
                  &sourcePs4576,
                  1,
                  &post6392,
                  1,
                  1280,
                  720,
                  state.pointSampler.Get());

    ID3D11RenderTargetView* post5140 = state.post5140View.Get();
    ID3D11ShaderResourceView* post5140Inputs[2] = {state.post6392ShaderView.Get(), sourcePs4576};
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr1.Get(),
                  state.constants[1].Get(),
                  state.rasterizer.Get(),
                  post5140Inputs,
                  2,
                  &post5140,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get(),
                  nullptr,
                  nullptr,
                  state.pointSampler.Get());

    ID3D11RenderTargetView* post4804 = state.post4804View.Get();
    ID3D11ShaderResourceView* post4804Input = state.post5140ShaderView.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr2.Get(),
                  state.constants[2].Get(),
                  state.rasterizer.Get(),
                  &post4804Input,
                  1,
                  &post4804,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());

    ID3D11RenderTargetView* post6392Second = state.post6392SecondView.Get();
    ID3D11ShaderResourceView* post6392SecondInput = state.post4804ShaderView.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr0.Get(),
                  state.constants[3].Get(),
                  state.rasterizer.Get(),
                  &post6392SecondInput,
                  1,
                  &post6392Second,
                  1,
                  2560,
                  1440,
                  state.pointSampler.Get());

    ID3D11RenderTargetView* post5140Second = state.post5140SecondView.Get();
    ID3D11ShaderResourceView* post5140SecondInputs[2] = {state.post6392SecondShaderView.Get(),
                                                         state.post4804ShaderView.Get()};
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr1.Get(),
                  state.constants[4].Get(),
                  state.rasterizer.Get(),
                  post5140SecondInputs,
                  2,
                  &post5140Second,
                  1,
                  5120,
                  2880,
                  state.pointSampler.Get(),
                  nullptr,
                  nullptr,
                  state.pointSampler.Get());

    ID3D11RenderTargetView* post4804Second = state.post4804SecondView.Get();
    ID3D11ShaderResourceView* post4804SecondInput = state.post5140SecondShaderView.Get();
    DrawD3D11Pass(device,
                  state.vertex.Get(),
                  state.superXbr2.Get(),
                  state.constants[5].Get(),
                  state.rasterizer.Get(),
                  &post4804SecondInput,
                  1,
                  &post4804Second,
                  1,
                  5120,
                  2880,
                  state.pointSampler.Get());

    if (!ViewMatchesTexture(state.post4804SecondShaderView.Get(), DXGI_FORMAT_R16G16B16A16_UNORM, 5120, 2880))
    {
        error_ = "NGU class-3 SuperXbr2 surface is invalid";
        return false;
    }

    // E-058/E-073: class-3 PS_536 consumes the target's f36
    // A16B16G16R16 post-chain surface and reads channel .x. The
    // existing P010/P016 path consumes R16_UNORM, so keep that contract
    // untouched and bind the class-3 surface directly here.
    ID3D11RenderTargetView* ycbcrTarget = tail.ycbcrView.Get();
    ID3D11ShaderResourceView* ycbcrInput = state.post4804SecondShaderView.Get();
    DrawD3D11Pass(device,
                  tail.vertex.Get(),
                  state.ycbcr.Get(),
                  nullptr,
                  tail.rasterizer.Get(),
                  &ycbcrInput,
                  1,
                  &ycbcrTarget,
                  1,
                  5120,
                  2880,
                  tail.pointSampler.Get());
    if (!RunNativeLuma4x720TailDownX(tail.ycbcrShaderView.Get(), tail.downXView.Get()) ||
        !RunNativeLuma4x720TailDownY(tail.downXShaderView.Get(), tail.downYView.Get()) ||
        !RunNativeLuma4x720TailSsimRt(tail.downYShaderView.Get(), tail.ssimRtView.Get()) ||
        !RunNativeLuma4x720TailSsimFinal(
            tail.ssimRtShaderView.Get(), tail.downYShaderView.Get(), tail.ssimFinalView.Get()) ||
        !RunNativeLuma4x720TailSharpenAr(tail.ssimFinalShaderView.Get(), tail.sharpenArView.Get()) ||
        !CopyTextureToTarget(gpu_, tail.sharpenArTexture.Get(), finalOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeClass3PostStage720(int stage, ID3D11RenderTargetView* output)
{
    if (!nativeClass3ChromaTail720_ || !nativeLuma4x720Tail_ || !output || stage < 0 || stage > 11)
    {
        error_ = "NGU class-3 post-stage copy request is invalid";
        return false;
    }
    ID3D11Texture2D* source = nullptr;
    switch (stage)
    {
    case 0:
        source = nativeClass3ChromaTail720_->post6392Texture.Get();
        break;
    case 1:
        source = nativeClass3ChromaTail720_->post5140Texture.Get();
        break;
    case 2:
        source = nativeClass3ChromaTail720_->post4804Texture.Get();
        break;
    case 3:
        source = nativeClass3ChromaTail720_->post6392SecondTexture.Get();
        break;
    case 4:
        source = nativeClass3ChromaTail720_->post5140SecondTexture.Get();
        break;
    case 5:
        source = nativeClass3ChromaTail720_->post4804SecondTexture.Get();
        break;
    case 6:
        source = nativeLuma4x720Tail_->ycbcrTexture.Get();
        break;
    case 7:
        source = nativeLuma4x720Tail_->downXTexture.Get();
        break;
    case 8:
        source = nativeLuma4x720Tail_->downYTexture.Get();
        break;
    case 9:
        source = nativeLuma4x720Tail_->ssimRtTexture.Get();
        break;
    case 10:
        source = nativeLuma4x720Tail_->ssimFinalTexture.Get();
        break;
    case 11:
        source = nativeLuma4x720Tail_->sharpenArTexture.Get();
        break;
    }
    if (!CopyTextureToTarget(gpu_, source, output))
    {
        error_ = "NGU class-3 post-stage copy failed";
        return false;
    }
    error_ = "";
    return true;
}

void NguScaler::Reset()
{
    if (hook_)
        pl_mpv_user_shader_destroy(&hook_);
    nativeLumaInitial_.reset();
    nativeLumaSplit_.reset();
    nativeDecoderSource_.reset();
    nativeChromaSplit_.reset();
    nativeChromaScale_.reset();
    nativeChromaNetwork_.reset();
    nativeChromaVeryHigh720_.reset();
    nativeChromaVeryHigh1080_.reset();
    nativeChromaVeryHighClass3_720_.reset();
    nativeChromaVeryHighClass3_1080_.reset();
    nativeChromaCombines_.reset();
    nativeChromaRealign_.reset();
    nativeChromaYCbCr_.reset();
    nativeChromaVeryHigh720Full_.reset();
    nativeChromaVeryHigh1080Full_.reset();
    nativeNgu720FromPlanes_.reset();
    nativeNgu1080FromPlanes_.reset();
    nativeLumaLayers_.reset();
    nativeLumaDense_.reset();
    nativeLumaScaleAdd_.reset();
    nativeLumaFinish_.reset();
    nativeLumaFinal_.reset();
    nativeHookOutput_.reset();
    nativeLumaVeryHigh1080_.reset();
    nativeLuma720Double_.reset();
    nativeLuma720DoubleHighBit_.reset();
    nativeSuperRes_.reset();
    nativeSuperResStrength3_.reset();
    nativeSuperResStrength3_1080_.reset();
    nativeLuma4x720Low_.reset();
    nativeLuma4x720High_.reset();
    nativeLuma4x720DenseAdd_.reset();
    nativeLuma4x720Tail_.reset();
    nativeClass3ChromaTail720_.reset();
    nativeChromaCoreClass3_720_.reset();
    nativeChromaCoreClass3_1080_.reset();
    nativeNguClass3_720_.reset();
    nativeNguClass3_1080_.reset();
    gpu_ = nullptr;
    hdr_ = false;
    colorDomain_ = ColorDomainProfile::Sdr;
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
    return error_.c_str();
}
