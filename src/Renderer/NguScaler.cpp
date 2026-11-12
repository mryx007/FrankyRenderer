#include "Renderer/NguScaler.h"
#include "Renderer/UserShaderSource.h"
#include <d3d11.h>
#include <d3d11_3.h>
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
    const bool formatMatches =
        desc.Format == format ||
        (desc.Format == DXGI_FORMAT_R16_TYPELESS &&
         viewFormat == format &&
         (format == DXGI_FORMAT_R16_UNORM || format == DXGI_FORMAT_R16_FLOAT)) ||
        (desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS &&
         viewFormat == format &&
         (format == DXGI_FORMAT_R16G16B16A16_UNORM ||
          format == DXGI_FORMAT_R16G16B16A16_FLOAT));
    return formatMatches && desc.Width == width && desc.Height == height &&
           desc.ArraySize == 1 && desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
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
} // namespace

void NguDither::SeedColorFromClock()
{
    SYSTEMTIME systemTime = {};
    FILETIME fileTime = {};
    GetSystemTime(&systemTime);
    SystemTimeToFileTime(&systemTime, &fileTime);
    SeedColor(static_cast<uint32_t>(GetTickCount()),
              fileTime.dwLowDateTime,
              fileTime.dwHighDateTime);
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
    ComPtr<ID3D11PixelShader> scaleY;
    ComPtr<ID3D11Buffer> scaleXConstants;
    ComPtr<ID3D11Buffer> scaleYConstants;
    ComPtr<ID3D11Buffer> scaleXConstants1080;
    ComPtr<ID3D11Buffer> scaleYConstants1080;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaNetwork
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> cubici2;
    ComPtr<ID3D11PixelShader> layer;
    ComPtr<ID3D11PixelShader> dense;
    ComPtr<ID3D11Buffer> cubici2Constants;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstants;
    ComPtr<ID3D11Buffer> denseConstants;
    ComPtr<ID3D11Buffer> cubici2Constants1080;
    std::array<ComPtr<ID3D11Buffer>, 28> layerConstants1080;
    ComPtr<ID3D11Buffer> denseConstants1080;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaVeryHighResources
{
    UINT width = 0;
    UINT height = 0;
    std::array<ComPtr<ID3D11Texture2D>, 4> initialTextures;
    std::array<ComPtr<ID3D11RenderTargetView>, 4> initialViews;
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> initialShaderViews;
    std::array<std::array<ComPtr<ID3D11Texture2D>, 4>, 2> groupTextures;
    std::array<std::array<ComPtr<ID3D11RenderTargetView>, 4>, 2> groupViews;
    std::array<std::array<ComPtr<ID3D11ShaderResourceView>, 4>, 2>
        groupShaderViews;
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
    ComPtr<ID3D11SamplerState> pointSampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
};

struct NguScaler::NativeChromaFullResources
{
    UINT width = 0;
    UINT height = 0;
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
    std::array<float, 16> constantsData{};
    ComPtr<ID3D11Texture2D> weightTexture;
    ComPtr<ID3D11ShaderResourceView> weightView;
    ComPtr<ID3D11Texture2D> ditherTexture;
    ComPtr<ID3D11ShaderResourceView> ditherView;
    ComPtr<ID3D11SamplerState> clampSampler;
    ComPtr<ID3D11SamplerState> wrapSampler;
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

    ComPtr<ID3D11PixelShader> downX1080;
    ComPtr<ID3D11PixelShader> downY1080;
    ComPtr<ID3D11PixelShader> diff1080;
    ComPtr<ID3D11PixelShader> toRgbMain1080;
    ComPtr<ID3D11PixelShader> toRgbBase1080;
    ComPtr<ID3D11Buffer> downXConstants1080;
    ComPtr<ID3D11Buffer> downYConstants1080;
    ComPtr<ID3D11Buffer> toRgbMainConstants1080;
    ComPtr<ID3D11Buffer> toRgbBaseConstants1080;
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\luma_1080_split.hlsl", 64 * 1024);
    const auto pixelCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_1080_split.cso", 0);
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU luma split vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLumaSplit>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
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

bool NguScaler::RunNativeLumaSplit(ID3D11ShaderResourceView* source,
                                   int width,
                                   int height,
                                   ID3D11RenderTargetView* luma,
                                   ID3D11RenderTargetView* cbcr)
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_source_copy.hlsl", 16 * 1024);
    const auto pixelCso =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_source_copy.cso", 0);
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU decoder source vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeDecoderSource>();
    if (FAILED(device->QueryInterface(
            __uuidof(ID3D11Device3),
            reinterpret_cast<void**>(state->device3.GetAddressOf()))))
    {
        error_ = "NGU decoder source import requires D3D11.3 plane views";
        return false;
    }
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
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
    if (!nativeDecoderSource_ || !source || width <= 0 || height <= 0 ||
        (width & 1) || (height & 1) || !luma || !chroma)
    {
        error_ = "NGU decoder source request is invalid";
        return false;
    }
    D3D11_TEXTURE2D_DESC sourceDesc = {};
    source->GetDesc(&sourceDesc);
    if ((sourceDesc.Format != DXGI_FORMAT_P010 && sourceDesc.Format != DXGI_FORMAT_P016) ||
        sourceDesc.MipLevels != 1 || sourceDesc.SampleDesc.Count != 1 ||
        arraySlice >= sourceDesc.ArraySize ||
        static_cast<UINT>(width) > sourceDesc.Width ||
        static_cast<UINT>(height) > sourceDesc.Height)
    {
        error_ = "NGU decoder source format or geometry is unsupported";
        return false;
    }
    if (!ViewMatchesTexture(luma, DXGI_FORMAT_R16_UNORM, width, height) ||
        !ViewMatchesTexture(
            chroma, DXGI_FORMAT_R16G16B16A16_UNORM, width / 2, height / 2))
    {
        error_ = "NGU decoder source output format or geometry is unsupported";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC1 lumaViewDesc = {};
    lumaViewDesc.Format = DXGI_FORMAT_R16_UNORM;
    lumaViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    lumaViewDesc.Texture2DArray.MipLevels = 1;
    lumaViewDesc.Texture2DArray.FirstArraySlice = arraySlice;
    lumaViewDesc.Texture2DArray.ArraySize = 1;
    lumaViewDesc.Texture2DArray.PlaneSlice = 0;
    ComPtr<ID3D11ShaderResourceView1> lumaView;
    if (FAILED(nativeDecoderSource_->device3->CreateShaderResourceView1(
            source, &lumaViewDesc, &lumaView)))
    {
        error_ = "NGU decoder luma plane view could not be created";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC1 chromaViewDesc = {};
    chromaViewDesc.Format = DXGI_FORMAT_R16G16_UNORM;
    chromaViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    chromaViewDesc.Texture2DArray.MipLevels = 1;
    chromaViewDesc.Texture2DArray.FirstArraySlice = arraySlice;
    chromaViewDesc.Texture2DArray.ArraySize = 1;
    chromaViewDesc.Texture2DArray.PlaneSlice = 1;
    ComPtr<ID3D11ShaderResourceView1> chromaView;
    if (FAILED(nativeDecoderSource_->device3->CreateShaderResourceView1(
            source, &chromaViewDesc, &chromaView)))
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_split.hlsl", 16 * 1024);
    const auto pixelCso =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_split.cso", 0);
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma split vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaSplit>();
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelCso.data(), static_cast<SIZE_T>(pixelCso.size()), nullptr, &state->pixel)))
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

bool NguScaler::RunNativeChromaSplit(ID3D11ShaderResourceView* packedUv,
                                     int width,
                                     int height,
                                     ID3D11RenderTargetView* cb,
                                     ID3D11RenderTargetView* cr)
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_scale.hlsl", 16 * 1024);
    const auto scaleXCso =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_x.cso", 0);
    const auto scaleYCso =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_scale_y.cso", 0);
    const auto scaleXConstants1080 =
        LoadBundledBytes(
            L"shaders\\Ngu\\ngu_chroma_scale_x_constants_1080.bin", 48);
    const auto scaleYConstants1080 =
        LoadBundledBytes(
            L"shaders\\Ngu\\ngu_chroma_scale_y_constants_1080.bin", 48);
    if (shaderText.empty() || scaleXCso.empty() || scaleYCso.empty() ||
        scaleXConstants1080.empty() || scaleYConstants1080.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma scale vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaScale>();
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
                                         &state->scaleY)))
    {
        error_ = "NGU chroma scale shader objects could not be created";
        return false;
    }

    const float scaleXConstants[12] = {
        640.0f, 0.00078125f, 0.0015625f, 0.0f,
        0.871875f, 0.240625f, -0.084375f, -0.028125f,
        640.0f, 360.0f, 0.0f, 0.0f};
    const float scaleYConstants[12] = {
        720.0f, 0.0013888889f, 0.0027777778f, 0.0f,
        0.871875f, 0.240625f, -0.084375f, -0.028125f,
        1280.0f, 720.0f, 0.0f, 0.0f};
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
    if (FAILED(device->CreateBuffer(
            &bufferDesc, &initial, &state->scaleXConstants1080)))
    {
        error_ = "NGU 1080p chroma X scale constants could not be created";
        return false;
    }
    initial.pSysMem = scaleYConstants1080.data();
    if (FAILED(device->CreateBuffer(
            &bufferDesc, &initial, &state->scaleYConstants1080)))
    {
        error_ = "NGU 1080p chroma Y scale constants could not be created";
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

bool NguScaler::RunNativeChromaScaleX(ID3D11ShaderResourceView* input,
                                      ID3D11RenderTargetView* output)
{
    const bool is720 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 360) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 360);
    const bool is1080 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 540) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 540);
    if (!nativeChromaScale_ || !input || !output ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma X scale request has unsupported geometry";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaScale_->vertex.Get(),
                  nativeChromaScale_->scaleX.Get(),
                  (is1080
                       ? nativeChromaScale_->scaleXConstants1080.Get()
                       : nativeChromaScale_->scaleXConstants.Get()),
                  nativeChromaScale_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 540 : 360,
                  nativeChromaScale_->sampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaScaleY(ID3D11ShaderResourceView* input,
                                      ID3D11RenderTargetView* output)
{
    const bool is720 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1280, 360) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 720);
    const bool is1080 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 1920, 540) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 1080);
    if (!nativeChromaScale_ || !input || !output ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma Y scale request has unsupported geometry";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaScale_->vertex.Get(),
                  nativeChromaScale_->scaleY.Get(),
                  (is1080
                       ? nativeChromaScale_->scaleYConstants1080.Get()
                       : nativeChromaScale_->scaleYConstants.Get()),
                  nativeChromaScale_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 1080 : 720,
                  nativeChromaScale_->sampler.Get());
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

    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_cubici2.hlsl", 64 * 1024);
    const auto cubici2Bytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2.cso", 0);
    const auto layerBytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer.cso", 0);
    const auto denseBytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense.cso", 0);
    const auto cubici2Constants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2_constants.bin",
                         42 * sizeof(float) * 4);
    const auto layerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer_constants.bin",
                         28 * 147 * sizeof(float) * 4);
    const auto denseConstants =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense_constants.bin",
                         67 * sizeof(float) * 4);
    const auto cubici2Constants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_cubici2_constants_1080.bin",
                          42 * sizeof(float) * 4);
    const auto layerConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_layer_constants_1080.bin",
                          28 * 147 * sizeof(float) * 4);
    const auto denseConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_dense_constants_1080.bin",
                          67 * sizeof(float) * 4);
    if (shaderText.empty() || cubici2Bytecode.empty() || layerBytecode.empty() ||
        denseBytecode.empty() || cubici2Constants.empty() ||
        layerConstants.empty() || denseConstants.empty() ||
        cubici2Constants1080.empty() || layerConstants1080.empty() ||
        denseConstants1080.empty())
    {
        error_ = "NGU chroma network assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags =
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma network vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaNetwork>();
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(cubici2Bytecode.data(),
                                         static_cast<SIZE_T>(cubici2Bytecode.size()),
                                         nullptr,
                                         &state->cubici2)) ||
        FAILED(device->CreatePixelShader(layerBytecode.data(),
                                         static_cast<SIZE_T>(layerBytecode.size()),
                                         nullptr,
                                         &state->layer)) ||
        FAILED(device->CreatePixelShader(denseBytecode.data(),
                                         static_cast<SIZE_T>(denseBytecode.size()),
                                         nullptr,
                                         &state->dense)))
    {
        error_ = "NGU chroma network shader objects could not be created";
        return false;
    }

    auto createConstants = [&](const std::vector<uint8_t>& bytes,
                               size_t offset,
                               UINT size,
                               ComPtr<ID3D11Buffer>& buffer)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = bytes.data() + offset;
        return SUCCEEDED(device->CreateBuffer(&desc, &initial, &buffer));
    };
    if (!createConstants(cubici2Constants,
                         0,
                         UINT(cubici2Constants.size()),
                         state->cubici2Constants))
    {
        error_ = "NGU chroma Cubici2 constants could not be created";
        return false;
    }
    constexpr size_t layerConstantBytes = 147 * sizeof(float) * 4;
    for (size_t layer = 0; layer < state->layerConstants.size(); ++layer)
    {
        if (!createConstants(layerConstants,
                             layer * layerConstantBytes,
                             UINT(layerConstantBytes),
                             state->layerConstants[layer]))
        {
            error_ = "NGU chroma layer constants could not be created";
            return false;
        }
    }
    if (!createConstants(denseConstants,
                         0,
                         UINT(denseConstants.size()),
                         state->denseConstants))
    {
        error_ = "NGU chroma dense constants could not be created";
        return false;
    }
    if (!createConstants(cubici2Constants1080,
                         0,
                         UINT(cubici2Constants1080.size()),
                         state->cubici2Constants1080))
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
    if (!createConstants(denseConstants1080,
                         0,
                         UINT(denseConstants1080.size()),
                         state->denseConstants1080))
    {
        error_ = "NGU 1080p chroma dense constants could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc,
                                              &state->rasterizer)))
    {
        error_ = "NGU chroma network rasterizer could not be created";
        return false;
    }
    nativeChromaNetwork_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCubici2(
    ID3D11ShaderResourceView* input,
    ID3D11RenderTargetView* const outputs[4])
{
    const bool is720 =
        input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 640, 360);
    const bool is1080 =
        input && ViewMatchesTexture(input, DXGI_FORMAT_R16_UNORM, 960, 540);
    const UINT width = is1080 ? 960 : 640;
    const UINT height = is1080 ? 540 : 360;
    if (!nativeChromaNetwork_ || !input || !outputs || !outputs[0] ||
        !outputs[1] || !outputs[2] || !outputs[3] ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma Cubici2 request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(outputs[index],
                                DXGI_FORMAT_R16G16B16A16_FLOAT,
                                width,
                                height))
        {
            error_ = "NGU chroma Cubici2 output is invalid";
            return false;
        }
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->cubici2.Get(),
                  (is1080 ? nativeChromaNetwork_->cubici2Constants1080.Get()
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

bool NguScaler::RunNativeChromaLayer(
    ID3D11ShaderResourceView* const inputs[4],
    int layer,
    ID3D11RenderTargetView* output)
{
    const bool is720 =
        inputs && inputs[0] &&
        ViewMatchesTexture(inputs[0],
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           640,
                           360);
    const bool is1080 =
        inputs && inputs[0] &&
        ViewMatchesTexture(inputs[0],
                           DXGI_FORMAT_R16G16B16A16_FLOAT,
                           960,
                           540);
    const UINT width = is1080 ? 960 : 640;
    const UINT height = is1080 ? 540 : 360;
    if (!nativeChromaNetwork_ || !inputs || !inputs[0] || !inputs[1] ||
        !inputs[2] || !inputs[3] || !output || layer < 0 ||
        layer >= int(nativeChromaNetwork_->layerConstants.size()) ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma layer request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(inputs[index],
                                DXGI_FORMAT_R16G16B16A16_FLOAT,
                                width,
                                height))
        {
            error_ = "NGU chroma layer input is invalid";
            return false;
        }
    }
    if (!ViewMatchesTexture(output,
                            DXGI_FORMAT_R16G16B16A16_FLOAT,
                            width,
                            height))
    {
        error_ = "NGU chroma layer output is invalid";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->layer.Get(),
                  (is1080
                       ? nativeChromaNetwork_->layerConstants1080[layer].Get()
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

bool NguScaler::RunNativeChromaDense(
    ID3D11ShaderResourceView* const inputs[4],
    ID3D11RenderTargetView* output)
{
    const bool output720 =
        output &&
        ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362);
    const bool output1080 =
        output &&
        ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542);
    const UINT inputWidth = output1080 ? 960 : 640;
    const UINT inputHeight = output1080 ? 540 : 360;
    const UINT outputWidth = output1080 ? 962 : 642;
    const UINT outputHeight = output1080 ? 542 : 362;
    if (!nativeChromaNetwork_ || !inputs || !inputs[0] || !inputs[1] ||
        !inputs[2] || !inputs[3] || !output ||
        (!output720 && !output1080))
    {
        error_ = "NGU chroma dense request is invalid";
        return false;
    }
    for (int index = 0; index < 4; ++index)
    {
        if (!ViewMatchesTexture(inputs[index],
                                DXGI_FORMAT_R16G16B16A16_FLOAT,
                                inputWidth,
                                inputHeight))
        {
            error_ = "NGU chroma dense input is invalid";
            return false;
        }
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaNetwork_->vertex.Get(),
                  nativeChromaNetwork_->dense.Get(),
                  (output1080 ? nativeChromaNetwork_->denseConstants1080.Get()
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

bool NguScaler::PrepareNativeChromaVeryHigh(
    pl_gpu gpu,
    int width,
    int height,
    std::unique_ptr<NativeChromaVeryHighResources>& state,
    const char* geometry)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU VeryHigh chroma requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) &&
        state->height == UINT(height))
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
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &view)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
    };
    for (size_t index = 0; index < next->initialTextures.size(); ++index)
    {
        if (!create(next->initialTextures[index],
                    next->initialViews[index],
                    next->initialShaderViews[index]))
        {
            error_ = std::string("NGU ") + geometry +
                     " VeryHigh chroma initializer surfaces failed";
            return false;
        }
    }
    for (size_t set = 0; set < next->groupTextures.size(); ++set)
    {
        for (size_t feature = 0; feature < next->groupTextures[set].size();
             ++feature)
        {
            if (!create(next->groupTextures[set][feature],
                        next->groupViews[set][feature],
                        next->groupShaderViews[set][feature]))
            {
                error_ = std::string("NGU ") + geometry +
                         " VeryHigh chroma layer surfaces failed";
                return false;
            }
        }
    }

    state = std::move(next);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaVeryHigh(
    ID3D11ShaderResourceView* cb,
    ID3D11RenderTargetView* denseOutput,
    int width,
    int height,
    NativeChromaVeryHighResources& state)
{
    if (state.width != UINT(width) || state.height != UINT(height) || !cb ||
        !denseOutput ||
        !ViewMatchesTexture(
            cb, DXGI_FORMAT_R16_UNORM, UINT(width), UINT(height)) ||
        !ViewMatchesTexture(denseOutput,
                            DXGI_FORMAT_R16G16B16A16_FLOAT,
                            UINT(width + 2),
                            UINT(height + 2)))
    {
        error_ = "NGU VeryHigh chroma request is invalid";
        return false;
    }

    std::array<ID3D11RenderTargetView*, 4> initialTargets = {};
    for (size_t index = 0; index < initialTargets.size(); ++index)
        initialTargets[index] = state.initialViews[index].Get();
    if (!RunNativeChromaCubici2(cb, initialTargets.data()))
        return false;

    std::array<ID3D11ShaderResourceView*, 4> current = {};
    for (size_t index = 0; index < current.size(); ++index)
        current[index] = state.initialShaderViews[index].Get();

    for (int layer = 0; layer < 28; ++layer)
    {
        const size_t set = size_t((layer / 4) % 2);
        const size_t feature = size_t(layer % 4);
        if (!RunNativeChromaLayer(
                current.data(), layer, state.groupViews[set][feature].Get()))
            return false;
        if (feature == 3)
        {
            for (size_t index = 0; index < current.size(); ++index)
                current[index] = state.groupShaderViews[set][index].Get();
        }
    }
    if (!RunNativeChromaDense(current.data(), denseOutput))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaVeryHigh720(pl_gpu gpu)
{
    return PrepareNativeChromaVeryHigh(
        gpu, 640, 360, nativeChromaVeryHigh720_, "720p");
}

bool NguScaler::RunNativeChromaVeryHigh720(
    ID3D11ShaderResourceView* cb,
    ID3D11RenderTargetView* denseOutput)
{
    if (!nativeChromaVeryHigh720_)
    {
        error_ = "NGU 720p VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaVeryHigh(
        cb, denseOutput, 640, 360, *nativeChromaVeryHigh720_);
}

bool NguScaler::PrepareNativeChromaVeryHigh1080(pl_gpu gpu)
{
    return PrepareNativeChromaVeryHigh(
        gpu, 960, 540, nativeChromaVeryHigh1080_, "1080p");
}

bool NguScaler::RunNativeChromaVeryHigh1080(
    ID3D11ShaderResourceView* cb,
    ID3D11RenderTargetView* denseOutput)
{
    if (!nativeChromaVeryHigh1080_)
    {
        error_ = "NGU 1080p VeryHigh chroma scheduler is not prepared";
        return false;
    }
    return RunNativeChromaVeryHigh(
        cb, denseOutput, 960, 540, *nativeChromaVeryHigh1080_);
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_combine.hlsl", 32 * 1024);
    const auto combine1Bytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine1.cso", 0);
    const auto combine2Bytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine2.cso", 0);
    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine_constants.bin",
                         2 * sizeof(float) * 4);
    const auto constantBytes1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_combine_constants_1080.bin",
                         2 * sizeof(float) * 4);
    if (shaderText.empty() || combine1Bytecode.empty() ||
        combine2Bytecode.empty() || constantBytes.empty() ||
        constantBytes1080.empty())
    {
        error_ = "NGU chroma combine assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags =
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma combine vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaCombines>();
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            combine1Bytecode.data(),
            static_cast<SIZE_T>(combine1Bytecode.size()),
            nullptr,
            &state->combine1)) ||
        FAILED(device->CreatePixelShader(
            combine2Bytecode.data(),
            static_cast<SIZE_T>(combine2Bytecode.size()),
            nullptr,
            &state->combine2)))
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
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU chroma combine constants could not be created";
        return false;
    }
    constantData.pSysMem = constantBytes1080.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->constants1080)))
    {
        error_ = "NGU 1080p chroma combine constants could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc,
                                              &state->rasterizer)))
    {
        error_ = "NGU chroma combine rasterizer could not be created";
        return false;
    }
    nativeChromaCombines_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCombine1(
    ID3D11ShaderResourceView* addInput,
    ID3D11ShaderResourceView* nnInput,
    ID3D11RenderTargetView* output)
{
    const bool is720 =
        addInput && nnInput && output &&
        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
        ViewMatchesTexture(
            nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1280, 720);
    const bool is1080 =
        addInput && nnInput && output &&
        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
        ViewMatchesTexture(
            nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16_UNORM, 1920, 1080);
    if (!nativeChromaCombines_ || !addInput || !nnInput || !output ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma combine 1 request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {addInput, nnInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaCombines_->vertex.Get(),
                  nativeChromaCombines_->combine1.Get(),
                  (is1080 ? nativeChromaCombines_->constants1080.Get()
                          : nativeChromaCombines_->constants.Get()),
                  nativeChromaCombines_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 1080 : 720);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaCombine2(
    ID3D11ShaderResourceView* addInput,
    ID3D11ShaderResourceView* cbInput,
    ID3D11ShaderResourceView* nnInput,
    ID3D11RenderTargetView* output)
{
    const bool is720 =
        addInput && cbInput && nnInput && output &&
        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
        ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1280, 720) &&
        ViewMatchesTexture(
            nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 642, 362) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1280, 720);
    const bool is1080 =
        addInput && cbInput && nnInput && output &&
        ViewMatchesTexture(addInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
        ViewMatchesTexture(cbInput, DXGI_FORMAT_R16_UNORM, 1920, 1080) &&
        ViewMatchesTexture(
            nnInput, DXGI_FORMAT_R16G16B16A16_FLOAT, 962, 542) &&
        ViewMatchesTexture(output, DXGI_FORMAT_R16G16_UNORM, 1920, 1080);
    if (!nativeChromaCombines_ || !addInput || !cbInput || !nnInput ||
        !output || (!is720 && !is1080))
    {
        error_ = "NGU chroma combine 2 request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[3] = {addInput, nnInput, cbInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaCombines_->vertex.Get(),
                  nativeChromaCombines_->combine2.Get(),
                  (is1080 ? nativeChromaCombines_->constants1080.Get()
                          : nativeChromaCombines_->constants.Get()),
                  nativeChromaCombines_->rasterizer.Get(),
                  inputs,
                  3,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 1080 : 720);
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_realign.hlsl", 96 * 1024);
    const auto pixelBytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign.cso", 0);
    const auto constantBytes =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign_constants.bin",
                         sizeof(float) * 4);
    const auto constantBytes1080 =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_realign_constants_1080.bin",
                         sizeof(float) * 4);
    if (shaderText.empty() || pixelBytecode.empty() || constantBytes.empty() ||
        constantBytes1080.empty())
    {
        error_ = "NGU chroma Realign assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags =
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma Realign vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaRealign>();
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(),
            static_cast<SIZE_T>(pixelBytecode.size()),
            nullptr,
            &state->pixel)))
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
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->constants)))
    {
        error_ = "NGU chroma Realign constants could not be created";
        return false;
    }
    constantData.pSysMem = constantBytes1080.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->constants1080)))
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
    if (FAILED(device->CreateSamplerState(&samplerDesc,
                                          &state->pointSampler)))
    {
        error_ = "NGU chroma Realign sampler could not be created";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc,
                                              &state->rasterizer)))
    {
        error_ = "NGU chroma Realign rasterizer could not be created";
        return false;
    }
    nativeChromaRealign_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaRealign(
    ID3D11ShaderResourceView* input,
    ID3D11RenderTargetView* output)
{
    const bool is720 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1280, 720) &&
        ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool is1080 =
        input && output &&
        ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1920, 1080) &&
        ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080);
    if (!nativeChromaRealign_ || !input || !output ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma Realign request is invalid";
        return false;
    }
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaRealign_->vertex.Get(),
                  nativeChromaRealign_->pixel.Get(),
                  (is1080 ? nativeChromaRealign_->constants1080.Get()
                          : nativeChromaRealign_->constants.Get()),
                  nativeChromaRealign_->rasterizer.Get(),
                  &input,
                  1,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 1080 : 720,
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
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_chroma_ycbcr.hlsl", 16 * 1024);
    const auto pixelBytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_chroma_ycbcr.cso", 0);
    if (shaderText.empty() || pixelBytecode.empty())
    {
        error_ = "NGU chroma YCbCr assets are unavailable";
        return false;
    }

    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags =
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU chroma YCbCr vertex shader failed";
        return false;
    }

    ID3D11Device* device = d3d->device;
    auto state = std::make_unique<NativeChromaYCbCr>();
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode.data(),
            static_cast<SIZE_T>(pixelBytecode.size()),
            nullptr,
            &state->pixel)))
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
    if (FAILED(device->CreateSamplerState(&samplerDesc,
                                          &state->pointSampler)))
    {
        error_ = "NGU chroma YCbCr sampler could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizerDesc,
                                              &state->rasterizer)))
    {
        error_ = "NGU chroma YCbCr rasterizer could not be created";
        return false;
    }
    nativeChromaYCbCr_ = std::move(state);
    gpu_ = gpu;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaYCbCr(
    ID3D11ShaderResourceView* yInput,
    ID3D11ShaderResourceView* cbcrInput,
    ID3D11RenderTargetView* output)
{
    const bool is720 =
        yInput && cbcrInput && output &&
        ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1280, 720) &&
        ViewMatchesTexture(output,
                           DXGI_FORMAT_R16G16B16A16_UNORM,
                           1280,
                           720) &&
        (ViewMatchesTexture(cbcrInput,
                            DXGI_FORMAT_R16G16_UNORM,
                            1280,
                            720) ||
         ViewMatchesTexture(cbcrInput,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            1280,
                            720));
    const bool is1080 =
        yInput && cbcrInput && output &&
        ViewMatchesTexture(yInput, DXGI_FORMAT_R8_UNORM, 1920, 1080) &&
        ViewMatchesTexture(output,
                           DXGI_FORMAT_R16G16B16A16_UNORM,
                           1920,
                           1080) &&
        (ViewMatchesTexture(cbcrInput,
                            DXGI_FORMAT_R16G16_UNORM,
                            1920,
                            1080) ||
         ViewMatchesTexture(cbcrInput,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            1920,
                            1080));
    if (!nativeChromaYCbCr_ || !yInput || !cbcrInput || !output ||
        (!is720 && !is1080))
    {
        error_ = "NGU chroma YCbCr request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[2] = {yInput, cbcrInput};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeChromaYCbCr_->vertex.Get(),
                  nativeChromaYCbCr_->pixel.Get(),
                  nullptr,
                  nativeChromaYCbCr_->rasterizer.Get(),
                  inputs,
                  2,
                  &output,
                  1,
                  is1080 ? 1920 : 1280,
                  is1080 ? 1080 : 720,
                  nativeChromaYCbCr_->pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaFull(
    pl_gpu gpu,
    int width,
    int height,
    std::unique_ptr<NativeChromaFullResources>& state,
    const char* geometry)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU full VeryHigh chroma requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) &&
        state->height == UINT(height))
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    const bool is1080 = width == 960 && height == 540;
    if (!PrepareNativeChromaSplit(gpu) ||
        !PrepareNativeChromaScale(gpu) ||
        (is1080 ? !PrepareNativeChromaVeryHigh1080(gpu)
                : !PrepareNativeChromaVeryHigh720(gpu)) ||
        !PrepareNativeChromaCombines(gpu) ||
        !PrepareNativeChromaRealign(gpu) ||
        !PrepareNativeChromaYCbCr(gpu) ||
        (is1080 ? !PrepareNativeSuperResStrength3_1080(gpu)
                : !PrepareNativeSuperResStrength3(gpu)))
        return false;

    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto next = std::make_unique<NativeChromaFullResources>();
    next->width = UINT(width);
    next->height = UINT(height);
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
        desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                         D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
    };
    if (!create(next->width, next->height, DXGI_FORMAT_R16_UNORM,
                next->cbTexture, next->cbView, next->cbShaderView) ||
        !create(next->width, next->height, DXGI_FORMAT_R16_UNORM,
                next->crTexture, next->crView, next->crShaderView) ||
        !create(outputWidth, next->height, DXGI_FORMAT_R16_UNORM,
                next->cbScaleXTexture,
                next->cbScaleXView,
                next->cbScaleXShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16_UNORM,
                next->cbScaleYTexture,
                next->cbScaleYView,
                next->cbScaleYShaderView) ||
        !create(outputWidth, next->height, DXGI_FORMAT_R16_UNORM,
                next->crScaleXTexture,
                next->crScaleXView,
                next->crScaleXShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16_UNORM,
                next->crScaleYTexture,
                next->crScaleYView,
                next->crScaleYShaderView) ||
        !create(denseWidth, denseHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                next->denseCbTexture,
                next->denseCbView,
                next->denseCbShaderView) ||
        !create(denseWidth, denseHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                next->denseCrTexture,
                next->denseCrView,
                next->denseCrShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16_UNORM,
                next->combine1Texture,
                next->combine1View,
                next->combine1ShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16G16_UNORM,
                next->combine2Texture,
                next->combine2View,
                next->combine2ShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16G16B16A16_UNORM,
                next->realignTexture,
                next->realignView,
                next->realignShaderView) ||
        !create(outputWidth, outputHeight, DXGI_FORMAT_R16G16B16A16_UNORM,
                next->ycbcrTexture,
                next->ycbcrView,
                next->ycbcrShaderView))
    {
        error_ = std::string("NGU ") + geometry +
                 " full VeryHigh chroma surfaces could not be created";
        return false;
    }
    state = std::move(next);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeChromaFull(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11ShaderResourceView* diffOriginal,
    ID3D11ShaderResourceView* mainOriginal,
    ID3D11ShaderResourceView* baseOriginal,
    ID3D11RenderTargetView* output,
    int width,
    int height,
    bool is1080,
    NativeChromaFullResources& state)
{
    const UINT outputWidth = UINT(width * 2);
    const UINT outputHeight = UINT(height * 2);
    const bool packedUvIsRgba16 =
        packedUv &&
        ViewMatchesTexture(
            packedUv,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            UINT(width),
            UINT(height));
    const bool packedUvIsBgra8 =
        packedUv &&
        ViewMatchesTexture(packedUv,
                           DXGI_FORMAT_B8G8R8A8_UNORM,
                           UINT(width),
                           UINT(height));
    const bool mainIsRgba16 =
        mainOriginal &&
        ViewMatchesTexture(
            mainOriginal,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            outputWidth,
            outputHeight);
    const bool mainIsR8 =
        mainOriginal &&
        ViewMatchesTexture(mainOriginal,
                           DXGI_FORMAT_R8_UNORM,
                           outputWidth,
                           outputHeight);
    const bool baseIsRgba16 =
        baseOriginal &&
        ViewMatchesTexture(
            baseOriginal,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            outputWidth,
            outputHeight);
    const bool baseIsR8 =
        baseOriginal &&
        ViewMatchesTexture(baseOriginal,
                           DXGI_FORMAT_R8_UNORM,
                           outputWidth,
                           outputHeight);
    if (state.width != UINT(width) || state.height != UINT(height) ||
        !packedUv || !yInput || !diffOriginal || !mainOriginal ||
        !baseOriginal || !output ||
        (!packedUvIsRgba16 && !packedUvIsBgra8) ||
        !ViewMatchesTexture(yInput,
                            DXGI_FORMAT_R8_UNORM,
                            outputWidth,
                            outputHeight) ||
        !ViewMatchesTexture(
            diffOriginal,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            UINT(width),
            UINT(height)) ||
        (!mainIsRgba16 && !mainIsR8) ||
        (!baseIsRgba16 && !baseIsR8) ||
        !ViewMatchesTexture(output,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            outputWidth,
                            outputHeight))
    {
        error_ = "NGU full VeryHigh chroma request is invalid";
        return false;
    }

    if (!RunNativeChromaSplit(
            packedUv, width, height, state.cbView.Get(), state.crView.Get()) ||
        (is1080 ? !RunNativeChromaVeryHigh1080(
                      state.cbShaderView.Get(), state.denseCbView.Get())
                : !RunNativeChromaVeryHigh720(
                      state.cbShaderView.Get(), state.denseCbView.Get())) ||
        !RunNativeChromaScaleX(
            state.cbShaderView.Get(), state.cbScaleXView.Get()) ||
        !RunNativeChromaScaleY(
            state.cbScaleXShaderView.Get(), state.cbScaleYView.Get()) ||
        !RunNativeChromaCombine1(state.cbScaleYShaderView.Get(),
                                 state.denseCbShaderView.Get(),
                                 state.combine1View.Get()) ||
        (is1080 ? !RunNativeChromaVeryHigh1080(
                      state.crShaderView.Get(), state.denseCrView.Get())
                : !RunNativeChromaVeryHigh720(
                      state.crShaderView.Get(), state.denseCrView.Get())) ||
        !RunNativeChromaScaleX(
            state.crShaderView.Get(), state.crScaleXView.Get()) ||
        !RunNativeChromaScaleY(
            state.crScaleXShaderView.Get(), state.crScaleYView.Get()) ||
        !RunNativeChromaCombine2(state.crScaleYShaderView.Get(),
                                 state.combine1ShaderView.Get(),
                                 state.denseCrShaderView.Get(),
                                 state.combine2View.Get()) ||
        !RunNativeChromaRealign(state.combine2ShaderView.Get(),
                                state.realignView.Get()) ||
        !RunNativeChromaYCbCr(yInput,
                              state.realignShaderView.Get(),
                              state.ycbcrView.Get()) ||
        (is1080
             ? !RunNativeSuperResStrength3_1080(state.ycbcrShaderView.Get(),
                                                diffOriginal,
                                                mainOriginal,
                                                baseOriginal,
                                                output)
             : !RunNativeSuperResStrength3(state.ycbcrShaderView.Get(),
                                           diffOriginal,
                                           mainOriginal,
                                           baseOriginal,
                                           output)))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeChromaVeryHigh720Full(pl_gpu gpu)
{
    return PrepareNativeChromaFull(
        gpu, 640, 360, nativeChromaVeryHigh720Full_, "720p");
}

bool NguScaler::RunNativeChromaVeryHigh720Full(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11ShaderResourceView* diffOriginal,
    ID3D11ShaderResourceView* mainOriginal,
    ID3D11ShaderResourceView* baseOriginal,
    ID3D11RenderTargetView* output)
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
                               *nativeChromaVeryHigh720Full_);
}

bool NguScaler::PrepareNativeChromaVeryHigh1080Full(pl_gpu gpu)
{
    return PrepareNativeChromaFull(
        gpu, 960, 540, nativeChromaVeryHigh1080Full_, "1080p");
}

bool NguScaler::RunNativeChromaVeryHigh1080Full(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11ShaderResourceView* diffOriginal,
    ID3D11ShaderResourceView* mainOriginal,
    ID3D11ShaderResourceView* baseOriginal,
    ID3D11RenderTargetView* output)
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
                               *nativeChromaVeryHigh1080Full_);
}

bool NguScaler::PrepareNativeNguFromPlanes(
    pl_gpu gpu,
    int width,
    int height,
    std::unique_ptr<NativeNguFromPlanesResources>& state,
    const char* geometry)
{
    if (!gpu || width <= 0 || height <= 0)
    {
        error_ = "NGU full plane entry requires a valid GPU and geometry";
        return false;
    }
    if (state && gpu == gpu_ && state->width == UINT(width) &&
        state->height == UINT(height))
        return true;
    if (gpu_ && gpu_ != gpu)
        Reset();
    const bool is1080 = width == 1920 && height == 1080;
    if ((is1080 ? !PrepareNativeChromaVeryHigh1080Full(gpu)
                : !PrepareNativeChromaVeryHigh720Full(gpu)) ||
        (is1080 ? !PrepareNativeLumaVeryHigh1080(gpu)
                : !PrepareNativeLuma4x720Full(gpu)))
        return false;
    ID3D11Device* device = pl_d3d11_get(gpu)->device;
    auto next = std::make_unique<NativeNguFromPlanesResources>();
    next->width = UINT(width);
    next->height = UINT(height);
    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\ngu_l8_expand.hlsl", 16 * 1024);
    const auto expandBytecode =
        LoadBundledBytes(L"shaders\\Ngu\\ngu_l8_expand.cso", 0);
    if (shaderText.empty() || expandBytecode.empty())
    {
        error_ = std::string("NGU ") + geometry +
                 " L8 expansion assets are unavailable";
        return false;
    }
    ComPtr<ID3DBlob> vertexBlob, errors;
    const UINT flags =
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
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
        FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &next->vertex)) ||
        FAILED(device->CreatePixelShader(
            expandBytecode.data(),
            static_cast<SIZE_T>(expandBytecode.size()),
            nullptr,
            &next->expandPixel)))
    {
        error_ = std::string("NGU ") + geometry +
                 " L8 expansion shaders could not be created";
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(
            &rasterizerDesc, &next->rasterizer)))
    {
        error_ = std::string("NGU ") + geometry +
                 " L8 expansion rasterizer could not be created";
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
    desc.BindFlags = D3D11_BIND_RENDER_TARGET |
                     D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &next->yTexture)) ||
        FAILED(device->CreateRenderTargetView(
            next->yTexture.Get(), nullptr, &next->yView)) ||
        FAILED(device->CreateShaderResourceView(
            next->yTexture.Get(), nullptr, &next->yShaderView)))
    {
        error_ = std::string("NGU ") + geometry +
                 " Y surface could not be created";
        return false;
    }
    if (FAILED(device->CreateTexture2D(
            &desc, nullptr, &next->chromaTexture)) ||
        FAILED(device->CreateRenderTargetView(
            next->chromaTexture.Get(), nullptr, &next->chromaView)) ||
        FAILED(device->CreateShaderResourceView(
            next->chromaTexture.Get(), nullptr, &next->chromaShaderView)))
    {
        error_ = std::string("NGU ") + geometry +
                 " chroma surface could not be created";
        return false;
    }
    state = std::move(next);
    error_ = "";
    return true;
}

bool NguScaler::RunNativeNguFromPlanes(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11RenderTargetView* finalOutput,
    int width,
    int height,
    bool is1080,
    NativeNguFromPlanesResources& state)
{
    if (state.width != UINT(width) || state.height != UINT(height) ||
        !packedUv || !yInput || !finalOutput ||
        !ViewMatchesTexture(
            yInput, DXGI_FORMAT_R8_UNORM, UINT(width), UINT(height)) ||
        !ViewMatchesTexture(
            finalOutput, DXGI_FORMAT_B8G8R8A8_UNORM, 3840, 2160))
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
    if ((is1080
             ? !RunNativeChromaVeryHigh1080Full(packedUv,
                                                yInput,
                                                packedUv,
                                                state.yShaderView.Get(),
                                                state.yShaderView.Get(),
                                                state.chromaView.Get())
             : !RunNativeChromaVeryHigh720Full(packedUv,
                                               yInput,
                                               packedUv,
                                               state.yShaderView.Get(),
                                               state.yShaderView.Get(),
                                               state.chromaView.Get())) ||
        (is1080 ? !RunNativeLumaVeryHigh1080(state.chromaShaderView.Get(),
                                             UINT(width),
                                             UINT(height),
                                             finalOutput)
                : !RunNativeLuma4x720Full(state.chromaShaderView.Get(),
                                          UINT(width),
                                          UINT(height),
                                          finalOutput)))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::PrepareNativeNgu720FromPlanes(pl_gpu gpu)
{
    return PrepareNativeNguFromPlanes(
        gpu, 1280, 720, nativeNgu720FromPlanes_, "720p");
}

bool NguScaler::RunNativeNgu720FromPlanes(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11RenderTargetView* finalOutput)
{
    if (!nativeNgu720FromPlanes_)
    {
        error_ = "NGU 720p full plane scheduler is not prepared";
        return false;
    }
    return RunNativeNguFromPlanes(packedUv,
                                  yInput,
                                  finalOutput,
                                  1280,
                                  720,
                                  false,
                                  *nativeNgu720FromPlanes_);
}

bool NguScaler::PrepareNativeNgu1080FromPlanes(pl_gpu gpu)
{
    return PrepareNativeNguFromPlanes(
        gpu, 1920, 1080, nativeNgu1080FromPlanes_, "1080p");
}

bool NguScaler::RunNativeNgu1080FromPlanes(
    ID3D11ShaderResourceView* packedUv,
    ID3D11ShaderResourceView* yInput,
    ID3D11RenderTargetView* finalOutput)
{
    if (!nativeNgu1080FromPlanes_)
    {
        error_ = "NGU 1080p full plane scheduler is not prepared";
        return false;
    }
    return RunNativeNguFromPlanes(packedUv,
                                  yInput,
                                  finalOutput,
                                  1920,
                                  1080,
                                  true,
                                  *nativeNgu1080FromPlanes_);
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
    if (FAILED(device->CreateTexture2D(
            &ditherDesc, &ditherData, &nativeLumaFinal_->ditherTexture)) ||
        FAILED(device->CreateShaderResourceView(nativeLumaFinal_->ditherTexture.Get(),
                                               nullptr,
                                               &nativeLumaFinal_->ditherView)))
    {
        error_ = "NGU native final dither texture could not be created";
        return false;
    }
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

bool NguScaler::RunNativeLumaFinalOwnedDither(ID3D11ShaderResourceView* input,
                                              ID3D11RenderTargetView* output)
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
    if (!PrepareNativeLumaSplit(gpu) ||
        !PrepareNativeLumaInitial(gpu) ||
        !PrepareNativeLumaLayers(gpu) ||
        !PrepareNativeLumaDense(gpu) ||
        !PrepareNativeLumaScaleAdd(gpu) ||
        !PrepareNativeLumaFinish(gpu) ||
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
        !create(3840,
                1080,
                DXGI_FORMAT_R16_UNORM,
                state->scaleXTexture,
                state->scaleXView,
                state->scaleXShaderView) ||
        !create(3840,
                2160,
                DXGI_FORMAT_R16_UNORM,
                state->scaleYTexture,
                state->scaleYView,
                state->scaleYShaderView) ||
        !create(3840,
                2160,
                DXGI_FORMAT_R16_UNORM,
                state->addTexture,
                state->addView,
                state->addShaderView) ||
        !create(3840,
                1080,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->upXTexture,
                state->upXView,
                state->upXShaderView) ||
        !create(3840,
                2160,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->upYTexture,
                state->upYView,
                state->upYShaderView) ||
        !create(3840,
                2160,
                DXGI_FORMAT_R16G16B16A16_UNORM,
                state->mergeTexture,
                state->mergeView,
                state->mergeShaderView))
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
    if (!nativeLumaVeryHigh1080_ || !sourceRgb || !finalOutput ||
        sourceWidth != 1920 || sourceHeight != 1080)
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
    if (!RunNativeLumaInitial(nativeLumaVeryHigh1080_->splitLumaShaderView.Get(),
                              sourceWidth,
                              sourceHeight,
                              initialViews.data()))
        return false;

    std::array<ID3D11ShaderResourceView*, 3> current = {};
    for (size_t i = 0; i < current.size(); ++i)
        current[i] = nativeLumaVeryHigh1080_->initialShaderViews[i].Get();
    for (int layer = 0; layer < 15; ++layer)
    {
        const size_t set = size_t((layer / 3) % 2);
        const size_t feature = size_t(layer % 3);
        if (!RunNativeLumaLayer(current.data(),
                                layer,
                                nativeLumaVeryHigh1080_->layerViews[set][feature].Get()))
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
        !RunNativeLumaUpX(nativeLumaVeryHigh1080_->splitCbCrShaderView.Get(),
                          nativeLumaVeryHigh1080_->upXView.Get()) ||
        !RunNativeLumaUpY(nativeLumaVeryHigh1080_->upXShaderView.Get(),
                          nativeLumaVeryHigh1080_->upYView.Get()) ||
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
        if (!RunNativeLumaFinalOwnedDither(
                nativeLumaVeryHigh1080_->mergeShaderView.Get(), finalOutput))
            return false;
    }
    else if (!RunNativeLumaFinal(nativeLumaVeryHigh1080_->mergeShaderView.Get(),
                                 nativeLumaFinal_->ditherView.Get(),
                                 finalOutput))
    {
        return false;
    }
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
    const auto downXCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_down_x_1080.cso", 0);
    const auto downYCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_down_y_1080.cso", 0);
    const auto diffCso1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_diff_1080.cso", 0);
    const auto toRgbMainCso1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_main_1080.cso", 0);
    const auto toRgbBaseCso1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_asm_base_1080.cso", 0);
    const auto downXConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_constants.bin", 2 * sizeof(float) * 4);
    const auto downYConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbMainConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_main_constants.bin", 2 * sizeof(float) * 4);
    const auto toRgbBaseConstants1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_asm_base_constants.bin", 6 * sizeof(float) * 4);
    const auto downXTable1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_table.bin", 4 * 960 * 8);
    const auto downYTable1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_table.bin", 4 * 540 * 8);
    const auto uvLut1080 = LoadBundledBytes(L"shaders\\Ngu\\superres_1080_uv_lut.bin", 4096 * 8);
    const auto downXUvLut1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_x_uv_lut.bin", (960 + 1080) * 8);
    const auto downYUvLut1080 =
        LoadBundledBytes(L"shaders\\Ngu\\superres_1080_down_y_uv_lut.bin", (960 + 540) * 8);
    if (vertexText.empty() || downXCso.empty() || downYCso.empty() || diffCso.empty() ||
        toRgbMainCso.empty() || toRgbBaseCso.empty() || downXConstants.empty() ||
        downYConstants.empty() || toRgbMainConstants.empty() || toRgbBaseConstants.empty() ||
        downXTable.empty() || downYTable.empty() || uvLut.empty() ||
        downXUvLut.empty() || downYUvLut.empty() || downXCso1080.empty() ||
        downYCso1080.empty() || diffCso1080.empty() || toRgbMainCso1080.empty() ||
        toRgbBaseCso1080.empty() || downXConstants1080.empty() ||
        downYConstants1080.empty() || toRgbMainConstants1080.empty() ||
        toRgbBaseConstants1080.empty() || downXTable1080.empty() ||
        downYTable1080.empty() || uvLut1080.empty() || downXUvLut1080.empty() ||
        downYUvLut1080.empty())
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
                                         &state->toRgbBase)) ||
        FAILED(device->CreatePixelShader(downXCso1080.data(),
                                         static_cast<SIZE_T>(downXCso1080.size()),
                                         nullptr,
                                         &state->downX1080)) ||
        FAILED(device->CreatePixelShader(downYCso1080.data(),
                                         static_cast<SIZE_T>(downYCso1080.size()),
                                         nullptr,
                                         &state->downY1080)) ||
        FAILED(device->CreatePixelShader(diffCso1080.data(),
                                         static_cast<SIZE_T>(diffCso1080.size()),
                                         nullptr,
                                         &state->diff1080)) ||
        FAILED(device->CreatePixelShader(toRgbMainCso1080.data(),
                                         static_cast<SIZE_T>(toRgbMainCso1080.size()),
                                         nullptr,
                                         &state->toRgbMain1080)) ||
        FAILED(device->CreatePixelShader(toRgbBaseCso1080.data(),
                                         static_cast<SIZE_T>(toRgbBaseCso1080.size()),
                                         nullptr,
                                         &state->toRgbBase1080)))
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
        !createConstants(downXConstants1080, state->downXConstants1080) ||
        !createConstants(downYConstants1080, state->downYConstants1080) ||
        !createConstants(toRgbMainConstants1080, state->toRgbMainConstants1080) ||
        !createConstants(toRgbBaseConstants1080, state->toRgbBaseConstants1080))
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
        !createTable(downYTable, 360, state->downYTable, state->downYTableView) ||
        !createTable(downXTable1080, 960,
                     state->downXTable1080, state->downXTableView1080) ||
        !createTable(downYTable1080, 540,
                     state->downYTable1080, state->downYTableView1080))
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
        !createUvLut(downXUvLut1080, 960 + 1080,
                     state->downXUvLut1080, state->downXUvLutView1080) ||
        !createUvLut(downYUvLut1080, 960 + 540,
                     state->downYUvLut1080, state->downYUvLutView1080))
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
    if (!create(640, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->downXTexture, state->downXView, state->downXShaderView) ||
        !create(640, 360, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->downYTexture, state->downYView, state->downYShaderView) ||
        !create(640, 360, DXGI_FORMAT_R16G16_FLOAT,
                state->diffTexture, state->diffView, state->diffShaderView) ||
        !create(1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->mainTexture, state->mainView, state->mainShaderView) ||
        !create(1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->outputTexture, state->outputView, state->outputShaderView))
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
                                           ID3D11RenderTargetView* output)
{
    const bool mainIsRgba16 =
        mainOriginal &&
        ViewMatchesTexture(
            mainOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool mainIsR8 =
        mainOriginal &&
        ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R8_UNORM, 1280, 720);
    const bool baseIsRgba16 =
        baseOriginal &&
        ViewMatchesTexture(
            baseOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720);
    const bool baseIsR8 =
        baseOriginal &&
        ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R8_UNORM, 1280, 720);
    if (!nativeSuperRes_ || !nativeSuperResStrength3_ || !source || !diffOriginal ||
        !mainOriginal || !baseOriginal || !output)
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler request is invalid";
        return false;
    }
    if (!ViewMatchesTexture(source, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720) ||
        (!mainIsRgba16 && !mainIsR8) ||
        (!baseIsRgba16 && !baseIsR8) ||
        !ViewMatchesTexture(diffOriginal, DXGI_FORMAT_B8G8R8A8_UNORM, 640, 360) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720))
    {
        error_ = "NGU SuperChromaRes strength-3 scheduler input format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeSuperResStrength3_;
    if (!RunNativeSuperResDownX(source, state.downXView.Get()) ||
        !RunNativeSuperResDownY(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbMain(
            mainOriginal, source, state.diffShaderView.Get(), state.mainView.Get()) ||
        !RunNativeSuperResDownX(state.mainShaderView.Get(), state.downXView.Get()) ||
        !RunNativeSuperResDownY(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff(diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbBase(
            baseOriginal, state.mainShaderView.Get(), state.diffShaderView.Get(), output))
        return false;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeSuperResDownX1080(ID3D11ShaderResourceView* input,
                                           ID3D11RenderTargetView* output)
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

bool NguScaler::RunNativeSuperResDownY1080(ID3D11ShaderResourceView* input,
                                           ID3D11RenderTargetView* output)
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
    ID3D11ShaderResourceView* inputs[3] = {
        original, downsample, nativeSuperRes_->downYUvLutView1080.Get()};
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
    ID3D11ShaderResourceView* inputs[4] = {
        original, work, difference, nativeSuperRes_->uvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbMain1080.Get(),
                  nativeSuperRes_->toRgbMainConstants1080.Get(),
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
                                               ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !original || !work || !difference || !output)
    {
        error_ = "NGU 1080p SuperChromaRes base ToRGB request is invalid";
        return false;
    }
    ID3D11ShaderResourceView* inputs[4] = {
        original, work, difference, nativeSuperRes_->uvLutView1080.Get()};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  nativeSuperRes_->vertex.Get(),
                  nativeSuperRes_->toRgbBase1080.Get(),
                  nativeSuperRes_->toRgbBaseConstants1080.Get(),
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
    if (!create(960, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->downXTexture, state->downXView, state->downXShaderView) ||
        !create(960, 540, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->downYTexture, state->downYView, state->downYShaderView) ||
        !create(960, 540, DXGI_FORMAT_R16G16_FLOAT,
                state->diffTexture, state->diffView, state->diffShaderView) ||
        !create(1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->mainTexture, state->mainView, state->mainShaderView) ||
        !create(1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                state->outputTexture, state->outputView, state->outputShaderView))
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
                                                ID3D11RenderTargetView* output)
{
    if (!nativeSuperRes_ || !nativeSuperResStrength3_1080_ || !source || !diffOriginal ||
        !mainOriginal || !baseOriginal || !output)
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler request is invalid";
        return false;
    }
    if (!ViewMatchesTexture(source, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesTexture(mainOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesTexture(baseOriginal, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080) ||
        !ViewMatchesTexture(diffOriginal, DXGI_FORMAT_B8G8R8A8_UNORM, 960, 540) ||
        !ViewMatchesTexture(output, DXGI_FORMAT_R16G16B16A16_UNORM, 1920, 1080))
    {
        error_ = "NGU 1080p SuperChromaRes strength-3 scheduler input format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeSuperResStrength3_1080_;
    if (!RunNativeSuperResDownX1080(source, state.downXView.Get()) ||
        !RunNativeSuperResDownY1080(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff1080(
            diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbMain1080(
            mainOriginal, source, state.diffShaderView.Get(), state.mainView.Get()) ||
        !RunNativeSuperResDownX1080(state.mainShaderView.Get(), state.downXView.Get()) ||
        !RunNativeSuperResDownY1080(state.downXShaderView.Get(), state.downYView.Get()) ||
        !RunNativeSuperResDiff1080(
            diffOriginal, state.downYShaderView.Get(), state.diffView.Get()) ||
        !RunNativeSuperResToRgbBase1080(
            baseOriginal, state.mainShaderView.Get(), state.diffShaderView.Get(), output))
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

    const std::string scaleXText =
        LoadBundledText(L"shaders\\Ngu\\luma_4x720_scale_x.hlsl", 64 * 1024);
    const std::string initialText =
        LoadBundledText(L"shaders\\Ngu\\luma_1080_cubici1.hlsl", 256 * 1024);
    const auto scaleXCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_x.cso", 0);
    const auto scaleYCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_y.cso", 0);
    const auto layerCso = LoadBundledBytes(L"shaders\\Ngu\\luma_1080_layer.cso", 0);
    const auto scaleXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_x_constants.bin", 32);
    const auto scaleYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_y_constants.bin", 32);
    const auto initialConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_initial_constants.bin", 512);
    const auto layerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_low_layer_constants.bin",
                         15 * 111 * sizeof(float) * 4);
    const auto scaleXTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_table_x.bin", 163840);
    const auto scaleYTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_scale_table_y.bin", 92160);
    if (scaleXText.empty() || initialText.empty() || scaleXCso.empty() ||
        scaleYCso.empty() || layerCso.empty() || scaleXConstants.empty() ||
        scaleYConstants.empty() || initialConstants.empty() ||
        layerConstants.empty() || scaleXTable.empty() || scaleYTable.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU 720p 4x vertex shader failed";
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU 720p 4x initializer shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720Low>();
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
        FAILED(device->CreatePixelShader(initialBlob->GetBufferPointer(),
                                         initialBlob->GetBufferSize(),
                                         nullptr,
                                         &state->initial)) ||
        FAILED(device->CreatePixelShader(layerCso.data(),
                                         static_cast<SIZE_T>(layerCso.size()),
                                         nullptr,
                                         &state->layer)))
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
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
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
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
    };
    if (!createTexture(1280, 720, DXGI_FORMAT_R16_UNORM,
                       state->splitLumaTexture, state->splitLumaView,
                       state->splitLumaShaderView) ||
        !createTexture(1280, 720, DXGI_FORMAT_R16G16_UNORM,
                       state->splitCbCrTexture, state->splitCbCrView,
                       state->splitCbCrShaderView) ||
        !createTexture(5120, 720, DXGI_FORMAT_R16_UNORM,
                       state->scaleXTexture, state->scaleXView,
                       state->scaleXShaderView) ||
        !createTexture(5120, 2880, DXGI_FORMAT_R16_UNORM,
                       state->scaleYTexture, state->scaleYView,
                       state->scaleYShaderView))
    {
        error_ = "NGU 720p 4x luma fixed surfaces could not be created";
        return false;
    }
    for (size_t i = 0; i < state->featureATextures.size(); ++i)
    {
        if (!createTexture(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT,
                           state->featureATextures[i], state->featureAViews[i],
                           state->featureAShaderViews[i]) ||
            !createTexture(1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT,
                           state->featureBTextures[i], state->featureBViews[i],
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
                                      int sourceHeight)
{
    if (!nativeLuma4x720Low_ || !nativeLumaSplit_ || !sourceRgb ||
        sourceWidth != 1280 || sourceHeight != 720)
    {
        error_ = "NGU 720p 4x luma core request is outside the verified path";
        return false;
    }
    if (!ViewMatchesTexture(
            sourceRgb, DXGI_FORMAT_R16G16B16A16_UNORM, 1280, 720))
    {
        error_ = "NGU 720p 4x luma source format or geometry is unsupported";
        return false;
    }

    auto& state = *nativeLuma4x720Low_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    if (!RunNativeLumaSplit(sourceRgb,
                            sourceWidth,
                            sourceHeight,
                            state.splitLumaView.Get(),
                            state.splitCbCrView.Get()))
        return false;

    ID3D11ShaderResourceView* scaleXInputs[2] = {
        state.splitLumaShaderView.Get(), state.scaleXTableView.Get()};
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
    ID3D11ShaderResourceView* scaleYInputs[2] = {
        state.scaleXShaderView.Get(), state.scaleYTableView.Get()};
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
        state.featureAViews[0].Get(),
        state.featureAViews[1].Get(),
        state.featureAViews[2].Get()};
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
            state.featureAShaderViews[0].Get(),
            state.featureAShaderViews[1].Get(),
            state.featureAShaderViews[2].Get()};
        for (size_t feature = 0; feature < 3; ++feature)
        {
            const size_t layer = stage * 3 + feature;
            ID3D11RenderTargetView* layerOutput =
                state.featureBViews[feature].Get();
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
    if (!nativeLuma4x720Low_ ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->splitLumaTexture.Get(), output))
    {
        error_ = "NGU 720p 4x split output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720SplitCbCr(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->splitCbCrTexture.Get(), output))
    {
        error_ = "NGU 720p 4x split CbCr output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720ScaleX(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->scaleXTexture.Get(), output))
    {
        error_ = "NGU 720p 4x X-scale output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720ScaleY(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Low_ ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->scaleYTexture.Get(), output))
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
    if (!nativeLuma4x720Low_ ||
        !CopyTextureToTarget(gpu_, nativeLuma4x720Low_->featureATextures[2].Get(), output))
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

    const auto upsampleCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_upsample.cso", 0);
    const auto convertCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_convert.cso", 0);
    const auto upsampleConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_upsample_constants.bin",
                         3 * 196 * sizeof(float) * 4);
    const auto convertConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_convert_constants.bin",
                         3 * 2 * sizeof(float) * 4);
    const auto highLayerConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_high_layer_constants.bin",
                         6 * 111 * sizeof(float) * 4);
    if (upsampleCso.empty() || convertCso.empty() ||
        upsampleConstants.empty() || convertConstants.empty() ||
        highLayerConstants.empty())
    {
        error_ = "NGU 720p 4x high-core assets are unavailable";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720High>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreatePixelShader(upsampleCso.data(),
                                         static_cast<SIZE_T>(upsampleCso.size()),
                                         nullptr,
                                         &state->upsample)) ||
        FAILED(device->CreatePixelShader(convertCso.data(),
                                         static_cast<SIZE_T>(convertCso.size()),
                                         nullptr,
                                         &state->convert)))
    {
        error_ = "NGU 720p 4x high-core shaders could not be created";
        return false;
    }

    auto createConstants = [&](const uint8_t* bytes,
                               UINT size,
                               ComPtr<ID3D11Buffer>& buffer)
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
        if (!createConstants(upsampleConstants.data() + feature * 196 * 16,
                             196 * 16,
                             state->upsampleConstants[feature]) ||
            !createConstants(convertConstants.data() + feature * 2 * 16,
                             2 * 16,
                             state->convertConstants[feature]))
        {
            error_ = "NGU 720p 4x upsampler/convert constants could not be created";
            return false;
        }
    }
    for (size_t layer = 0; layer < 6; ++layer)
    {
        if (!createConstants(highLayerConstants.data() + layer * 111 * 16,
                             111 * 16,
                             state->highLayerConstants[layer]))
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
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
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
                                       int sourceHeight)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ ||
        !RunNativeLuma4x720Low(sourceRgb, sourceWidth, sourceHeight))
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
            low.featureAShaderViews[0].Get(),
            low.featureAShaderViews[1].Get(),
            low.featureAShaderViews[2].Get()};
        ID3D11RenderTargetView* outputs[4] = {
            high.upFeatureViews[feature][0].Get(),
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
        ID3D11ShaderResourceView* inputs[4] = {
            high.upFeatureShaderViews[feature][0].Get(),
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
        ID3D11ShaderResourceView* inputs[3] = {
            inputViews[0].Get(), inputViews[1].Get(), inputViews[2].Get()};
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

bool NguScaler::CopyNativeLuma4x720Upsample(int feature,
                                            int part,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ || !output ||
        feature < 0 || feature >= 3 || part < 0 || part >= 4)
    {
        error_ = "NGU 720p 4x upsampler output request is invalid";
        return false;
    }
    auto& high = *nativeLuma4x720High_;
    if (!CopyTextureToTarget(
            gpu_, high.upFeatureTextures[feature][part].Get(), output))
    {
        error_ = "NGU 720p 4x upsampler output copy failed";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Convert(int feature,
                                           ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !nativeLuma4x720Low_ || !output ||
        feature < 0 || feature >= 3)
    {
        error_ = "NGU 720p 4x converter output request is invalid";
        return false;
    }
    auto& low = *nativeLuma4x720Low_;
    auto& high = *nativeLuma4x720High_;
    ID3D11ShaderResourceView* inputs[4] = {
        high.upFeatureShaderViews[feature][0].Get(),
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

bool NguScaler::CopyNativeLuma4x720HighLayer(int feature,
                                             ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720High_ || !output || feature < 0 || feature >= 3 ||
        !CopyTextureToTarget(
            gpu_, nativeLuma4x720High_->convertTextures[feature].Get(), output))
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
    const auto denseConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_dense_constants.bin", 816);
    const auto addConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_add_constants.bin", 32);
    if (denseCso.empty() || addCso.empty() ||
        denseConstants.empty() || addConstants.empty())
    {
        error_ = "NGU 720p 4x dense/add assets are unavailable";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720DenseAdd>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreatePixelShader(denseCso.data(),
                                         static_cast<SIZE_T>(denseCso.size()),
                                         nullptr,
                                         &state->dense)) ||
        FAILED(device->CreatePixelShader(addCso.data(),
                                         static_cast<SIZE_T>(addCso.size()),
                                         nullptr,
                                         &state->add)))
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
    if (!createConstants(denseConstants, state->denseConstants) ||
        !createConstants(addConstants, state->addConstants))
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
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
    };
    if (!createTexture(2562,
                       1442,
                       DXGI_FORMAT_R16G16B16A16_FLOAT,
                       state->denseTexture,
                       state->denseView,
                       state->denseShaderView) ||
        !createTexture(5120,
                       2880,
                       DXGI_FORMAT_R16_UNORM,
                       state->addTexture,
                       state->addView,
                       state->addShaderView))
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
                                           int sourceHeight)
{
    if (!nativeLuma4x720DenseAdd_ || !nativeLuma4x720High_ ||
        !nativeLuma4x720Low_ ||
        !RunNativeLuma4x720High(sourceRgb, sourceWidth, sourceHeight))
        return false;

    auto& low = *nativeLuma4x720Low_;
    auto& high = *nativeLuma4x720High_;
    auto& state = *nativeLuma4x720DenseAdd_;
    ID3D11Device* device = pl_d3d11_get(gpu_)->device;
    ID3D11ShaderResourceView* denseInputs[3] = {
        high.convertShaderViews[0].Get(),
        high.convertShaderViews[1].Get(),
        high.convertShaderViews[2].Get()};
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

    ID3D11ShaderResourceView* addInputs[2] = {
        low.scaleYShaderView.Get(), state.denseShaderView.Get()};
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
    if (!nativeLuma4x720DenseAdd_ ||
        !CopyTextureToTarget(
            gpu_, nativeLuma4x720DenseAdd_->denseTexture.Get(), output))
    {
        error_ = "NGU 720p 4x dense output is unavailable";
        return false;
    }
    error_ = "";
    return true;
}

bool NguScaler::CopyNativeLuma4x720Add(ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720DenseAdd_ ||
        !CopyTextureToTarget(
            gpu_, nativeLuma4x720DenseAdd_->addTexture.Get(), output))
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

    const std::string shaderText =
        LoadBundledText(L"shaders\\Ngu\\luma_4x720_tail_ycbcr.hlsl", 32 * 1024);
    const auto ycbcrCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ycbcr.cso", 0);
    const auto downXCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x.cso", 0);
    const auto downXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_constants.bin", 32);
    const auto downXTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_table.bin", 184320);
    const auto downXUvLut =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_x_uv_lut.bin", 61440);
    const auto downYCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y.cso", 0);
    const auto downYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_constants.bin", 32);
    const auto downYTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_table.bin", 103680);
    const auto downYUvLut =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_down_y_uv_lut.bin", 61440);
    const auto ssimRtCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_rt.cso", 0);
    const auto ssimRtConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_rt_constants.bin", 32);
    const auto ssimFinalCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_final.cso", 0);
    const auto ssimFinalConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_ssim_final_constants.bin", 32);
    const auto sharpenArCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_sharpen_ar.cso", 0);
    const auto sharpenArConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_sharpen_ar_constants.bin", 4096);
    const auto chromaXCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x.cso", 0);
    const auto chromaYCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y.cso", 0);
    const auto mergeCso =
        LoadBundledBytes(L"shaders\\Ngu\\luma_2160_merge.cso", 0);
    const auto chromaXConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x_constants.bin", 32);
    const auto chromaYConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y_constants.bin", 32);
    const auto mergeConstants =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_merge_constants.bin", 32);
    const auto chromaXTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_x_table.bin", 122880);
    const auto chromaYTable =
        LoadBundledBytes(L"shaders\\Ngu\\luma_4x720_tail_chroma_y_table.bin", 69120);
    if (shaderText.empty() || ycbcrCso.empty() || downXCso.empty() ||
        downXConstants.empty() || downXTable.empty() || downXUvLut.empty() ||
        downYCso.empty() || downYConstants.empty() || downYTable.empty() ||
        downYUvLut.empty() || ssimRtCso.empty() || ssimRtConstants.empty() ||
        ssimFinalCso.empty() || ssimFinalConstants.empty() ||
        sharpenArCso.empty() || sharpenArConstants.empty() ||
        chromaXCso.empty() || chromaYCso.empty() || mergeCso.empty() ||
        chromaXConstants.empty() || chromaYConstants.empty() ||
        mergeConstants.empty() || chromaXTable.empty() || chromaYTable.empty())
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
        error_ = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                          "NGU 720p 4x tail vertex shader failed";
        return false;
    }

    auto state = std::make_unique<NativeLuma4x720Tail>();
    ID3D11Device* device = d3d->device;
    if (FAILED(device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          nullptr,
                                          &state->vertex)) ||
        FAILED(device->CreatePixelShader(ycbcrCso.data(),
                                         static_cast<SIZE_T>(ycbcrCso.size()),
                                         nullptr,
                                         &state->ycbcr)) ||
        FAILED(device->CreatePixelShader(downXCso.data(),
                                         static_cast<SIZE_T>(downXCso.size()),
                                         nullptr,
                                         &state->downX)) ||
        FAILED(device->CreatePixelShader(downYCso.data(),
                                         static_cast<SIZE_T>(downYCso.size()),
                                         nullptr,
                                         &state->downY)) ||
        FAILED(device->CreatePixelShader(ssimRtCso.data(),
                                         static_cast<SIZE_T>(ssimRtCso.size()),
                                         nullptr,
                                         &state->ssimRt)) ||
        FAILED(device->CreatePixelShader(ssimFinalCso.data(),
                                         static_cast<SIZE_T>(ssimFinalCso.size()),
                                         nullptr,
                                         &state->ssimFinal)) ||
        FAILED(device->CreatePixelShader(sharpenArCso.data(),
                                         static_cast<SIZE_T>(sharpenArCso.size()),
                                         nullptr,
                                         &state->sharpenAr)) ||
        FAILED(device->CreatePixelShader(chromaXCso.data(),
                                         static_cast<SIZE_T>(chromaXCso.size()),
                                         nullptr,
                                         &state->chromaX)) ||
        FAILED(device->CreatePixelShader(chromaYCso.data(),
                                         static_cast<SIZE_T>(chromaYCso.size()),
                                         nullptr,
                                         &state->chromaY)) ||
        FAILED(device->CreatePixelShader(mergeCso.data(),
                                         static_cast<SIZE_T>(mergeCso.size()),
                                         nullptr,
                                         &state->merge)))
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
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->downXConstants)))
    {
        error_ = "NGU 720p 4x tail down-X constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(downYConstants.size());
    constantData.pSysMem = downYConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->downYConstants)))
    {
        error_ = "NGU 720p 4x tail down-Y constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(ssimRtConstants.size());
    constantData.pSysMem = ssimRtConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->ssimRtConstants)))
    {
        error_ = "NGU 720p 4x tail SSIM RT constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(ssimFinalConstants.size());
    constantData.pSysMem = ssimFinalConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->ssimFinalConstants)))
    {
        error_ = "NGU 720p 4x tail SSIM final constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(sharpenArConstants.size());
    constantData.pSysMem = sharpenArConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->sharpenArConstants)))
    {
        error_ = "NGU 720p 4x tail SharpenAR constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(chromaXConstants.size());
    constantData.pSysMem = chromaXConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->chromaXConstants)))
    {
        error_ = "NGU 720p 4x tail chroma-X constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(chromaYConstants.size());
    constantData.pSysMem = chromaYConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->chromaYConstants)))
    {
        error_ = "NGU 720p 4x tail chroma-Y constants could not be created";
        return false;
    }
    constantDesc.ByteWidth = static_cast<UINT>(mergeConstants.size());
    constantData.pSysMem = mergeConstants.data();
    if (FAILED(device->CreateBuffer(
            &constantDesc, &constantData, &state->mergeConstants)))
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
    if (FAILED(device->CreateTexture2D(
            &tableDesc, &tableData, &state->downXTable)) ||
        FAILED(device->CreateShaderResourceView(
            state->downXTable.Get(), nullptr, &state->downXTableView)))
    {
        error_ = "NGU 720p 4x tail down-X table could not be created";
        return false;
    }
    tableDesc.Height = 2160;
    tableData.pSysMem = downYTable.data();
    if (FAILED(device->CreateTexture2D(
            &tableDesc, &tableData, &state->downYTable)) ||
        FAILED(device->CreateShaderResourceView(
            state->downYTable.Get(), nullptr, &state->downYTableView)))
    {
        error_ = "NGU 720p 4x tail down-Y table could not be created";
        return false;
    }
    tableDesc.Width = 4;
    tableDesc.Height = 3840;
    tableData.pSysMem = chromaXTable.data();
    tableData.SysMemPitch = 4 * 8;
    if (FAILED(device->CreateTexture2D(
            &tableDesc, &tableData, &state->chromaXTable)) ||
        FAILED(device->CreateShaderResourceView(
            state->chromaXTable.Get(), nullptr, &state->chromaXTableView)))
    {
        error_ = "NGU 720p 4x tail chroma-X table could not be created";
        return false;
    }
    tableDesc.Height = 2160;
    tableData.pSysMem = chromaYTable.data();
    if (FAILED(device->CreateTexture2D(
            &tableDesc, &tableData, &state->chromaYTable)) ||
        FAILED(device->CreateShaderResourceView(
            state->chromaYTable.Get(), nullptr, &state->chromaYTableView)))
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
    if (FAILED(device->CreateTexture2D(
            &uvDesc, &uvData, &state->downXUvLut)) ||
        FAILED(device->CreateShaderResourceView(
            state->downXUvLut.Get(), nullptr, &state->downXUvLutView)))
    {
        error_ = "NGU 720p 4x tail down-X UV LUT could not be created";
        return false;
    }
    uvData.pSysMem = downYUvLut.data();
    if (FAILED(device->CreateTexture2D(
            &uvDesc, &uvData, &state->downYUvLut)) ||
        FAILED(device->CreateShaderResourceView(
            state->downYUvLut.Get(), nullptr, &state->downYUvLutView)))
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
               SUCCEEDED(device->CreateRenderTargetView(
                   texture.Get(), nullptr, &renderTarget)) &&
               SUCCEEDED(device->CreateShaderResourceView(
                   texture.Get(), nullptr, &shaderView));
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
        !createIntermediate(3840,
                            2160,
                            DXGI_FORMAT_R16G16B16A16_UNORM,
                            state->mergeTexture,
                            state->mergeView,
                            state->mergeShaderView))
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

bool NguScaler::RunNativeLuma4x720TailYcbcr(ID3D11ShaderResourceView* input,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16_UNORM, 5120, 2880) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 5120, 2880))
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

bool NguScaler::RunNativeLuma4x720TailDownX(ID3D11ShaderResourceView* input,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16G16B16A16_UNORM, 5120, 2880) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2880))
    {
        error_ = "NGU 720p 4x tail down-X requires exact 5120/3840 surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {
        input, state.downXTableView.Get(), state.downXUvLutView.Get()};
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

bool NguScaler::RunNativeLuma4x720TailDownY(ID3D11ShaderResourceView* input,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2880) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail down-Y requires exact 3840x2880/2160 surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {
        input, state.downYTableView.Get(), state.downYUvLutView.Get()};
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

bool NguScaler::RunNativeLuma4x720TailSsimRt(ID3D11ShaderResourceView* input,
                                             ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_FLOAT, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SSIM RT requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {
        input, state.downYUvLutView.Get()};
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
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            rt, DXGI_FORMAT_R16G16B16A16_FLOAT, 3840, 2160) ||
        !ViewMatchesTexture(
            source, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SSIM final requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[3] = {
        rt, source, state.downYUvLutView.Get()};
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

bool NguScaler::RunNativeLuma4x720TailSharpenAr(ID3D11ShaderResourceView* input,
                                                ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail SharpenAR requires exact 4K surfaces";
        return false;
    }

    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {
        input, state.downYUvLutView.Get()};
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

bool NguScaler::RunNativeLuma4x720TailChromaX(ID3D11ShaderResourceView* input,
                                              ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(input, DXGI_FORMAT_R16G16_UNORM, 1280, 720) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 720))
    {
        error_ = "NGU 720p 4x tail chroma-X requires exact 1280x720/3840x720 surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.chromaXTableView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(), state.chromaX.Get(),
                  state.chromaXConstants.Get(), state.rasterizer.Get(),
                  inputs, 2, targets, 1, 3840, 720,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailChromaY(ID3D11ShaderResourceView* input,
                                              ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            input, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 720) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail chroma-Y requires exact 3840x720/2160 surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {input, state.chromaYTableView.Get()};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(), state.chromaY.Get(),
                  state.chromaYConstants.Get(), state.rasterizer.Get(),
                  inputs, 2, targets, 1, 3840, 2160,
                  state.pointSampler.Get());
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLuma4x720TailMerge(ID3D11ShaderResourceView* luma,
                                            ID3D11ShaderResourceView* chroma,
                                            ID3D11RenderTargetView* output)
{
    if (!nativeLuma4x720Tail_ ||
        !ViewMatchesTexture(
            luma, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(
            chroma, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160) ||
        !ViewMatchesTexture(
            output, DXGI_FORMAT_R16G16B16A16_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x tail merge requires exact 4K surfaces";
        return false;
    }
    auto& state = *nativeLuma4x720Tail_;
    ID3D11ShaderResourceView* inputs[2] = {luma, chroma};
    ID3D11RenderTargetView* targets[1] = {output};
    DrawD3D11Pass(pl_d3d11_get(gpu_)->device,
                  state.vertex.Get(), state.merge.Get(),
                  state.mergeConstants.Get(), state.rasterizer.Get(),
                  inputs, 2, targets, 1, 3840, 2160,
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
    if (nativeLuma4x720Tail_ && nativeLuma4x720DenseAdd_ &&
        nativeLumaFinal_ && gpu == gpu_)
    {
        cached_.quality = NguQuality::VeryHigh;
        cached_.stage = NguScalerStage::VeryHighLuma4x720Full;
        cached_.sourceWidth = 1280;
        cached_.sourceHeight = 720;
        hasCached_ = true;
        return true;
    }
    if (!PrepareNativeLuma4x720DenseAdd(gpu) ||
        !PrepareNativeLuma4x720Tail(gpu) ||
        !PrepareNativeLumaFinal(gpu))
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
    if (!nativeLuma4x720Low_ || !nativeLuma4x720High_ ||
        !nativeLuma4x720DenseAdd_ || !nativeLuma4x720Tail_ ||
        !nativeLumaFinal_ || !sourceRgb || !finalOutput ||
        sourceWidth != 1280 || sourceHeight != 720 ||
        !ViewMatchesTexture(
            finalOutput, DXGI_FORMAT_B8G8R8A8_UNORM, 3840, 2160))
    {
        error_ = "NGU 720p 4x full request is outside the verified path";
        return false;
    }

    auto& low = *nativeLuma4x720Low_;
    auto& denseAdd = *nativeLuma4x720DenseAdd_;
    auto& tail = *nativeLuma4x720Tail_;
    if (!RunNativeLuma4x720DenseAdd(sourceRgb, sourceWidth, sourceHeight) ||
        !RunNativeLuma4x720TailYcbcr(
            denseAdd.addShaderView.Get(), tail.ycbcrView.Get()) ||
        !RunNativeLuma4x720TailDownX(
            tail.ycbcrShaderView.Get(), tail.downXView.Get()) ||
        !RunNativeLuma4x720TailDownY(
            tail.downXShaderView.Get(), tail.downYView.Get()) ||
        !RunNativeLuma4x720TailSsimRt(
            tail.downYShaderView.Get(), tail.ssimRtView.Get()) ||
        !RunNativeLuma4x720TailSsimFinal(
            tail.ssimRtShaderView.Get(),
            tail.downYShaderView.Get(),
            tail.ssimFinalView.Get()) ||
        !RunNativeLuma4x720TailSharpenAr(
            tail.ssimFinalShaderView.Get(), tail.sharpenArView.Get()) ||
        !RunNativeLuma4x720TailChromaX(
            low.splitCbCrShaderView.Get(), tail.chromaXView.Get()) ||
        !RunNativeLuma4x720TailChromaY(
            tail.chromaXShaderView.Get(), tail.chromaYView.Get()) ||
        !RunNativeLuma4x720TailMerge(
            tail.sharpenArShaderView.Get(),
            tail.chromaYShaderView.Get(),
            tail.mergeView.Get()) ||
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

bool NguScaler::RunNativeLuma4x720SuperResFull(
    ID3D11ShaderResourceView* source,
    ID3D11ShaderResourceView* diffOriginal,
    ID3D11ShaderResourceView* mainOriginal,
    ID3D11ShaderResourceView* baseOriginal,
    ID3D11RenderTargetView* finalOutput)
{
    if (!nativeSuperResStrength3_ || !source || !diffOriginal ||
        !mainOriginal || !baseOriginal || !finalOutput)
    {
        error_ = "NGU 720p SuperRes full request is invalid";
        return false;
    }
    auto& superRes = *nativeSuperResStrength3_;
    if (!RunNativeSuperResStrength3(source,
                                    diffOriginal,
                                    mainOriginal,
                                    baseOriginal,
                                    superRes.outputView.Get()))
        return false;
    return RunNativeLuma4x720Full(
        superRes.outputShaderView.Get(), 1280, 720, finalOutput);
}

bool NguScaler::PrepareNativeLumaVeryHigh1080SuperResFull(pl_gpu gpu)
{
    if (!PrepareNativeSuperResStrength3_1080(gpu) ||
        !PrepareNativeLumaVeryHigh1080(gpu))
        return false;
    cached_.quality = NguQuality::VeryHigh;
    cached_.stage = NguScalerStage::VeryHighLuma1080SuperResFull;
    cached_.sourceWidth = 1920;
    cached_.sourceHeight = 1080;
    hasCached_ = true;
    error_ = "";
    return true;
}

bool NguScaler::RunNativeLumaVeryHigh1080SuperResFull(
    ID3D11ShaderResourceView* source,
    ID3D11ShaderResourceView* diffOriginal,
    ID3D11ShaderResourceView* mainOriginal,
    ID3D11ShaderResourceView* baseOriginal,
    ID3D11RenderTargetView* finalOutput)
{
    if (!nativeSuperResStrength3_1080_ || !source || !diffOriginal ||
        !mainOriginal || !baseOriginal || !finalOutput)
    {
        error_ = "NGU 1080p SuperRes full request is invalid";
        return false;
    }
    auto& superRes = *nativeSuperResStrength3_1080_;
    if (!RunNativeSuperResStrength3_1080(source,
                                         diffOriginal,
                                         mainOriginal,
                                         baseOriginal,
                                         superRes.outputView.Get()))
        return false;
    return RunNativeLumaVeryHigh1080(
        superRes.outputShaderView.Get(), 1920, 1080, finalOutput);
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
    nativeLumaVeryHigh1080_.reset();
    nativeSuperRes_.reset();
    nativeSuperResStrength3_.reset();
    nativeSuperResStrength3_1080_.reset();
    nativeLuma4x720Low_.reset();
    nativeLuma4x720High_.reset();
    nativeLuma4x720DenseAdd_.reset();
    nativeLuma4x720Tail_.reset();
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
