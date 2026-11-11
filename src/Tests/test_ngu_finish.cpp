#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open binary");
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(expected))), "read binary");
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      int width,
                                      int height,
                                      DXGI_FORMAT format,
                                      UINT bind,
                                      const void* initial = nullptr)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    const UINT pixelBytes = format == DXGI_FORMAT_R16_UNORM ? 2
                           : format == DXGI_FORMAT_R16G16_UNORM ? 4
                                                               : 8;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = width * pixelBytes;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, initial ? &data : nullptr, &texture)), "create texture");
    return texture;
}

std::vector<uint8_t> Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source)
{
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "create staging texture");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map staging texture");
    const UINT pixelBytes = desc.Format == DXGI_FORMAT_R16_UNORM ? 2 : 8;
    const size_t rowBytes = size_t(desc.Width) * pixelBytes;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CountComponents(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0, largest = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t left = uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t right = uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        if (left != right)
        {
            ++different;
            largest = std::max(largest, std::abs(int(left) - int(right)));
        }
    }
    maximum = largest;
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_finish <capture-root> <actual-prefix>");
        const std::filesystem::path root(argv[1]);
        const std::wstring actualPrefix = std::filesystem::path(argv[2]).wstring();

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScaler ngu;
        const bool prepared = ngu.PrepareNativeLumaFinish(d3d->gpu);
        Require(prepared, ngu.LastError());

        const auto xInputBytes =
            ReadBytes(root / "input_31724_PS_1284_386cc8d7_s0.bin", size_t(1920) * 1080 * 4);
        std::vector<uint16_t> xInputExpanded(size_t(1920) * 1080 * 4);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
        {
            xInputExpanded[i * 4] = uint16_t(xInputBytes[i * 4]) | (uint16_t(xInputBytes[i * 4 + 1]) << 8);
            xInputExpanded[i * 4 + 1] = uint16_t(xInputBytes[i * 4 + 2]) | (uint16_t(xInputBytes[i * 4 + 3]) << 8);
            xInputExpanded[i * 4 + 2] = 65535;
            xInputExpanded[i * 4 + 3] = 65535;
        }
        auto xInput = CreateTexture(
            device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, D3D11_BIND_SHADER_RESOURCE,
            xInputExpanded.data());
        ComPtr<ID3D11ShaderResourceView> xInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(xInput.Get(), nullptr, &xInputView)), "X input SRV");
        auto xOutput = CreateTexture(
            device, 3840, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> xView;
        ComPtr<ID3D11ShaderResourceView> xSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(xOutput.Get(), nullptr, &xView)), "X RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(xOutput.Get(), nullptr, &xSrv)), "X SRV");
        const bool xRan = ngu.RunNativeLumaUpX(xInputView.Get(), xView.Get());
        Require(xRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto xActual = Download(device, context.Get(), xOutput.Get());
        int xMaximum = 0;
        const int xDifferent = CountComponents(
            xActual,
            ReadBytes(root / "draw_31724_PS_1284_386cc8d7_rt0_3840x1080_f36.bin", size_t(3840) * 1080 * 8),
            xMaximum);

        auto yOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> yView;
        ComPtr<ID3D11ShaderResourceView> ySrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(yOutput.Get(), nullptr, &yView)), "Y RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(yOutput.Get(), nullptr, &ySrv)), "Y SRV");
        const bool yRan = ngu.RunNativeLumaUpY(xSrv.Get(), yView.Get());
        Require(yRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto yActual = Download(device, context.Get(), yOutput.Get());
        int yMaximum = 0;
        const int yDifferent = CountComponents(
            yActual,
            ReadBytes(root / "draw_31725_PS_1284_dc97a0d0_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            yMaximum);

        const auto lumaCapture =
            ReadBytes(root / "input_31726_PS_548_d6670d4b_s0.bin", size_t(3840) * 2160 * 8);
        std::vector<uint16_t> luma(size_t(3840) * 2160);
        for (size_t i = 0; i < luma.size(); ++i)
            std::memcpy(&luma[i], lumaCapture.data() + i * 8, sizeof(uint16_t));
        auto lumaInput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16_UNORM, D3D11_BIND_SHADER_RESOURCE, luma.data());
        ComPtr<ID3D11ShaderResourceView> lumaSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(lumaInput.Get(), nullptr, &lumaSrv)), "luma SRV");
        auto mergeOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> mergeView;
        Require(SUCCEEDED(device->CreateRenderTargetView(mergeOutput.Get(), nullptr, &mergeView)), "merge RTV");
        const bool mergeRan = ngu.RunNativeLumaMerge(lumaSrv.Get(), ySrv.Get(), mergeView.Get());
        Require(mergeRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto mergeActual = Download(device, context.Get(), mergeOutput.Get());
        int mergeMaximum = 0;
        const int mergeDifferent = CountComponents(
            mergeActual,
            ReadBytes(root / "draw_31726_PS_548_d6670d4b_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            mergeMaximum);

        std::printf("UpX different=%d maximum=%d\n", xDifferent, xMaximum);
        std::printf("UpY different=%d maximum=%d\n", yDifferent, yMaximum);
        std::printf("Merge different=%d maximum=%d\n", mergeDifferent, mergeMaximum);
        const bool pass = xDifferent == 0 && yDifferent == 0 && mergeDifferent == 0;
        for (const auto& [name, bytes] :
             {std::pair{L"upx", xActual}, std::pair{L"upy", yActual}, std::pair{L"merge", mergeActual}})
        {
            std::ofstream file(actualPrefix + name + L".bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        std::printf("%s native NGU 4x reconstruct + merge\n", pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");
        ngu.Reset();
        mergeView.Reset();
        mergeOutput.Reset();
        lumaSrv.Reset();
        lumaInput.Reset();
        ySrv.Reset();
        yView.Reset();
        yOutput.Reset();
        xSrv.Reset();
        xView.Reset();
        xOutput.Reset();
        xInputView.Reset();
        xInput.Reset();
        context.Reset();
        pl_d3d11_destroy(&d3d);
        return pass ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
