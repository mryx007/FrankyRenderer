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
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
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

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      UINT width,
                                      UINT height,
                                      DXGI_FORMAT format,
                                      const void* data)
{
    const UINT pixelBytes = format == DXGI_FORMAT_R16_UNORM ? 2
                           : format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
                                                                  : 8;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    initial.SysMemPitch = width * pixelBytes;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture)),
            "CreateTexture2D failed");
    return texture;
}

std::vector<uint8_t> Download(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "staging texture failed");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map failed");
    const size_t rowBytes = size_t(desc.Width) * 8;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3,
                "usage: test_ngu_quality_initial <low|med> <capture-root>");
        const std::string qualityName(argv[1]);
        Require(qualityName == "low" || qualityName == "med",
                "quality must be low or med");
        const NguQuality quality =
            qualityName == "low" ? NguQuality::Low : NguQuality::Medium;
        const std::filesystem::path root(argv[2]);
        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(d3d != nullptr, "D3D11 device unavailable");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        const auto inputBytes = ReadBytes(
            root / "input_0006_PS_260_cdfc6b55_s0_640x360_f21_921600.bin",
            size_t(640) * 360 * 4);
        auto input = CreateTexture(
            d3d->device, 640, 360, DXGI_FORMAT_B8G8R8A8_UNORM, inputBytes.data());
        ComPtr<ID3D11ShaderResourceView> inputView;
        Require(SUCCEEDED(d3d->device->CreateShaderResourceView(
                    input.Get(), nullptr, &inputView)),
                "input SRV");

        NguScaler ngu;
        Require(ngu.PrepareNativeChromaNetwork(d3d->gpu), ngu.LastError());
        Require(ngu.PrepareNativeChromaSplit(d3d->gpu), ngu.LastError());
        auto cb = CreateTexture(
            d3d->device, 640, 360, DXGI_FORMAT_R16_UNORM, nullptr);
        auto cr = CreateTexture(
            d3d->device, 640, 360, DXGI_FORMAT_R16_UNORM, nullptr);
        ComPtr<ID3D11RenderTargetView> cbView;
        ComPtr<ID3D11RenderTargetView> crView;
        ComPtr<ID3D11ShaderResourceView> cbShaderView;
        Require(SUCCEEDED(d3d->device->CreateRenderTargetView(
                    cb.Get(), nullptr, &cbView)) &&
                    SUCCEEDED(d3d->device->CreateRenderTargetView(
                        cr.Get(), nullptr, &crView)) &&
                    SUCCEEDED(d3d->device->CreateShaderResourceView(
                        cb.Get(), nullptr, &cbShaderView)),
                "split views");
        Require(ngu.RunNativeChromaSplit(
                    inputView.Get(), 640, 360, cbView.Get(), crView.Get()),
                ngu.LastError());
        const size_t outputCount = quality == NguQuality::Low ? 2 : 3;
        std::array<ComPtr<ID3D11Texture2D>, 3> outputs;
        std::array<ComPtr<ID3D11RenderTargetView>, 3> outputViews;
        std::array<ID3D11RenderTargetView*, 3> rawOutputs = {};
        for (size_t index = 0; index < outputCount; ++index)
        {
            outputs[index] = CreateTexture(
                d3d->device, 640, 360, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr);
            Require(SUCCEEDED(d3d->device->CreateRenderTargetView(
                        outputs[index].Get(), nullptr, &outputViews[index])),
                    "output RTV");
            rawOutputs[index] = outputViews[index].Get();
        }
        Require(ngu.RunNativeChromaInitialForTest(
                    quality,
                    cbShaderView.Get(),
                    rawOutputs.data(),
                    int(outputCount)),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        int failures = 0;
        uint16_t maximum = 0;
        const char* shader =
            quality == NguQuality::Low ? "PS_1152_a9a5033a" : "PS_1412_aad01c2f";
        for (size_t index = 0; index < outputCount; ++index)
        {
            const auto actual = Download(d3d->device, context.Get(), outputs[index].Get());
            const auto expected = ReadBytes(
                root / ("draw_0009_" + std::string(shader) + "_rt" +
                        std::to_string(index) +
                        "_640x360_f113_1843200.bin"),
                size_t(640) * 360 * 8);
            int different = 0;
            for (size_t offset = 0; offset < actual.size(); offset += 2)
            {
                const uint16_t left =
                    uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
                const uint16_t right =
                    uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
                if (left == right)
                    continue;
                ++different;
                maximum = std::max(
                    maximum, uint16_t(std::abs(int(left) - int(right))));
            }
            failures += different;
            std::printf("%s initializer rt%zu: different=%d maximum=%u\n",
                        qualityName.c_str(),
                        index,
                        different,
                        unsigned(maximum));
        }
        std::printf("%s %s initializer: failures=%d maximum=%u\n",
                    failures == 0 ? "PASS_PRODUCT" : "FAIL_PRODUCT",
                    qualityName.c_str(),
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
