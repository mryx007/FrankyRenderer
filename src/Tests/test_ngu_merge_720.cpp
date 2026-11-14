#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
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
constexpr UINT kWidth = 2560;
constexpr UINT kHeight = 1440;
constexpr size_t kPixelBytes = 8;

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path,
                               size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open binary");
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           std::streamsize(expected))),
            "read binary");
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      DXGI_FORMAT format,
                                      UINT bind,
                                      const void* initial = nullptr)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = UINT(kWidth * kPixelBytes);
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, initial ? &data : nullptr, &texture)),
            "create texture");
    return texture;
}

std::vector<uint8_t> Download(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              ID3D11Texture2D* source)
{
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "create staging texture");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging texture");
    const size_t rowBytes = size_t(kWidth) * kPixelBytes;
    std::vector<uint8_t> bytes(rowBytes * kHeight);
    for (UINT y = 0; y < kHeight; ++y)
    {
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    rowBytes);
    }
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CountComponents(const std::vector<uint8_t>& actual,
                    const std::vector<uint8_t>& expected,
                    int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    maximum = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t left =
            uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        if (left != right)
        {
            ++different;
            maximum = std::max(
                maximum, std::abs(int(left) - int(right)));
        }
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3,
                "usage: test_ngu_merge_720 <luma_canonical27> <actual.bin>");
        const std::filesystem::path root(argv[1]);
        const std::filesystem::path actualPath(argv[2]);

        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(bool(d3d), "create D3D11 backend");
        pl_d3d11 d3d11 = pl_d3d11_get(d3d->gpu);
        Require(bool(d3d11) && bool(d3d11->device), "D3D11 device");
        ID3D11Device* device = d3d11->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        const size_t components = size_t(kWidth) * kHeight * kPixelBytes;
        auto lumaBytes =
            ReadBytes(root / "input_03917_PS_548_d6670d4b_s0.bin",
                      components);
        auto cbcrBytes =
            ReadBytes(root / "input_03917_PS_548_d6670d4b_s2.bin",
                      components);
        auto expected =
            ReadBytes(root /
                          "draw_03917_PS_548_d6670d4b_rt0_2560x1440_f36.bin",
                      components);

        auto luma = CreateTexture(device,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  D3D11_BIND_SHADER_RESOURCE,
                                  lumaBytes.data());
        auto cbcr = CreateTexture(device,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  D3D11_BIND_SHADER_RESOURCE,
                                  cbcrBytes.data());
        auto output = CreateTexture(
            device,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11ShaderResourceView> lumaView;
        ComPtr<ID3D11ShaderResourceView> cbcrView;
        ComPtr<ID3D11RenderTargetView> outputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    luma.Get(), nullptr, &lumaView)),
                "luma SRV");
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    cbcr.Get(), nullptr, &cbcrView)),
                "CbCr SRV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    output.Get(), nullptr, &outputView)),
                "output RTV");

        NguScaler ngu;
        Require(ngu.PrepareNativeLuma720Double(d3d->gpu),
                ngu.LastError());
        Require(ngu.RunNativeLuma720Merge(
                    lumaView.Get(), cbcrView.Get(), outputView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto actual = Download(device, context.Get(), output.Get());
        int maximum = 0;
        const int different =
            CountComponents(actual, expected, maximum);

        std::ofstream actualFile(actualPath, std::ios::binary);
        Require(bool(actualFile), "open actual output");
        actualFile.write(reinterpret_cast<const char*>(actual.data()),
                         std::streamsize(actual.size()));
        actualFile.close();

        std::printf("Merge720 different=%d maximum=%d\n",
                    different, maximum);
        ngu.Reset();
        context.Reset();
        pl_d3d11_destroy(&d3d);
        return different == 0 && maximum == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
