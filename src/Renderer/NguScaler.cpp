#include "Renderer/NguScaler.h"
#include "Renderer/UserShaderSource.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;

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
                   ID3D11SamplerState* sampler3 = nullptr)
{
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    if (!context)
        return;

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
} // namespace

struct NguScaler::NativeLumaInitial
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
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
    std::array<float, 16> constantsData{};
    ComPtr<ID3D11Texture2D> weightTexture;
    ComPtr<ID3D11ShaderResourceView> weightView;
    ComPtr<ID3D11SamplerState> clampSampler;
    ComPtr<ID3D11SamplerState> wrapSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
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

bool NguScaler::PrepareNativeLumaInitial(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU native scaler requires a GPU";
        return false;
    }
    if (nativeLumaInitial_ && gpu == gpu_)
        return true;
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
        error_ =
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "NGU native vertex shader failed";
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
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                         pixelBlob->GetBufferSize(),
                                         nullptr,
                                         &state->pixel)))
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU native layer vertex shader failed";
        return false;
    }
    auto state = std::make_unique<NativeLumaLayers>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(pixelBytecode.data(),
                                         static_cast<SIZE_T>(pixelBytecode.size()),
                                         nullptr,
                                         &state->pixel)))
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

bool NguScaler::RunNativeLumaLayer(ID3D11ShaderResourceView* const inputs[3],
                                   int layer,
                                   ID3D11RenderTargetView* output)
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
    Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native dense scaler requires the D3D11 backend";
        return false;
    }
    const std::string shaderText = LoadBundledText(L"shaders\\Ngu\\luma_1080_dense.hlsl", 256 * 1024);
    const auto pixelBytecode = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense.cso", 0);
    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_dense_constants.bin", 51 * sizeof(float) * 4);
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU native dense vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaDense>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(pixelBytecode.data(),
                                         static_cast<SIZE_T>(pixelBytecode.size()),
                                         nullptr,
                                         &state->pixel)))
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

bool NguScaler::RunNativeLumaDense(ID3D11ShaderResourceView* const inputs[3],
                                   ID3D11RenderTargetView* output)
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
    const auto addConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_add_constants.bin", 2 * sizeof(float) * 4);
    if (scaleText.empty() || scaleXCso.empty() || scaleYCso.empty() || addCso.empty() ||
        scaleXConstants.empty() || scaleYConstants.empty() || addConstants.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU native scale vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaScaleAdd>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(scaleXCso.data(),
                                         static_cast<SIZE_T>(scaleXCso.size()),
                                         nullptr,
                                         &state->scaleX)) ||
        FAILED(device->CreatePixelShader(scaleYCso.data(),
                                         static_cast<SIZE_T>(scaleYCso.size()),
                                         nullptr,
                                         &state->scaleY)) ||
        FAILED(device->CreatePixelShader(addCso.data(),
                                         static_cast<SIZE_T>(addCso.size()),
                                         nullptr,
                                         &state->add)))
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
    const auto upXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_x_constants.bin", 2 * sizeof(float) * 4);
    const auto upYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_y_constants.bin", 2 * sizeof(float) * 4);
    const auto mergeConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_merge_constants.bin", 2 * sizeof(float) * 4);
    const auto upXTable = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_x_table.bin", 4 * 3840 * 8);
    const auto upYTable = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_up_y_table.bin", 4 * 2160 * 8);
    if (shaderText.empty() || upXCso.empty() || upYCso.empty() || mergeCso.empty() ||
        upXConstants.empty() || upYConstants.empty() || mergeConstants.empty() || upXTable.empty() ||
        upYTable.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU native finish vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaFinish>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(upXCso.data(), static_cast<SIZE_T>(upXCso.size()), nullptr, &state->upX)) ||
        FAILED(device->CreatePixelShader(upYCso.data(), static_cast<SIZE_T>(upYCso.size()), nullptr, &state->upY)) ||
        FAILED(device->CreatePixelShader(
            mergeCso.data(), static_cast<SIZE_T>(mergeCso.size()), nullptr, &state->merge)))
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
    if (!createConstants(upXConstants, state->upXConstants) ||
        !createConstants(upYConstants, state->upYConstants) ||
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
    Reset();
    pl_d3d11 d3d = pl_d3d11_get(gpu);
    if (!d3d || !d3d->device)
    {
        error_ = "NGU native final requires the D3D11 backend";
        return false;
    }
    const std::string vertexText = LoadBundledText(L"shaders\\Ngu\\luma_2160_up_x.hlsl", 256 * 1024);
    const auto finalCso = LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final.cso", 0);
    const auto finalConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final_constants.bin", 4 * sizeof(float) * 4);
    const auto finalWeights =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_final_weights.bin", 32 * 32 * sizeof(float));
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU native final vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaFinal>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            finalCso.data(), static_cast<SIZE_T>(finalCso.size()), nullptr, &state->pixel)))
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
        FAILED(device->CreateShaderResourceView(
            state->weightTexture.Get(), nullptr, &state->weightView)))
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
    nativeLumaFinal_->constantsData[9] = phase.y;
    nativeLumaFinal_->constantsData[10] = phase.z;
    ID3D11DeviceContext* context = nullptr;
    pl_d3d11_get(gpu_)->device->GetImmediateContext(&context);
    context->UpdateSubresource(nativeLumaFinal_->constants.Get(),
                               0,
                               nullptr,
                               nativeLumaFinal_->constantsData.data(),
                               0,
                               0);
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

bool NguScaler::PrepareNativeSuperResPrimitives(pl_gpu gpu)
{
    if (!gpu)
    {
        error_ = "NGU SuperChromaRes requires a GPU";
        return false;
    }
    if (nativeSuperRes_ && gpu == gpu_)
        return true;
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
    const auto downXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_constants.bin", 2 * sizeof(float) * 4);
    const auto downYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbMainConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstants =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_constants.bin", 6 * sizeof(float) * 4);
    const auto downXTable = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_table.bin", 4 * 640 * 8);
    const auto downYTable = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_table.bin", 4 * 360 * 8);
    const auto uvLut = LoadBundledBytes(L"shaders\\Ngu\\superres_uv_lut.bin", 2048 * 8);
    const auto downXUvLut =
        LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_uv_lut.bin", (640 + 720) * 8);
    const auto downYUvLut =
        LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_uv_lut.bin", (640 + 360) * 8);
    if (vertexText.empty() || downXCso.empty() || downYCso.empty() || diffCso.empty() ||
        toRgbMainCso.empty() || toRgbBaseCso.empty() || downXConstants.empty() ||
        downYConstants.empty() || toRgbMainConstants.empty() || toRgbBaseConstants.empty() ||
        downXTable.empty() || downYTable.empty() || uvLut.empty() ||
        downXUvLut.empty() || downYUvLut.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU SuperChromaRes vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeSuperResPrimitives>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            downXCso.data(), static_cast<SIZE_T>(downXCso.size()), nullptr, &state->downX)) ||
        FAILED(device->CreatePixelShader(
            downYCso.data(), static_cast<SIZE_T>(downYCso.size()), nullptr, &state->downY)) ||
        FAILED(device->CreatePixelShader(
            diffCso.data(), static_cast<SIZE_T>(diffCso.size()), nullptr, &state->diff)) ||
        FAILED(device->CreatePixelShader(toRgbMainCso.data(),
                                         static_cast<SIZE_T>(toRgbMainCso.size()),
                                         nullptr,
                                         &state->toRgbMain)) ||
        FAILED(device->CreatePixelShader(toRgbBaseCso.data(),
                                         static_cast<SIZE_T>(toRgbBaseCso.size()),
                                         nullptr,
                                         &state->toRgbBase)))
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
        !createConstants(toRgbBaseConstants, state->toRgbBaseConstants))
    {
        error_ = "NGU SuperChromaRes constants could not be created";
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
        !createTable(downYTable, 360, state->downYTable, state->downYTableView))
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
        !createUvLut(downYUvLut, 640 + 360, state->downYUvLut, state->downYUvLutView))
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

bool NguScaler::RunNativeSuperResDownX(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* output)
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

bool NguScaler::RunNativeSuperResDownY(ID3D11ShaderResourceView* input,
                                       ID3D11RenderTargetView* output)
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
    ID3D11ShaderResourceView* inputs[3] = {
        original, downsample, nativeSuperRes_->downYUvLutView.Get()};
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
    ID3D11ShaderResourceView* inputs[4] = {
        original, work, difference, nativeSuperRes_->uvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbMain.Get(),
                  nativeSuperRes_->toRgbMainConstants.Get(),
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
                                           ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU SuperChromaRes base ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {
        original, work, difference, nativeSuperRes_->uvLutView.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbBase.Get(),
                  nativeSuperRes_->toRgbBaseConstants.Get(),
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

void NguScaler::Reset()
{
    if (hook_)
        pl_mpv_user_shader_destroy(&hook_);
    nativeLumaInitial_.reset();
    nativeLumaLayers_.reset();
    nativeLumaDense_.reset();
    nativeLumaScaleAdd_.reset();
    nativeLumaFinish_.reset();
    nativeLumaFinal_.reset();
    nativeSuperRes_.reset();
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
    return error_.c_str();
}
