#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT Width = 640;
constexpr UINT Height = 360;
constexpr UINT Components = 4;
constexpr size_t FeatureBytes = size_t(Width) * Height * Components * sizeof(uint16_t);
constexpr size_t DenseBytes = size_t(642) * 362 * Components * sizeof(uint16_t);

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), path.string() + ": open failed");
    Require(size_t(file.tellg()) == bytes, path.string() + ": unexpected size");
    file.seekg(0);
    std::vector<uint8_t> data(bytes);
    Require(bool(file.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes))),
            path.string() + ": read failed");
    return data;
}

ComPtr<ID3D11Texture2D>
CreateTexture(ID3D11Device* device, UINT width, UINT height, const void* data = nullptr)
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
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    initial.SysMemPitch = width * Components * sizeof(uint16_t);
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture)),
            "CreateTexture2D failed");
    return texture;
}

std::vector<uint8_t>
Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "CreateTexture2D staging failed");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "Map staging failed");
    const size_t rowBytes = size_t(desc.Width) * Components * sizeof(uint16_t);
    std::vector<uint8_t> data(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(data.data() + y * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return data;
}

struct Difference
{
    int components = 0;
    uint16_t maximum = 0;
};

Difference Compare(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size mismatch");
    Difference result;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left =
            uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left == right)
            continue;
        ++result.components;
        result.maximum =
            std::max(result.maximum, uint16_t(std::abs(int(left) - int(right))));
    }
    return result;
}

std::filesystem::path DrawPath(const std::filesystem::path& root,
                               int draw,
                               const wchar_t* shader,
                               const wchar_t* dimensions,
                               size_t bytes)
{
    wchar_t name[256];
    swprintf_s(name,
               L"draw_%04d_%s_rt0_%s_f113_%zu.bin",
               draw,
               shader,
               dimensions,
               bytes);
    return root / name;
}

std::filesystem::path InputPath(const std::filesystem::path& root,
                                int draw,
                                const wchar_t* shader,
                                int stage)
{
    wchar_t name[256];
    swprintf_s(name,
               L"input_%04d_%s_s%d_640x360_f113_%zu.bin",
               draw,
               shader,
               stage,
               FeatureBytes);
    return root / name;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: test_ngu_quality <high-720-capture-root>");
        const std::filesystem::path root = std::filesystem::path(argv[1]);

        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        std::fprintf(stderr, "stage=d3d=%p gpu=%p\n", (const void*)d3d,
                     d3d ? (const void*)d3d->gpu : nullptr);
        Require(d3d != nullptr, "D3D11 device unavailable");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScaler ngu;
        const bool prepared = ngu.PrepareNativeChromaNetwork(d3d->gpu);
        std::fprintf(stderr, "stage=prepare result=%d error=%s\n", prepared,
                     ngu.LastError());
        Require(prepared, ngu.LastError());

        int failures = 0;
        uint16_t maximum = 0;
        for (int layer = 0; layer < 15; ++layer)
        {
            const int draw = 10 + layer;
            std::array<std::vector<uint8_t>, 3> inputBytes;
            std::array<ComPtr<ID3D11Texture2D>, 3> inputTextures;
            std::array<ComPtr<ID3D11ShaderResourceView>, 3> inputs;
            std::array<ID3D11ShaderResourceView*, 3> rawInputs = {};
            for (int stage : {0, 2, 3})
            {
                const int slot = stage == 0 ? 0 : stage == 2 ? 1 : 2;
                inputBytes[slot] =
                    ReadBytes(InputPath(root, draw, L"PS_3588_80fb311e", stage),
                              FeatureBytes);
                inputTextures[slot] =
                    CreateTexture(device, Width, Height, inputBytes[slot].data());
                Require(SUCCEEDED(device->CreateShaderResourceView(
                            inputTextures[slot].Get(), nullptr, &inputs[slot])),
                        "input SRV");
                rawInputs[slot] = inputs[slot].Get();
            }

            auto output = CreateTexture(device, Width, Height);
            ComPtr<ID3D11RenderTargetView> outputView;
            Require(SUCCEEDED(device->CreateRenderTargetView(output.Get(), nullptr, &outputView)),
                    "output RTV");
            Require(ngu.RunNativeChromaQualityLayerForTest(
                        NguQuality::High, rawInputs.data(), 3, layer, outputView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);

            const auto actual = Download(device, context.Get(), output.Get());
            const auto expected =
                ReadBytes(DrawPath(root, draw, L"PS_3588_80fb311e", L"640x360", FeatureBytes),
                          FeatureBytes);
            const auto difference = Compare(actual, expected);
            failures += difference.components;
            maximum = std::max(maximum, difference.maximum);
            std::printf("High layer %02d: different=%d maximum=%u\n",
                        layer,
                        difference.components,
                        unsigned(difference.maximum));
        }

        std::array<std::vector<uint8_t>, 3> denseInputBytes;
        std::array<ComPtr<ID3D11Texture2D>, 3> denseInputTextures;
        std::array<ComPtr<ID3D11ShaderResourceView>, 3> denseInputs;
        std::array<ID3D11ShaderResourceView*, 3> rawDenseInputs = {};
        for (int stage : {0, 2, 3})
        {
            const int slot = stage == 0 ? 0 : stage == 2 ? 1 : 2;
            denseInputBytes[slot] =
                ReadBytes(InputPath(root, 25, L"PS_2100_ba9671a7", stage), FeatureBytes);
            denseInputTextures[slot] =
                CreateTexture(device, Width, Height, denseInputBytes[slot].data());
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        denseInputTextures[slot].Get(), nullptr, &denseInputs[slot])),
                    "dense input SRV");
            rawDenseInputs[slot] = denseInputs[slot].Get();
        }
        auto dense = CreateTexture(device, 642, 362);
        ComPtr<ID3D11RenderTargetView> denseView;
        Require(SUCCEEDED(device->CreateRenderTargetView(dense.Get(), nullptr, &denseView)),
                "dense RTV");
        Require(ngu.RunNativeChromaQualityDenseForTest(
                    NguQuality::High, rawDenseInputs.data(), 3, denseView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto denseActual = Download(device, context.Get(), dense.Get());
        const auto denseExpected =
            ReadBytes(DrawPath(root, 25, L"PS_2100_ba9671a7", L"642x362", DenseBytes),
                      DenseBytes);
        const auto denseDifference = Compare(denseActual, denseExpected);
        failures += denseDifference.components;
        maximum = std::max(maximum, denseDifference.maximum);
        std::printf("High dense: different=%d maximum=%u\n",
                    denseDifference.components,
                    unsigned(denseDifference.maximum));

        std::printf("%s High NGU Sharp chroma profile: failures=%d maximum=%u\n",
                    failures == 0 ? "PASS_PRODUCT" : "FAIL_PRODUCT",
                    failures,
                    unsigned(maximum));
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
