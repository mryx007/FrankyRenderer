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
    static constexpr StageConfig Luma = {"ngu_sharp_veryhigh_luma_1080_core.glsl", 1920, 1080};
    switch (stage)
    {
    case NguScalerStage::VeryHighChromaReference:
        return &Chroma;
    case NguScalerStage::VeryHighLumaInitial1080:
        return &LumaInitial;
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
    if (size != static_cast<std::streamoff>(expected))
        return {};
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return {};
    return bytes;
}
} // namespace

struct NguScaler::NativeLumaInitial
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
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

void NguScaler::Reset()
{
    if (hook_)
        pl_mpv_user_shader_destroy(&hook_);
    nativeLumaInitial_.reset();
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
