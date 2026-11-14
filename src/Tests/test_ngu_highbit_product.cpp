#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT kSourceWidth = 1280;
constexpr UINT kSourceHeight = 720;
constexpr UINT kOutputWidth = 2560;
constexpr UINT kOutputHeight = 1440;

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path,
                               size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open capture");
    Require(size_t(file.tellg()) == expected, "capture size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           std::streamsize(expected))),
            "read capture");
    return bytes;
}

template <typename T>
ComPtr<T> CreateResource(T* value)
{
    ComPtr<T> result;
    result.Attach(value);
    return result;
}

std::vector<uint8_t> Download(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              ID3D11Texture2D* source,
                              UINT pixelBytes)
{
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "create staging");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging");
    std::vector<uint8_t> bytes(size_t(desc.Width) * desc.Height * pixelBytes);
    for (UINT y = 0; y < desc.Height; ++y)
    {
        std::memcpy(bytes.data() + size_t(y) * desc.Width * pixelBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    size_t(desc.Width) * pixelBytes);
    }
    context->Unmap(staging.Get(), 0);
    return bytes;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc >= 2 && argc <= 4,
                "usage: test_ngu_highbit_product <draw-4576-input> [ps908] [final]");
        const std::filesystem::path input(argv[1]);
        const auto sourceBytes =
            ReadBytes(input, size_t(kSourceWidth) * kSourceHeight * 8);

        auto params = pl_d3d11_default_params;
        auto backend = pl_d3d11_create(nullptr, &params);
        Require(bool(backend), "create libplacebo D3D11 backend");
        pl_d3d11 d3d = pl_d3d11_get(backend->gpu);
        Require(bool(d3d) && bool(d3d->device), "D3D11 device");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        D3D11_TEXTURE2D_DESC sourceDesc = {};
        sourceDesc.Width = kSourceWidth;
        sourceDesc.Height = kSourceHeight;
        sourceDesc.MipLevels = 1;
        sourceDesc.ArraySize = 1;
        sourceDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        sourceDesc.SampleDesc.Count = 1;
        sourceDesc.Usage = D3D11_USAGE_DEFAULT;
        sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sourceData = {};
        sourceData.pSysMem = sourceBytes.data();
        sourceData.SysMemPitch = kSourceWidth * 8;
        ComPtr<ID3D11Texture2D> sourceTexture;
        ComPtr<ID3D11ShaderResourceView> sourceView;
        Require(SUCCEEDED(device->CreateTexture2D(
                    &sourceDesc, &sourceData, &sourceTexture)) &&
                    SUCCEEDED(device->CreateShaderResourceView(
                        sourceTexture.Get(), nullptr, &sourceView)),
                "create source");

        D3D11_TEXTURE2D_DESC outputDesc = {};
        outputDesc.Width = kOutputWidth;
        outputDesc.Height = kOutputHeight;
        outputDesc.MipLevels = 1;
        outputDesc.ArraySize = 1;
        outputDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        outputDesc.SampleDesc.Count = 1;
        outputDesc.Usage = D3D11_USAGE_DEFAULT;
        outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> outputTexture;
        ComPtr<ID3D11RenderTargetView> outputView;
        Require(SUCCEEDED(device->CreateTexture2D(
                    &outputDesc, nullptr, &outputTexture)) &&
                    SUCCEEDED(device->CreateRenderTargetView(
                        outputTexture.Get(), nullptr, &outputView)),
                "create output");
        D3D11_TEXTURE2D_DESC rgbDesc = outputDesc;
        rgbDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        ComPtr<ID3D11Texture2D> rgbTexture;
        ComPtr<ID3D11RenderTargetView> rgbView;
        Require(SUCCEEDED(device->CreateTexture2D(
                    &rgbDesc, nullptr, &rgbTexture)) &&
                    SUCCEEDED(device->CreateRenderTargetView(
                        rgbTexture.Get(), nullptr, &rgbView)),
                "create RGB output");

        NguScaler ngu;
        Require(ngu.PrepareNativeLuma720DoubleHighBit(backend->gpu),
                ngu.LastError());
        int phaseRuns = 1;
        if (const char* value = std::getenv("NGU_HIGHBIT_720_PHASE_RUNS"))
        {
            phaseRuns = std::max(1, std::atoi(value));
        }
        for (int run = 0; run < phaseRuns; ++run)
        {
            Require(ngu.RunNativeLuma720DoubleHighBit(
                        sourceView.Get(), outputView.Get()),
                    ngu.LastError());
        }
        Require(ngu.CopyNativeLuma720DoubleHighBitRgb(rgbView.Get()),
                ngu.LastError());
        pl_gpu_finish(backend->gpu);
        const auto output =
            Download(device, context.Get(), outputTexture.Get(), 4);
        const auto rgb =
            Download(device, context.Get(), rgbTexture.Get(), 8);
        Require(std::any_of(output.begin(), output.end(), [](uint8_t value) {
                    return value != 0;
                }),
                "P010 720 Double output is black");

        if (const char* dump = std::getenv("NGU_HIGHBIT_720_PRODUCT_DUMP"))
        {
            std::ofstream file(dump, std::ios::binary);
            Require(bool(file), "open product dump");
            file.write(reinterpret_cast<const char*>(output.data()),
                       std::streamsize(output.size()));
        }
        if (const char* dump = std::getenv("NGU_HIGHBIT_720_RGB_DUMP"))
        {
            std::ofstream file(dump, std::ios::binary);
            Require(bool(file), "open RGB dump");
            file.write(reinterpret_cast<const char*>(rgb.data()),
                       std::streamsize(rgb.size()));
        }
        if (const char* directory = std::getenv("NGU_HIGHBIT_720_SURFACE_DIR"))
        {
            std::filesystem::create_directories(directory);
            auto dumpSurface = [&](NguHighBit720Surface surface,
                                   const char* name,
                                   UINT width,
                                   UINT height,
                                   DXGI_FORMAT format,
                                   UINT pixelBytes) {
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = width;
                desc.Height = height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = format;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                ComPtr<ID3D11Texture2D> texture;
                ComPtr<ID3D11RenderTargetView> view;
                Require(SUCCEEDED(device->CreateTexture2D(
                            &desc, nullptr, &texture)) &&
                            SUCCEEDED(device->CreateRenderTargetView(
                                texture.Get(), nullptr, &view)),
                        "create surface dump");
                Require(ngu.CopyNativeLuma720DoubleHighBitSurface(
                            surface, view.Get()),
                        ngu.LastError());
                auto bytes = Download(device, context.Get(), texture.Get(), pixelBytes);
                std::ofstream file(
                    std::filesystem::path(directory) / name, std::ios::binary);
                Require(bool(file), "open surface dump");
                file.write(reinterpret_cast<const char*>(bytes.data()),
                           std::streamsize(bytes.size()));
            };
            dumpSurface(NguHighBit720Surface::SplitY,
                        "split_y.bin",
                        kSourceWidth,
                        kSourceHeight,
                        DXGI_FORMAT_R16_UNORM,
                        2);
            dumpSurface(NguHighBit720Surface::SplitCb,
                        "split_cb.bin",
                        kSourceWidth,
                        kSourceHeight,
                        DXGI_FORMAT_R16_UNORM,
                        2);
            dumpSurface(NguHighBit720Surface::SplitCr,
                        "split_cr.bin",
                        kSourceWidth,
                        kSourceHeight,
                        DXGI_FORMAT_R16_UNORM,
                        2);
            for (int channel = 0; channel < 3; ++channel)
            {
                const char* names[] = {
                    "upscaled_y.bin", "upscaled_cb.bin", "upscaled_cr.bin"};
                dumpSurface(static_cast<NguHighBit720Surface>(
                                int(NguHighBit720Surface::UpscaledY) + channel),
                            names[channel],
                            kOutputWidth,
                            kOutputHeight,
                            DXGI_FORMAT_R16_UNORM,
                            2);
            }
            dumpSurface(NguHighBit720Surface::LumaResidual,
                        "luma_residual.bin",
                        1282,
                        722,
                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                        8);
            dumpSurface(NguHighBit720Surface::ChromaResidualCb,
                        "chroma_residual_cb.bin",
                        1282,
                        722,
                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                        8);
            dumpSurface(NguHighBit720Surface::ChromaResidualCr,
                        "chroma_residual_cr.bin",
                        1282,
                        722,
                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                        8);
            dumpSurface(NguHighBit720Surface::ChromaCb,
                        "chroma_result_cb.bin",
                        kOutputWidth,
                        kOutputHeight,
                        DXGI_FORMAT_R16_UNORM,
                        2);
            dumpSurface(NguHighBit720Surface::ChromaCr,
                        "chroma_result_cr.bin",
                        kOutputWidth,
                        kOutputHeight,
                        DXGI_FORMAT_R16_UNORM,
                        2);
        }
        if (argc >= 3)
        {
            const auto expected =
                ReadBytes(argv[2], size_t(kOutputWidth) * kOutputHeight * 8);
            int maximum = 0;
            int overOnePixels = 0;
            size_t different = 0;
            for (size_t pixel = 0; pixel < size_t(kOutputWidth) * kOutputHeight;
                 ++pixel)
            {
                bool overOne = false;
                for (size_t component = 0; component < 4; ++component)
                {
                    const size_t word = (pixel * 4 + component) * 2;
                    const uint16_t a =
                        uint16_t(rgb[word]) | (uint16_t(rgb[word + 1]) << 8);
                    const uint16_t b = uint16_t(expected[word]) |
                        (uint16_t(expected[word + 1]) << 8);
                    const int delta = std::abs(int(a) - int(b));
                    if (delta)
                        ++different;
                    maximum = std::max(maximum, delta);
                    overOne = overOne || delta > 1;
                }
                overOnePixels += overOne;
            }
            std::printf("PS908 different=%zu maximum=%d diff_gt_1_pixels=%d\n",
                        different,
                        maximum,
                        overOnePixels);
            Require(maximum <= 1 && overOnePixels == 0,
                    "PS908 same-frame numerical gate");
        }
        if (argc == 4)
        {
            const auto expected =
                ReadBytes(argv[3], size_t(kOutputWidth) * kOutputHeight * 4);
            int maximum = 0;
            int overOnePixels = 0;
            size_t different = 0;
            for (size_t pixel = 0; pixel < size_t(kOutputWidth) * kOutputHeight;
                 ++pixel)
            {
                bool overOne = false;
                for (size_t component = 0; component < 4; ++component)
                {
                    const size_t offset = pixel * 4 + component;
                    const int delta =
                        std::abs(int(output[offset]) - int(expected[offset]));
                    if (delta)
                        ++different;
                    maximum = std::max(maximum, delta);
                    overOne = overOne || delta > 1;
                }
                overOnePixels += overOne;
            }
            std::printf("Final different=%zu maximum=%d diff_gt_1_pixels=%d\n",
                        different,
                        maximum,
                        overOnePixels);
            Require(maximum <= 1 && overOnePixels == 0,
                    "Final same-frame numerical gate");
        }
        std::printf("PASS_PRODUCT P010 720 Double high-bit native graph\n");
        ngu.Reset();
        context.Reset();
        pl_d3d11_destroy(&backend);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
